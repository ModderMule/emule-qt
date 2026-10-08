#include "pch.h"
/// @file ServerSocket.cpp
/// @brief TCP connection to an ED2K server — replaces MFC CServerSocket.

#include "net/ServerSocket.h"
#include "net/Packet.h"
#include "app/AppContext.h"
#include "ipfilter/IPFilter.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "server/Server.h"
#include "server/ServerList.h"
#include "stats/Statistics.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/TimeUtils.h"


#include <cerrno>
#include <system_error>
#include <QHostAddress>
#include <QUrl>

namespace eMule {

namespace {

/// Human-readable name for a ServerConnState, for server-verbose logging.
const char* serverConnStateName(ServerConnState s)
{
    switch (s) {
    case ServerConnState::NotConnected: return "NotConnected";
    case ServerConnState::Connecting:   return "Connecting";
    case ServerConnState::WaitForLogin: return "WaitForLogin";
    case ServerConnState::Connected:    return "Connected";
    case ServerConnState::ServerDead:   return "ServerDead";
    case ServerConnState::FatalError:   return "FatalError";
    case ServerConnState::Disconnected: return "Disconnected";
    case ServerConnState::ServerFull:   return "ServerFull";
    case ServerConnState::Error:        return "Error";
    }
    return "?";
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ServerSocket::ServerSocket(bool manualSingleConnect, QObject* parent)
    : EMSocket(parent)
    , m_manualSingleConnect(manualSingleConnect)
{

    connect(this, &QAbstractSocket::connected, this, &ServerSocket::onSocketConnected);
    connect(this, &QAbstractSocket::disconnected, this, &ServerSocket::onSocketDisconnected);
    connect(this, &QAbstractSocket::errorOccurred, this, &ServerSocket::onSocketError);
}

ServerSocket::~ServerSocket()
{
    m_isDeleting = true;
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void ServerSocket::connectTo(const Server& server, bool noCrypt, const Address& dialAddress)
{
    m_curServer = std::make_unique<Server>(server);
    m_noCrypt = noCrypt;
    m_familyFallback = !dialAddress.isNull();
    m_tcpConnected = false;
    m_startNewMessageLog = true;

    setConnectionState(ServerConnState::Connecting);

    // If server has a dynamic IP, resolve it first
    if (m_curServer->hasDynIP()) {
        // A literal that ended up in the dynIP slot (a legacy staticservers.dat line, an
        // [emDynIP:] echo) must never reach the resolver: QDnsLookup(A, "8.8.8.8")
        // NXDOMAINs and the server is marked dead.
        if (const Address literal = Address::fromString(m_curServer->dynIP()); !literal.isNull()) {
            logServerVerbose(QStringLiteral("connectTo: dynIP '%1' is a literal — no DNS needed")
                                 .arg(m_curServer->dynIP()));
            m_curServer->setIpAddress(literal);
            m_sessionAddress = literal;
        } else {
            logServerVerbose(QStringLiteral("connectTo: resolving dynIP hostname '%1' for server %2")
                                 .arg(m_curServer->dynIP()).arg(m_curServer->name()));
            m_dnsTriedFallback = false;
            startDnsLookup(thePrefs.serverPreferIPv6() ? QDnsLookup::AAAA : QDnsLookup::A);
            return;
        }
    }

    // Direct connection via IP. A dual-stack server dials the preferred family (IPv4
    // unless serverPreferIPv6: only an IPv4 session can hand out a HighID); ServerConnect
    // passes the other family explicitly when that one failed.
    if (!m_curServer->hasDynIP()) {
        m_sessionAddress = (!dialAddress.isNull() && m_curServer->hasAddress(dialAddress))
            ? dialAddress
            : m_curServer->dialAddress(thePrefs.serverPreferIPv6());
    }
    uint16 port = m_curServer->port();

    // Configure encryption
    if (!noCrypt && m_curServer->supportsObfuscationTCP()) {
        setConnectionEncryption(true, nullptr, true);
        port = m_curServer->obfuscationPortTCP();
    }

    logInfo(QStringLiteral("Connecting to server %1 (%2) noCrypt=%3 supportsObfuTCP=%4 obfuPort=%5")
                .arg(m_curServer->name())
                .arg(Endpoint(m_sessionAddress, port).toString())
                .arg(noCrypt)
                .arg(m_curServer->supportsObfuscationTCP())
                .arg(m_curServer->obfuscationPortTCP()));

    connectToPeer(m_sessionAddress, port);
}

// ---------------------------------------------------------------------------
// Packet sending override
// ---------------------------------------------------------------------------

void ServerSocket::sendPacket(std::unique_ptr<Packet> packet, bool controlPacket,
                              uint32 actualPayloadSize, bool forceImmediateSend)
{
    if (auto* stats = theApp.statistics)
        stats->addUpDataOverheadServer(packet->size);

    m_lastTransmission = getTickCount();
    EMSocket::sendPacket(std::move(packet), controlPacket, actualPayloadSize, forceImmediateSend);
}

// ---------------------------------------------------------------------------
// Packet processing
// ---------------------------------------------------------------------------

bool ServerSocket::packetReceived(Packet* packet)
{
    m_lastTransmission = getTickCount();

    if (auto* stats = theApp.statistics)
        stats->addDownDataOverheadServer(packet->size);

    // Decompress zlib-packed server packets (PR_ZLIB / OP_PACKEDPROT).
    // MFC: CServerSocket::ProcessPacket() calls UnPackPacket(250000) — the server
    // cap is 250 KB, not the 50 KB Packet default, so large OP_SEARCHRESULT /
    // OP_FOUNDSOURCES / OP_SERVERLIST sets inflate instead of hitting Z_BUF_ERROR.
    //
    // A bad packet never returns false unless the socket was failed first: false makes
    // EMSocket drop the rest of the read buffer and desyncs the stream (MFC
    // CServerSocket::PacketReceived returns true and either continues or disconnects).
    if (packet->prot == OP_PACKEDPROT) {
        if (!packet->unPackPacket(250000)) {
            logWarning(QStringLiteral("ServerSocket: Failed to decompress packed packet (opcode 0x%1)")
                           .arg(packet->opcode, 2, 16, QLatin1Char('0')));
            return true;
        }
    } else if (packet->prot != OP_EDONKEYPROT) {
        return true; // MFC ServerSocket.cpp:697 — only eDonkey packets are processed
    }

    const auto* data = reinterpret_cast<const uint8*>(packet->pBuffer);
    return processPacket(data, packet->size, packet->opcode);
}

bool ServerSocket::processPacket(const uint8* packet, uint32 size, uint8 opcode)
{
    switch (opcode) {
    case OP_SERVERMESSAGE: {
        // Format: uint16 msgLen, char[msgLen] message
        if (size < 2)
            return failMalformed();

        uint16 msgLen = peekUInt16(packet);
        if (size < 2u + msgLen)
            return failMalformed();

        // Charset follows the server's advertised Unicode support, as the reference does
        // (CSafeMemFile::ReadString(pServer && pServer->GetUnicodeSupport())). Sniffing
        // does not work here: QString::fromUtf8 substitutes U+FFFD for invalid input and
        // never returns an empty string, so the old `if (msg.isEmpty())` fallback to
        // Latin-1 could not fire and legacy servers came through as replacement chars.
        const char* text = reinterpret_cast<const char*>(packet + 2);
        const QString msg = (m_curServer && m_curServer->supportsUnicode())
            ? QString::fromUtf8(text, msgLen)
            : QString::fromLatin1(text, msgLen);

        // The reference only Debug()s here — the message text belongs in the Server Info
        // pane, not the log. Keeping it on the verbose channel preserves diagnostics.
        logServerVerbose(QStringLiteral("<<< OP_SERVERMESSAGE (%1 bytes): %2").arg(msgLen).arg(msg));
        emit serverMessage(msg);
        break;
    }

    case OP_IDCHANGE: {
        // MFC: CServerSocket::ProcessPacket() — ServerSocket.cpp:275-350.
        // Layout: uint32 clientID, [uint32 tcpFlags], [uint32 auxPort],
        //         [uint32 serverReportedIP]. There is NO obfuscation-TCP-port
        //         field here — the reference derives that from the flags.
        if (size < 4)
            return failMalformed();

        uint32 clientID = peekUInt32(packet);
        uint32 tcpFlags = 0;
        if (size >= 8)
            tcpFlags = peekUInt32(packet + 4);

        // The extended answer tells us the IP the server sees us on, which is the
        // only public-IP source available when it hands us a LowID. MFC reads it
        // at offset 12 once size >= 16 (ServerSocket.cpp:306-315).
        uint32 serverReportedIP = 0;
        if (size >= 16) {
            serverReportedIP = peekUInt32(packet + 12);
            // MFC asserts on this and zeroes it — a LowID here is nonsense, since
            // the whole point of the field is to report a routable address.
            if (isLowID(serverReportedIP))
                serverReportedIP = 0;
        }

        if (clientID == 0) {
            // Server is full
            failWith(ServerConnState::ServerFull,
                     {ServerFailure::Phase::Handshake, ServerFailure::Reason::ProtocolRejection});
            return false;
        }

        // If the live connection is obfuscated the server won't have advertised
        // that in the flags — OR it in so the persistent entry records it. MFC:
        // CServerSocket::ProcessPacket() — ServerSocket.cpp:296.
        if (isServerCryptEnabledConnection())
            tcpFlags |= SrvTcpFlag::TcpObfuscation;

        // Apply server TCP flags to our server copy; #14 also wires them through to
        // the persistent list entry via the loginReceived consumer.
        if (m_curServer)
            m_curServer->setTCPFlags(tcpFlags);

        logInfo(QStringLiteral("New client ID is %1").arg(clientID));
        if (isLowID(clientID))
            logWarning(QStringLiteral("You have a Low ID. Please check your port forwarding and firewall settings."));

        // The smart-LowID decision is made by ServerConnect BEFORE we promote the
        // connection, faithfully matching srchybrid CServerSocket::ProcessPacket
        // (decide, then SetConnectionState(CS_CONNECTED) — ServerSocket.cpp:326-348).
        // On a bounce ServerConnect calls requestLowIDBounce() and we skip promotion
        // entirely, mirroring the reference's `break`.
        m_lowIDBounced = false;
        emit loginReceived(clientID, tcpFlags, serverReportedIP);
        if (m_lowIDBounced)
            return true;  // abandoned this LowID; ServerConnect is trying another server

        setConnectionState(ServerConnState::Connected);
        break;
    }

    case OP_SEARCHRESULT: {
        // Raw search result data — forward to search engine
        if (size < 4)
            break;

        // The "more results available" flag is a trailing byte the search parser
        // reads authoritatively (SearchList::processSearchAnswer), which also
        // surfaces it to the user (#19). The socket-level flag here is unused.
        logServerVerbose(QStringLiteral("<<< OP_SEARCHRESULT received (%1 bytes) from %2")
                             .arg(size).arg(m_curServer ? m_curServer->name() : QStringLiteral("?")));
        emit searchResultReceived(packet, size, /*moreResultsAvailable=*/false);
        break;
    }

    case OP_FOUNDSOURCES:
    case OP_FOUNDSOURCES_OBFU: {
        // Format: hash16[16], uint8 sourceCount, sources...
        // short: MFC swallows the read error for the plain opcode only
        if (size < 17) {
            if (opcode == OP_FOUNDSOURCES_OBFU)
                return failMalformed();
            break;
        }

        bool obfuscated = (opcode == OP_FOUNDSOURCES_OBFU);
        logServerVerbose(QStringLiteral("<<< OP_FOUNDSOURCES%1 received (%2 bytes)")
                             .arg(obfuscated ? QStringLiteral("_OBFU") : QString()).arg(size));
        emit foundSourcesReceived(packet, size, obfuscated);
        break;
    }

    case OP_SERVERSTATUS: {
        // Format: uint32 users, uint32 files
        //
        // This is ALL that is defined for the TCP status packet — see original
        // eMule ServerSocket.cpp `case OP_SERVERSTATUS`, which reads exactly 8
        // bytes and treats anything beyond as unknown trailing data.
        //
        // Do NOT parse maxUsers/udpFlags/serverKeyUDP/obfuscation ports here.
        // Those fields belong to the *UDP* OP_GLOBSERVSTATRES packet (0x97) and
        // have a different layout (it carries a leading 4-byte challenge).
        // Reading them out of a TCP packet yields garbage — in particular a
        // bogus serverKeyUDP would make us obfuscate UDP with a key the server
        // cannot derive, silently breaking global search against that server.
        // See ServerList::processStatusResponse() for the real parser.
        if (size < 8)
            break;

        uint32 users = peekUInt32(packet);
        uint32 files = peekUInt32(packet + 4);

        if (m_curServer) {
            m_curServer->setUsers(users);
            m_curServer->setFiles(files);
        }

        logServerVerbose(QStringLiteral("<<< OP_SERVERSTATUS: users=%1 files=%2").arg(users).arg(files));

        if (size > 8) {
            logServerVerbose(QStringLiteral("ServerSocket: OP_SERVERSTATUS has %1 trailing bytes (ignored)")
                         .arg(size - 8));
        }

        emit serverStatusReceived(users, files);
        break;
    }

    case OP_SERVERIDENT: {
        // Format: hash16[16], ip[4], port[2], tagCount[4], tags...
        if (size < 26)
            break;

        const uint8* serverHash = packet;
        uint32 serverIP = peekUInt32(packet + 16);
        uint16 serverPort = peekUInt16(packet + 20);
        uint32 tagCount = peekUInt32(packet + 22);

        QString name;
        QString description;
        QString metaApiUrl;
        QString metaApiPin;
        uint32 metaApiVersion = 0;
        Address serverIPv6;

        // Parse tags
        try {
            SafeMemFile tagData(const_cast<uint8*>(packet + 26), size - 26);
            checkTagCount(tagData, tagCount, kMaxWireTags);
            for (uint32 i = 0; i < tagCount; ++i) {
                Tag tag(tagData, true);
                if (tag.nameId() == ST_SERVERNAME && tag.isStr())
                    name = tag.strValue();
                else if (tag.nameId() == ST_DESCRIPTION && tag.isStr())
                    description = tag.strValue();
                else if (tag.nameId() == ST_META_API && tag.isStr())
                    metaApiUrl = tag.strValue();
                else if (tag.nameId() == ST_META_API_FP && tag.isStr())
                    metaApiPin = tag.strValue();
                else if (tag.nameId() == ST_META_API_VER && tag.isInt())
                    metaApiVersion = tag.intValue();
                else if (tag.nameId() == CT_MOD_SVR_IP_V6 && tag.isHash()) {
                    // The server's own public IPv6: over an IPv4 session it joins the
                    // list entry (one row per dual-stack server). Unknown tags such as
                    // ST_NAT_PORT are consumed and ignored (S6 out of scope).
                    serverIPv6 = Address::fromIPv6Bytes(tag.hashValue());
                    logServerVerbose(QStringLiteral("<<< OP_SERVERIDENT: server IPv6 %1")
                                         .arg(serverIPv6.toString()));
                }
                else if (tag.nameId() == CT_MOD_YOUR_IP && tag.isHash()) {
                    // The server tells us the IPv6 it observed our session arriving on — the
                    // most authoritative source of our own public IPv6. A server can only
                    // observe that for a session that actually connected over IPv6, so a v4
                    // session sending it is either buggy or hostile: enforce the family here
                    // rather than trusting the sender. m_curServer->ipAddress() is exact —
                    // connectTo() dials it, and the dynIP path writes the resolved address
                    // back before connecting.
                    const Address observed = Address::fromIPv6Bytes(tag.hashValue());
                    if (!m_curServer || !m_sessionAddress.isIPv6()) {
                        logServerVerbose(QStringLiteral("<<< OP_SERVERIDENT: ignoring CT_MOD_YOUR_IP "
                                                        "(%1): this session is IPv4")
                                             .arg(observed.toString()));
                    } else {
                        theApp.setPublicIPv6Observed(observed, m_sessionAddress.toString());
                    }
                }
                else if (tag.nameId() == ST_IPV6_STATUS && tag.isInt()) {
                    // The server's verdict on whether the IPv6 we advertised is reachable.
                    const uint8 status = static_cast<uint8>(tag.intValue());
                    theApp.setPublicIPv6Status(status);
                    logServerVerbose(QStringLiteral("<<< OP_SERVERIDENT: IPv6 status have=%1 reachable=%2 probed=%3")
                                         .arg((status & IPV6ST_HAVE) ? 1 : 0)
                                         .arg((status & IPV6ST_REACHABLE) ? 1 : 0)
                                         .arg((status & IPV6ST_PROBED) ? 1 : 0));
                }
            }
        } catch (...) {
            // Tag parsing failed — acceptable, name/desc may be partial
        }

        // The address of the other family, for the dual-stack merge (emitted below,
        // after serverIdentReceived has stored the ident hash on the list entry).
        Address learned;
        if (m_curServer) {
            m_curServer->setServerHash(serverHash);
            // The ident body has a 4-byte IP field, so a dual-stack server always reports
            // its IPv4 there. It never replaces the address we are connected to (MFC
            // only logs it): over an IPv6 session it is the server's other address.
            const Address identIP = Address::fromNetworkOrder(serverIP);
            if (m_sessionAddress.isIPv6()) {
                if (identIP.isIPv4() && !m_curServer->hasAddress(identIP))
                    learned = identIP;
            } else {
                if (serverIPv6.isIPv6() && !m_curServer->hasAddress(serverIPv6))
                    learned = serverIPv6;
            }

            // eNode Meta API: contract v1 over http(s) only
            const bool apiOk = metaApiVersion == 1 && Server::isMetaApiUrl(metaApiUrl);
            // the copy starts out with what server.met remembered: an ident without
            // the tags takes it back
            if (!apiOk)
                m_curServer->clearMetaApi();
            if (apiOk) {
                m_curServer->setMetaApi(metaApiUrl, metaApiPin);
                logServerVerbose(QStringLiteral("<<< OP_SERVERIDENT: Meta API %1%2")
                                     .arg(metaApiUrl, metaApiPin.isEmpty() ? QString()
                                                                           : QStringLiteral(" (pinned)")));
            } else if (!metaApiUrl.isEmpty()) {
                logServerVerbose(QStringLiteral("<<< OP_SERVERIDENT: ignoring Meta API '%1' (version %2)")
                                     .arg(metaApiUrl).arg(metaApiVersion));
            }
        }

        logServerVerbose(QStringLiteral("<<< OP_SERVERIDENT: name='%1' (%2) ident=%3 tags=%4")
                             .arg(name)
                             .arg(Endpoint(m_sessionAddress, peerPort()).toString())
                             .arg(Endpoint::fromNetworkOrder(serverIP, serverPort).toString())
                             .arg(tagCount));
        emit serverIdentReceived(serverHash, serverIP, serverPort, name, description);
        if (!learned.isNull())
            emit serverAddressLearned(learned);
        break;
    }

    case OP_SERVERLIST: {
        // Format: uint8 count, [ip4 port2]* count
        if (size < 1)
            break;

        logServerVerbose(QStringLiteral("<<< OP_SERVERLIST received (%1 bytes, %2 entries advertised)")
                             .arg(size).arg(static_cast<uint>(packet[0])));
        emit serverListReceived(packet, size);
        break;
    }

    case OP_CALLBACKREQUESTED: {
        // Format: ip[4], port[2] [, cryptFlags[1], userHash[16]]
        if (size < 6)
            break;

        uint32 clientIP = peekUInt32(packet);
        uint16 clientPort = peekUInt16(packet + 4);

        logServerVerbose(QStringLiteral("<<< OP_CALLBACKREQUESTED from %1:%2 (%3 crypt bytes)")
                             .arg(Address::fromNetworkOrder(clientIP).toString()).arg(clientPort)
                             .arg(size > 6 ? size - 6 : 0));

        if (auto* filter = theApp.ipFilter) {
            if (filter->isFiltered(clientIP, thePrefs.ipFilterLevel())) {
                if (auto* stats = theApp.statistics)
                    stats->addFilteredClient();
                break;
            }
        }

        const uint8* cryptOptions = nullptr;
        uint32 cryptSize = 0;
        if (size > 6) {
            cryptOptions = packet + 6;
            cryptSize = size - 6;
        }

        emit callbackRequested(clientIP, clientPort, cryptOptions, cryptSize);
        break;
    }

    case OP_CALLBACKREQUESTED_IPV6: {
        // Format: ipv6[16] (network order), port[2] (LITTLE-endian — PR_NAT is the only
        // BE path). The server sends this to a v6-capable LowID target when the requester
        // is reachable only over IPv6; we call the requester back over IPv6.
        if (size < 18)
            break;

        const Endpoint requester(Address::fromIPv6Bytes(packet), peekUInt16(packet + 16));
        logServerVerbose(QStringLiteral("<<< OP_CALLBACKREQUESTED_IPV6 from %1")
                             .arg(requester.toString()));
        emit callbackRequestedIPv6(requester);
        break;
    }

    case OP_CALLBACK_FAIL:
        logWarning(QStringLiteral("Server: Callback attempt failed"));
        break;

    case OP_REJECT:
        logWarning(QStringLiteral("Server rejected our request"));
        emit rejectReceived();
        break;

    default:
        logServerVerbose(QStringLiteral("ServerSocket: Unknown opcode 0x%1, size %2")
                     .arg(opcode, 2, 16, QLatin1Char('0'))
                     .arg(size));
        break;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Connection state management
// ---------------------------------------------------------------------------

bool ServerSocket::failMalformed()
{
    // MFC: the read throws and ProcessPacket ends in SetConnectionState(CS_DISCONNECTED)
    failWith(ServerConnState::Disconnected,
             failureFor(m_connectionState, m_tcpConnected, std::nullopt));
    return false;
}

void ServerSocket::failWith(ServerConnState newState, ServerFailure failure)
{
    if (m_connectionState != newState)
        m_lastFailure = failure;
    setConnectionState(newState);
}

void ServerSocket::setConnectionState(ServerConnState newState)
{
    if (m_connectionState == newState)
        return;

    logServerVerbose(QStringLiteral("[%1] socket state: %2 -> %3")
                         .arg(m_curServer ? m_curServer->name() : QStringLiteral("?"))
                         .arg(QLatin1String(serverConnStateName(m_connectionState)))
                         .arg(QLatin1String(serverConnStateName(newState))));

    m_connectionState = newState;
    emit connectionStateChanged(newState);

    // The reference fires ConnectionFailed() for every state below CS_CONNECTING —
    // CServerSocket::SetConnectionState(), srchybrid/ServerSocket.cpp:726-733. Its
    // constants are negative for the terminal states (CS_FATALERROR=-5 ...
    // CS_SERVERFULL=-1, CS_NOTCONNECTED=0), so CS_DISCONNECTED is in that set.
    // Narrowing this to ServerDead/FatalError/ServerFull is what made the
    // Disconnected branch of ServerConnect::connectionFailed() dead code.
    switch (newState) {
    case ServerConnState::Connecting:
    case ServerConnState::WaitForLogin:
    case ServerConnState::Connected:
        break;
    default:
        emit connectionFailed(newState);
        break;
    }
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

void ServerSocket::onError(int errorCode)
{
    logWarning(QStringLiteral("ServerSocket error: %1 server=%2 peer=%3 connState=%4 cryptState=%5")
                   .arg(errorCode)
                   .arg(m_curServer ? m_curServer->name() : QStringLiteral("?"))
                   .arg(Endpoint(m_sessionAddress, peerPort()).toString())
                   .arg(static_cast<int>(m_connectionState))
                   .arg(static_cast<int>(m_streamCryptState)));

    // Any socket error on an *established* connection is a lost connection, not a
    // dead server or a fatal error. Qt reports the peer's FIN as an error before
    // disconnected(), so without this an ordinary server drop landed in FatalError:
    // the wrong log line, no auto-reconnect, and on the sibling ServerDead route
    // Server::incFailedCount() could eventually delete a healthy server from the
    // list. The reference sees the close directly and maps CS_CONNECTED to
    // CS_DISCONNECTED — CServerSocket::OnClose(), srchybrid/ServerSocket.cpp:713-724.
    //
    // Both this and onSocketError() are live entry points — EMSocket funnels its
    // errors here, Qt's errorOccurred goes to onSocketError() — and either can land
    // first, so they must make the same decision.
    // A Qt socket error: the code decides. EMSocket hands it over because the int
    // above is shared with the protocol error codes (wrong header, encryption).
    if (const auto reported = std::exchange(m_socketErrorInFlight, std::nullopt)) {
        const auto socketError = refinedSocketError(*reported, errorString(), m_tcpConnected);
        failWith(stateForSocketError(m_connectionState, socketError),
                 failureFor(m_connectionState, m_tcpConnected, socketError));
        return;
    }

    const ServerFailure failure = failureFor(m_connectionState, m_tcpConnected, std::nullopt);
    if (m_connectionState == ServerConnState::Connected) {
        failWith(ServerConnState::Disconnected, failure);
    } else if (m_connectionState == ServerConnState::Connecting ||
               m_connectionState == ServerConnState::WaitForLogin) {
        failWith(ServerConnState::ServerDead, failure);   // the server sent garbage
    } else {
        failWith(ServerConnState::FatalError, failure);
    }
}

QAbstractSocket::SocketError ServerSocket::refinedSocketError(QAbstractSocket::SocketError error,
                                                             const QString& errorText,
                                                             bool tcpConnected)
{
    if (error != QAbstractSocket::NetworkError || tcpConnected)
        return error;
    // The text is the system's own for ECONNREFUSED (seen on Darwin, where the
    // refusal surfaces after a connected() without a peer).
    const QString refused = QString::fromStdString(std::generic_category().message(ECONNREFUSED));
    if (errorText.compare(refused, Qt::CaseInsensitive) == 0
        || errorText.contains(QLatin1String("connection refused"), Qt::CaseInsensitive))
        return QAbstractSocket::ConnectionRefusedError;
    return error;
}

ServerFailure ServerSocket::failureFor(ServerConnState current, bool tcpConnected,
                                       std::optional<QAbstractSocket::SocketError> socketError)
{
    using Phase = ServerFailure::Phase;
    using Reason = ServerFailure::Reason;

    if (current == ServerConnState::Connected)
        return {Phase::Established, Reason::EstablishedDisconnect};

    const Phase phase = tcpConnected ? Phase::Handshake : Phase::Connect;
    if (!socketError)
        return {phase, Reason::ProtocolRejection};

    switch (*socketError) {
    case QAbstractSocket::ConnectionRefusedError:
        return {phase, Reason::ConnectionRefused};
    case QAbstractSocket::SocketTimeoutError:
        return {phase, Reason::TimeoutUnreachable};
    case QAbstractSocket::RemoteHostClosedError:
        return {phase, Reason::ProtocolRejection};
    case QAbstractSocket::HostNotFoundError:
        return {Phase::Resolve, Reason::DnsResolution};

    case QAbstractSocket::NetworkError:
    case QAbstractSocket::SocketAccessError:
    case QAbstractSocket::SocketResourceError:
    case QAbstractSocket::AddressInUseError:
    case QAbstractSocket::SocketAddressNotAvailableError:
    case QAbstractSocket::UnsupportedSocketOperationError:
    case QAbstractSocket::ProxyAuthenticationRequiredError:
    case QAbstractSocket::ProxyConnectionRefusedError:
    case QAbstractSocket::ProxyConnectionClosedError:
    case QAbstractSocket::ProxyConnectionTimeoutError:
    case QAbstractSocket::ProxyNotFoundError:
    case QAbstractSocket::ProxyProtocolError:
    case QAbstractSocket::OperationError:
    case QAbstractSocket::TemporaryError:
        return {tcpConnected ? phase : Phase::SocketSetup, Reason::LocalBindInterface};

    default:
        return {phase, Reason::TransportOther};
    }
}

ServerConnState ServerSocket::stateForSocketError(ServerConnState current,
                                                  QAbstractSocket::SocketError error)
{
    // A drop on an established connection is a disconnect whatever the code.
    if (current == ServerConnState::Connected)
        return ServerConnState::Disconnected;

    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
    case QAbstractSocket::SocketTimeoutError:
        return ServerConnState::ServerDead;

    case QAbstractSocket::RemoteHostClosedError:
        // Closed on us before the login was answered: full, not dead.
        // MFC CServerSocket::OnClose, srchybrid/ServerSocket.cpp:713-724.
        return current == ServerConnState::WaitForLogin ? ServerConnState::ServerFull
                                                        : ServerConnState::ServerDead;

    case QAbstractSocket::HostNotFoundError:
        return ServerConnState::Error;   // a resolver failure is not the server's

    // Our side or the path: no route, interface gone, bind refused, proxy trouble.
    // MFC leaves WSAENETUNREACH out of the dead-server errors for this reason
    // (srchybrid/ServerSocket.cpp:117).
    case QAbstractSocket::NetworkError:
    case QAbstractSocket::SocketAccessError:
    case QAbstractSocket::SocketResourceError:
    case QAbstractSocket::AddressInUseError:
    case QAbstractSocket::SocketAddressNotAvailableError:
    case QAbstractSocket::UnsupportedSocketOperationError:
    case QAbstractSocket::ProxyAuthenticationRequiredError:
    case QAbstractSocket::ProxyConnectionRefusedError:
    case QAbstractSocket::ProxyConnectionClosedError:
    case QAbstractSocket::ProxyConnectionTimeoutError:
    case QAbstractSocket::ProxyNotFoundError:
    case QAbstractSocket::ProxyProtocolError:
    case QAbstractSocket::OperationError:
    case QAbstractSocket::TemporaryError:
        return ServerConnState::FatalError;

    default:
        return current == ServerConnState::Connecting ? ServerConnState::ServerDead
                                                      : ServerConnState::FatalError;
    }
}

// ---------------------------------------------------------------------------
// Socket event handlers
// ---------------------------------------------------------------------------

void ServerSocket::onSocketConnected()
{
    // Darwin 27 answers a refused connect with EISCONN and Qt emits connected() with
    // no peer (port 0) before the refusal — that is not a TCP connect.
    m_tcpConnected = peerPort() != 0;
    if (!m_tcpConnected) {
        // Nothing to log in over: the error for this dial follows (or our own
        // timeout does). Going on used to send a login into a dead socket and
        // report the attempt as a handshake rather than a failed connect.
        logServerVerbose(QStringLiteral("[%1] connected() without a peer — waiting for the error")
                             .arg(m_curServer ? m_curServer->name() : QStringLiteral("?")));
        return;
    }
    logInfo(QStringLiteral("ServerSocket connected to %1 (%2) serverCrypt=%3")
                .arg(m_curServer ? m_curServer->name() : QStringLiteral("?"))
                .arg(Endpoint(Address::fromQHostAddress(peerAddress()), peerPort()).toString())
                .arg(isServerCryptEnabledConnection()));
    m_lastTransmission = getTickCount();

    if (isServerCryptEnabledConnection()) {
        // Defer WaitForLogin until DH handshake completes —
        // onEncryptionHandshakeComplete() will trigger it.
        logServerVerbose(QStringLiteral("TCP connected — waiting for obfuscation (DH) handshake before login"));
        m_pendingLogin = true;
    } else {
        setConnectionState(ServerConnState::WaitForLogin);
    }
}

void ServerSocket::onEncryptionHandshakeComplete()
{
    if (m_pendingLogin) {
        logServerVerbose(QStringLiteral("Obfuscation handshake complete — proceeding to login"));
        m_pendingLogin = false;
        setConnectionState(ServerConnState::WaitForLogin);
    }
}

void ServerSocket::onSocketDisconnected()
{
    if (m_isDeleting)
        return;

    const ServerFailure failure = failureFor(m_connectionState, m_tcpConnected, std::nullopt);
    if (m_connectionState == ServerConnState::Connected) {
        failWith(ServerConnState::Disconnected, failure);
    } else if (m_connectionState == ServerConnState::WaitForLogin) {
        // MFC CServerSocket::OnClose, srchybrid/ServerSocket.cpp:713-724.
        failWith(ServerConnState::ServerFull, failure);
    } else if (m_connectionState == ServerConnState::Connecting) {
        failWith(ServerConnState::ServerDead, failure);
    }
    // Any other state is already terminal: onSocketError() normally runs first (Qt
    // reports the peer's FIN as an error before disconnected()) and has set it.
    // Overwriting it here would re-fire connectionFailed() with the wrong reason.
}

void ServerSocket::onSocketError(QAbstractSocket::SocketError error)
{
    if (m_isDeleting)
        return;

    logWarning(QStringLiteral("ServerSocket::onSocketError: %1 (%2) server=%3 peer=%4 connState=%5 cryptState=%6")
                   .arg(static_cast<int>(error)).arg(errorString())
                   .arg(m_curServer ? m_curServer->name() : QStringLiteral("?"))
                   .arg(Endpoint(m_sessionAddress, peerPort()).toString())
                   .arg(static_cast<int>(m_connectionState))
                   .arg(static_cast<int>(m_streamCryptState)));

    // Normally the second notification: EMSocket routed the error to onError()
    // first and the state is already terminal. Firing again would report the same
    // failure twice.
    switch (m_connectionState) {
    case ServerConnState::Connecting:
    case ServerConnState::WaitForLogin:
    case ServerConnState::Connected:
        break;
    default:
        return;
    }
    m_socketErrorInFlight.reset();
    const auto socketError = refinedSocketError(error, errorString(), m_tcpConnected);
    failWith(stateForSocketError(m_connectionState, socketError),
             failureFor(m_connectionState, m_tcpConnected, socketError));
}

void ServerSocket::startDnsLookup(QDnsLookup::Type type)
{
    // Never destroy the old QDnsLookup from inside its own finished handler.
    if (m_dnsLookup) {
        m_dnsLookup->disconnect(this);
        m_dnsLookup.release()->deleteLater();
    }

    m_dnsLookup = std::make_unique<QDnsLookup>(type, m_curServer->dynIP(), this);
    connect(m_dnsLookup.get(), &QDnsLookup::finished, this, &ServerSocket::onDnsLookupFinished);
    m_dnsLookup->lookup();
}

void ServerSocket::onDnsLookupFinished()
{
    if (!m_dnsLookup || !m_curServer)
        return;

    const bool failed = m_dnsLookup->error() != QDnsLookup::NoError
                        || m_dnsLookup->hostAddressRecords().isEmpty();
    if (failed) {
        // Try the other family once. This is what makes an AAAA-only server hostname
        // reachable; the default order is A first because a server reached over IPv6
        // with no routable IPv4 hands out a LowID unconditionally.
        if (!m_dnsTriedFallback) {
            m_dnsTriedFallback = true;
            const auto next = (m_dnsLookup->type() == QDnsLookup::A) ? QDnsLookup::AAAA
                                                                     : QDnsLookup::A;
            logServerVerbose(QStringLiteral("DNS %1 lookup for %2 found nothing — trying %3")
                                 .arg(m_dnsLookup->type() == QDnsLookup::A
                                          ? QStringLiteral("A") : QStringLiteral("AAAA"))
                                 .arg(m_curServer->dynIP())
                                 .arg(next == QDnsLookup::A
                                          ? QStringLiteral("A") : QStringLiteral("AAAA")));
            startDnsLookup(next);
            return;
        }

        logWarning(QStringLiteral("DNS lookup failed for %1: %2")
                       .arg(m_curServer->dynIP())
                       .arg(m_dnsLookup->errorString()));
        // Not the server's failure (it may be our resolver): move on, count nothing.
        failWith(ServerConnState::Error,
                 {ServerFailure::Phase::Resolve, ServerFailure::Reason::DnsResolution});
        return;
    }

    const QHostAddress addr = m_dnsLookup->hostAddressRecords().first().value();
    const Address resolved = Address::fromQHostAddress(addr);   // either family

    // A dynIP server skipped the addServer filter; check the resolved IP now and have
    // the list entry deleted. MFC: CServerSocket::OnHostnameResolved() — ServerSocket.cpp:91.
    if (ServerList::isFilteredServerIP(resolved, u"TCP/DNSResolve", m_curServer->dynIP())) {
        emit dynIPFiltered(resolved);
        // CS_ERROR: no failed-count bump; our own filter, not the server's doing
        failWith(ServerConnState::Error,
                 {ServerFailure::Phase::Resolve, ServerFailure::Reason::TransportOther});
        return;
    }

    m_curServer->setIpAddress(resolved);
    m_sessionAddress = resolved;
    emit dynIPResolved(resolved, m_curServer->dynIP());

    // Now connect
    uint16 port = m_curServer->port();
    if (!m_noCrypt && m_curServer->supportsObfuscationTCP()) {
        setConnectionEncryption(true, nullptr, true);
        port = m_curServer->obfuscationPortTCP();
    }

    connectToPeer(resolved, port);
}

} // namespace eMule
