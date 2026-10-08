#include "pch.h"
/// @file Server.cpp
/// @brief ED2K server data entity implementation — port of CServer from MFC.

#include "Server.h"
#include "app/AppContext.h"
#include "protocol/Tag.h"

#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QUrl>

namespace eMule {

static std::atomic<uint32> s_nextServerId{1};

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

Server::Server(uint32 ip, uint16 port)
    : m_serverId(s_nextServerId++)
    , m_address(Address::fromNetworkOrder(ip))
    , m_port(port)
{
}

Server::Server(const Address& addr, uint16 port)
    : m_serverId(s_nextServerId++)
    , m_address(addr)
    , m_port(port)
{
}

Server::Server(FileDataIO& data, bool optUTF8)
    : m_serverId(s_nextServerId++)
{
    m_address = Address::fromNetworkOrder(data.readUInt32());
    m_port    = data.readUInt16();

    const uint32 tagCount = readTagCount(data, kMaxFileTags);
    for (uint32 i = 0; i < tagCount; ++i) {
        Tag tag(data, optUTF8);
        addTagFromFile(tag);
    }
}

Server::Server(const Server& other)
    : m_serverId(other.m_serverId)
    , m_address(other.m_address)
    , m_addressV6(other.m_addressV6)
    , m_port(other.m_port)
    , m_dynIP(other.m_dynIP)
    , m_name(other.m_name)
    , m_description(other.m_description)
    , m_version(other.m_version)
    , m_preference(other.m_preference)
    , m_staticMember(other.m_staticMember)
    , m_files(other.m_files)
    , m_users(other.m_users)
    , m_maxUsers(other.m_maxUsers)
    , m_softFiles(other.m_softFiles)
    , m_hardFiles(other.m_hardFiles)
    , m_lowIDUsers(other.m_lowIDUsers)
    , m_ping(other.m_ping)
    , m_failedCount(other.m_failedCount)
    , m_disabled(other.m_disabled)
    , m_lastPingedTime(other.m_lastPingedTime)
    , m_realLastPingedTime(other.m_realLastPingedTime)
    , m_lastPinged(other.m_lastPinged)
    , m_lastDescPingedCount(other.m_lastDescPingedCount)
    , m_challenge(other.m_challenge)
    , m_descReqChallenge(other.m_descReqChallenge)
    , m_tcpFlags(other.m_tcpFlags)
    , m_udpFlags(other.m_udpFlags)
    , m_obfuscationPortTCP(other.m_obfuscationPortTCP)
    , m_obfuscationPortUDP(other.m_obfuscationPortUDP)
    , m_serverKeyUDP(other.m_serverKeyUDP)
    , m_serverKeyUDPIP(other.m_serverKeyUDPIP)
    , m_cryptPingReplyPending(other.m_cryptPingReplyPending)
    , m_triedCryptOnce(other.m_triedCryptOnce)
    , m_auxPortsList(other.m_auxPortsList)
    , m_serverHash(other.m_serverHash)
    , m_hasServerHash(other.m_hasServerHash)
    , m_metaApiUrl(other.m_metaApiUrl)
    , m_metaApiPin(other.m_metaApiPin)
    , m_metaNetworks(other.m_metaNetworks)
{
}

// ---------------------------------------------------------------------------
// address()
// ---------------------------------------------------------------------------

QString Server::address() const
{
    return m_dynIP.isEmpty() ? ipstr(m_address) : m_dynIP;
}

QString Server::addressWithPort() const
{
    return QStringLiteral("%1:%2").arg(bracketedAddress()).arg(m_port);
}

QString Server::bracketedAddress() const
{
    if (m_dynIP.isEmpty() && m_address.isIPv6())
        return QStringLiteral("[%1]").arg(ipstr(m_address));
    return address();
}

// ---------------------------------------------------------------------------
// Dual-stack addresses
// ---------------------------------------------------------------------------

bool Server::addAddress(const Address& addr)
{
    if (addr.isNull() || !m_dynIP.isEmpty())
        return false;
    if (addr.isIPv4()) {
        if (m_address.isIPv6() && m_addressV6.isNull())
            m_addressV6 = m_address;    // v6-only entry learns its IPv4: v4 becomes primary
        m_address = addr;
    } else if (m_address.isIPv4()) {
        m_addressV6 = addr;
    } else {
        m_address = addr;               // null or v6 primary: replace
    }
    return true;
}

const Address& Server::dialAddress(bool preferIPv6) const
{
    if (preferIPv6 && !m_addressV6.isNull())
        return m_addressV6;
    return m_address;
}

Address Server::otherFamilyAddress(const Address& current) const
{
    if (m_addressV6.isNull() || current.isNull())
        return {};
    return current.isIPv6() ? ipv4Address() : m_addressV6;
}

void Server::setServerHash(const uint8* hash)
{
    if (!hash)
        return;
    std::copy_n(hash, m_serverHash.size(), m_serverHash.begin());
    m_hasServerHash = true;
}

// ---------------------------------------------------------------------------
// fromAddressString()
// ---------------------------------------------------------------------------

std::unique_ptr<Server> Server::fromAddressString(const QString& host, uint16 port)
{
    QString bare = host.trimmed();
    if (bare.size() > 2 && bare.startsWith(u'[') && bare.endsWith(u']'))
        bare = bare.mid(1, bare.size() - 2);
    if (bare.isEmpty())
        return nullptr;

    if (const Address addr = Address::fromString(bare); !addr.isNull())
        return std::make_unique<Server>(addr, port);

    // Not a literal — keep it as a hostname so ServerSocket re-resolves it on every
    // connect (A first, then AAAA). Storing a one-shot resolved IP instead would
    // silently pin a dynamic-IP server to a stale address.
    auto server = std::make_unique<Server>(uint32{0}, port);
    server->setDynIP(bare);
    return server;
}

// ---------------------------------------------------------------------------
// setLastDescPingedCount()
// ---------------------------------------------------------------------------

void Server::setLastDescPingedCount(bool reset)
{
    if (reset)
        m_lastDescPingedCount = 0;
    else
        ++m_lastDescPingedCount;
}

// ---------------------------------------------------------------------------
// UDP obfuscation key — bound to the public IP it was issued for
// ---------------------------------------------------------------------------

uint32 Server::serverKeyUDP() const
{
    // MFC: CServer::GetServerKeyUDP() — Server.cpp:322. Every send/receive site
    // in srchybrid/UDPSocket.cpp calls the non-forced form, so a key restored
    // from server.met after our IP changed is never used to obfuscate. Without
    // this the packet goes out encrypted to port+14 with a key the server has
    // long since dropped, and no reply ever comes back.
    return hasValidUDPKey(theApp.publicIP()) ? m_serverKeyUDP : 0;
}

void Server::setServerKeyUDP(uint32 key)
{
    // MFC: CServer::SetServerKeyUDP() — Server.cpp:329. The stamp and the key
    // must be set together; splitting them is how a key ends up attributed to
    // the wrong IP.
    m_serverKeyUDP = key;
    m_serverKeyUDPIP = theApp.publicIP();
}

bool Server::isMetaApiUrl(const QString& url)
{
    const QUrl u(url);
    return u.isValid() && !u.host().isEmpty() && (u.scheme() == u"https" || u.scheme() == u"http");
}

// ---------------------------------------------------------------------------
// addTagFromFile() — apply a deserialized tag to server properties
// ---------------------------------------------------------------------------

void Server::addTagFromFile(const Tag& tag)
{
    switch (tag.nameId()) {
    case ST_SERVERNAME:
        if (tag.isStr() && m_name.isEmpty())
            m_name = tag.strValue();
        break;
    case ST_DESCRIPTION:
        if (tag.isStr() && m_description.isEmpty())
            m_description = tag.strValue();
        break;
    case ST_PING:
        if (tag.isInt())
            m_ping = tag.intValue();
        break;
    case ST_FAIL:
        if (tag.isInt())
            m_failedCount = tag.intValue();
        break;
    case ST_PREFERENCE:
        if (tag.isInt())
            m_preference = static_cast<ServerPriority>(tag.intValue());
        break;
    case ST_DYNIP:
        if (tag.isStr() && !tag.strValue().isEmpty() && m_dynIP.isEmpty()) {
            m_dynIP = tag.strValue();
            m_address = Address();  // reset outdated IP when dynIP is set
            m_addressV6 = Address();
        }
        break;
    case ST_IPV6:
        // Local extension: the server's IPv6. With a 0 header it is the only address;
        // next to an IPv4 header it is the second address of a dual-stack server
        // (stock eMule skips the tag and keeps the IPv4). First tag wins; a dynIP wins.
        if (tag.isHash() && m_dynIP.isEmpty() && ipv6Address().isNull()) {
            if (const Address v6 = Address::fromIPv6Bytes(tag.hashValue()); v6.isIPv6())
                addAddress(v6);
        }
        break;
    case ST_PORT:
    case ST_IP:
        // IP and port are authoritative from the ServerMet_Struct header (read in
        // the Server(FileDataIO&) ctor before this tag loop). An ST_PORT/ST_IP tag
        // is discarded so a crafted server.met from an untrusted list cannot
        // redirect us to an attacker-chosen address. MFC: CServer::AddTagFromFile()
        // — Server.cpp:203-206.
        break;
    case ST_MAXUSERS:
        if (tag.isInt())
            m_maxUsers = tag.intValue();
        break;
    case ST_SOFTFILES:
        if (tag.isInt())
            m_softFiles = tag.intValue();
        break;
    case ST_HARDFILES:
        if (tag.isInt())
            m_hardFiles = tag.intValue();
        break;
    case ST_LASTPING:
        if (tag.isInt())
            m_lastPingedTime = tag.intValue();
        break;
    case ST_VERSION:
        if (tag.isStr()) {
            if (m_version.isEmpty())
                m_version = tag.strValue();
        } else if (tag.isInt()) {
            // Integer version: major.minor format
            m_version = QStringLiteral("%1.%2")
                .arg(tag.intValue() >> 16)
                .arg(tag.intValue() & 0xFFFF, 2, 10, QChar(u'0'));
        }
        break;
    case ST_UDPFLAGS:
        if (tag.isInt())
            m_udpFlags = tag.intValue();
        break;
    case ST_AUXPORTSLIST:
        if (tag.isStr())
            m_auxPortsList = tag.strValue();
        break;
    case ST_LOWIDUSERS:
        if (tag.isInt())
            m_lowIDUsers = tag.intValue();
        break;
    case ST_UDPKEY:
        if (tag.isInt())
            m_serverKeyUDP = tag.intValue();
        break;
    case ST_UDPKEYIP:
        if (tag.isInt())
            m_serverKeyUDPIP = tag.intValue();
        break;
    case ST_TCPPORTOBFUSCATION:
        if (tag.isInt())
            m_obfuscationPortTCP = static_cast<uint16>(tag.intValue());
        break;
    case ST_UDPPORTOBFUSCATION:
        if (tag.isInt())
            m_obfuscationPortUDP = static_cast<uint16>(tag.intValue());
        break;
    default:
        // Handle legacy string-named tags: "files" and "users"
        if (tag.nameId() == 0 && tag.name() == QByteArray("files")) {
            if (tag.isInt())
                m_files = tag.intValue();
        } else if (tag.nameId() == 0 && tag.name() == QByteArray("users")) {
            if (tag.isInt())
                m_users = tag.intValue();
        } else if (tag.nameId() == 0 && tag.name() == QByteArray("disabled")) {
            // Local extension, string-named so it cannot collide with an ST_ id.
            if (tag.isInt())
                m_disabled = tag.intValue() != 0;
        } else if (tag.nameId() == 0 && tag.name() == QByteArray("metaapi")) {
            // eNode Meta API, learned from the ident: kept so a server can be asked
            // without logging in to it first
            if (tag.isStr() && isMetaApiUrl(tag.strValue()))
                m_metaApiUrl = tag.strValue();
        } else if (tag.nameId() == 0 && tag.name() == QByteArray("metaapipin")) {
            if (tag.isStr())
                m_metaApiPin = tag.strValue();
        } else if (tag.nameId() == 0 && tag.name() == QByteArray("metanetworks")) {
            if (tag.isInt())
                m_metaNetworks = static_cast<uint32>(tag.intValue());
        } else {
            logWarning(QStringLiteral("Unknown server.met tag: nameId=0x%1")
                .arg(tag.nameId(), 2, 16, QChar(u'0')));
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// writeTags() — serialize non-default properties as tags
// ---------------------------------------------------------------------------

uint32 Server::writeTags(FileDataIO& file) const
{
    uint32 count = 0;

    if (!m_name.isEmpty()) {
        Tag(ST_SERVERNAME, m_name).writeNewEd2kTag(file, UTF8Mode::OptBOM);
        ++count;
    }

    if (!m_dynIP.isEmpty()) {
        Tag(ST_DYNIP, m_dynIP).writeNewEd2kTag(file, UTF8Mode::OptBOM);
        ++count;
    }

    // An IPv6 does not fit the 4-byte header field (0 for a v6-only server, the IPv4
    // for a dual-stack one) — persist it here or it is lost on the next load.
    if (const Address v6 = ipv6Address(); v6.isIPv6() && m_dynIP.isEmpty()) {
        Tag(ST_IPV6, v6.ipv6Bytes().data()).writeNewEd2kTag(file);
        ++count;
    }

    if (!m_description.isEmpty()) {
        Tag(ST_DESCRIPTION, m_description).writeNewEd2kTag(file, UTF8Mode::OptBOM);
        ++count;
    }

    if (m_failedCount != 0) {
        Tag(ST_FAIL, m_failedCount).writeNewEd2kTag(file);
        ++count;
    }

    if (m_disabled) {
        Tag(QByteArray("disabled"), uint32{1}).writeTagToFile(file);
        ++count;
    }

    if (hasMetaApi()) {
        Tag(QByteArray("metaapi"), m_metaApiUrl).writeTagToFile(file, UTF8Mode::OptBOM);
        ++count;
        if (!m_metaApiPin.isEmpty()) {
            Tag(QByteArray("metaapipin"), m_metaApiPin).writeTagToFile(file, UTF8Mode::OptBOM);
            ++count;
        }
        if (m_metaNetworks != 0) {
            Tag(QByteArray("metanetworks"), m_metaNetworks).writeTagToFile(file);
            ++count;
        }
    }

    if (m_preference != ServerPriority::Normal) {
        Tag(ST_PREFERENCE, static_cast<uint32>(m_preference)).writeNewEd2kTag(file);
        ++count;
    }

    if (m_users != 0) {
        Tag(QByteArray("users"), m_users).writeTagToFile(file);
        ++count;
    }

    if (m_files != 0) {
        Tag(QByteArray("files"), m_files).writeTagToFile(file);
        ++count;
    }

    if (m_ping != 0) {
        Tag(ST_PING, m_ping).writeNewEd2kTag(file);
        ++count;
    }

    if (m_lastPingedTime != 0) {
        Tag(ST_LASTPING, m_lastPingedTime).writeNewEd2kTag(file);
        ++count;
    }

    if (m_maxUsers != 0) {
        Tag(ST_MAXUSERS, m_maxUsers).writeNewEd2kTag(file);
        ++count;
    }

    if (m_softFiles != 0) {
        Tag(ST_SOFTFILES, m_softFiles).writeNewEd2kTag(file);
        ++count;
    }

    if (m_hardFiles != 0) {
        Tag(ST_HARDFILES, m_hardFiles).writeNewEd2kTag(file);
        ++count;
    }

    if (!m_version.isEmpty()) {
        Tag(ST_VERSION, m_version).writeNewEd2kTag(file, UTF8Mode::OptBOM);
        ++count;
    }

    if (m_udpFlags != 0) {
        Tag(ST_UDPFLAGS, m_udpFlags).writeNewEd2kTag(file);
        ++count;
    }

    if (m_lowIDUsers != 0) {
        Tag(ST_LOWIDUSERS, m_lowIDUsers).writeNewEd2kTag(file);
        ++count;
    }

    if (m_serverKeyUDP != 0) {
        Tag(ST_UDPKEY, m_serverKeyUDP).writeNewEd2kTag(file);
        ++count;
    }

    if (m_serverKeyUDPIP != 0) {
        Tag(ST_UDPKEYIP, m_serverKeyUDPIP).writeNewEd2kTag(file);
        ++count;
    }

    if (m_obfuscationPortTCP != 0) {
        Tag(ST_TCPPORTOBFUSCATION, static_cast<uint32>(m_obfuscationPortTCP)).writeNewEd2kTag(file);
        ++count;
    }

    if (m_obfuscationPortUDP != 0) {
        Tag(ST_UDPPORTOBFUSCATION, static_cast<uint32>(m_obfuscationPortUDP)).writeNewEd2kTag(file);
        ++count;
    }

    if (!m_auxPortsList.isEmpty()) {
        Tag(ST_AUXPORTSLIST, m_auxPortsList).writeNewEd2kTag(file, UTF8Mode::OptBOM);
        ++count;
    }

    return count;
}

} // namespace eMule
