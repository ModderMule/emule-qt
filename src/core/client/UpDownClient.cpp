#include "pch.h"
/// @file UpDownClient.cpp
/// @brief UpDownClient implementation — identity, state, Compare, version, handshake,
///        connection management, secure identity, chat, preview, firewall.
///
/// Ported from MFC srchybrid/BaseClient.cpp + DownloadClient.cpp.

#include "client/UpDownClient.h"
#include "media/FrameGrabThread.h"
#include "client/ClientCensus.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/DeadSourceList.h"
#include "app/AppContext.h"
#include "crypto/AICHHashSet.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "httpcache/HttpCacheManager.h"
#include "files/SharedFileList.h"
#include "friends/Friend.h"
#include "friends/FriendList.h"
#include "ipfilter/IPFilter.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUDPListener.h"
#include "net/ClientReqSocket.h"
#include "net/ClientUDPSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "net/PeerVetting.h"
#include "transfer/DownloadQueue.h"
#include "stats/Statistics.h"
#include "transfer/UploadQueue.h"
#include "utils/TimeUtils.h"

#include "utils/Log.h"

#include <utility>
#include <QBuffer>
#include <QCoreApplication>
#include <QCborArray>
#include <QCborMap>
#include <QImage>
#include <QThreadPool>
#include <QPointer>
#include <QImageReader>
#include <QRandomGenerator>




namespace eMule {

// ===========================================================================
// Construction / Destruction
// ===========================================================================

UpDownClient::UpDownClient(QObject* parent)
    : QObject(parent)
{
    init();
}

UpDownClient::UpDownClient(uint16 port, uint32 userId, uint32 serverIP,
                           uint16 serverPort, PartFile* reqFile,
                           bool ed2kID, QObject* parent)
    : QObject(parent)
    , m_reqFile(reqFile)
{
    init();

    m_userPort = port;

    // Convert ED2K user ID to hybrid format if needed
    m_userIDHybrid = (ed2kID && !isLowID(userId)) ? ntohl(userId) : userId;

    // For high-ID clients, set the connect IP
    if (!hasLowID())
        m_connectAddress = Address::fromNetworkOrder(ed2kID ? userId : ntohl(userId));

    m_serverAddress = Address::fromNetworkOrder(serverIP);
    m_serverPort = serverPort;
}

UpDownClient::~UpDownClient()
{
    // An AICH recovery request dies with us — hand the part to another source rather
    // than letting it sit unanswered for ever. MFC srchybrid/BaseClient.cpp:262-267.
    if (m_aichRequested) {
        m_aichRequested = false;
        AICHRecoveryHashSet::clientAICHRequestFailed(this);
    }

    // Break the friend link from this side, so the friend entry is not left pointing at a
    // destroyed client — and, since setLinkedClient() takes the slot flag back with it, so
    // the friend keeps its slot for when it reconnects. MFC srchybrid/BaseClient.cpp:269-273.
    if (Friend* f = friendPtr()) {
        // A dial in flight ends here; the report must not touch this client, which is
        // already inside its own destructor. MFC srchybrid/BaseClient.cpp:269-272.
        if (f->isTryingToConnect())
            f->updateFriendConnectionState(FriendConnectReport::Deleted);
        f->setLinkedClient(nullptr);
    }
    m_friend = nullptr;

    // Clear rate data
    m_averageUDR.clear();
    m_averageDDR.clear();
    m_upPartStatus.clear();
    m_partStatus.clear();

    // Clean up pending block requests
    clearDownloadBlockRequests();

    // Clean up upload block requests
    flushSendBlocks();

    // Clean up requested files list
    for (auto* req : m_requestedFiles)
        delete req;
    m_requestedFiles.clear();

    // Clean up waiting packets
    m_waitingPackets.clear();

    // Leave the file's source list. No swap: a client that is going away must not be
    // filed as a source of another file.
    if (m_reqFile) {
        m_reqFile->removeSource(this);
        m_reqFile = nullptr;
    }
    // Never leave this client in another file's A4AF list.
    removeFromAllOtherLists();

    // Remove from upload file
    if (m_uploadFile) {
        m_uploadFile->removeUploadingClient(this);
        m_uploadFile = nullptr;
    }

    // Do NOT delete socket — ownership is external
    m_socket = nullptr;
}

// ===========================================================================
// init() — matches MFC CUpDownClient::Init() (BaseClient.cpp:101-260)
// ===========================================================================

void UpDownClient::init()
{
    m_credits = nullptr;
    m_friend = nullptr;
    m_lastPartAsked = UINT16_MAX;
    m_addNextConnect = false;

    // If socket existed we would get peer IP here, but Phase 1 has no socket
    m_userAddress = Address();
    m_connectAddress = Address();

    m_serverAddress = Address();
    m_userIDHybrid = 0;
    m_userPort = 0;
    m_serverPort = 0;
    m_clientVersion = 0;

    m_emuleVersion = 0;
    m_dataCompVer = 0;
    m_emuleProtocol = false;
    m_isHybrid = false;

    m_username.clear();
    md4clr(m_userHash.data());
    m_udpPort = 0;
    m_kadPort = 0;

    m_udpVer = 0;
    m_sourceExchange1Ver = 0;
    m_acceptCommentVer = 0;
    m_extendedRequestsVer = 0;

    m_compatibleClient = 0;
    m_friendSlot = false;
    m_commentDirty = false;
    m_isMLDonkey = false;

    m_gplEvildoer = false;
    m_helloAnswerPending = false;
    m_infoPacketsReceived = InfoPacketState::None;
    m_supportSecIdent = 0;

    m_lastSignatureAddress = Address();
    m_lastSourceRequest = 0;
    m_lastSourceAnswer = 0;
    m_lastAskedForSources = 0;
    m_searchID = 0;
    m_fileListRequested = 0;

    m_fileRating = 0;
    m_messagesReceived = 0;
    m_messagesSent = 0;
    m_multiPacket = false;

    m_unicodeSupport = false;
    m_buddyPort = 0;

    m_kadVersion = 0;
    m_captchasSent = 0;

    m_buddyAddress = Address();
    m_lastBuddyPingPongTime = getTickCount();
    setBuddyID(nullptr);

    m_clientSoft = ClientSoftware::Unknown;
    m_chatState = ChatState::None;
    m_kadState = KadState::None;
    m_secureIdentState = SecureIdentState::Unavailable;
    m_uploadState = UploadState::None;
    m_downloadState = DownloadState::None;
    m_sourceFrom = SourceFrom::Server;
    m_chatCaptchaState = ChatCaptchaState::None;
    m_connectingState = ConnectingState::None;

    m_transferredUp = 0;
    m_uploadTime = 0;
    m_askedCount = 0;
    m_lastUpRequest = 0;
    m_curSessionUp = 0;
    m_curSessionDown = 0;
    m_curQueueSessionPayloadUp = 0;
    m_addedPayloadQueueSession = 0;
    m_upPartCount = 0;
    m_upCompleteSourcesCount = 0;
    md4clr(m_reqUpFileId.data());
    m_slotNumber = 0;
    m_collectionUploadSlot = false;

    m_downAskedCount = 0;
    m_transferredDown = 0;
    m_curSessionPayloadDown = 0;
    m_downStartTime = 0;
    m_lastBlockOffset = UINT64_MAX;
    m_lastBlockReceived = 0;
    m_totalUDPPackets = 0;
    m_failedUDPPackets = 0;
    m_remoteQueueRank = 0;

    m_remoteQueueFull = false;
    m_completeSource = false;
    m_partCount = 0;

    m_showDR = 0;
    m_reaskPending = false;
    m_udpPending = false;
    m_transferredDownMini = false;

    m_reqStart = 0;
    m_reqEnd = 0;
    m_urlStartPos = UINT64_MAX;

    m_upDatarate = 0;
    m_sumForAvgUpDataRate = 0;

    m_downDatarate = 0;
    m_downDataRateMS = 0;
    m_sumForAvgDownDataRate = 0;

    m_lastRefreshedDLDisplay = 0;
    m_lastRefreshedULDisplay = getTickCount();

    // Random update wait: 0..999ms
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32> dist(0, SEC2MS(1) - 1);
    m_randomUpdateWait = dist(rng);

    // Capability flags (all false/0 by default)
    m_hashsetRequestingMD4 = false;
    m_sharedDirectories = false;
    m_sentCancelTransfer = false;
    m_noViewSharedFiles = false;
    m_supportsPreview = false;
    m_previewReqPending = false;
    m_previewAnsPending = false;
    m_isSpammer = false;
    m_messageFiltered = false;
    m_peerCache = false;
    m_queueRankPending = false;
    m_unaskQueueRankRecv = 0;
    m_failedFileIdReqs = 0;
    m_needOurPublicIP = false;
    m_supportsAICH = 0;
    m_aichRequested = false;
    m_sentOutOfPartReqs = false;
    m_supportsLargeFiles = false;
    m_extMultiPacket = false;
    m_requestsCryptLayer = false;
    m_supportsCryptLayer = false;
    m_requiresCryptLayer = false;
    m_supportsSourceEx2 = false;
    m_supportsCaptcha = false;
    m_directUDPCallback = false;
    m_supportsFileIdent = false;
    m_hashsetRequestingAICH = false;

    m_lastSwapForSourceExchangeTick = 0;
    m_lastTriedToConnect = m_lastRefreshedULDisplay - MIN2MS(20);
    m_sourceExchangeSwapped = false;

    // Phase 3 new members
    m_uploadFile = nullptr;
}

// ===========================================================================
// Identity
// ===========================================================================

void UpDownClient::setUserHash(const uint8* hash)
{
    if (hash)
        std::memcpy(m_userHash.data(), hash, 16);
    else
        md4clr(m_userHash.data());
    sourceIdentityChanged();
}

void UpDownClient::sourceIdentityChanged()
{
    if (m_reqFile)
        m_reqFile->rekeySource(this);
}

bool UpDownClient::hasValidHash() const
{
    return !isnulmd4(m_userHash.data());
}

int UpDownClient::hashType() const
{
    // Matches MFC GetHashType() (BaseClient.cpp:1766-1775)
    if (m_userHash[5] == 13 && m_userHash[14] == 110)
        return static_cast<int>(ClientSoftware::OldEMule);
    if (m_userHash[5] == 14 && m_userHash[14] == 111)
        return static_cast<int>(ClientSoftware::eMule);
    if (m_userHash[5] == 'M' && m_userHash[14] == 'L')
        return static_cast<int>(ClientSoftware::MLDonkey);
    return static_cast<int>(ClientSoftware::Unknown);
}

bool UpDownClient::hasLowID() const
{
    return isLowID(m_userIDHybrid);
}

DeadSourceKey UpDownClient::deadSourceKey() const
{
    DeadSourceKey key;
    if (hasLowID() && !m_userIPv6.isNull()) {
        key.ipv6 = m_userIPv6;
        key.port = m_userPort;
    } else if (!hasLowID() || !m_serverAddress.isNull()) {
        key.serverAddress = m_serverAddress;
        key.userID = m_userIDHybrid;
        key.port = m_userPort;
        key.kadPort = hasLowID() ? uint16{0} : m_kadPort;
    } else {
        // No ID to go by: the user hash (MFC: only for a peer with a buddy or direct
        // callback — the others cannot be dialled anyway).
        key.hash = m_userHash;
    }
    return key;
}

Friend* UpDownClient::friendPtr() const
{
    // MFC srchybrid/BaseClient.cpp:2814-2819.
    if (m_friend && theApp.friendList && !theApp.friendList->isValid(m_friend))
        return nullptr;
    return m_friend;
}

bool UpDownClient::isReachableForSlot() const
{
    // MFC srchybrid/UploadQueue.cpp:131 / UploadClient.cpp:205, plus the IPv6 disjunct this
    // port needs — see the note on the declaration.
    return !hasLowID()
        || m_connectAddress.isIPv6()
        || (m_socket && m_socket->isConnected());
}

bool UpDownClient::isIPv6Connection() const
{
    // The live socket is authoritative: it is the link the data actually rides.
    // fromQHostAddress() normalizes IPv4-mapped (::ffff:a.b.c.d) peers back to plain
    // IPv4, so a dual-stack listener never misreports an IPv4 peer as v6.
    if (m_socket) {
        const Address peer = Address::fromQHostAddress(m_socket->peerAddress());
        if (!peer.isNull())
            return peer.isIPv6();
    }
    if (!m_connectAddress.isNull())
        return m_connectAddress.isIPv6();
    return m_userAddress.isIPv6();
}

void UpDownClient::setBuddyID(const uint8* id)
{
    if (id) {
        std::memcpy(m_buddyID.data(), id, 16);
        m_buddyIDValid = !isnulmd4(id);
    } else {
        md4clr(m_buddyID.data());
        m_buddyIDValid = false;
    }
}

void UpDownClient::setReqUpFileId(const uint8* id)
{
    if (id)
        std::memcpy(m_reqUpFileId.data(), id, 16);
    else
        md4clr(m_reqUpFileId.data());
}

// ===========================================================================
// State machine setters
// ===========================================================================

void UpDownClient::setUploadState(UploadState state)
{
    if (state != m_uploadState) {
        // Clear rate data when leaving Uploading
        if (m_uploadState == UploadState::Uploading) {
            m_upDatarate = 0;
            m_sumForAvgUpDataRate = 0;
            m_averageUDR.clear();
        }
        if (state == UploadState::Uploading) {
            m_sentOutOfPartReqs = false;
            m_uploadTime = getTickCount();
        }

        m_uploadState = state;
        emit uploadStateChanged(state);
    }
}

void UpDownClient::setDownloadState(DownloadState state)
{
    // Control flow when leaving Downloading: clear rate data, reset socket
    // timeout, and clear the download rate limit via clearDownloadLimit()
    // (not disableDownloadLimit which re-enters onReadyRead).  This lets
    // the socket read the next file's control packets (multipacket answer,
    // hashset response) without being throttled by a stale limit.
    if (state != m_downloadState) {
        // The re-dial clock. MFC stamps it on entering Connecting and, for a Kad source
        // that hit the connection cap, rewinds it a full window so the source is retried
        // at once rather than being stuck for 20 minutes
        // (srchybrid/DownloadClient.cpp:615-623).
        if (state == DownloadState::Connecting)
            setLastTriedToConnectNow();
        else if (state == DownloadState::TooManyConnsKad)
            m_lastTriedToConnect = getTickCount() - MIN2MS(20);

        // MFC: Increase socket timeout to 4x when entering Downloading state
        // to give the uploader time to start sending data blocks
        if (state == DownloadState::Downloading && m_socket) {
            m_socket->setTimeOut(CONNECTION_TIMEOUT * 4);
        }

        // Track downloading sources on the PartFile (MFC DownloadClient.cpp:647-649)
        if (state == DownloadState::Downloading && m_reqFile)
            m_reqFile->addDownloadingSource(this);
        else if (m_downloadState == DownloadState::Downloading && m_reqFile)
            m_reqFile->removeDownloadingSource(this);

        // Clear rate data and reset socket timeout when leaving Downloading.
        // Also disable download rate limiting so the socket can read control
        // packets (e.g. multipacket answers) without being blocked by a stale
        // download limit from the previous file's transfer.
        if (m_downloadState == DownloadState::Downloading) {
            // A session that carried a block's worth wipes the record of a bad one.
            if (sessionPayloadDown() >= EMBLOCKSIZE) {
                m_fruitlessSessionTick = 0;
                if (auto* ledger = fruitlessLedger())
                    ledger->noteProductiveSession(userHash());
            }
            m_unmatchedPackets = 0;
            m_abandonedBlocks.clear();

            // Session statistics (srchybrid/DownloadClient.cpp:676-682): a session that
            // delivered something and did not end in an error was a successful one.
            if (theApp.downloadQueue) {
                const uint64 started = m_downStartTime;
                const uint64 now = getTickCount();
                theApp.downloadQueue->noteDownloadSession(
                    m_transferredDownMini && state != DownloadState::Error,
                    started && now > started ? static_cast<uint32>((now - started) / 1000) : 0);
            }
            m_transferredDownMini = false;

            // The session figures are per download session, not per client lifetime — this is
            // the mark MFC takes when the session ends (srchybrid/DownloadClient.cpp:674) and
            // it is what makes sessionDown() / sessionPayloadDown() mean anything.
            resetSessionDown();

            // The blocks this source held go back to the file for the others
            // (MFC srchybrid/DownloadClient.cpp:687). Without it they stayed reserved
            // until the source disconnected.
            clearDownloadBlockRequests();

            m_downDatarate = 0;
            m_downDataRateMS = 0;
            m_sumForAvgDownDataRate = 0;
            m_averageDDR.clear();
            if (m_socket) {
                m_socket->setTimeOut(CONNECTION_TIMEOUT);
                // Clear the download rate limit flag — use clearDownloadLimit()
                // not disableDownloadLimit() to avoid re-entrant onReadyRead()
                // during state transition.  The next readyRead will process
                // pending data without limit.
                m_socket->clearDownloadLimit();
            }
        }

        m_downloadState = state;

        // A slot ends the wait: no rank, not full (MFC DownloadClient.cpp:702-706).
        if (state == DownloadState::Downloading) {
            if (isEmuleClient())
                setRemoteQueueFull(false);
            setRemoteQueueRank(0);
        }

        // MFC: record reask baseline on NNP entry so doubled reask timing works
        if (state == DownloadState::NoNeededParts)
            setLastAskedTime();

        emit downloadStateChanged(state);
    }
}

// ===========================================================================
// setConnectOptions — MFC BaseClient.cpp:2901-2907
// ===========================================================================

void UpDownClient::setConnectOptions(uint8 options, bool encryption, bool callback)
{
    m_supportsCryptLayer  = (options & 0x01) != 0 && encryption;
    m_requestsCryptLayer  = (options & 0x02) != 0 && encryption;
    m_requiresCryptLayer  = (options & 0x04) != 0 && encryption;
    m_directUDPCallback   = (options & 0x08) != 0 && callback;
}

// ===========================================================================
// clearHelloProperties — MFC BaseClient.cpp:310-337
// ===========================================================================

void UpDownClient::clearHelloProperties()
{
    m_udpPort = 0;
    m_udpVer = 0;
    m_dataCompVer = 0;
    m_emuleVersion = 0;
    m_sourceExchange1Ver = 0;
    m_acceptCommentVer = 0;
    m_extendedRequestsVer = 0;
    m_compatibleClient = 0;
    m_kadPort = 0;
    m_supportSecIdent = 0;
    m_supportsPreview = false;
    m_clientVersion = 0;
    m_sharedDirectories = false;
    m_multiPacket = false;
    m_peerCache = false;
    m_kadVersion = 0;
    m_supportsLargeFiles = false;
    m_extMultiPacket = false;
    m_requestsCryptLayer = false;
    m_supportsCryptLayer = false;
    m_requiresCryptLayer = false;
    m_supportsSourceEx2 = false;
    m_supportsCaptcha = false;
    m_directUDPCallback = false;
    m_supportsFileIdent = false;
    m_supportsIPv6 = false;
    m_openIPv6 = false;
    m_supportsExtendedXS = false;
    m_supportsExtSXSkipTags = false;
    m_supportsHttpCache = false;
    m_userIPv6 = Address{};
    m_buddyIPv6 = Address{};
    sourceIdentityChanged();
}

// ===========================================================================
// compare — MFC DownloadClient.cpp:124-173
// ===========================================================================

bool UpDownClient::compare(const UpDownClient* other, bool ignoreUserHash) const
{
    // Compare user hashes first if both valid
    if (!ignoreUserHash && hasValidHash() && other->hasValidHash())
        return md4equ(userHash(), other->userHash());

    if (hasLowID()) {
        // Firewalled client: check IP + port matches
        if (!m_userAddress.isNull() && m_userAddress == other->m_userAddress) {
            if (userPort() != 0 && userPort() == other->userPort())
                return true;
            if (kadPort() != 0 && kadPort() == other->kadPort())
                return true;
        }
        // Same low ID on same server
        if (userIDHybrid() != 0 && userIDHybrid() == other->userIDHybrid()
            && !m_serverAddress.isNull() && m_serverAddress == other->m_serverAddress
            && serverPort() != 0 && serverPort() == other->serverPort())
        {
            return true;
        }
        return false;
    }

    // High-ID client: check port (TCP or Kad) + IP/UserIDHybrid
    if ((userPort() != 0 && userPort() == other->userPort())
        || (kadPort() != 0 && kadPort() == other->kadPort()))
    {
        // MFC compares verified IPv4s here. A HighID peer reached over IPv6 has a v6
        // userAddress but keeps its v4 ID, so a family mismatch means "no verified IP on one
        // side" and falls through to the ID, as MFC does for an unset GetIP().
        if (!m_userAddress.isNull() && !other->m_userAddress.isNull()
            && m_userAddress.isIPv4() == other->m_userAddress.isIPv4())
        {
            if (m_userAddress == other->m_userAddress)
                return true;
        } else if (userIDHybrid() == other->userIDHybrid()) {
            return true;
        }
    }

    return false;
}

// ===========================================================================
// initClientSoftwareVersion — MFC BaseClient.cpp:1583-1764
// ===========================================================================

void UpDownClient::initClientSoftwareVersion()
{
    if (m_username.isEmpty()) {
        m_clientSoft = ClientSoftware::Unknown;
        m_clientSoftwareStr.clear();
        return;
    }

    const int iHashType = hashType();

    if (m_emuleProtocol || iHashType == static_cast<int>(ClientSoftware::eMule)) {
        QString softwareName;

        switch (m_compatibleClient) {
        case static_cast<uint8>(ClientSoftware::cDonkey):
            m_clientSoft = ClientSoftware::cDonkey;
            softwareName = QStringLiteral("cDonkey");
            break;
        case static_cast<uint8>(ClientSoftware::xMule):
            m_clientSoft = ClientSoftware::xMule;
            softwareName = QStringLiteral("xMule");
            break;
        case static_cast<uint8>(ClientSoftware::aMule):
            m_clientSoft = ClientSoftware::aMule;
            softwareName = QStringLiteral("aMule");
            break;
        case static_cast<uint8>(ClientSoftware::Shareaza):
        case 40:
            m_clientSoft = ClientSoftware::Shareaza;
            softwareName = QStringLiteral("Shareaza");
            break;
        case static_cast<uint8>(ClientSoftware::lphant):
            m_clientSoft = ClientSoftware::lphant;
            softwareName = QStringLiteral("lphant");
            break;
        default:
            if (m_isMLDonkey || m_compatibleClient == static_cast<uint8>(ClientSoftware::MLDonkey)) {
                m_clientSoft = ClientSoftware::MLDonkey;
                softwareName = QStringLiteral("MLdonkey");
            } else if (m_isHybrid) {
                m_clientSoft = ClientSoftware::eDonkeyHybrid;
                softwareName = QStringLiteral("eDonkeyHybrid");
            } else if (m_compatibleClient != 0) {
                m_clientSoft = ClientSoftware::xMule;  // means 'eMule Compatible'
                softwareName = QStringLiteral("eMule Compat");
            } else {
                m_clientSoft = ClientSoftware::eMule;
                softwareName = QStringLiteral("eMule");
            }
            break;
        }

        if (m_emuleVersion == 0) {
            m_clientVersion = makeClientVersion(0, 0, 0);
            m_clientSoftwareStr = softwareName;
        } else if (m_emuleVersion != 0x99) {
            const uint32 minVer = (m_emuleVersion >> 4) * 10 + (m_emuleVersion & 0x0f);
            m_clientVersion = makeClientVersion(0, minVer, 0);
            m_clientSoftwareStr = QStringLiteral("%1 v0.%2").arg(softwareName).arg(minVer);
        } else {
            const uint32 majVer = (m_clientVersion >> 17) & 0x7f;
            const uint32 minVer = (m_clientVersion >> 10) & 0x7f;
            const uint32 upVer  = (m_clientVersion >> 7) & 0x07;
            m_clientVersion = makeClientVersion(majVer, minVer, upVer);

            if (m_clientSoft == ClientSoftware::eMule) {
                m_clientSoftwareStr = QStringLiteral("%1 v%2.%3%4")
                    .arg(softwareName).arg(majVer).arg(minVer)
                    .arg(QChar(u'a' + upVer));
            } else if (m_clientSoft == ClientSoftware::aMule || upVer != 0) {
                m_clientSoftwareStr = QStringLiteral("%1 v%2.%3.%4")
                    .arg(softwareName).arg(majVer).arg(minVer).arg(upVer);
            } else if (m_clientSoft == ClientSoftware::lphant) {
                m_clientSoftwareStr = QStringLiteral("%1 v%2.%3")
                    .arg(softwareName).arg(majVer - 1).arg(minVer, 2, 10, QChar(u'0'));
            } else {
                m_clientSoftwareStr = QStringLiteral("%1 v%2.%3")
                    .arg(softwareName).arg(majVer).arg(minVer);
            }
        }
        return;
    }

    if (m_isHybrid) {
        m_clientSoft = ClientSoftware::eDonkeyHybrid;

        uint32 majVer, minVer, upVer;
        if (m_clientVersion > 100000) {
            const uint32 uMaj = m_clientVersion / 100000;
            majVer = uMaj - 1;
            minVer = (m_clientVersion - uMaj * 100000) / 100;
            upVer = m_clientVersion % 100;
        } else if (m_clientVersion >= 10100 && m_clientVersion <= 10309) {
            const uint32 uMaj = m_clientVersion / 10000;
            majVer = uMaj;
            minVer = (m_clientVersion - uMaj * 10000) / 100;
            upVer = m_clientVersion % 10;
        } else if (m_clientVersion > 10000) {
            const uint32 uMaj = m_clientVersion / 10000;
            majVer = uMaj - 1;
            minVer = (m_clientVersion - uMaj * 10000) / 10;
            upVer = m_clientVersion % 10;
        } else if (m_clientVersion >= 1000 && m_clientVersion < 1020) {
            const uint32 uMaj = m_clientVersion / 1000;
            majVer = uMaj;
            minVer = (m_clientVersion - uMaj * 1000) / 10;
            upVer = m_clientVersion % 10;
        } else if (m_clientVersion > 1000) {
            const uint32 uMaj = m_clientVersion / 1000;
            majVer = uMaj - 1;
            minVer = m_clientVersion - uMaj * 1000;
            upVer = 0;
        } else if (m_clientVersion > 100) {
            const uint32 uMin = m_clientVersion / 10;
            majVer = 0;
            minVer = uMin;
            upVer = m_clientVersion - uMin * 10;
        } else {
            majVer = 0;
            minVer = m_clientVersion;
            upVer = 0;
        }
        m_clientVersion = makeClientVersion(majVer, minVer, upVer);

        if (upVer)
            m_clientSoftwareStr = QStringLiteral("eDonkeyHybrid v%1.%2.%3").arg(majVer).arg(minVer).arg(upVer);
        else
            m_clientSoftwareStr = QStringLiteral("eDonkeyHybrid v%1.%2").arg(majVer).arg(minVer);
        return;
    }

    if (m_isMLDonkey || iHashType == static_cast<int>(ClientSoftware::MLDonkey)) {
        m_clientSoft = ClientSoftware::MLDonkey;
        const uint32 minVer = m_clientVersion;
        m_clientVersion = makeClientVersion(0, minVer, 0);
        m_clientSoftwareStr = QStringLiteral("MLdonkey v0.%1").arg(minVer);
        return;
    }

    if (iHashType == static_cast<int>(ClientSoftware::OldEMule)) {
        m_clientSoft = ClientSoftware::OldEMule;
        const uint32 minVer = m_clientVersion;
        m_clientVersion = makeClientVersion(0, minVer, 0);
        m_clientSoftwareStr = QStringLiteral("Old eMule v0.%1").arg(minVer);
        return;
    }

    m_clientSoft = ClientSoftware::eDonkey;
    const uint32 minVer = m_clientVersion;
    m_clientVersion = makeClientVersion(0, minVer, 0);
    m_clientSoftwareStr = QStringLiteral("eDonkey v0.%1").arg(minVer);
}

// ===========================================================================
// Debug strings
// ===========================================================================

QString UpDownClient::dbgGetUploadState(UploadState state)
{
    static constexpr const char* names[] = {
        "Uploading", "OnUploadQueue", "Connecting", "Banned", "None"
    };
    const auto idx = static_cast<int>(state);
    if (idx >= 0 && idx < static_cast<int>(std::size(names)))
        return QString::fromLatin1(names[idx]);
    return QStringLiteral("*Unknown*");
}

QString UpDownClient::dbgGetDownloadState(DownloadState state)
{
    static constexpr const char* names[] = {
        "Downloading", "OnQueue", "Connected", "Connecting",
        "WaitCallback", "WaitCallbackKad", "ReqHashSet",
        "NoNeededParts", "TooManyConns", "TooManyConnsKad",
        "LowToLowIp", "Banned", "Error", "None", "RemoteQueueFull"
    };
    const auto idx = static_cast<int>(state);
    if (idx >= 0 && idx < static_cast<int>(std::size(names)))
        return QString::fromLatin1(names[idx]);
    return QStringLiteral("*Unknown*");
}

QString UpDownClient::dbgGetKadState(KadState state)
{
    static constexpr const char* names[] = {
        "None", "FwCheckQueued", "FwCheckConnecting", "FwCheckConnected",
        "BuddyQueued", "BuddyIncoming", "BuddyConnecting", "BuddyConnected",
        "QueuedFWCheckUDP", "FWCheckUDP", "FwCheckConnectingUDP"
    };
    const auto idx = static_cast<int>(state);
    if (idx >= 0 && idx < static_cast<int>(std::size(names)))
        return QString::fromLatin1(names[idx]);
    return QStringLiteral("*Unknown*");
}

QString UpDownClient::dbgGetFullClientSoftVer() const
{
    if (m_modVersion.isEmpty())
        return m_clientSoftwareStr;
    return QStringLiteral("%1 [%2]").arg(m_clientSoftwareStr, m_modVersion);
}

QString UpDownClient::dbgGetClientInfo(bool formatIP) const
{
    if (hasLowID()) {
        if (!m_connectAddress.isNull()) {
            return QStringLiteral("%1@%2 (%3) '%4' (%5,%6/%7/%8)")
                .arg(userIDHybrid())
                .arg(ipstr(m_serverAddress))
                .arg(ipstr(m_connectAddress))
                .arg(userName())
                .arg(dbgGetFullClientSoftVer())
                .arg(dbgGetDownloadState())
                .arg(dbgGetUploadState())
                .arg(dbgGetKadState());
        }
        return QStringLiteral("%1@%2 '%3' (%4,%5/%6/%7)")
            .arg(userIDHybrid())
            .arg(ipstr(m_serverAddress))
            .arg(userName())
            .arg(dbgGetFullClientSoftVer())
            .arg(dbgGetDownloadState())
            .arg(dbgGetUploadState())
            .arg(dbgGetKadState());
    }

    Q_UNUSED(formatIP);
    return QStringLiteral("%1 '%2' (%3,%4/%5/%6)")
        .arg(ipstr(m_connectAddress))
        .arg(userName())
        .arg(dbgGetFullClientSoftVer())
        .arg(dbgGetDownloadState())
        .arg(dbgGetUploadState())
        .arg(dbgGetKadState());
}

// ===========================================================================
// Phase 2 — Hello Handshake & Mule Info Exchange
// Ported from MFC BaseClient.cpp lines 356-1090
// ===========================================================================

// Trailer value: MLDonkey writes 'M','L','D','K' at end of hello, read as LE uint32
static constexpr uint32 kMLDonkeyTrailer = 0x4B444C4Du; // "MLDK" as little-endian uint32

// ===========================================================================
// checkForGPLEvildoer — MFC BaseClient.cpp:2415-2428
// ===========================================================================

void UpDownClient::checkForGPLEvildoer()
{
    if (m_modVersion.isEmpty())
        return;

    const QString trimmed = m_modVersion.trimmed();
    if (trimmed.startsWith(QStringLiteral("LH"), Qt::CaseInsensitive)
        || trimmed.startsWith(QStringLiteral("LIO"), Qt::CaseInsensitive)
        || trimmed.startsWith(QStringLiteral("PLUS PLUS"), Qt::CaseInsensitive))
    {
        m_gplEvildoer = true;
    }
}

// ===========================================================================
// processHelloPacket — MFC BaseClient.cpp:340-355
// ===========================================================================

bool UpDownClient::processHelloPacket(const uint8* data, uint32 size)
{
    SafeMemFile file(data, size);
    file.readUInt8(); // discard userhash size byte (always 16)
    clearHelloProperties();
    return processHelloTypePacket(file);
}

// ===========================================================================
// processHelloAnswer — MFC BaseClient.cpp:674-693
// ===========================================================================

bool UpDownClient::processHelloAnswer(const uint8* data, uint32 size)
{
    SafeMemFile file(data, size);
    const bool isMule = processHelloTypePacket(file);
    m_helloAnswerPending = false;
    return isMule;
}

// ===========================================================================
// processHelloTypePacket — MFC BaseClient.cpp:356-673
// ===========================================================================

bool UpDownClient::processHelloTypePacket(SafeMemFile& data)
{
    m_helloInfo.clear();
    // The hello rewrites hash, ID, ports and server — also when it throws half way.
    const auto rekey = qScopeGuard([this] { sourceIdentityChanged(); });

    // Reset hello-only properties
    m_isHybrid = false;
    m_isMLDonkey = false;
    m_noViewSharedFiles = false;
    m_unicodeSupport = false;

    // Read identity
    data.readHash16(m_userHash.data());
    m_userIDHybrid = data.readUInt32();
    uint16 nUserPort = data.readUInt16();

    // Read tags
    const uint32 tagCount = readTagCount(data, kMaxWireTags);

    bool bIsMule = false;
    bool bPrTag = false;

    for (uint32 i = 0; i < tagCount; ++i) {
        Tag tag(data, true);

        switch (tag.nameId()) {
        case CT_NAME:
            if (tag.isStr())
                setUserName(tag.strValue());
            break;

        case CT_VERSION:
            if (tag.isInt())
                m_clientVersion = tag.intValue();
            break;

        case CT_PORT:
            if (tag.isInt())
                nUserPort = static_cast<uint16>(tag.intValue());
            break;

        case CT_MOD_VERSION:
            if (tag.isStr())
                m_modVersion = tag.strValue();
            else if (tag.isInt())
                m_modVersion = QString::number(tag.intValue());
            checkForGPLEvildoer();
            break;

        case CT_EMULE_UDPPORTS:
            if (tag.isInt()) {
                m_kadPort = static_cast<uint16>((tag.intValue() >> 16) & 0xFFFF);
                m_udpPort = static_cast<uint16>(tag.intValue() & 0xFFFF);
            }
            break;

        case CT_EMULE_BUDDYUDP:
            if (tag.isInt())
                m_buddyPort = static_cast<uint16>(tag.intValue());
            break;

        case CT_EMULE_BUDDYIP:
            if (tag.isInt())
                m_buddyAddress = Address::fromNetworkOrder(tag.intValue());
            break;

        case CT_EMULE_MISCOPTIONS1:
            if (tag.isInt()) {
                const uint32 opts = tag.intValue();
                m_supportsAICH        = static_cast<uint8>((opts >> 29) & 0x07);
                m_unicodeSupport      = ((opts >> 28) & 0x01) != 0;
                m_udpVer              = static_cast<uint8>((opts >> 24) & 0x0F);
                m_dataCompVer         = static_cast<uint8>((opts >> 20) & 0x0F);
                m_supportSecIdent     = static_cast<uint8>((opts >> 16) & 0x0F);
                m_sourceExchange1Ver  = static_cast<uint8>((opts >> 12) & 0x0F);
                m_extendedRequestsVer = static_cast<uint8>((opts >>  8) & 0x0F);
                m_acceptCommentVer    = static_cast<uint8>((opts >>  4) & 0x0F);
                m_peerCache           = ((opts >> 3) & 0x01) != 0;
                m_noViewSharedFiles   = ((opts >> 2) & 0x01) != 0;
                m_multiPacket         = ((opts >> 1) & 0x01) != 0;
                m_supportsPreview     = ((opts >> 0) & 0x01) != 0;
            }
            break;

        case CT_EMULE_MISCOPTIONS2:
            if (tag.isInt()) {
                const uint32 opts = tag.intValue();
                m_kadVersion         = static_cast<uint8>((opts >> 0) & 0x0F);
                m_supportsLargeFiles = ((opts >>  4) & 0x01) != 0;
                m_extMultiPacket     = ((opts >>  5) & 0x01) != 0;
                // bit 6 reserved
                m_supportsCryptLayer = ((opts >>  7) & 0x01) != 0;
                m_requestsCryptLayer = ((opts >>  8) & 0x01) != 0;
                m_requiresCryptLayer = ((opts >>  9) & 0x01) != 0;
                m_supportsSourceEx2  = ((opts >> 10) & 0x01) != 0;
                m_supportsCaptcha    = ((opts >> 11) & 0x01) != 0;
                m_directUDPCallback  = ((opts >> 12) & 0x01) != 0;
                m_supportsFileIdent  = ((opts >> 13) & 0x01) != 0;

                // Enforce crypt dependency chain
                m_requestsCryptLayer &= m_supportsCryptLayer;
                m_requiresCryptLayer &= m_requestsCryptLayer;
            }
            break;

        case CT_EMULE_VERSION:
            if (tag.isInt()) {
                m_compatibleClient = static_cast<uint8>((tag.intValue() >> 24) & 0xFF);
                m_clientVersion = tag.intValue() & 0x00FFFFFF;
                m_emuleVersion = 0x99;
                m_sharedDirectories = true;
                bIsMule = true;
            }
            break;

        case CT_MOD_MISCOPTIONS:
            if (tag.isInt()) {
                const uint32 opts = tag.intValue();
                m_supportsIPv6           = (opts & MODMISC_IPV6) != 0;
                m_supportsExtendedXS     = (opts & MODMISC_EXTXS) != 0;
                m_supportsExtSXSkipTags  = (opts & MODMISC_EXTXS_SKIPTAGS) != 0;
                m_supportsHttpCache      = (opts & MODMISC_HTTPCACHE) != 0;
            }
            break;

        case CT_MOD_YOUR_IP:
            // A peer reports the IPv6 address it sees us coming from. A single claim is
            // unverifiable, so it is not adopted directly: it is fed to the corroboration
            // tracker, which makes it our public IPv6 only once enough distinct peers agree
            // (and only while no server has observed our egress). The IPv4 form is ignored
            // here — our public IPv4 comes from the server (OP_IDCHANGE) and Kad.
            if (tag.isHash()) {
                // Key the vote on the address we *observe* the peer at, never on its
                // self-declared user hash: a hash costs nothing to invent, so hash-keying
                // would let a single host cast the whole threshold on its own. Without a
                // socket there is no observed address, so the claim is unverifiable and
                // simply does not count.
                const Address peerAddr = m_socket ? Address::fromQHostAddress(m_socket->peerAddress())
                                                  : Address{};
                if (!peerAddr.isNull()) {
                    theApp.recordPeerObservedIPv6(Address::fromIPv6Bytes(tag.hashValue()),
                                                  peerAddr.toString().toUtf8());
                }
            }
            break;

        case CT_MOD_IP_V6:
            // Validate before latching m_openIPv6, matching the ExtSX ingest path. An
            // unvalidated fe80:: or ::1 here becomes the address tryToConnect() dials and
            // keeps an unreachable source alive in the SX candidate set. isPublicIP() is
            // lab-mode aware, so a test network still works (Address::setLabNetworkMode).
            if (tag.isHash()) {
                const Address advertised = Address::fromIPv6Bytes(tag.hashValue());
                if (advertised.isPublicIP()) {
                    m_userIPv6 = advertised;
                    m_openIPv6 = true;
                }
            }
            break;

        case CT_EMULE_SERVINGBUDDYIPV6:
            if (tag.isHash()) {
                const Address buddyV6 = Address::fromIPv6Bytes(tag.hashValue());
                if (buddyV6.isPublicIP())
                    m_buddyIPv6 = buddyV6;
            }
            break;

        default:
            // Check for string-named "pr" tag (eDonkeyHybrid marker)
            if (!tag.nameId() && tag.name() == QByteArrayLiteral("pr"))
                bPrTag = true;
            break;
        }
    }

    m_userPort = nUserPort;

    // Read server info
    m_serverAddress = Address::fromNetworkOrder(data.readUInt32());
    m_serverPort = data.readUInt16();

    // Hybrid 0.40-1.2 and MLDonkey append one uint32. MFC reads it only when exactly that much
    // is left (BaseClient.cpp:565-586); any other trailer is ignored rather than over-read.
    if (data.length() - data.position() == 4) {
        const uint32 trailer = data.readUInt32();
        if (trailer == kMLDonkeyTrailer)
            m_isMLDonkey = true;
        else
            m_isHybrid = true;
    }

    // Extract peer IP from socket if available
    if (m_socket) {
        const auto addr = m_socket->peerAddress();
        if (!addr.isNull())
            setUserAddress(Address::fromQHostAddress(addr));
    }

    // Learn the peer's server only when the user asked for it, and at low priority —
    // MFC BaseClient.cpp:593-598.
    if (thePrefs.addServersFromClients() && theApp.serverList
        && !m_serverAddress.isNull() && m_serverPort != 0)
    {
        if (!theApp.serverList->findByIPTcp(m_serverAddress.toNetworkUint32(), m_serverPort)) {
            auto newServer = std::make_unique<Server>(m_serverAddress.toNetworkUint32(), m_serverPort);
            newServer->setName(newServer->address());
            newServer->setPreference(ServerPriority::Low);
            theApp.serverList->addServer(std::move(newServer));
        }
    }

    // Credits, and the ban on a changed user hash — MFC BaseClient.cpp:608-622. Without it a
    // peer presenting another client's hash simply inherits that client's credits.
    if (theApp.clientCredits) {
        ClientCredits* found = theApp.clientCredits->getCredit(m_userHash.data());
        if (!m_credits) {
            setCredits(found);
            // First hello on this object: check what this IP:port presented last time.
            if (theApp.clientList
                && !theApp.clientList->comparePriorUserhash(m_userAddress, m_userPort, found)) {
                if (thePrefs.logBannedClients())
                    logWarning(QStringLiteral("Clients: %1 (%2), Ban reason: Userhash changed (Found in TrackedClientsList)")
                                   .arg(userName(), ipstr(m_userAddress)));
                ban(QStringLiteral("Userhash changed"));
            }
        } else if (m_credits != found) {
            // Allowed, but the peer sits out the ban before the new hash counts.
            setCredits(found);
            if (thePrefs.logBannedClients())
                logWarning(QStringLiteral("Clients: %1 (%2), Ban reason: Userhash changed")
                               .arg(userName(), ipstr(m_userAddress)));
            ban(QStringLiteral("Userhash changed"));
        }
    }

    // Friend linking — MFC srchybrid/BaseClient.cpp:625-648.
    if (theApp.friendList) {
        Friend* cur = friendPtr();

        // The peer is answering with a different user hash than the friend we had linked, so
        // it is not that friend any more. Unlink first, which hands the slot back to the
        // friend entry instead of leaving it on a client that no longer earns it.
        if (cur && cur->hasUserhash() && !md4equ(cur->userHash().data(), m_userHash.data())) {
            if (cur->isTryingToConnect()) {
                // We dialled a friend and somebody else answered: the attempt failed, and
                // the friend gets a fresh client object carrying the hash we want.
                cur->updateFriendConnectionState(FriendConnectReport::UserHashFailed);
            } else {
                cur->setLinkedClient(nullptr);
            }
            cur = nullptr;
        }

        // Don't replace a friend matched only by address with one matched by hash: both
        // would fit this client, and swapping between them on every hello is worse than
        // keeping whichever we already had.
        if (!cur || cur->hasUserhash()
            || cur->lastUsedAddress() != m_connectAddress
            || cur->lastUsedPort() != m_userPort)
        {
            Friend* found = theApp.friendList->searchFriend(
                m_userHash.data(), m_userAddress.toNetworkUint32(), m_userPort);
            if (found) {
                // A friend matched by address alone learns the hash here, which is what
                // makes it findable in Kad later. MFC srchybrid/BaseClient.cpp:645-646.
                if (!found->hasUserhash() && hasValidHash())
                    found->setUserHash(m_userHash.data());
                found->setLinkedClient(this);
            } else {
                // Nothing matches, so whatever slot this instance is carrying is stale —
                // otherwise an unwanted client instance keeps a friend slot forever.
                if (cur)
                    cur->setLinkedClient(nullptr);
                m_friend = nullptr;
                setFriendSlot(false);
            }
        }
    }

    // High-ID conversion: if not low-ID, convert userIDHybrid to match userAddress.
    // IPv4 only — for an IPv6 peer toUint32() is 0, which would clobber the LowID the
    // peer was assigned; a v6 peer keeps the hybrid ID read from the hello above.
    if (m_userAddress.isIPv4()) {
        if (!hasLowID() || m_userIDHybrid == 0 || m_userIDHybrid == m_userAddress.toNetworkUint32())
            m_userIDHybrid = m_userAddress.toUint32();
    }

    // Set info packets received flag
    m_infoPacketsReceived |= InfoPacketState::EDonkeyProtPack;

    // MFC BaseClient.cpp:658-661: if CT_EMULE_VERSION was in the HELLO,
    // set both EMuleProtPack and emuleProtocol.  processMuleInfoPacket()
    // also sets EMuleProtPack (harmless |= idempotent).
    if (bIsMule) {
        m_emuleProtocol = true;
        m_infoPacketsReceived |= InfoPacketState::EMuleProtPack;
    }

    if (bPrTag)
        m_isHybrid = true;

    initClientSoftwareVersion();

    // A hello on a TCP connection: the hash goes with a real address.
    if (theApp.clientCensus)
        theApp.clientCensus->noteSeen(m_userHash.data(), m_userAddress);

    if (m_isHybrid)
        m_sharedDirectories = true;

    return bIsMule;
}

// ===========================================================================
// sendHelloPacket — MFC BaseClient.cpp:890-910
// ===========================================================================


namespace {

// Up overhead by packet kind — the mirror of the receive side in ClientReqSocket.cpp.
// MFC books at each call site; one table keeps the two directions in step.
void bookUpOverhead(const Packet& packet)
{
    auto* stats = theApp.statistics;
    if (!stats)
        return;

    if (packet.prot == OP_EDONKEYPROT) {
        switch (packet.opcode) {
        case OP_REQUESTFILENAME:
        case OP_SETREQFILEID:
        case OP_FILEREQANSNOFIL:
        case OP_REQFILENAMEANSWER:
        case OP_FILESTATUS:
        case OP_HASHSETREQUEST:
        case OP_HASHSETANSWER:
        case OP_STARTUPLOADREQ:
        case OP_ACCEPTUPLOADREQ:
        case OP_CANCELTRANSFER:
        case OP_OUTOFPARTREQS:
        case OP_REQUESTPARTS:
        case OP_QUEUERANK:
            stats->addUpDataOverheadFileRequest(packet.size);
            return;
        default:
            break;
        }
    } else if (packet.prot == OP_EMULEPROT) {
        switch (packet.opcode) {
        case OP_REQUESTSOURCES:
        case OP_ANSWERSOURCES:
        case OP_REQUESTSOURCES2:
        case OP_ANSWERSOURCES2:
            stats->addUpDataOverheadSourceExchange(packet.size);
            return;
        case OP_REQUESTPARTS_I64:
        case OP_QUEUERANKING:
        case OP_FILEDESC:
        case OP_AICHREQUEST:
        case OP_AICHANSWER:
        case OP_AICHFILEHASHREQ:
        case OP_AICHFILEHASHANS:
        case OP_HASHSETREQUEST2:
        case OP_HASHSETANSWER2:
        case OP_MULTIPACKET:
        case OP_MULTIPACKETANSWER:
        case OP_MULTIPACKET_EXT:
        case OP_MULTIPACKET_EXT2:
        case OP_MULTIPACKETANSWER_EXT2:
        case OP_REASKCALLBACKTCP:   // buddy relay, MFC ClientUDPSocket.cpp:220
            stats->addUpDataOverheadFileRequest(packet.size);
            return;
        default:
            break;
        }
    }
    stats->addUpDataOverheadOther(packet.size);
}

} // namespace

void UpDownClient::sendHelloPacket()
{
    if (!m_socket) {
        logDebug(QStringLiteral("sendHelloPacket: NO SOCKET, skipping"));
        return;
    }

    SafeMemFile data;
    data.writeUInt8(16); // userhash size
    sendHelloTypePacket(data);

    auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_HELLO);
    logDebug(QStringLiteral("sendHelloPacket: sending OP_HELLO size=%1 to %2:%3")
                 .arg(packet->size).arg(m_socket->peerAddress().toString()).arg(m_socket->peerPort()));
    bookUpOverhead(*packet);
    m_socket->sendPacket(std::move(packet));
    m_helloAnswerPending = true;
}

// ===========================================================================
// sendHelloAnswer — MFC BaseClient.cpp:1092-1108
// ===========================================================================

void UpDownClient::sendHelloAnswer()
{
    if (!m_socket)
        return;

    SafeMemFile data;
    sendHelloTypePacket(data);

    // Via sendPacket() so the answer lands in the overhead statistics, as MFC does
    // (BaseClient.cpp:902).
    auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_HELLOANSWER);
    sendPacket(std::move(packet));
    m_helloAnswerPending = false;

    if (thePrefs.verbose())
        logDebug(QStringLiteral("sendHelloAnswer: sent OP_HELLOANSWER to %1:%2")
                     .arg(m_socket->peerAddress().toString())
                     .arg(m_socket->peerPort()));
}

// ===========================================================================
// sendHelloTypePacket — MFC BaseClient.cpp:911-1052
// ===========================================================================

void UpDownClient::sendHelloTypePacket(SafeMemFile& data)
{
    // Write our user hash
    const auto hash = thePrefs.userHash();
    data.writeHash16(hash.data());

    // Write our client ID
    data.writeUInt32(theApp.getID());

    // Write our port
    data.writeUInt16(theApp.advertisedTcpPort());

    // Determine if we need buddy tags (Low-ID + have buddy for Kad callback)
    const bool sendBuddyTags = theApp.clientList && theApp.clientList->getBuddy()
                                && theApp.isFirewalled();
    UpDownClient* buddy = sendBuddyTags ? theApp.clientList->getBuddy() : nullptr;

    // IPv6 / extended-capability MOD tags (additive; a legacy peer skips unknown tags,
    // so only the exact count matters). CT_MOD_MISCOPTIONS is always sent; the address
    // tags are sent only when we actually have the value.
    const bool haveYourIP    = !m_userAddress.isNull();          // the peer's IP as we see it
    const Address ourIPv6    = theApp.publicIPv6();
    // The single advertise gate — shared with the Kad source publish and the server login.
    const bool haveIPv6      = theApp.shouldAdvertisePublicIPv6();
    const bool haveBuddyIPv6 = buddy && !buddy->userIPv6().isNull();

    const uint32 tagCount = (sendBuddyTags ? 9u : 7u)
                          + 1u                            // CT_MOD_MISCOPTIONS (always)
                          + (haveYourIP    ? 1u : 0u)     // CT_MOD_YOUR_IP
                          + (haveIPv6      ? 1u : 0u)     // CT_MOD_IP_V6
                          + (haveBuddyIPv6 ? 1u : 0u);    // CT_EMULE_SERVINGBUDDYIPV6
    data.writeUInt32(tagCount);

    // CT_NAME — our nickname
    Tag(CT_NAME, thePrefs.nick()).writeTagToFile(data, UTF8Mode::Raw);

    // CT_VERSION — eDonkey version
    Tag(CT_VERSION, static_cast<uint32>(EDONKEYVERSION)).writeTagToFile(data);

    // CT_EMULE_UDPPORTS — (kadPort << 16) | udpPort
    // MFC: Kad port is 0 when not connected; uses external port when connected + verified
    uint16 kadPortVal = 0;
    if (auto* kadInst = kad::Kademlia::instance(); kadInst && kadInst->isConnected()) {
        if (auto* kadPrefs = kad::Kademlia::getInstancePrefs()) {
            if (kadPrefs->externalKadPort() != 0
                && kadPrefs->useExternKadPort()
                && kad::UDPFirewallTester::isVerified()) {
                kadPortVal = kadPrefs->externalKadPort();
            } else {
                kadPortVal = kadPrefs->internKadPort();
            }
        }
    }
    const uint32 udpPorts = (static_cast<uint32>(kadPortVal) << 16) | theApp.advertisedUdpPort();
    Tag(CT_EMULE_UDPPORTS, udpPorts).writeTagToFile(data);

    // CT_EMULE_MISCOPTIONS1 — capability bits
    // MFC: NoViewShared = (CanSeeShares() == vsfaNobody), Preview = (CanSeeShares() != vsfaNobody)
    const bool noViewShared = (thePrefs.viewSharedFilesAccess() == 0); // 0 = nobody
    const bool supportPreview = (thePrefs.viewSharedFilesAccess() != 0);
    const uint32 miscOpts1 =
        (static_cast<uint32>(1) << 29) | // AICH version = 1
        (static_cast<uint32>(1) << 28) | // Unicode
        (static_cast<uint32>(4) << 24) | // UDP version
        (static_cast<uint32>(1) << 20) | // Data compression
        (static_cast<uint32>((theApp.clientCredits && theApp.clientCredits->cryptoAvailable()) ? 3 : 0) << 16) | // Secure ident (MFC: CryptoAvailable() ? 3 : 0)
        // MFC: deprecated - hardcode 4 (was SOURCEEXCHANGE2_VERSION), will be set back
        // to 3 with next release due to bug in earlier eMule versions.
        // Use SupportsSourceEx2 and new opcodes instead.
        (static_cast<uint32>(4) << 12) | // Source exchange
        (static_cast<uint32>(2) <<  8) | // Extended requests
        (static_cast<uint32>(1) <<  4) | // Comments
        (static_cast<uint32>(0) <<  3) | // Peer cache (not supported)
        (static_cast<uint32>(noViewShared ? 1 : 0) <<  2) | // No view shared (MFC: vsfaNobody)
        (static_cast<uint32>(1) <<  1) | // Multi packet
        (static_cast<uint32>(supportPreview ? 1 : 0) <<  0);  // Preview (MFC: != vsfaNobody)
    Tag(CT_EMULE_MISCOPTIONS1, miscOpts1).writeTagToFile(data);

    // CT_EMULE_MISCOPTIONS2 — more capability bits
    // MFC: DirectUDPCallback = Kad running && firewalled && !firewalled UDP && verified
    const bool directUDPCallback =
        kad::Kademlia::instance() && kad::Kademlia::instance()->isRunning()
        && theApp.isFirewalled()
        && !kad::UDPFirewallTester::isFirewalledUDP(true)
        && kad::UDPFirewallTester::isVerified();
    const uint32 miscOpts2 =
        (static_cast<uint32>(KADEMLIA_VERSION) << 0) | // Kad version
        (static_cast<uint32>(1) <<  4) | // Large files
        (static_cast<uint32>(1) <<  5) | // Ext multi packet
        // bit 6 reserved
        (static_cast<uint32>(thePrefs.cryptLayerSupported() ? 1 : 0) << 7) |
        (static_cast<uint32>(thePrefs.cryptLayerRequested() ? 1 : 0) << 8) |
        (static_cast<uint32>(thePrefs.cryptLayerRequired()  ? 1 : 0) << 9) |
        (static_cast<uint32>(1) << 10) | // Source exchange 2
        (static_cast<uint32>(1) << 11) | // Captcha
        (static_cast<uint32>(directUDPCallback ? 1 : 0) << 12) | // Direct UDP callback (MFC: conditional)
        (static_cast<uint32>(1) << 13);  // File identifiers
    Tag(CT_EMULE_MISCOPTIONS2, miscOpts2).writeTagToFile(data);

    // CT_EMULE_VERSION — (compatClient << 24) | (majVer << 17) | (minVer << 10) | (upVer << 7)
    // MFC: upper byte (m_byCompatibleClient) is 0 = standard eMule. Non-zero triggers
    // "eMule Compat" label in other clients' InitClientSoftwareVersion().
    const uint32 emuleVer =
        (static_cast<uint32>(0) << 24) |                          // compatible client = 0 (standard eMule)
        (static_cast<uint32>(SEND_EMULE_VERSION_MJR) << 17) |    // major
        (static_cast<uint32>(SEND_EMULE_VERSION_MIN) << 10) |    // minor
        (static_cast<uint32>(SEND_EMULE_VERSION_UPD) <<  7);     // update (0=a, 1=b, ...)
    Tag(CT_EMULE_VERSION, emuleVer).writeTagToFile(data);

    // CT_MOD_VERSION — identify ourselves as eMule Qt
    Tag(CT_MOD_VERSION, QStringLiteral("eMule Qt " EMULE_VERSION_STRING)).writeTagToFile(data);

    // CT_MOD_MISCOPTIONS — extended capability bitfield. We advertise IPv6, Extended
    // Source Exchange and HTTP Cache. ExtSX rides at source-exchange version 1: each
    // per-source record is a self-describing tag block, so it cannot desync a correct
    // tag-skipping reader. Both ends gate on this bit before using the tag-block format;
    // a legacy peer that lacks it gets the classic fixed-format SX2 records unchanged.
    //
    // MODMISC_HTTPCACHE is advertised unconditionally, the way MFC advertised its
    // PeerCache bit: it says "I can parse OP_HTTPCACHE", not "I have it switched on".
    // Whether we act on an offer is a local decision made in HttpCacheManager, and a
    // peer must be free to change that at runtime without re-handshaking.
    Tag(CT_MOD_MISCOPTIONS,
        static_cast<uint32>(MODMISC_IPV6 | MODMISC_EXTXS | MODMISC_EXTXS_SKIPTAGS
                            | MODMISC_HTTPCACHE))
        .writeTagToFile(data);

    // CT_MOD_YOUR_IP — tell the peer the address we see it coming from, so it can learn
    // its own public IP. uint32 (network order) for IPv4, hash16 for IPv6.
    if (haveYourIP) {
        if (m_userAddress.isIPv6())
            Tag(CT_MOD_YOUR_IP, m_userAddress.ipv6Bytes().data()).writeTagToFile(data);
        else
            Tag(CT_MOD_YOUR_IP, m_userAddress.toNetworkUint32()).writeTagToFile(data);
    }

    // CT_MOD_IP_V6 — our public IPv6 as a 16-byte hash tag (only when we have one).
    if (haveIPv6)
        Tag(CT_MOD_IP_V6, ourIPv6.ipv6Bytes().data()).writeTagToFile(data);

    // CT_EMULE_BUDDYIP + CT_EMULE_BUDDYUDP — MFC: sent when firewalled with buddy
    if (sendBuddyTags) {
        Tag(CT_EMULE_BUDDYIP, buddy->connectAddress().toNetworkUint32()).writeTagToFile(data);
        Tag(CT_EMULE_BUDDYUDP, static_cast<uint32>(buddy->udpPort())).writeTagToFile(data);
    }

    // CT_EMULE_SERVINGBUDDYIPV6 — our serving buddy's public IPv6 (hash16), so a v6 peer
    // can reach us through the buddy over IPv6.
    if (haveBuddyIPv6)
        Tag(CT_EMULE_SERVINGBUDDYIPV6, buddy->userIPv6().ipv6Bytes().data()).writeTagToFile(data);

    // Write server info
    if (theApp.serverConnect && theApp.serverConnect->isConnected()) {
        if (auto* srv = theApp.serverConnect->currentServer()) {
            data.writeUInt32(srv->ipAddress().toNetworkUint32());
            data.writeUInt16(srv->port());
        } else {
            data.writeUInt32(0);
            data.writeUInt16(0);
        }
    } else {
        data.writeUInt32(0);
        data.writeUInt16(0);
    }
}

// ===========================================================================
// sendMuleInfoPacket — MFC BaseClient.cpp:695-732
// ===========================================================================

void UpDownClient::sendMuleInfoPacket(bool answer)
{
    if (!m_socket)
        return;

    SafeMemFile data;
    // eMule version byte: BCD of the minor version (0.50a sends 0x50, 0.70b sends 0x70).
    // MFC builds it by formatting the minor as decimal and re-reading it as hex
    // (Emule.cpp:1277). The major is always 0 and is not part of the encoding.
    data.writeUInt8(((SEND_EMULE_VERSION_MIN / 10) << 4) | (SEND_EMULE_VERSION_MIN % 10));
    data.writeUInt8(EMULE_PROTOCOL); // protocol version

    constexpr uint32 tagCount = 7;
    data.writeUInt32(tagCount);

    Tag(ET_COMPRESSION, static_cast<uint32>(1)).writeTagToFile(data);
    Tag(ET_UDPVER, static_cast<uint32>(4)).writeTagToFile(data);
    Tag(ET_UDPPORT, static_cast<uint32>(theApp.advertisedUdpPort())).writeTagToFile(data);
    Tag(ET_SOURCEEXCHANGE, static_cast<uint32>(3)).writeTagToFile(data); // MFC: hardcodes 3 (legacy compat); MISCOPTIONS1 uses version 4
    Tag(ET_COMMENTS, static_cast<uint32>(1)).writeTagToFile(data);
    Tag(ET_EXTENDEDREQUEST, static_cast<uint32>(2)).writeTagToFile(data);

    // ET_FEATURES — secure ident + preview bits
    // MFC: bits 0-1 = CryptoAvailable() (RSA key for secure ident), NOT cryptLayerSupported (transport obfuscation)
    // MFC: bit 7 = preview = (CanSeeShares() != vsfaNobody)
    const bool previewEnabled = (thePrefs.viewSharedFilesAccess() != 0);
    const uint32 features =
        (static_cast<uint32>((theApp.clientCredits && theApp.clientCredits->cryptoAvailable()) ? 3 : 0)) | // sec ident (bits 0-1)
        (static_cast<uint32>(previewEnabled ? 1 : 0) << 7); // preview (bit 7)
    Tag(ET_FEATURES, features).writeTagToFile(data);

    const uint8 opcode = answer ? OP_EMULEINFOANSWER : OP_EMULEINFO;
    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, opcode);
    bookUpOverhead(*packet);
    m_socket->sendPacket(std::move(packet));
}

// ===========================================================================
// processMuleInfoPacket — MFC BaseClient.cpp:734-887
// ===========================================================================

void UpDownClient::processMuleInfoPacket(const uint8* data, uint32 size)
{
    SafeMemFile file(data, size);

    // MFC: reset compatible client — stale value from Hello must not leak through
    m_compatibleClient = 0;

    const uint8 prevEmuleVersion = m_emuleVersion;
    m_emuleVersion = file.readUInt8();
    const uint8 protVersion = file.readUInt8();

    // Must be our protocol
    if (protVersion != EMULE_PROTOCOL)
        return;

    // MFC: version fixup — 0x2B is treated as 0x22
    if (m_emuleVersion == 0x2B)
        m_emuleVersion = 0x22;

    // Set implicit version-based capabilities for old clients (MFC exact logic)
    if (m_emuleVersion < 0x25 && m_emuleVersion > 0x22)
        m_udpVer = 1;
    if (m_emuleVersion < 0x25 && m_emuleVersion > 0x21)
        m_sourceExchange1Ver = 1;
    if (m_emuleVersion == 0x24)
        m_acceptCommentVer = 1;
    // MFC: >= 0x28 && !isMLDonkey → shared dirs
    if (m_emuleVersion >= 0x28 && !m_isMLDonkey)
        m_sharedDirectories = true;

    // Read tags
    const uint32 tagCount = readTagCount(file, kMaxWireTags);

    for (uint32 i = 0; i < tagCount; ++i) {
        // Not UTF-8: MFC BaseClient.cpp:772 reads EMULEINFO tags as plain strings.
        Tag tag(file, false);

        switch (tag.nameId()) {
        case ET_COMPRESSION:
            if (tag.isInt())
                m_dataCompVer = static_cast<uint8>(tag.intValue());
            break;

        case ET_UDPPORT:
            if (tag.isInt())
                m_udpPort = static_cast<uint16>(tag.intValue());
            break;

        case ET_UDPVER:
            if (tag.isInt())
                m_udpVer = static_cast<uint8>(tag.intValue());
            break;

        case ET_SOURCEEXCHANGE:
            if (tag.isInt())
                m_sourceExchange1Ver = static_cast<uint8>(tag.intValue());
            break;

        case ET_COMMENTS:
            if (tag.isInt())
                m_acceptCommentVer = static_cast<uint8>(tag.intValue());
            break;

        case ET_EXTENDEDREQUEST:
            if (tag.isInt())
                m_extendedRequestsVer = static_cast<uint8>(tag.intValue());
            break;

        case ET_COMPATIBLECLIENT:
            if (tag.isInt())
                m_compatibleClient = static_cast<uint8>(tag.intValue());
            break;

        case ET_FEATURES:
            if (tag.isInt()) {
                m_supportSecIdent = static_cast<uint8>(tag.intValue() & 0x03);
                m_supportsPreview = (tag.intValue() & 0x80) != 0;
            }
            break;

        case ET_MOD_VERSION:
            if (tag.isStr())
                m_modVersion = tag.strValue();
            else if (tag.isInt())
                m_modVersion = QString::number(tag.intValue());
            checkForGPLEvildoer();
            break;

        default:
            break;
        }
    }

    // If data compression is 0, zero out related capabilities
    if (m_dataCompVer == 0) {
        m_sourceExchange1Ver = 0;
        m_extendedRequestsVer = 0;
        m_acceptCommentVer = 0;
        m_udpPort = 0;
    }

    m_emuleProtocol = true;
    m_infoPacketsReceived |= InfoPacketState::EMuleProtPack;

    // Only re-parse version if Hello didn't already provide the full version
    // via CT_EMULE_VERSION (which sets m_emuleVersion=0x99). Re-parsing after
    // that would corrupt m_clientVersion, which was already converted from
    // the raw bitfield to makeClientVersion() decimal format.
    if (prevEmuleVersion != 0x99)
        initClientSoftwareVersion();
}

// ===========================================================================
// processMuleCommentPacket — MFC BaseClient.cpp:1054-1090 (simplified)
// ===========================================================================

void UpDownClient::processMuleCommentPacket(const uint8* data, uint32 size)
{
    SafeMemFile file(data, size);

    uint8 rating = file.readUInt8();

    QString comment;
    const uint32 commentLen = file.readUInt32();
    if (commentLen > 0) {
        comment = file.readString(true, commentLen);
        if (comment.length() > MAXFILECOMMENTLEN)
            comment.truncate(MAXFILECOMMENTLEN);
    }
    if (thePrefs.wantsRatingDescLog()) {
        const QString fileName = m_reqFile ? m_reqFile->fileName() : QString();
        if (rating > 0)
            logDebug(QStringLiteral("Rating for file '%1' received: %2").arg(fileName).arg(rating));
        if (!comment.isEmpty())
            logDebug(QStringLiteral("Description for file '%1' received: %2").arg(fileName, comment));
    }

    // Spam filter, MFC BaseClient.cpp:1075-1091. The default list is the usual
    // link bait ("http://|www.|ftp." ...); a hit voids the whole submission,
    // rating included, because a peer advertising a URL is not rating the file.
    const QString filter = thePrefs.commentFilter();
    if (!comment.isEmpty() && !filter.isEmpty()) {
        const QString lower = comment.toLower();
        const QStringList terms = filter.split(QLatin1Char('|'), Qt::SkipEmptyParts);
        for (const QString& term : terms) {
            // The stored filter is already lower case; compare like for like.
            if (lower.contains(term)) {
                comment.clear();
                rating = 0;
                setSpammer(true);
                break;
            }
        }
    }

    // Only a surviving submission overwrites what this peer said before —
    // matching MFC, where a filtered comment leaves the earlier one standing
    // rather than silently erasing it.
    if (!comment.isEmpty() || rating > 0) {
        m_fileComment = comment;
        m_fileRating = rating;
        // The whole point of receiving this: fold it into the file's aggregate,
        // which is what the rating indicator draws. Without this call the value
        // was stored and never read by anything.
        if (m_reqFile)
            m_reqFile->updateFileRatingCommentAvail();
    }

    m_commentDirty = true;
}

// ===========================================================================
// sendPacket — helper
// ===========================================================================

bool UpDownClient::sendPacket(std::unique_ptr<Packet> packet, bool /*verifyConnection*/)
{
    if (!m_socket)
        return false;

    bookUpOverhead(*packet);
    m_socket->sendPacket(std::move(packet));
    return true;
}

// ===========================================================================
// checkHandshakeFinished
// ===========================================================================

void UpDownClient::setLastTriedToConnectNow()
{
    m_lastTriedToConnect = getTickCount();
}

bool UpDownClient::checkHandshakeFinished() const
{
    // MFC BaseClient.cpp:2403-2413: the only question is whether a HELLO of ours is still
    // waiting for its answer. The old test — both info packets seen — can never be true
    // for a pure eDonkey / MLDonkey / non-mule peer, which never sends the eMule one, so
    // such a peer was permanently "mid-handshake": never granted an upload slot in place,
    // never taken through onHandshakeCompleted() on an existing connection.
    // (MFC's IP_BOTH is a consumed edge, cleared inside InfoPacketsReceived(), and was
    // never meant as a lasting predicate; ours latches, which is what made it look usable.)
    return !m_helloAnswerPending;
}

// ===========================================================================
// Phase 3 — Connection Management
// Ported from MFC BaseClient.cpp lines 1238-1581
// ===========================================================================

// ===========================================================================
// tryToConnect — MFC BaseClient.cpp:1238-1478
// ===========================================================================

bool UpDownClient::tryToConnect(bool ignoreMaxCon, bool noCallbacks)
{
    if (m_connectingState != ConnectingState::None) {
        logDebug(QStringLiteral("tryToConnect: already connecting (state=%1) for %2")
                     .arg(static_cast<int>(m_connectingState)).arg(userName()));
        return true;
    }

    // Already connected — send file request directly for the new file.
    // This must be checked BEFORE the reask throttle: after doSwap() sets
    // state=None the source needs to re-request immediately, but the
    // throttle would block it for 1 minute.
    if (m_socket && m_socket->isConnected()) {
        if (thePrefs.logRawSocketPackets())
            logDebug(QStringLiteral("tryToConnect: already connected, reqFile=%1 dlState=%2")
                         .arg(m_reqFile ? m_reqFile->fileName() : QStringLiteral("null"))
                         .arg(static_cast<int>(m_downloadState)));
        // MFC BaseClient.cpp:1274-1282 runs the whole of ConnectionEstablished() here,
        // and its download-state switch (BaseClient.cpp:1541-1548) is what turns
        // DS_CONNECTING into a file request on the link we already hold. That switch is
        // in connectionEstablished(), which this branch must not call — the socket is
        // already up and a second hello would be spurious — so its three states are
        // handled inline. askForDownload() sets Connecting before calling here, so
        // without them a re-ask over an existing connection sends nothing at all.
        // DownloadState::None has no MFC counterpart and stays: it covers doSwap().
        if (m_reqFile && (m_downloadState == DownloadState::None
                          || m_downloadState == DownloadState::Connecting
                          || m_downloadState == DownloadState::WaitCallback
                          || m_downloadState == DownloadState::WaitCallbackKad)) {
            m_reaskPending = false;
            setDownloadState(DownloadState::Connected);
            sendFileRequest();
        }
        if (checkHandshakeFinished())
            onHandshakeCompleted();
        return true;
    }

    // Socket limit — MFC srchybrid/BaseClient.cpp:1290, including the override this used to
    // accept and ignore. Every caller that passes true has already committed to the
    // connection (an upload slot just allocated, a Kad probe, an answered callback), so
    // refusing them here loses the slot or the probe outright.
    // From here on a way out is a failed attempt: disconnected() reads this to drop the
    // source (MFC srchybrid/BaseClient.cpp:1285).
    m_connectingState = ConnectingState::Preconditions;

    if (!ignoreMaxCon && theApp.listenSocket && theApp.listenSocket->tooManySockets()) {
        logDebug(QStringLiteral("tryToConnect: too many sockets"));
        disconnected(QStringLiteral("Too many connections"));
        return false;
    }

    // Obfuscation settings that cannot meet — MFC BaseClient.cpp:1300-1308, which sits in
    // the same place and likewise refuses rather than trying plaintext. Every attempt
    // would fail at the handshake, so this is a connect saved, not one lost.
    if (isCryptLayerIncompatible(/*requireHash*/ false)) {
        disconnected(QStringLiteral("CryptLayer-Settings (Obfuscation) incompatible"));
        return false;
    }

    // The re-ask throttle that used to sit here has moved to askForDownload(), which is
    // where MFC keeps it (SetLastTriedToConnectTime, srchybrid/DownloadClient.cpp:186/193).
    // Here it applied to every caller, so an upload slot handed to a peer we had spoken to
    // in the last minute was silently never dialled — and addUpNextClient() then opened the
    // slot anyway.
    m_lastTriedToConnect = getTickCount();

    // Choose the address to dial when none is set yet. Prefer IPv6 for a peer that
    // advertised a reachable IPv6 and for which we have a public IPv6 — this bypasses
    // the IPv4 Low-ID restriction, since a v6-open peer accepts a direct inbound v6
    // connection even when its IPv4 side is firewalled. Otherwise fall back to the
    // High-ID → IPv4 derivation.
    // Note this is the *confidence* gate, not shouldAdvertisePublicIPv6(): dialling out over
    // v6 stays valid even when a server probed our inbound v6 and found it closed.
    if (m_connectAddress.isNull()) {
        if (m_openIPv6 && !m_userIPv6.isNull() && theApp.hasConfidentPublicIPv6())
            m_connectAddress = m_userIPv6;
        else if (!hasLowID())
            m_connectAddress = Address::fromHostOrder(m_userIDHybrid);
    }

    // IP filter, on the address we are actually about to dial.  Received IPs are already
    // filtered where they enter (server sources, source exchange, Kad, incoming accepts),
    // but the list may have been reloaded since, so outgoing attempts are filtered too —
    // MFC BaseClient.cpp:1310-1330, which likewise resolves the target address first and
    // only then filters.  Address-typed, so an IPv6 target is checked against the v6
    // range table rather than skipped: it used to be gated on isIPv4(), which a v6-only
    // source fails, and running before the resolution above meant a v6-only source was
    // still null-addressed here and escaped the check either way.
    if (theApp.ipFilter && !m_connectAddress.isNull()
        && theApp.ipFilter->isFiltered(m_connectAddress, thePrefs.ipFilterLevel())) {
        if (thePrefs.logFilteredIPs()) {
            logWarning(QStringLiteral("Refused to connect to filtered client (IP=%1) - IP filter (%2)")
                           .arg(ipstr(m_connectAddress), theApp.ipFilter->lastHitDescription()));
        }
        if (theApp.statistics)
            theApp.statistics->addFilteredClient();
        disconnected(QStringLiteral("IPFilter"));   // drops it as a source, MFC :1318-1322
        return false;
    }

    // Checked again for safety: the address may have been banned since this source was added.
    // MFC BaseClient.cpp:1327-1336.
    if (theApp.clientList && !m_connectAddress.isNull()
        && theApp.clientList->isBannedClient(m_connectAddress)) {
        if (thePrefs.logBannedClients())
            logDebug(QStringLiteral("Refused to connect to banned client %1 (IP=%2)")
                         .arg(userName(), ipstr(m_connectAddress)));
        disconnected(QStringLiteral("Banned IP"));
        return false;
    }

    // Whether this peer takes the direct-TCP branch below. Computed once so the prechecks
    // and the branch itself cannot disagree — the branch mutates m_kadState on the way in.
    const bool dialDirectly = !hasLowID() || m_connectAddress.isIPv6()
                           || m_kadState == KadState::QueuedFwCheck
                           || m_kadState == KadState::QueuedFwCheckUDP;

    // ---- Immediate-fail prechecks for a peer we can only reach by callback ----
    // MFC BaseClient.cpp:1339-1372, previously not ported at all. Without them a peer with
    // no viable route was retried forever: every attempt walked all three callback branches,
    // fell out of the bottom and returned false, leaving the download state untouched so
    // nothing upstream ever learned the source was unreachable.
    //
    // Return value: MFC's TryToConnect returns true from these paths, but its bool means
    // "the client was not deleted" — it deletes on Disconnected() and this port never does,
    // leaving that to the ClientList reaper. The contract that survives the difference, and
    // the one every existing caller already assumes, is "did we start something": these
    // paths return false, which is also what lets addUpNextClient() decline to open a slot
    // it cannot use.
    if (!dialDirectly) {
        // The three routes, in the order the branches below try them.
        const bool directUdp = supportsDirectUDPCallback() && theApp.listeningUdpPort() != 0
                            && !m_connectAddress.isNull();
        const bool kadCallback = hasValidBuddyID() && kad::Kademlia::instance()
                              && kad::Kademlia::instance()->isConnected()
                              && ((!m_buddyAddress.isNull() && m_buddyPort != 0) || m_reqFile);
        const bool serverCallback = theApp.serverConnect
                                 && theApp.serverConnect->isLocalServer(
                                        m_serverAddress.toNetworkUint32(), m_serverPort);

        // The caller asked for a direct dial only — a Kad firewall check or a buddy request,
        // where a callback would answer a question we never asked.
        if (noCallbacks) {
            logWarning(QStringLiteral("tryToConnect: callback wanted for a no-callback client %1")
                           .arg(userName()));
            disconnected(QStringLiteral("LowID: no callback allowed"));
            return false;
        }

        // No route at all — fail once instead of walking all three branches on every retry.
        if (!directUdp && !kadCallback && !serverCallback) {
            logDebug(QStringLiteral("tryToConnect: no callback route available for %1")
                         .arg(userName()));
            disconnected(QStringLiteral("LowID: no callback option available"));
            return false;
        }

        // Low-ID to Low-ID. MFC applies canDoCallback() to every Low-ID client
        // (srchybrid/BaseClient.cpp:1341); here it gates only the case the rule is actually
        // about — a relay through our server. Its two arms are "the callback must go via the
        // server, so we need a High ID there" and "the peer is on the same server we are, and
        // asking that server to call one of its own clients gets us banned". Neither premise
        // holds when we have our own direct-UDP or Kad-buddy route to the peer, and applying
        // it there would disable direct UDP callbacks outright whenever Kad happens to be
        // down — which is exactly the situation they exist for.
        //
        // Kept as a source rather than dropped, in the hope our own ID changes;
        // PartFile::process() revives it from this state.
        if (!directUdp && !kadCallback && !theApp.canDoCallback(this)) {
            logDebug(QStringLiteral("tryToConnect: LowID->LowID, no callback route to %1")
                         .arg(userName()));
            disconnected(QStringLiteral("LowID->LowID"));
            return false;
        }
    }

    // MFC BaseClient.cpp:1379 — track connecting client for 45s timeout
    if (theApp.clientList)
        theApp.clientList->addConnectingClient(this);

    // ---- Path 3: Normal outgoing TCP connection (high-ID clients) ----
    // MFC BaseClient.cpp:1383-1396. Also taken for an IPv6 target: a peer reachable
    // over IPv6 accepts a direct inbound connection regardless of its IPv4 Low-ID.
    if (dialDirectly) {
        // Transition Kad FW check states before connecting
        if (m_kadState == KadState::QueuedFwCheck)
            setKadState(KadState::ConnectingFwCheck);
        if (m_kadState == KadState::QueuedFwCheckUDP)
            setKadState(KadState::ConnectingFwCheckUDP);

        // Set download state so connectionEstablished() sends the file request.
        if (m_reqFile && m_downloadState == DownloadState::None)
            setDownloadState(DownloadState::Connecting);

        // Endpoint::toString() renders both families and brackets IPv6 ([addr]:port).
        // The old connectIP=0x%x field is gone on purpose: it came from toUint32(),
        // which is 0 for every IPv6 address — it printed 0.0.0.0 on exactly this branch.
        logDebug(QStringLiteral("tryToConnect: DIRECT TCP to %1 (userIDHybrid=0x%2)")
                     .arg(Endpoint(m_connectAddress, m_userPort).toString())
                     .arg(m_userIDHybrid, 8, 16, QLatin1Char('0')));

        m_connectingState = ConnectingState::DirectTCP;
        connect();
        return true;
    }

    // ---- Path 4: Direct Callback via UDP (firewalled but UDP open) ----
    // MFC BaseClient.cpp:1399-1413
    if (supportsDirectUDPCallback() && theApp.listeningUdpPort() != 0 && !m_connectAddress.isNull()) {
        m_connectingState = ConnectingState::DirectCallback;

        // Build connect options byte: MFC GetMyConnectOptions(true, false)
        // Bit 0: CryptLayer supported, Bit 1: CryptLayer requested,
        // Bit 2: CryptLayer required, Bit 3: Direct UDP callback (disabled for outgoing)
        const auto connectOpts = static_cast<uint8>(
            (static_cast<uint8>(thePrefs.cryptLayerSupported()) << 0) |
            (static_cast<uint8>(thePrefs.cryptLayerRequested()) << 1) |
            (static_cast<uint8>(thePrefs.cryptLayerRequired()) << 2));

        SafeMemFile data;
        data.writeUInt16(theApp.advertisedTcpPort());              // our TCP port
        data.writeHash16(thePrefs.userHash().data());  // our user hash
        data.writeUInt8(connectOpts);          // connection/crypto options

        auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_DIRECTCALLBACKREQ);
        if (theApp.clientUDP) {
            // Endpoint form: the uint32 overload takes host order, so passing
            // toNetworkUint32() there reversed the octets — and an IPv6 target
            // collapsed to 0 entirely.
            theApp.clientUDP->sendPacket(std::move(packet),
                                          Endpoint(m_connectAddress, m_kadPort),
                                          shouldReceiveCryptUDPPackets(),
                                          m_userHash.data(), false, 0);
        }

        logDebug(QStringLiteral("tryToConnect: DIRECT CALLBACK via UDP to %1:%2")
                     .arg(m_connectAddress.toQHostAddress().toString()).arg(m_kadPort));
        return true;
    }

    // ---- Paths 6 & 7: Server / Kad callback (firewalled, no direct UDP) ----
    // MFC BaseClient.cpp:1417-1418: set WaitCallback before callback attempts
    if (m_downloadState == DownloadState::Connecting)
        setDownloadState(DownloadState::WaitCallback);

    // Low-ID client — need callback via server
    if (hasLowID() && theApp.serverConnect && theApp.serverConnect->isConnected()) {
        // Check if the source is on the same server we're connected to
        if (theApp.serverConnect->isLocalServer(m_serverAddress.toNetworkUint32(), m_serverPort)) {
            // Send callback request via server
            SafeMemFile data;
            data.writeUInt32(m_userIDHybrid);
            auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_CALLBACKREQUEST);
            theApp.serverConnect->sendPacket(std::move(packet));
            m_connectingState = ConnectingState::ServerCallback;
            logDebug(QStringLiteral("tryToConnect: SERVER CALLBACK for lowID=%1").arg(m_userIDHybrid));
            return true;
        }
    }

    // Kademlia callback path — send KADEMLIA_CALLBACK_REQ via UDP to target's buddy.
    // Matches MFC BaseClient.cpp:1435-1449.
    if (hasValidBuddyID() && kad::Kademlia::instance()
        && kad::Kademlia::instance()->isConnected()
        && ((!m_buddyAddress.isNull() && m_buddyPort != 0) || m_reqFile != nullptr))
    {
        // Try to find buddy IP from Kad routing table if we don't have it
        if (m_buddyAddress.isNull() || m_buddyPort == 0) {
            kad::UInt128 buddyKadId(m_buddyID.data());
            if (auto* zone = kad::Kademlia::getInstanceRoutingZone()) {
                if (auto* contact = zone->getContact(buddyKadId)) {
                    m_buddyAddress = contact->address();
                    m_buddyPort = contact->getUDPPort();
                    logDebug(QStringLiteral("tryToConnect: found buddy IP from Kad routing table: %1:%2")
                                 .arg(m_buddyAddress.toQHostAddress().toString()).arg(m_buddyPort));
                }
            }
        }

        if (!m_buddyAddress.isNull() && m_buddyPort != 0 && m_reqFile != nullptr) {
            SafeMemFile data;
            kad::UInt128 buddyKadId(m_buddyID.data());
            kad::io::writeUInt128(data, buddyKadId);
            kad::UInt128 fileId(m_reqFile->fileHash());
            kad::io::writeUInt128(data, fileId);
            data.writeUInt16(theApp.advertisedTcpPort());

            auto packet = std::make_unique<Packet>(data, OP_KADEMLIAHEADER, KADEMLIA_CALLBACK_REQ);
            m_connectingState = ConnectingState::KadCallback;
            // MFC FIXME: We don't know which kad version the buddy has, so we need to send unencrypted
            if (theApp.clientUDP)
                theApp.clientUDP->sendPacket(std::move(packet),
                                             Endpoint(m_buddyAddress, m_buddyPort),
                                             false, nullptr, true, 0);
            setDownloadState(DownloadState::WaitCallbackKad);
            logDebug(QStringLiteral("tryToConnect: KAD CALLBACK via buddy %1:%2")
                         .arg(m_buddyAddress.toQHostAddress().toString()).arg(m_buddyPort));
            return true;
        }
        // Buddy IP not in routing table — fall back to FindSource search
        // to locate the buddy on the DHT. Matches MFC BaseClient.cpp:1450-1471.
        if (m_reqFile != nullptr) {
            kad::UInt128 buddyKadId(m_buddyID.data());
            if (kad::Kademlia::instance()->getPrefs()->totalSource() > 0
                || kad::SearchManager::alreadySearchingFor(buddyKadId))
            {
                logWarning(QStringLiteral("tryToConnect: Buddy without known IP, FindSource lookup currently impossible"));
                return true;
            }
            auto* findSource = new kad::Search;
            findSource->setSearchType(kad::SearchType::FindSource);
            findSource->setTargetID(buddyKadId);
            // the payload we look for
            findSource->addFileID(kad::UInt128(m_reqFile->fileHash()));
            findSource->setGUIName(m_reqFile->fileName());
            if (kad::SearchManager::startSearch(findSource)) {
                m_connectingState = ConnectingState::KadCallback;
                setDownloadState(DownloadState::WaitCallbackKad);
                logDebug(QStringLiteral("tryToConnect: started FindSource lookup for buddy of %1")
                             .arg(userName()));
            } else {
                delete findSource;
            }
            return true;
        }
        logDebug(QStringLiteral("tryToConnect: Kad buddy without known IP for %1").arg(userName()));
        disconnected(QStringLiteral("LowID: buddy without known address"));
        return false;
    }

    logDebug(QStringLiteral("tryToConnect: no viable connection path for %1 "
                            "(connectAddr=%2 lowID=%3 buddyValid=%4)")
                 .arg(userName())
                 .arg(m_connectAddress.isNull() ? QStringLiteral("<none>")
                                                : Endpoint(m_connectAddress, m_userPort).toString())
                 .arg(hasLowID())
                 .arg(hasValidBuddyID()));
    disconnected(QStringLiteral("No connection path"));
    return false;
}

// ===========================================================================
// connect — MFC BaseClient.cpp:1480-1497
// ===========================================================================

void UpDownClient::connect()
{
    // Create a ClientReqSocket for this peer connection
    auto* reqSocket = new ClientReqSocket(this);
    if (!reqSocket->createSocket()) {
        logDebug(QStringLiteral("connect: failed to create socket for %1").arg(userName()));
        delete reqSocket;
        // Still connecting: the connect timeout cleans up (MFC BaseClient.cpp:1390)
        return;
    }

    setSocket(reqSocket);
    m_incomingConnection = false;   // we are dialling out

    // Register with ListenSocket for connection tracking; a dial counts against the
    // per-5-s limit (MFC CClientReqSocket::Create).
    if (theApp.listenSocket) {
        theApp.listenSocket->addSocket(reqSocket);
        theApp.listenSocket->addConnection();
    }

    // Connect socket signals
    QObject::connect(reqSocket, &ClientReqSocket::clientDisconnected,
                     this, [this, reqSocket](const QString& reason) {
        if (m_socket == reqSocket)   // a replaced socket must not tear down its successor
            disconnected(reason, true);
    });

    QObject::connect(reqSocket, &ClientReqSocket::extPacketReceived,
                     this, &UpDownClient::onExtPacketReceived);

    QObject::connect(reqSocket, &ClientReqSocket::packetForClient,
                     this, &UpDownClient::onPacketForClient);

    QObject::connect(reqSocket, &ClientReqSocket::helloReceived,
                     this, &UpDownClient::onHelloReceived);

    QObject::connect(reqSocket, &ClientReqSocket::fileRequestReceived,
                     this, &UpDownClient::onFileRequestReceived);

    QObject::connect(reqSocket, &ClientReqSocket::uploadRequestReceived,
                     this, &UpDownClient::onUploadRequestReceived);

    QObject::connect(reqSocket, &ClientReqSocket::socketConnected,
                     this, &UpDownClient::connectionEstablished);

    // Monitor all QAbstractSocket state changes for debugging
    QObject::connect(reqSocket, &QAbstractSocket::stateChanged,
                     this, [reqSocket](QAbstractSocket::SocketState state) {
        static const char* stateNames[] = {
            "UnconnectedState", "HostLookupState", "ConnectingState",
            "ConnectedState", "BoundState", "ListeningState", "ClosingState"
        };
        const char* name = (state >= 0 && state <= 6) ? stateNames[state] : "Unknown";
        logDebug(QStringLiteral("Socket state → %1 for %2:%3 (fd=%4)")
                     .arg(QLatin1String(name))
                     .arg(reqSocket->peerAddress().toString())
                     .arg(reqSocket->peerPort())
                     .arg(reqSocket->socketDescriptor()));
    });

    // Set up connection encryption if the peer supports/requests it and we have their hash.
    // MFC BaseClient.cpp:1487-1491.
    // MFC: IsCryptLayerEnabled() = cryptLayerSupported(), IsCryptLayerPreferred() = cryptLayerRequested()
    reqSocket->setObfuscationConfig(thePrefs.obfuscationConfig());
    bool encrypted = false;
    logDebug(QStringLiteral("connect: encryption check — hasValidHash=%1 supportsCrypt=%2 prefCryptSupported=%3 requestsCrypt=%4 prefCryptRequested=%5")
                 .arg(hasValidHash()).arg(supportsCryptLayer()).arg(thePrefs.cryptLayerSupported())
                 .arg(requestsCryptLayer()).arg(thePrefs.cryptLayerRequested()));
    if (hasValidHash()) {
        QString hashHex;
        for (int i = 0; i < 16; ++i)
            hashHex += QStringLiteral("%1").arg(userHash()[i], 2, 16, QLatin1Char('0'));
        logDebug(QStringLiteral("connect: targetHash=%1").arg(hashHex));
    }
    if (hasValidHash() && supportsCryptLayer() && thePrefs.cryptLayerSupported()
        && (requestsCryptLayer() || thePrefs.cryptLayerRequested())) {
        reqSocket->setConnectionEncryption(true, userHash(), false);
        encrypted = true;
    } else {
        // Explicitly mark the socket as plaintext so the send path
        // doesn't warn about an unknown encryption state.
        reqSocket->setConnectionEncryption(false, nullptr, false);
    }

    // Configure proxy
    reqSocket->initProxySupport(thePrefs.proxySettings());

    // Initiate TCP connection. toQHostAddress() dials both families — an IPv6
    // m_connectAddress connects over IPv6, an IPv4 one exactly as before.
    const QHostAddress addr = m_connectAddress.toQHostAddress();
    logDebug(QStringLiteral("connect: connectToHost(%1, %2) encrypted=%3 socketState=%4 fd=%5")
                 .arg(addr.toString()).arg(m_userPort)
                 .arg(encrypted)
                 .arg(static_cast<int>(reqSocket->state()))
                 .arg(reqSocket->socketDescriptor()));

    reqSocket->connectToPeer(m_connectAddress, m_userPort);

    logDebug(QStringLiteral("connect: after connectToHost — socketState=%1 fd=%2")
                 .arg(static_cast<int>(reqSocket->state()))
                 .arg(reqSocket->socketDescriptor()));

    reqSocket->waitForOnConnect();

    m_connectingState = ConnectingState::DirectTCP;
}

// ===========================================================================
// wireIncomingSocket — production incoming connection handler
// ===========================================================================

void UpDownClient::wireIncomingSocket(ClientReqSocket* socket)
{
    setSocket(socket);
    socket->setClient(this);
    m_incomingConnection = true;

    QObject::connect(socket, &ClientReqSocket::clientDisconnected,
                     this, [this, socket](const QString& reason) {
        if (m_socket == socket)   // a replaced socket must not tear down its successor
            disconnected(reason, true);
    });

    QObject::connect(socket, &ClientReqSocket::helloReceived,
                     this, &UpDownClient::onHelloReceived);

    QObject::connect(socket, &ClientReqSocket::fileRequestReceived,
                     this, &UpDownClient::onFileRequestReceived);

    QObject::connect(socket, &ClientReqSocket::uploadRequestReceived,
                     this, &UpDownClient::onUploadRequestReceived);

    QObject::connect(socket, &ClientReqSocket::extPacketReceived,
                     this, &UpDownClient::onExtPacketReceived);

    QObject::connect(socket, &ClientReqSocket::packetForClient,
                     this, &UpDownClient::onPacketForClient);
}

// ===========================================================================
// connectionEstablished — MFC BaseClient.cpp:1499-1581
// ===========================================================================

void UpDownClient::connectionEstablished()
{
    logDebug(QStringLiteral("connectionEstablished: peer=%1:%2 socket=%3")
                 .arg(m_socket ? m_socket->peerAddress().toString() : QStringLiteral("null"))
                 .arg(m_socket ? m_socket->peerPort() : 0)
                 .arg(m_socket ? QStringLiteral("valid") : QStringLiteral("null")));

    if (theApp.clientList)
        theApp.clientList->removeConnectingClient(this);

    m_connectingState = ConnectingState::None;

    // Send HELLO on outgoing connections if handshake not started
    logDebug(QStringLiteral("connectionEstablished: handshakeFinished=%1 helloAnswerPending=%2 downloadState=%3")
                 .arg(checkHandshakeFinished()).arg(m_helloAnswerPending)
                 .arg(static_cast<int>(m_downloadState)));
    // Only on an outgoing connection. On an accepted one the peer opened with OP_HELLO and
    // we have just answered it, so sending our own hello here would be a spurious extra
    // packet. MFC sends this from Connect() at dial time and relies on CEMSocket buffering
    // it; here the socketConnected signal is deliberately deferred past the RC4 handshake
    // (ClientReqSocket::onSocketConnected), which is the same moment in practice.
    if (!m_incomingConnection && !m_helloAnswerPending) {
        sendHelloPacket();
    }

    // Handle download state transitions including callback states.
    // Matches MFC BaseClient.cpp:1541-1548.
    if (m_downloadState == DownloadState::Connecting
        || m_downloadState == DownloadState::WaitCallback
        || m_downloadState == DownloadState::WaitCallbackKad)
    {
        m_reaskPending = false;                         // MFC BaseClient.cpp:1545
        setDownloadState(DownloadState::Connected);
        if (m_helloAnswerPending) {
            // Outgoing: we sent OP_HELLO and owe the peer's OP_HELLOANSWER before we know
            // its extended-request version, so the request is built there instead.
            logDebug(QStringLiteral("connectionEstablished: deferring file request until "
                                    "HELLO_ANSWER (downloadState=%1)")
                         .arg(static_cast<int>(m_downloadState)));
            m_pendingFileRequest = true;
        } else {
            // Inbound (typically a LowID source answering our OP_CALLBACKREQUEST): its
            // OP_HELLO already carried everything, and no OP_HELLOANSWER will ever arrive
            // to release a deferred request. Send it now, as MFC does inline.
            logDebug(QStringLiteral("connectionEstablished: sending file request inline "
                                    "(downloadState=%1)")
                         .arg(static_cast<int>(m_downloadState)));
            sendFileRequest();
        }
    }

    // Kademlia state handling
    switch (m_kadState) {
    case KadState::ConnectingFwCheck:
        setKadState(KadState::ConnectedFwCheck);
        {
            // MFC BaseClient.cpp — send ACK so the remote node knows its TCP port is reachable
            auto packet = std::make_unique<Packet>(OP_KAD_FWTCPCHECK_ACK, 0);
            packet->prot = OP_EMULEPROT;
            sendPacket(std::move(packet));
        }
        break;
    case KadState::QueuedBuddy:
    case KadState::ConnectingBuddy:
    case KadState::IncomingBuddy:
        setKadState(KadState::ConnectedBuddy);
        break;
    case KadState::ConnectingFwCheckUDP:
        logDebug(QStringLiteral("connectionEstablished: KadState → FwCheckUDP, sending FW check request"));
        setKadState(KadState::FwCheckUDP);
        sendFirewallCheckUDPRequest();
        break;
    default:
        break;
    }

    // Chat session. While a friend attempt is carrying this connection the friend owns
    // the transition — it still has secure identification to wait for.
    // MFC srchybrid/BaseClient.cpp:1533-1539.
    if (m_chatState == ChatState::Connecting || m_chatState == ChatState::Chatting) {
        Friend* f = friendPtr();
        if (f && f->isTryingToConnect()) {
            f->updateFriendConnectionState(FriendConnectReport::Established);
            if (m_credits
                && m_credits->currentIdentState(m_connectAddress)
                       == IdentState::IdFailed)
            {
                f->updateFriendConnectionState(FriendConnectReport::SecureIdentFailed);
            }
        } else if (m_chatState == ChatState::Connecting) {
            setChatState(ChatState::Chatting);
            sendPendingChatMessage();
            emit chatStateChanged();
        }
    }
}

// ===========================================================================
// onHandshakeCompleted — MFC BaseClient.cpp:1550-1573
//
// See the declaration in UpDownClient.h for why these three blocks live here
// rather than in connectionEstablished().
// ===========================================================================

void UpDownClient::onHandshakeCompleted()
{
    // Packets queued by safeConnectAndSendPacket() while we were dialling. They go out
    // here, not in connectionEstablished(): on an outgoing connection that one runs at
    // TCP connect, and a peer drops whoever asks for something before saying hello.
    // MFC BaseClient.cpp:1575-1579 (its ConnectionEstablished runs after the hello).
    for (auto& packet : std::exchange(m_waitingPackets, {})) {
        if (m_socket) {
            bookUpOverhead(*packet);
            m_socket->sendPacket(std::move(packet));
        }
    }

    // (a) We owed this peer a file re-ask that askForDownload() delayed on one of its
    //     LowID paths, and it is now connected — send the request we held back.
    //     MFC BaseClient.cpp:1550-1556. The flag is cleared unconditionally, even when
    //     the inner guard rejects, so it cannot latch across the peer's next re-ask.
    //     (The in-flight *UDP* re-ask is a separate flag, m_udpPending, charged as a
    //     failure at the head of askForDownload().)
    if (m_reaskPending) {
        m_reaskPending = false;
        if (m_downloadState != DownloadState::None
            && m_downloadState != DownloadState::Downloading)
        {
            setDownloadState(DownloadState::Connected);
            sendFileRequest();
        }
    }

    // (b) The upload queue granted this client a slot and asked us to dial it; the
    //     handshake is now done, so activate the slot. MFC BaseClient.cpp:1558-1565.
    //     isDownloading() is an m_uploadingList membership test — without it we
    //     would promote clients that were never granted a slot at all.
    if (m_uploadState == UploadState::Connecting
        && theApp.uploadQueue && theApp.uploadQueue->isDownloading(this))
    {
        setUploadState(UploadState::Uploading);
        auto packet = std::make_unique<Packet>(OP_ACCEPTUPLOADREQ, 0);
        packet->prot = OP_EDONKEYPROT;
        sendPacket(std::move(packet));
    }

    // (c) requestSharedFileList() armed the counter and dialled; send the request
    //     now. MFC BaseClient.cpp:1567-1573 — note the strict "== 1": once the peer
    //     answers with a directory list the counter becomes the directory count, and
    //     this must not re-fire. The counter is cleared by the answer handlers, not
    //     here, exactly as in MFC.
    if (m_fileListRequested == 1) {
        auto packet = std::make_unique<Packet>(
            m_sharedDirectories ? OP_ASKSHAREDDIRS : OP_ASKSHAREDFILES, 0);
        packet->prot = OP_EDONKEYPROT;
        sendPacket(std::move(packet));
    }
}

// ===========================================================================
// maybeBootstrapKadFromPeer — MFC ListenSocket.cpp:289-290
// ===========================================================================

void UpDownClient::maybeBootstrapKadFromPeer()
{
    if (m_kadPort == 0 || m_kadVersion < KADEMLIA_VERSION2_47a)
        return;

    // KADEMLIA2_BOOTSTRAP_REQ goes out over UDPv4, and toUint32() is 0 for a v6
    // address — bootstrapping off an IPv6 peer would target 0.0.0.0.
    if (!m_userAddress.isIPv4())
        return;

    // Kademlia::bootstrap() already no-ops while connected and rate-limits itself to
    // one attempt per 10s, matching MFC CKademlia::Bootstrap — so calling it on every
    // qualifying hello is cheap. It wants host byte order, which toUint32() returns
    // (MFC does the same conversion with ntohl(GetIP())).
    if (auto* kadInst = kad::Kademlia::instance())
        kadInst->bootstrap(m_userAddress.toUint32(), m_kadPort);
}

// ===========================================================================
// releaseSocket
// ===========================================================================

void UpDownClient::releaseSocket(bool destroy)
{
    EMSocket* sock = m_socket;
    m_socket = nullptr;
    if (!sock)
        return;

    // Cut the wiring first: a dying socket must not report back, or its close would run
    // disconnected() again against whatever socket this client holds by then.
    QObject::disconnect(sock, nullptr, this, nullptr);

    auto* reqSocket = qobject_cast<ClientReqSocket*>(sock);
    if (!reqSocket)
        return;
    reqSocket->setClient(nullptr);   // checkTimeOut() reads it, and this client may go first
    if (destroy) {
        // safeDelete() does not unregister, and ListenSocket::process() would walk a
        // dangling pointer.
        if (theApp.listenSocket)
            theApp.listenSocket->removeSocket(reqSocket);
        reqSocket->safeDelete();
    }
}

// ===========================================================================
// disconnected — MFC BaseClient.cpp:1101-1233
// ===========================================================================

bool UpDownClient::disconnected(const QString& reason, bool fromSocket)
{
    logDebug(QStringLiteral("Client disconnected: %1 reason: %2").arg(userName(), reason));

    if (theApp.clientList)
        theApp.clientList->removeConnectingClient(this);

    // Read before it is cleared: a connect attempt that ended here never reached the peer.
    // A failure at our own proxy is not the peer's (MFC BaseClient.cpp:1185).
    const bool proxyFailed = m_socket && m_socket->proxyConnectFailed();
    const bool connectFailed = m_connectingState != ConnectingState::None && !proxyFailed;
    m_connectingState = ConnectingState::None;

    // The preview we were waiting for is not coming.
    if (m_previewReqPending) {
        m_previewReqPending = false;
        emit previewAnswerReceived(m_previewReqHash, {});
    }

    // Detach the socket so tryToConnect() can create a fresh one. One we did not get here from
    // is still live and is closed too — MFC BaseClient.cpp:1204-1208. Left wired, it kept
    // delivering packets and later ran this function again from its own close.
    releaseSocket(/*destroy*/ !fromSocket);

    // A recovery request that was in flight is lost with the connection. Report it so
    // the part is asked of another source instead of stalling until this client is
    // reaped. MFC BaseClient.cpp:1134-1138.
    if (m_aichRequested) {
        m_aichRequested = false;
        AICHRecoveryHashSet::clientAICHRequestFailed(this);
    }

    // Reset handshake state so the next connection starts a fresh HELLO
    // exchange.  Without this, checkHandshakeFinished() returns true on
    // reconnect and connectionEstablished() never sends OP_HELLO.
    m_infoPacketsReceived = InfoPacketState::None;
    m_helloAnswerPending  = false;
    m_secIdentSent        = false;
    m_incomingConnection  = false;

    // Save session stats.
    // Connecting counts as well as Uploading: addUpNextClient() pushes the client onto
    // m_uploadingList on BOTH of its branches, so a client whose connect never completed
    // is still holding a slot. Clearing the state alone would leak it. MFC removes for
    // both states too (BaseClient.cpp:1118-1120).
    if (m_uploadState == UploadState::Uploading
        || m_uploadState == UploadState::Connecting)
    {
        setUploadState(UploadState::None);
        if (theApp.uploadQueue)
            theApp.uploadQueue->removeFromUploadQueue(this);
    }

    // A hashset request that died with the connection goes back to the file, or no
    // other source is ever asked for it. MFC BaseClient.cpp:1143-1151.
    if (m_reqFile) {
        if (m_hashsetRequestingMD4)
            m_reqFile->setMD4HashsetNeeded(true);
        if (m_hashsetRequestingAICH)
            m_reqFile->setAICHPartHashsetNeeded(true);
    }
    m_hashsetRequestingMD4 = false;   // MFC BaseClient.cpp:1223-1224
    m_hashsetRequestingAICH = false;

    // A shared-file-list request died with the connection. Reset the counter, or
    // requestSharedFileList()'s "already in progress" refusal would latch forever.
    // MFC BaseClient.cpp:1152-1155.
    if (m_fileListRequested) {
        m_fileListRequested = 0;
        logStatusWarning(QStringLiteral("Failed to retrieve shared files from user %1")
                             .arg(statusName()));
    }

    // MFC BaseClient.cpp:1122-1192. A transfer that drops is still a good source: back on the
    // queue, where PartFile::process() re-asks it. A source waiting in the peer's queue or
    // with no needed parts is kept as it is. Dead, and no longer a source of anything:
    // one that never answered our file request, and one we could not reach at all.
    bool sourceDied = false;
    if (m_downloadState == DownloadState::Downloading) {
        noteDownloadSessionEndedByPeer();
        setDownloadState(DownloadState::OnQueue);
    } else {
        const bool unanswered = m_downloadState == DownloadState::Connected
                             || m_downloadState == DownloadState::ReqHashSet;
        // A download state that only exists during an attempt says the same as the
        // connecting state, for the callers that set one without the other.
        const bool unreachable = connectFailed
                              || (!proxyFailed
                                  && (m_downloadState == DownloadState::Connecting
                                      || m_downloadState == DownloadState::WaitCallback
                                      || m_downloadState == DownloadState::WaitCallbackKad))
                              || m_downloadState == DownloadState::Error;
        if (!isEd2kClient()) {
            // URL and HTTP Cache sources run their own retry and stay attached.
            if (unanswered || m_downloadState == DownloadState::Connecting)
                setDownloadState(DownloadState::None);
        } else if (unanswered || unreachable) {
            sourceDied = unreachable;
            if (theApp.clientList)
                theApp.clientList->globalDeadSourceList.addDeadSource(deadSourceKey(), hasLowID());
            // Leaves the file's source list; the ClientList reaper takes the client
            // once nothing else holds it. Without this a long download fills up with
            // dead peers until maxSourcesPerFile refuses every new one.
            if (theApp.downloadQueue && m_downloadState != DownloadState::None)
                theApp.downloadQueue->removeSource(this);
            else
                setDownloadState(DownloadState::None);
        }
    }

    // MFC's bDelete: is this client still needed for anything?
    bool unneeded = sourceDied;
    if (!sourceDied) {
        switch (m_downloadState) {
        case DownloadState::OnQueue:
        case DownloadState::TooManyConns:
        case DownloadState::NoNeededParts:
        case DownloadState::LowToLowIP:
            break;
        default:
            unneeded = m_uploadState != UploadState::OnUploadQueue;
        }
    }
    if (m_chatState != ChatState::None)
        unneeded = false;

    // Clear pending block requests on disconnect
    clearDownloadBlockRequests();

    // Reset chat state. A friend attempt may carry on from here — it can go looking for
    // the friend in Kad — so it decides what the session state becomes.
    // MFC srchybrid/BaseClient.cpp:1197-1200.
    if (Friend* f = friendPtr(); f && f->isTryingToConnect())
        f->updateFriendConnectionState(FriendConnectReport::Disconnected);
    else if (m_chatState == ChatState::Connecting)
        setChatState(ChatState::UnableToConnect);

    // Update friend's last-seen info
    if (m_friend) {
        m_friend->setLastUsedAddress(m_connectAddress);
        m_friend->setLastUsedPort(m_userPort);
        m_friend->setLastSeen(std::time(nullptr));
    }

    // GUI refresh
    emit updateDisplayedInfoRequested();

    m_sentCancelTransfer = false;

    // Handle Kad UDP firewall check cancellation/failure — MFC BaseClient.cpp:1109-1112
    if (m_kadState == KadState::QueuedFwCheckUDP
        || m_kadState == KadState::ConnectingFwCheckUDP) {
        kad::UDPFirewallTester::setUDPFWCheckResult(false, true, m_connectAddress.toUint32(), 0);
    } else if (m_kadState == KadState::FwCheckUDP) {
        kad::UDPFirewallTester::setUDPFWCheckResult(false, false, m_connectAddress.toUint32(), 0);
    }
    setKadState(KadState::None);

    // Callers do not delete on this: ClientList::process() reaps a client nothing needs.
    return unneeded;
}

// ===========================================================================
// onSocketConnected — MFC BaseClient.cpp:2430-2439
// ===========================================================================

void UpDownClient::onSocketConnected(int errorCode)
{
    Q_UNUSED(errorCode);
    m_connectingState = ConnectingState::None;
}

// ===========================================================================
// Phase 3 — Protocol Utility
// ===========================================================================

// ===========================================================================
// resetFileStatusInfo
// ===========================================================================

void UpDownClient::resetFileStatusInfo()
{
    m_partStatus.clear();
    m_partCount = 0;
    m_completeSource = false;
    m_clientFilename.clear();
    // m_aichRequested deliberately survives, as in MFC (BaseClient.cpp:2040-2052): the
    // request is tracked globally, and clearing it here would orphan the entry and make
    // the answer look unrequested.
    m_hashsetRequestingMD4 = false;
    m_hashsetRequestingAICH = false;
    m_reqFileAICHHash.reset();   // MFC BaseClient.cpp:2050
    m_remoteQueueRank = 0;       // the rank was the old file's (MFC :2044)
}

// ===========================================================================
// infoPacketsReceived — called after handshake complete
// ===========================================================================

void UpDownClient::onInfoPacketsReceived()
{
    // Complete buddy link after HELLO exchange (MFC ProcessMuleInfoPacket)
    if (m_kadState == KadState::ConnectedBuddy && theApp.clientList) {
        theApp.clientList->setBuddy(this, BuddyStatus::Connected);
    }

    // Send SecureIdent state packet once per connection, only after BOTH
    // eDonkey and eMule info packets have been received.  MFC checks
    // GetInfoPacketsReceived() == IP_BOTH at every call site before calling
    // InfoPacketsReceived().  The m_secIdentSent flag still guards against
    // duplicate sends across the multiple call sites.
    // MFC tests GetInfoPacketsReceived() == IP_BOTH at each of its four call sites before
    // entering InfoPacketsReceived() (BaseClient.cpp:2029-2038). That test moved here when
    // checkHandshakeFinished() stopped meaning "both info packets seen". Unlike MFC we keep
    // the latch instead of consuming it back to None; m_secIdentSent is the duplicate guard.
    if ((m_infoPacketsReceived & InfoPacketState::Both) == InfoPacketState::Both &&
        m_supportSecIdent != 0 && m_credits && !m_secIdentSent) {
        m_secIdentSent = true;
        sendSecIdentStatePacket();
    }
    m_failedFileIdReqs = 0;
}

// ===========================================================================
// isBanned
// ===========================================================================

bool UpDownClient::isBanned() const
{
    // The ban is the 2 h entry in the IP list; the upload state only records that this
    // object earned one, and must not outlive it (MFC BaseClient.cpp:2054). With no
    // address there is no entry to expire, so the state is all there is.
    if (m_connectAddress.isNull() || !theApp.clientList)
        return m_uploadState == UploadState::Banned;
    return theApp.clientList->isBannedClient(m_connectAddress);
}

// ===========================================================================
// processEmuleQueueRank
// ===========================================================================

void UpDownClient::processEmuleQueueRank(const uint8* data, uint32 size)
{
    // MFC: strict 12-byte packet (uint16 rank + 10 bytes padding)
    if (size != 12)
        return;

    const uint16 rank = peekUInt16(data);
    setRemoteQueueFull(false);
    setRemoteQueueRank(rank, m_downloadState == DownloadState::OnQueue);
    checkQueueRankFlood();
}

// ===========================================================================
// processEdonkeyQueueRank
// ===========================================================================

void UpDownClient::processEdonkeyQueueRank(const uint8* data, uint32 size)
{
    if (size < 4)
        return;

    SafeMemFile file(data, size);
    const uint32 rank = file.readUInt32();
    setRemoteQueueRank(rank, m_downloadState == DownloadState::OnQueue);
    checkQueueRankFlood();
}

// ===========================================================================
// checkQueueRankFlood
// ===========================================================================

// A queue-rank packet we did not ask for. Three in a row without an intervening
// request is a flood (MFC DownloadClient.cpp:1997-2015). This used to only log — it
// now feeds the address-scoped two-strikes counter like the other detectors.
void UpDownClient::checkQueueRankFlood()
{
    if (m_queueRankPending) {
        // Solicited: this is the answer we were waiting for, so the streak resets.
        m_queueRankPending = false;
        m_unaskQueueRankRecv = 0;
        return;
    }

    // A client actively sending us data is exempt — its rank updates are part of a
    // legitimate transfer.
    if (m_downloadState == DownloadState::Downloading)
        return;

    if (m_unaskQueueRankRecv < 3)
        ++m_unaskQueueRankRecv;

    if (m_unaskQueueRankRecv == 3) {
        logDebug(QStringLiteral("Queue rank flood detected from %1").arg(userName()));
        registerBadRequest(QStringLiteral("QR flood"));
    }
}

// ===========================================================================
// requestSharedFileList
// ===========================================================================

void UpDownClient::requestSharedFileList()
{
    if (m_noViewSharedFiles) {
        logStatusWarning(QStringLiteral("User %1 does not allow viewing their shared files")
                             .arg(statusName()));
        return;
    }

    // MFC BaseClient.cpp:1783-1791. Arm the counter and connect; the request itself goes
    // out from onHandshakeCompleted() once the hello exchange is done. Sending it here
    // instead would mean giving up on every client we are not already connected to — and
    // would leave the counter at 0 at connect time, which is the value the whole
    // OP_ASKSHAREDDIRS answer chain keys off.
    if (m_fileListRequested != 0) {
        logStatusWarning(QStringLiteral("Requesting shared files from user %1 (%2) is already in progress")
                             .arg(statusName()).arg(m_userIDHybrid));
        return;
    }

    logStatusInfo(QStringLiteral("Requesting shared files from '%1'").arg(statusName()));
    m_fileListRequested = 1;
    tryToConnect(true);
}

// ===========================================================================
// processSharedFileList
// ===========================================================================

void UpDownClient::processSharedFileList(const uint8* data, uint32 size, const QString& dir,
                                         bool directoryAnswer)
{
    if (m_fileListRequested <= 0) {
        logDebug(QStringLiteral("processSharedFileList: unrequested response from %1").arg(userName()));
        return;
    }

    // One answer, one count — flat list or directory, whatever its name
    // (MFC BaseClient.cpp:1795-1796).
    --m_fileListRequested;

    if (!data || size < 4 || !theApp.searchList)
        return;

    // Into the peer's own Search tab, not the current ED2K search (MFC BaseClient.cpp:1797)
    const uint32 fileCount = peekUInt32(data);
    uint32 searchID = 0;
    try {
        searchID = theApp.searchList->processClientSharedFiles(*this, data, size, dir);
    } catch (const std::exception& ex) {
        logWarning(QStringLiteral("Malformed shared file list from %1: %2")
                       .arg(userName(), QString::fromUtf8(ex.what())));
        searchID = m_searchID; // tab may exist already; show what did parse
    }
    if (searchID == 0)
        return;

    if (fileCount == 0 && !directoryAnswer)
        logStatusInfo(QStringLiteral("User %1 shares no files").arg(statusName()));

    emit sharedFileListReceived(
        QByteArray(reinterpret_cast<const char*>(m_userHash.data()), 16),
        userName(), searchID);
}

// ===========================================================================
// checkFailedFileIdReqs
// ===========================================================================

// MFC BaseClient.cpp:2538-2555. The hash matters: asking for a file we deliberately
// unshared, or one we are still downloading, is a legitimate miss and must not count —
// ignoring it (as this did) accumulated strikes against honest clients.
void UpDownClient::checkFailedFileIdReqs(const uint8* fileHash)
{
    if (fileHash) {
        if (theApp.sharedFileList && theApp.sharedFileList->isUnsharedFile(fileHash))
            return;
        if (theApp.downloadQueue && theApp.downloadQueue->fileByID(fileHash))
            return;
    }

    // Threshold 6, matching the reference. File-request floods are never exempt, not
    // even for a client we are downloading from.
    if (m_failedFileIdReqs < 6)
        ++m_failedFileIdReqs;

    if (m_failedFileIdReqs == 6) {
        logDebug(QStringLiteral("FileReq flood detected from %1").arg(userName()));
        registerBadRequest(QStringLiteral("FileReq flood"));
    }
}

// ===========================================================================
// sendPublicIPRequest
// ===========================================================================

void UpDownClient::sendPublicIPRequest()
{
    if (!m_socket)
        return;

    auto packet = std::make_unique<Packet>(OP_PUBLICIP_REQ, 0);
    packet->prot = OP_EMULEPROT;
    sendPacket(std::move(packet));
    m_needOurPublicIP = true;
}

// ===========================================================================
// processPublicIPAnswer
// ===========================================================================

void UpDownClient::processPublicIPAnswer(const uint8* data, uint32 size)
{
    if (size != 4)
        return;

    SafeMemFile file(data, size);
    const uint32 ip = file.readUInt32();

    // MFC: CUpDownClient::ProcessPublicIPAnswer() — BaseClient.cpp:3896. All three
    // guards matter: a peer is the least trustworthy public-IP source we have, and
    // this value feeds server UDP-key stamping, Kad key derivation and
    // EncryptedDatagramSocket. Without them any peer could dictate our public IP.
    if (!m_needOurPublicIP)
        return;  // unsolicited — we never asked this client

    m_needOurPublicIP = false;

    // theApp.publicIP() consults Kad first and the ED2K value second, so this one
    // check is what makes a peer answer the last resort of the three sources.
    if (theApp.publicIP() != 0 || isLowID(ip))
        return;

    theApp.setPublicIP(ip);  // expiry of stale server UDP keys happens in there
}

// ===========================================================================
// sendSharedDirectories
// ===========================================================================

void UpDownClient::sendSharedDirectories()
{
    if (!m_socket)
        return;

    // Pseudo names, never local paths (MFC BaseClient.cpp:2909-2931).
    std::vector<QString> dirs;
    if (theApp.sharedFileList) {
        dirs = theApp.sharedFileList->browseDirectoryNames();
        if (theApp.downloadQueue && theApp.downloadQueue->fileCount() > 0)
            dirs.push_back(QStringLiteral(OP_INCOMPLETE_SHARED_FILES));
        if (theApp.sharedFileList->hasSingleSharedFiles())
            dirs.push_back(QStringLiteral(OP_OTHER_SHARED_FILES));
    }

    SafeMemFile data;
    data.writeUInt32(static_cast<uint32>(dirs.size()));
    for (const auto& dir : dirs)
        data.writeString(dir, peerStringMode());

    auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_ASKSHAREDDIRSANS);
    sendPacket(std::move(packet));
}

// ===========================================================================
// safeConnectAndSendPacket
// ===========================================================================

bool UpDownClient::safeConnectAndSendPacket(std::unique_ptr<Packet> packet)
{
    if (!packet)
        return false;

    if (m_socket && m_socket->isConnected()) {
        // Connected but our hello is still unanswered: after the handshake.
        if (checkHandshakeFinished())
            sendPacket(std::move(packet));
        else
            m_waitingPackets.push_back(std::move(packet));
        return true;
    }

    // Queue packet for sending after connection
    m_waitingPackets.push_back(std::move(packet));
    return tryToConnect();
}

// ===========================================================================
// isObfuscatedConnectionEstablished
// ===========================================================================

bool UpDownClient::isObfuscatedConnectionEstablished() const
{
    if (m_socket && m_socket->isConnected())
        return m_socket->isObfuscating();
    return false;
}

// ===========================================================================
// isCryptLayerIncompatible
// ===========================================================================

bool UpDownClient::isCryptLayerIncompatible(bool requireHash) const
{
    // MFC's accessors chain the flags (UpdownClient.h:208-210, Preferences.h:1388-1391).
    // Ours are raw members and setConnectOptions() does not chain them, so do it here.
    const bool peerRequires =
        m_supportsCryptLayer && m_requestsCryptLayer && m_requiresCryptLayer;
    const bool weRequire = thePrefs.cryptLayerSupported() && thePrefs.cryptLayerRequested()
                        && thePrefs.cryptLayerRequired();
    const bool hashMissing = requireHash && !hasValidHash();

    if (peerRequires && (!thePrefs.cryptLayerSupported() || hashMissing))
        return true;
    if (weRequire && (!m_supportsCryptLayer || hashMissing))
        return true;
    return false;
}

// ===========================================================================
// shouldReceiveCryptUDPPackets
// ===========================================================================

bool UpDownClient::shouldReceiveCryptUDPPackets() const
{
    // MFC BaseClient.cpp:2582. The Kad version this used to demand has nothing to do with
    // ed2k UDP obfuscation: an ed2k-only peer that asks for it was sent plaintext, which
    // it then drops — losing exactly the queue-rank refreshes the reask exists for.
    if (!thePrefs.cryptLayerSupported() || !m_supportsCryptLayer || !hasValidHash())
        return false;

    // MFC tests GetPublicIP() != 0, which is IPv4-only. A peer we reach over IPv6 is
    // keyed the same way and only needs us to know a public v6 address.
    const bool knowOwnAddress = theApp.publicIP() != 0
        || (m_connectAddress.isIPv6() && !theApp.publicIPv6().isNull());
    if (!knowOwnAddress)
        return false;

    return thePrefs.cryptLayerRequested() || m_requestsCryptLayer;
}

// ===========================================================================
// getUnicodeSupport
// ===========================================================================

uint8 UpDownClient::getUnicodeSupport() const
{
    return m_unicodeSupport ? 1 : 0;
}

// ===========================================================================
// downloadStateDisplayString
// ===========================================================================

QString UpDownClient::downloadStateDisplayString() const
{
    return dbgGetDownloadState(m_downloadState);
}

// ===========================================================================
// uploadStateDisplayString
// ===========================================================================

// What the GUI maps to text (ClientStateText.h): the state, an upload slot split into
// Transferring / Standby by its position. "Stalled" is uploadStalled() on top of that.
QString UpDownClient::uploadStateToken() const
{
    switch (m_uploadState) {
    case UploadState::OnUploadQueue: return QStringLiteral("OnQueue");
    case UploadState::Banned:        return QStringLiteral("Banned");
    case UploadState::Connecting:    return QStringLiteral("Connecting");
    case UploadState::Uploading:
        return theApp.uploadQueue
                   && m_slotNumber <= static_cast<uint32>(theApp.uploadQueue->maxActiveClientsShortTime())
            ? QStringLiteral("Transferring") : QStringLiteral("Standby");
    default:
        return {};
    }
}

bool UpDownClient::uploadStalled() const
{
    return m_uploadState == UploadState::Uploading && payloadInBuffer() == 0;
}

QString UpDownClient::uploadStateDisplayString() const
{
    // MFC CUpDownClient::GetUploadStateDisplayString (srchybrid/BaseClient.cpp:2480-2510).
    // The literals are MFC's own English resource strings, so the Uploads list reads the way
    // the reference screenshots do: IDS_ONQUEUE (emule.rc:2037), IDS_TRANSFERRING (:2038),
    // IDS_TRICKLING (:2700 — spelled "Standby", not "Trickling") and IDS_US_STALLEDW4BR
    // (:3623). Distinct from dbgGetUploadState(), which stays the debug spelling.
    switch (m_uploadState) {
    case UploadState::OnUploadQueue:
        return QCoreApplication::translate("UpDownClient", "On Queue");
    case UploadState::Banned:
        return QCoreApplication::translate("UpDownClient", "Banned");
    case UploadState::Connecting:
        return QCoreApplication::translate("UpDownClient", "Connecting");
    case UploadState::Uploading:
        // Nothing left in the send buffers means the peer has stopped asking for blocks, not
        // that we stopped sending. MFC shows this only in advanced mode, because on a healthy
        // link it flickers between states between two block requests.
        if (thePrefs.showExtControls() && payloadInBuffer() == 0)
            return QCoreApplication::translate("UpDownClient",
                                               "Stalled! Waiting for block request.");
        if (theApp.uploadQueue
            && m_slotNumber <= static_cast<uint32>(theApp.uploadQueue->maxActiveClientsShortTime()))
        {
            return QCoreApplication::translate("UpDownClient", "Transferring");
        }
        return QCoreApplication::translate("UpDownClient", "Standby");
    default:
        // MFC returns an empty string for US_NONE, and the known-clients list relies on it —
        // an idle peer shows a blank Upload Status cell rather than the word "None".
        return {};
    }
}

// ===========================================================================
// Phase 3 — Secure Identity
// Ported from MFC BaseClient.cpp lines 1820-2027
// ===========================================================================

// ===========================================================================
// sendPublicKeyPacket
// ===========================================================================

void UpDownClient::sendPublicKeyPacket()
{
    if (!m_credits || !m_socket)
        return;

    if (!theApp.clientCredits || !theApp.clientCredits->cryptoAvailable())
        return;

    // Send OUR public key (not the remote client's stored key)
    const uint8 keyLen = theApp.clientCredits->pubKeyLen();
    if (keyLen == 0)
        return;

    auto packet = std::make_unique<Packet>(OP_PUBLICKEY, 1 + keyLen);
    packet->prot = OP_EMULEPROT;
    packet->pBuffer[0] = static_cast<char>(keyLen);
    std::memcpy(packet->pBuffer + 1, theApp.clientCredits->publicKey(), keyLen);
    sendPacket(std::move(packet));

    if (thePrefs.logSecureIdent())
        logDebug(QStringLiteral("sendPublicKeyPacket: keyLen=%1 to %2").arg(keyLen).arg(userName()));

    m_secureIdentState = SecureIdentState::SignatureNeeded;
}

// ===========================================================================
// sendSignaturePacket
// ===========================================================================

void UpDownClient::sendSignaturePacket()
{
    if (!m_credits || !m_socket)
        return;

    if (!theApp.clientCredits || !theApp.clientCredits->cryptoAvailable())
        return;

    // MFC: bail out if we don't have the remote's public key yet —
    // will be called again from processPublicKeyPacket when it arrives
    if (m_credits->secIDKeyLen() == 0) {
        logDebug(QStringLiteral("sendSignaturePacket: no remote public key yet for %1 — deferring").arg(userName()));
        return;
    }

    // Signature requires a valid challenge from the peer
    if (m_credits->cryptRndChallengeFrom == 0) {
        logDebug(QStringLiteral("sendSignaturePacket: no challenge available for %1").arg(userName()));
        return;
    }

    // Determine signature version and IP challenge
    // MFC: v2 if bit 0 is NOT set. We will use v1 as default, except if only v2 is supported.
    const bool useV2 = !(m_supportSecIdent & 1);
    uint32 challengeIP = 0;
    uint8 chaIPKind = 0;  // V1 default: no IP binding (MFC: byChaIPKind = 0)

    if (useV2) {
        // MFC: when clientID is 0 or low-ID, use remote client's IP.
        // Also handles serverConnect == null (Kad-only mode).
        if (theApp.serverConnect && !theApp.serverConnect->isLowID()
            && theApp.serverConnect->clientID() != 0) {
            challengeIP = theApp.serverConnect->clientID();
            chaIPKind = kCryptCipLocalClient;
        } else {
            challengeIP = m_userAddress.toNetworkUint32();
            chaIPKind = kCryptCipRemoteClient;
        }
    }

    uint8 sig[200];
    uint8 sigLen = theApp.clientCredits->createSignature(
        m_credits, sig, sizeof(sig), challengeIP, chaIPKind);

    if (sigLen == 0) {
        logDebug(QStringLiteral("sendSignaturePacket: signature creation failed for %1").arg(userName()));
        return;
    }

    // Build OP_SIGNATURE packet: [sigLen:1][sigData:sigLen] + optional [ipKind:1] for v2
    // MFC: siglen + 1 + static_cast<int>(bUseV2)
    const uint32 packetSize = sigLen + 1 + static_cast<uint32>(useV2);
    auto packet = std::make_unique<Packet>(OP_SIGNATURE, packetSize, OP_EMULEPROT);
    packet->pBuffer[0] = static_cast<char>(sigLen);
    std::memcpy(packet->pBuffer + 1, sig, sigLen);

    if (useV2)
        packet->pBuffer[1 + sigLen] = static_cast<char>(chaIPKind);

    sendPacket(std::move(packet));
    m_secureIdentState = SecureIdentState::AllRequestsSend;
}

// ===========================================================================
// processPublicKeyPacket
// ===========================================================================

void UpDownClient::processPublicKeyPacket(const uint8* data, uint32 size)
{
    // Track before validating: MFC records the client on entry, so a peer cannot dodge
    // the per-address accounting by sending a malformed key.
    if (theApp.clientList)
        theApp.clientList->addTrackClient(this);

    // MFC: strict validation — keyLen must fill entire packet, size 10-250
    if (!m_socket || !m_credits || !data || data[0] != size - 1 || size < 10 || size > 250)
        return;

    if (!theApp.clientCredits || !theApp.clientCredits->cryptoAvailable())
        return;

    if (m_credits->setSecureIdent(data + 1, data[0])) {
        if (thePrefs.logSecureIdent())
            logDebug(QStringLiteral("processPublicKeyPacket: stored %1-byte key from %2, state=%3")
                         .arg(data[0]).arg(userName()).arg(static_cast<int>(m_secureIdentState)));
        // MFC: If we were waiting to send our signature (deferred because we
        // didn't have the remote's public key yet), send it now.
        if (m_secureIdentState == SecureIdentState::SignatureNeeded)
            sendSignaturePacket();
        else if (m_secureIdentState == SecureIdentState::KeyAndSigNeeded)
            logDebug(QStringLiteral("processPublicKeyPacket: invalid state IS_KEYANDSIGNEEDED"));
    } else {
        logDebug(QStringLiteral("processPublicKeyPacket: setSecureIdent failed for %1 (keyLen=%2)")
                     .arg(userName()).arg(data[0]));
    }
}

// ===========================================================================
// processSignaturePacket
// ===========================================================================

void UpDownClient::processSignaturePacket(const uint8* data, uint32 size)
{
    // MFC: strict size validation
    if (!m_socket || !m_credits || !data || size > 250 || size < 10)
        return;

    // MFC: exact V1/V2 detection based on packet structure
    uint8 chaIPKind;
    if (data[0] == size - 1) {
        // V1: sigLen fills the rest of the packet
        chaIPKind = 0;
    } else if (data[0] == size - 2 && (m_supportSecIdent & 2) > 0) {
        // V2: sigLen + 1 byte for IP kind at end
        chaIPKind = data[size - 1];
    } else {
        return;
    }

    if (!theApp.clientCredits || !theApp.clientCredits->cryptoAvailable())
        return;

    // Prevent duplicate signatures from the same IP (MFC uses GetIP())
    if (m_lastSignatureAddress == m_userAddress) {
        logDebug(QStringLiteral("processSignaturePacket: duplicate signature from %1").arg(userName()));
        return;
    }

    // Must have their public key
    if (m_credits->secIDKeyLen() == 0) {
        logDebug(QStringLiteral("processSignaturePacket: no public key stored for %1").arg(userName()));
        return;
    }

    // Must have a valid challenge
    if (m_credits->cryptRndChallengeFor == 0) {
        logDebug(QStringLiteral("processSignaturePacket: no challenge for %1").arg(userName()));
        return;
    }

    m_lastSignatureAddress = m_userAddress;

    bool verified = theApp.clientCredits->verifyIdent(m_credits, data + 1, data[0], m_userAddress, chaIPKind);
    if (verified && theApp.clientCensus)
        theApp.clientCensus->noteIdentified(m_userHash.data());
    if (thePrefs.logSecureIdent())
        logDebug(QStringLiteral("processSignaturePacket: sigLen=%1 chaIPKind=%2 verified=%3 for %4")
                     .arg(data[0]).arg(chaIPKind).arg(verified).arg(userName()));

    // A friend attempt waiting on authentication has been waiting for exactly this.
    // MFC srchybrid/BaseClient.cpp:1962-1972.
    if (Friend* f = friendPtr(); f && f->isTryingToConnect()) {
        f->updateFriendConnectionState(verified ? FriendConnectReport::UserHashVerified
                                                : FriendConnectReport::SecureIdentFailed);
    }
}

// ===========================================================================
// sendSecIdentStatePacket
// ===========================================================================

void UpDownClient::sendSecIdentStatePacket()
{
    // MFC: SendSecIdentStatePacket() — BaseClient.cpp:1977-2006
    if (!m_socket || !extProtocolAvailable())
        return;
    if (!theApp.clientCredits || !theApp.clientCredits->cryptoAvailable())
        return;
    if (!m_credits)
        return;

    // MFC state logic: need key → KeyAndSigNeeded, need sig → SignatureNeeded,
    // already verified from this IP → skip entirely (don't send Unavailable).
    uint8 state;
    if (m_credits->secIDKeyLen() == 0) {
        state = static_cast<uint8>(SecureIdentState::KeyAndSigNeeded);
    } else if (m_lastSignatureAddress != m_userAddress) {
        state = static_cast<uint8>(SecureIdentState::SignatureNeeded);
    } else {
        // Already verified from this IP — MFC returns without sending.
        return;
    }

    SafeMemFile data;
    data.writeUInt8(state);

    // MFC always regenerates the challenge: dwRandom = rand() + 1
    // We must do the same to avoid stale challenges from previous connections.
    m_credits->cryptRndChallengeFor = getRandomUInt32() | 1; // ensure non-zero
    data.writeUInt32(m_credits->cryptRndChallengeFor);

    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_SECIDENTSTATE);
    sendPacket(std::move(packet));

    if (thePrefs.logSecureIdent())
        logDebug(QStringLiteral("sendSecIdentStatePacket: state=%1 challenge=%2 to %3")
                     .arg(state).arg(m_credits->cryptRndChallengeFor).arg(userName()));
}

// ===========================================================================
// processSecIdentStatePacket
// ===========================================================================

void UpDownClient::processSecIdentStatePacket(const uint8* data, uint32 size)
{
    // MFC: exact 5 bytes required
    if (size != 5)
        return;

    if (!m_credits)
        return;

    // Extract the 4-byte random challenge the remote wants us to sign
    m_credits->cryptRndChallengeFrom = peekUInt32(data + 1);

    if (thePrefs.logSecureIdent()) {
        logDebug(QStringLiteral("processSecIdentStatePacket: state=%1 size=%2 credits=%3 challenge=%4")
                     .arg(data[0]).arg(size)
                     .arg(QLatin1StringView(m_credits ? "yes" : "null"))
                     .arg(m_credits->cryptRndChallengeFrom));
    }

    // MFC state mapping: 0→Unavailable, 1→SignatureNeeded, 2→KeyAndSigNeeded
    switch (data[0]) {
    case 0:
        m_secureIdentState = SecureIdentState::Unavailable;
        break;
    case 1:
        m_secureIdentState = SecureIdentState::SignatureNeeded;
        sendSignaturePacket();  // may defer if remote key not yet available
        break;
    case 2:
        m_secureIdentState = SecureIdentState::KeyAndSigNeeded;
        sendPublicKeyPacket();
        sendSignaturePacket();  // may defer if remote key not yet available
        break;
    default:
        break;
    }
}

// ===========================================================================
// hasPassedSecureIdent
// ===========================================================================

bool UpDownClient::hasPassedSecureIdent(bool passIfUnavailable) const
{
    if (!m_credits) {
        return passIfUnavailable;
    }

    const IdentState state = m_credits->currentIdentState(m_connectAddress);
    if (state == IdentState::Identified)
        return true;

    if (passIfUnavailable && state == IdentState::NotAvailable)
        return true;

    return false;
}

// ===========================================================================
// Phase 3 — Chat & Captcha
// Ported from MFC BaseClient.cpp lines 2625-2811
// ===========================================================================

// ===========================================================================
// processChatMessage
// ===========================================================================

void UpDownClient::processChatMessage(SafeMemFile& data, uint32 length)
{
    // MFC CUpDownClient::ProcessChatMessage (srchybrid/BaseClient.cpp:2625-2763)
    const bool isFriend = m_friend != nullptr;

    // msgSecure is stricter than MFC's option of that name, which only asks for a
    // user name (BaseClient.cpp:2628) — deliberate, see the default in Preferences.
    if ((thePrefs.msgOnlyFriends() && !isFriend)
        || (thePrefs.msgSecure() && !hasPassedSecureIdent(false))) {
        if (!m_messageFiltered)
            logDebug(QStringLiteral("Filtered message from '%1'").arg(statusName()));
        m_messageFiltered = true;
        return;
    }

    QByteArray raw(static_cast<qsizetype>(length), Qt::Uninitialized);
    data.read(raw.data(), length);
    QString message = QString::fromUtf8(raw);
    // The length cap may have cut a character in two
    while (message.endsWith(QChar::ReplacementCharacter))
        message.chop(1);
    if (message.isEmpty())
        return;

    // Word filter
    const bool advSpamFilter = thePrefs.enableSpamFilter();
    const QStringList keywords = thePrefs.messageFilter().split(QLatin1Char('|'));
    for (const auto& keyword : keywords) {
        const QString trimmed = keyword.trimmed();
        if (trimmed.isEmpty() || !message.contains(trimmed, Qt::CaseInsensitive))
            continue;
        if (advSpamFilter && !isFriend && m_messagesSent == 0) {
            setSpammer(true);
            endChatSession();
        }
        logDebug(QStringLiteral("Message from '%1' matched filter '%2'")
                     .arg(statusName(), trimmed));
        return;
    }

    // Captcha: outranks the checks below — whoever solves it is no spam bot
    if (advSpamFilter && thePrefs.useChatCaptchas() && !isFriend
        && m_messagesSent == 0 && m_messagesReceived == 0
        && m_chatCaptchaState != ChatCaptchaState::CaptchaSolved)
    {
        if (m_chatCaptchaState != ChatCaptchaState::ChallengeSent) {
            sendCaptchaChallenge(message);
            return;
        }

        // The message after a challenge is the answer; anything before it is ignored
        const QString answer = message.trimmed().right(m_captchaChallenge.length());
        const bool solved = !m_captchaChallenge.isEmpty()
                         && answer.compare(m_captchaChallenge, Qt::CaseInsensitive) == 0;
        m_captchaChallenge.clear();

        auto result = std::make_unique<Packet>(OP_CHATCAPTCHARES, 1, OP_EMULEPROT, false);
        if (!solved) {
            m_chatCaptchaState = ChatCaptchaState::None;
            m_captchaPendingMsg.clear();
            result->pBuffer[0] = m_captchasSent < 3 ? 1 : 2;   // 2: no tries left
        } else {
            // Not persistent; from here on the message counter keeps the peer through
            m_chatCaptchaState = ChatCaptchaState::CaptchaSolved;
            message = std::exchange(m_captchaPendingMsg, QString());
            m_captchasSent = 0;
            result->pBuffer[0] = 0;
        }
        if (!safeConnectAndSendPacket(std::move(result)) || !solved)
            return;
    }

    if (advSpamFilter && !isFriend) {
        bool isSpam = m_isSpammer;
        if (!isSpam && m_messagesSent == 0) {
            // A link in a message we never answered, or four of them unanswered
            static const QStringList kUrlIndicators = QStringLiteral(
                "http:|www.|.de |.net |.com |.org |.to |.tk |.cc |.fr |ftp:|ed2k:|https:|ftp."
                "|.info|.biz|.uk|.eu|.es|.tv|.cn|.tw|.ws|.nu|.jp").split(QLatin1Char('|'));
            isSpam = m_messagesReceived > 3
                  || std::ranges::any_of(kUrlIndicators, [&message](const QString& token) {
                         return message.contains(token);
                     });
        }
        if (isSpam) {
            logDebug(QStringLiteral("'%1' has been marked as spammer").arg(statusName()));
            setSpammer(true);
            endChatSession();
            return;
        }
    }

    // Counted only when shown (MFC CChatSelector::ProcessMessage)
    incMessagesReceived();
    emit chatMessageReceived(m_username, message);
}

// ===========================================================================
// sendChatMessage
// ===========================================================================

void UpDownClient::sendPendingChatMessage()
{
    if (m_pendingChatMessage.isEmpty() || !m_socket || !m_socket->isConnected())
        return;
    sendChatMessage(takePendingChatMessage());
}

void UpDownClient::setSpammer(bool v)
{
    if (v)
        ban(QStringLiteral("Identified as Spammer"));
    else if (m_isSpammer && isBanned())
        unBan();
    m_isSpammer = v;
}

void UpDownClient::endChatSession()
{
    m_chatState = ChatState::None;
    m_chatCaptchaState = ChatCaptchaState::None;
    (void)takePendingChatMessage();
    emit chatStateChanged();
}

void UpDownClient::sendChatMessage(const QString& message)
{
    if (!m_socket || message.isEmpty())
        return;

    // MFC CChatSelector::SendText (srchybrid/ChatSelector.cpp:285-296)
    incMessagesSent();
    setSpammer(false);
    if (m_chatCaptchaState == ChatCaptchaState::CaptchaRecv)
        m_chatCaptchaState = ChatCaptchaState::SolutionSent;   // this message is the answer
    else if (m_chatCaptchaState != ChatCaptchaState::SolutionSent)
        m_chatCaptchaState = ChatCaptchaState::Accepting;      // a challenge may come back

    sendChatPacket(message);
}

// ===========================================================================
// processCaptchaRequest
// ===========================================================================

void UpDownClient::processCaptchaRequest(SafeMemFile& data)
{
    // Only as the reply to a message we just sent (MFC srchybrid/BaseClient.cpp:2765-2797);
    // otherwise any peer could push images at us.
    if (m_chatCaptchaState != ChatCaptchaState::Accepting) {
        logDebug(QStringLiteral("Unrequested captcha from %1 ignored").arg(statusName()));
        return;
    }

    try {
        // Tags, for future use
        for (uint32 i = data.readUInt8(); i > 0; --i)
            Tag tag(data, true);
    } catch (const FileException&) {
        logDebug(QStringLiteral("Captcha from %1: bad tags").arg(statusName()));
        return;
    }

    // A small captcha, not a wallpaper
    const qint64 imgSize = data.length() - data.position();
    if (imgSize <= 128 || imgSize >= 4096) {
        logDebug(QStringLiteral("Captcha from %1: bad image size %2").arg(statusName()).arg(imgSize));
        return;
    }
    QByteArray imgData(static_cast<qsizetype>(imgSize), Qt::Uninitialized);
    data.read(imgData.data(), static_cast<uint32>(imgSize));

    QImage captchaImg;
    if (!captchaImg.loadFromData(imgData, "BMP")
        || captchaImg.height() <= 10 || captchaImg.height() >= 50
        || captchaImg.width() <= 10 || captchaImg.width() >= 150) {
        logDebug(QStringLiteral("Captcha from %1: not a usable image").arg(statusName()));
        return;
    }

    m_chatCaptchaState = ChatCaptchaState::CaptchaRecv;
    emit captchaRequestReceived(m_username, captchaImg);
}

// ===========================================================================
// processCaptchaReqRes
// ===========================================================================

void UpDownClient::processCaptchaReqRes(uint8 status)
{
    // Meaningful only while our answer is out (MFC srchybrid/BaseClient.cpp:2799-2811)
    const bool expected = m_chatCaptchaState == ChatCaptchaState::SolutionSent;
    m_chatCaptchaState = ChatCaptchaState::None;
    if (expected)
        emit captchaResultReceived(m_username, status == 0);
    else
        logDebug(QStringLiteral("Unrequested captcha result from %1 ignored").arg(statusName()));
}

// ===========================================================================
// Phase 3 — Preview
// Ported from MFC BaseClient.cpp lines 2059-2167
// ===========================================================================

// ===========================================================================
// sendPreviewRequest
// ===========================================================================

bool UpDownClient::sendPreviewRequest(const AbstractFile& file)
{
    // MFC BaseClient.cpp:2059-2071. Whether the peer can answer was settled when the
    // result was listed; a client made for this request has seen no hello yet.
    if (m_previewReqPending) {
        logStatusWarning(QStringLiteral("A preview from %1 is already being fetched").arg(userName()));
        return false;
    }
    m_previewReqPending = true;
    std::memcpy(m_previewReqHash.data(), file.fileHash(), 16);

    SafeMemFile data;
    data.writeHash16(file.fileHash());

    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_REQUESTPREVIEW);
    if (!safeConnectAndSendPacket(std::move(packet)) && m_previewReqPending) {
        // No way to the peer. disconnected() may have reported it already.
        m_previewReqPending = false;
        emit previewAnswerReceived(m_previewReqHash, {});
        return false;
    }
    return true;
}

namespace {

/// One PNG frame of at most 200 px for the image at @p path, empty when it cannot be
/// read or is too large to decode. Runs on a pool thread.
QByteArray renderPreviewFrame(const QString& path)
{
    static constexpr qint64 kMaxSourcePixels = 64ll * 1000 * 1000;

    QImageReader reader(path);
    const QSize dims = reader.size();
    if (!dims.isValid() || dims.isEmpty()
        || static_cast<qint64>(dims.width()) * dims.height() > kMaxSourcePixels)
        return {};
    if (dims.width() > 200 || dims.height() > 200)
        reader.setScaledSize(dims.scaled(200, 200, Qt::KeepAspectRatio));
    const QImage image = reader.read();
    if (image.isNull())
        return {};

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}

} // namespace

// ===========================================================================
// sendPreviewAnswer
// ===========================================================================

void UpDownClient::sendPreviewAnswer(const uint8* fileHash, const std::vector<QByteArray>& pngFrames)
{
    m_previewAnsPending = false;

    // MFC BaseClient.cpp:2073-2104: the hash of the file asked for, zero when unknown.
    // The requester finds its search entry by it.
    static constexpr uint8 kZeroHash[16] = {};
    SafeMemFile data;
    data.writeHash16(fileHash ? fileHash : kZeroHash);
    data.writeUInt8(static_cast<uint8>(pngFrames.size()));
    for (const QByteArray& frame : pngFrames) {
        data.writeUInt32(static_cast<uint32>(frame.size()));
        data.write(frame.constData(), frame.size());
    }

    // The frames take a while: reconnect if the peer left meanwhile
    // (MFC BaseClient.cpp:2109).
    safeConnectAndSendPacket(std::make_unique<Packet>(data, OP_EMULEPROT, OP_PREVIEWANSWER));
}

// ===========================================================================
// processPreviewReq
// ===========================================================================

void UpDownClient::processPreviewReq(const uint8* data, uint32 size)
{
    // MFC BaseClient.cpp:2117-2118: one answer at a time, and only for those who may
    // see our files at all. Checked first: who may not ask gets no reaction.
    const int access = thePrefs.viewSharedFilesAccess();
    if (access == 0 || (access == 1 && m_friend == nullptr))
        return;
    if (!data || size < 16)
        throw FileException("OP_REQUESTPREVIEW: wrong packet size");   // MFC :2114
    if (m_previewAnsPending)
        return;
    m_previewAnsPending = true;

    KnownFile* file = theApp.sharedFileList ? theApp.sharedFileList->getFileByID(data) : nullptr;
    if (!file) {
        sendPreviewAnswer(nullptr, {});
        return;
    }

    std::array<uint8, 16> hash{};
    std::memcpy(hash.data(), file->fileHash(), 16);
    QString path = file->filePath();
    const ED2KFileType type = getED2KFileTypeID(file->fileName());
    if (file->isPartFile()) {
        // A download in progress: video only, and only once its head is there
        // (MFC PartFile.cpp:4991-5001).
        auto* partFile = static_cast<PartFile*>(file);
        const PartFileStatus status = partFile->status();
        const bool grabbable = type == ED2KFileType::Video
            && (status == PartFileStatus::Ready || status == PartFileStatus::Paused)
            && partFile->partCount() >= 2 && partFile->isComplete(0);
        path = grabbable ? partFile->dataFilePath() : QString();
    }
    if (path.isEmpty() || (type != ED2KFileType::Image && type != ED2KFileType::Video)) {
        sendPreviewAnswer(hash.data(), {});
        return;
    }

    if (type == ED2KFileType::Video) {
        // Four frames from 15 s on, 450 px wide, reduced colours (MFC BaseClient.cpp:2125).
        // A decoder per request; more than two at once get an empty answer.
        static int s_grabsRunning = 0;
        if (s_grabsRunning >= 2) {
            sendPreviewAnswer(hash.data(), {});
            return;
        }
        ++s_grabsRunning;
        auto* grabber = new FrameGrabThread(qApp);
        QPointer<UpDownClient> self(this);
        const auto finish = [self, hash, grabber](const std::vector<QImage>& images) {
            --s_grabsRunning;
            grabber->deleteLater();
            if (!self)
                return;
            std::vector<QByteArray> frames;
            for (const QImage& image : images) {
                QByteArray png;
                QBuffer buffer(&png);
                buffer.open(QIODevice::WriteOnly);
                if (image.save(&buffer, "PNG"))
                    frames.push_back(std::move(png));
            }
            self->sendPreviewAnswer(hash.data(), frames);
        };
        QObject::connect(grabber, &FrameGrabThread::finished, qApp,
                [finish](const FrameGrabResult& result) { finish(result.frames); });
        QObject::connect(grabber, &FrameGrabThread::error, qApp, [finish](const QString&) { finish({}); });
        FrameGrabRequest request;
        request.filePath = path;
        request.frameCount = 4;
        request.startTimeSec = 15.0;
        request.reduceColor = true;
        request.maxWidth = 450;
        grabber->requestGrab(std::move(request));
        return;
    }

    // Decoding and encoding stay off the main thread: a large image takes long enough
    // to stall every socket.
    QPointer<UpDownClient> self(this);
    QThreadPool::globalInstance()->start([self, hash, path] {
        std::vector<QByteArray> frames;
        if (QByteArray png = renderPreviewFrame(path); !png.isEmpty())
            frames.push_back(std::move(png));
        QMetaObject::invokeMethod(qApp, [self, hash, frames = std::move(frames)] {
            if (self)
                self->sendPreviewAnswer(hash.data(), frames);
        }, Qt::QueuedConnection);
    });
}

// ===========================================================================
// processPreviewAnswer
// ===========================================================================

void UpDownClient::processPreviewAnswer(const uint8* data, uint32 size)
{
    if (!m_previewReqPending)
        return;
    m_previewReqPending = false;

    if (!data || size < 17) // 16 hash + 1 count minimum
        return;

    SafeMemFile file(data, size);
    std::array<uint8, 16> fileHash{};
    file.readHash16(fileHash.data());

    // An answer for a file we did not ask about is no answer. A peer that does not
    // know the file sends a zero hash.
    const uint8 frameCount = file.readUInt8();
    const bool ours = fileHash == m_previewReqHash;
    if (frameCount == 0 || !ours) {
        logDebug(QStringLiteral("processPreviewAnswer: remote sent %1")
                     .arg(ours ? QStringLiteral("0 frames") : QStringLiteral("another file")));
        emit previewAnswerReceived(m_previewReqHash, {});
        return;
    }

    // Stock sends four frames of 450 px. More, or bigger, is not a preview.
    static constexpr uint8 kMaxFrames = 8;
    static constexpr int kMaxFrameEdge = 2048;

    std::vector<QImage> previewImages;
    for (uint8 i = 0; i < std::min(frameCount, kMaxFrames); ++i) {
        const uint32 imgSize = file.readUInt32();
        if (imgSize == 0 || imgSize > size) {
            logDebug(QStringLiteral("processPreviewAnswer: frame %1 size exceeds packet").arg(i));
            break;
        }

        QByteArray imgData(static_cast<qsizetype>(imgSize), Qt::Uninitialized);
        file.read(imgData.data(), imgSize);

        // The header says how much memory a decode takes; ask before decoding.
        QBuffer buffer(&imgData);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "png");
        const QSize dims = reader.size();
        if (!dims.isValid() || dims.width() > kMaxFrameEdge || dims.height() > kMaxFrameEdge) {
            logDebug(QStringLiteral("processPreviewAnswer: frame %1 refused (%2x%3)")
                         .arg(i).arg(dims.width()).arg(dims.height()));
            continue;
        }
        if (QImage image = reader.read(); !image.isNull())
            previewImages.push_back(std::move(image));
        else
            logDebug(QStringLiteral("processPreviewAnswer: failed to decode frame %1").arg(i));
    }

    emit previewAnswerReceived(fileHash, previewImages);
}

// ===========================================================================
// Phase 3 — Firewall
// Ported from MFC BaseClient.cpp lines 2837-2907
// ===========================================================================

// ===========================================================================
// sendFirewallCheckUDPRequest
// ===========================================================================

void UpDownClient::sendFirewallCheckUDPRequest()
{
    // MFC BaseClient.cpp:2837-2858
    if (m_kadState != KadState::FwCheckUDP) {
        logDebug(QStringLiteral("sendFirewallCheckUDPRequest: wrong state %1, expected FwCheckUDP")
                     .arg(static_cast<int>(m_kadState)));
        return;
    }

    if (!kad::Kademlia::instance() || !kad::Kademlia::instance()->isRunning()) {
        logDebug(QStringLiteral("sendFirewallCheckUDPRequest: Kad not running, aborting"));
        setKadState(KadState::None);
        return;
    }

    // Cancel if the client has other active connections or insufficient Kad version
    if (m_uploadState != UploadState::None || m_downloadState != DownloadState::None
        || m_chatState != ChatState::None
        || m_kadVersion <= KADEMLIA_VERSION5_48a || kadPort() == 0)
    {
        logDebug(QStringLiteral("sendFirewallCheckUDPRequest: cancelled — upState=%1 downState=%2 "
                                "chatState=%3 kadVer=%4 kadPort=%5")
                     .arg(static_cast<int>(m_uploadState))
                     .arg(static_cast<int>(m_downloadState))
                     .arg(static_cast<int>(m_chatState))
                     .arg(m_kadVersion).arg(kadPort()));
        kad::UDPFirewallTester::setUDPFWCheckResult(false, true, m_connectAddress.toUint32(), 0);
        setKadState(KadState::None);
        return;
    }

    auto* kadPrefs = kad::Kademlia::getInstancePrefs();
    if (!kadPrefs) {
        logDebug(QStringLiteral("sendFirewallCheckUDPRequest: no KadPrefs, aborting"));
        setKadState(KadState::None);
        return;
    }

    const uint16 internPort = kadPrefs->internKadPort();
    const uint16 externPort = kadPrefs->externalKadPort();
    //const uint32 verifyKey = kad::KadPrefs::getUDPVerifyKey(m_connectAddress.toNetworkUint32()); // NBO
    const uint32 verifyKey = kad::KadPrefs::getUDPVerifyKey(m_connectAddress.toUint32()); // HBO, m_connectAddress is stored in NBO but getUDPVerifyKey() expects HBO

    SafeMemFile data;
    data.writeUInt16(internPort);
    data.writeUInt16(externPort);
    data.writeUInt32(verifyKey);

    logDebug(QStringLiteral("sendFirewallCheckUDPRequest: sending OP_FWCHECKUDPREQ "
                            "internPort=%1 externPort=%2 verifyKey=0x%3 to %4")
                 .arg(internPort).arg(externPort)
                 .arg(verifyKey, 8, 16, QLatin1Char('0'))
                 .arg(userName()));

    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_FWCHECKUDPREQ);
    safeConnectAndSendPacket(std::move(packet));
}

// ===========================================================================
// processFirewallCheckUDPRequest
// ===========================================================================

void UpDownClient::processFirewallCheckUDPRequest(SafeMemFile& data)
{
    // MFC BaseClient.cpp:2860-2899
    auto* kadInstance = kad::Kademlia::instance();
    if (!kadInstance || !kadInstance->isRunning() || !kadInstance->getUDPListener()) {
        logDebug(QStringLiteral("processFirewallCheckUDPRequest: Kad not running, ignoring"));
        return;
    }

    // If we already know this client, the result might be biased
    const bool errorAlreadyKnown =
        m_uploadState != UploadState::None
        || m_downloadState != DownloadState::None
        || m_chatState != ChatState::None
        || (kad::Kademlia::getInstanceRoutingZone()
            && kad::Kademlia::getInstanceRoutingZone()->getContact(m_connectAddress.toUint32(), 0, false) != nullptr);

    const uint16 remoteInternPort = data.readUInt16();
    const uint16 remoteExternPort = data.readUInt16();
    const uint32 senderKey = data.readUInt32();

    if (remoteInternPort == 0) {
        logDebug(QStringLiteral("processFirewallCheckUDPRequest: intern port == 0"));
        return;
    }

    if (senderKey == 0)
        logDebug(QStringLiteral("processFirewallCheckUDPRequest: sender key == 0"));

    auto* udpListener = kadInstance->getUDPListener();

    // Send test packet to internal port
    SafeMemFile testPacket1;
    testPacket1.writeUInt8(static_cast<uint8>(errorAlreadyKnown ? 1 : 0));
    testPacket1.writeUInt16(remoteInternPort);
    udpListener->sendPacket(testPacket1, KADEMLIA2_FIREWALLUDP, m_connectAddress.toUint32(),
                            remoteInternPort,
                            kad::KadUDPKey(senderKey, theApp.publicIP()), nullptr);

    // If external port differs, test that too (PAT router scenario)
    if (remoteExternPort != 0 && remoteExternPort != remoteInternPort) {
        SafeMemFile testPacket2;
        testPacket2.writeUInt8(static_cast<uint8>(errorAlreadyKnown ? 1 : 0));
        testPacket2.writeUInt16(remoteExternPort);
        udpListener->sendPacket(testPacket2, KADEMLIA2_FIREWALLUDP, m_connectAddress.toUint32(),
                                remoteExternPort,
                                kad::KadUDPKey(senderKey, theApp.publicIP()), nullptr);
    }

    logDebug(QStringLiteral("Answered UDP firewall check request from %1").arg(dbgGetClientInfo()));
}

// ===========================================================================
// processKadFwTcpCheckAck — MFC ListenSocket.cpp:1672-1681
// ===========================================================================

// ===========================================================================
// processHttpCachePacket — hand OP_HTTPCACHE to the manager
// ===========================================================================

void UpDownClient::processHttpCachePacket(const uint8* data, uint32 size)
{
    // Deliberately no policy here. Whether we act on an offer depends on
    // preferences, the download queue, the IP filter and how many fetches are
    // already running — all of which the manager owns and none of which belongs
    // on a per-peer object.
    if (theApp.httpCache)
        theApp.httpCache->handlePacket(this, data, size);
}

void UpDownClient::processKadFwTcpCheckAck()
{
    // Only count the ACK from an IP we actually asked to firewall-check us — otherwise a
    // spoofed ACK could falsely clear our firewalled state. The firewall-check connection
    // carries no HELLO, so userAddress() may still be unset; use the socket peer IP, which
    // is MFC's client->GetIP(). MFC: CListenSocket OP_KAD_FWTCPCHECK_ACK ->
    // IsKadFirewallCheckIP (srchybrid/ListenSocket.cpp:1676).
    uint32 peerIpNet = 0;
    if (m_socket) {
        const auto addr = m_socket->peerAddress();
        if (!addr.isNull())
            peerIpNet = Address::fromQHostAddress(addr).toNetworkUint32();
    }
    if (theApp.clientList && !theApp.clientList->takeKadFirewallAck(peerIpNet)) {
        logWarning(QStringLiteral("Unrequested OP_KAD_FWTCPCHECK_ACK from %1").arg(dbgGetClientInfo()));
        return;
    }
    if (auto* prefs = kad::Kademlia::getInstancePrefs())
        prefs->incFirewalled();
}

// ===========================================================================
// processCallbackPacket — MFC ListenSocket.cpp OP_CALLBACK (lines 1365-1400)
//
// Received from our Kad buddy via TCP. The buddy forwards a callback
// request from a firewalled client that wants to download from us.
// Packet: <check 16><fileid 16><ip 4><tcp 2>
// ===========================================================================

void UpDownClient::processCallbackPacket(const uint8* data, uint32 size)
{
    // Minimum: 16 (check) + 16 (fileid) + 4 (IP) + 2 (port) = 38 bytes
    if (!data || size < 38)
        return;

    auto* kadInst = kad::Kademlia::instance();
    if (!kadInst || !kadInst->isRunning())
        return;

    SafeMemFile file(data, size);

    // Read the check hash and verify it matches our Kad ID XOR'd with 0xFF..FF
    kad::UInt128 check = kad::io::readUInt128(file);
    kad::UInt128 flipped(true); // all bits set (0xFF...)
    check.xorWith(flipped);

    auto* kadPrefs = kad::Kademlia::getInstancePrefs();
    if (!kadPrefs || check != kadPrefs->kadId())
        return;

    // Only our buddy relays callbacks. The check value is our Kad ID inverted and the
    // file is one we share — both public — so without this anyone could make us dial out.
    if (!theApp.clientList || theApp.clientList->getBuddy() != this
        || theApp.clientList->buddyStatus() != BuddyStatus::Connected)
    {
        logDebug(QStringLiteral("OP_CALLBACK from %1, which is not our buddy").arg(dbgGetClientInfo()));
        return;
    }

    // Read the file ID the requester wants
    kad::UInt128 fileId = kad::io::readUInt128(file);
    uint8 fileidMD4[16];
    fileId.toByteArray(fileidMD4);

    // A file we share or download, else a failed request (MFC ListenSocket.cpp:1382-1386)
    const bool shared = theApp.sharedFileList && theApp.sharedFileList->getFileByID(fileidMD4);
    if (!shared && !(theApp.downloadQueue && theApp.downloadQueue->fileByID(fileidMD4))) {
        checkFailedFileIdReqs(fileidMD4);
        return;
    }

    // Read the requester's IP and TCP port
    const uint32 ip = file.readUInt32();
    const uint16 tcp = file.readUInt16();

    // The target comes from the packet: vet it like any other peer address, never
    // ourselves, and at most one dial per address per rate window.
    const Address target = vetPeerAddress(Address::fromHostOrder(ip), theApp.ipFilter,
                                          theApp.clientList);
    if (target.isNull() || tcp == 0
        || (theApp.isOwnTcpPort(tcp) && target == Address::fromNetworkOrder(theApp.publicIP()))
        || !theApp.clientList->allowCallbackRequest(target))
    {
        logDebug(QStringLiteral("OP_CALLBACK: refused target %1:%2")
                     .arg(Address::fromHostOrder(ip).toString()).arg(tcp));
        return;
    }
    theApp.clientList->addTrackCallbackRequests(target);

    // Find or create a client for the requester
    UpDownClient* callback = theApp.clientList->findByConnIP(ntohl(ip), tcp);
    if (!callback) {
        // The address goes in the userId slot (MFC ListenSocket.cpp:1392): with 0 there
        // the client reads as LowID and is never dialled.
        callback = new UpDownClient(tcp, ip, 0, 0, nullptr);
        callback->setConnectAddress(target);
        theApp.clientList->addClient(callback);
    }

    callback->tryToConnect(true);
}

// ===========================================================================
// processReaskCallbackTCP — MFC ListenSocket.cpp OP_REASKCALLBACKTCP (1433-1519)
//
// Received from our Kad buddy via TCP. The buddy forwards a UDP file
// reask from a firewalled client. We respond with OP_REASKACK,
// OP_QUEUEFULL, or OP_FILENOTFOUND back via UDP to the requester.
// Packet: <ip 4><port 2><filehash 16>[...]
//     or: <0xFFFFFFFF 4><ipv6 16><port 2><filehash 16>[...]
//
// The 0xFFFFFFFF form is the compatibility target's IPv6 relay (its ClientUDPSocket writer
// and ListenSocket reader). 255.255.255.255 is never a real requester, so the sentinel is
// unambiguous. Without this branch a v6 relay was read as destIP=255.255.255.255 with the
// first 16 address bytes consumed as the file hash — silent corruption, not a miss.
// ===========================================================================

void UpDownClient::processReaskCallbackTCP(const uint8* data, uint32 size)
{
    if (!data || size < 22) // 4 (IP) + 2 (port) + 16 (hash) minimum
        return;

    // Verify the sender is our buddy
    UpDownClient* buddy = nullptr;
    if (theApp.clientList)
        buddy = theApp.clientList->getBuddy();

    if (buddy != this) {
        logDebug(QStringLiteral("processReaskCallbackTCP: received from non-buddy %1").arg(userName()));
        return;
    }

    SafeMemFile dataIn(data, size);

    // The IP field is an ED2K wire address: network byte order. Wrap it as an Endpoint
    // once here — sendPacket's uint32 overload takes HOST order, so passing destIP to it
    // directly reversed the octets and sent every reply to a bogus address.
    const uint32 destIP = dataIn.readUInt32();

    Address destAddr;
    if (destIP == IPV6_SOURCE_SENTINEL) {
        // IPv6 form: 16 raw address bytes follow, then the port. Needs 4+16+2+16 bytes.
        if (size < 38)
            return;
        uint8 destIPv6[16];
        dataIn.readHash16(destIPv6);
        destAddr = Address::fromIPv6Bytes(destIPv6);
    } else {
        destAddr = Address::fromNetworkOrder(destIP);
    }

    const uint16 destPort = dataIn.readUInt16();
    const Endpoint destEP(destAddr, destPort);

    uint8 reqFileHash[16];
    dataIn.readHash16(reqFileHash);

    // Identical to a direct OP_REASKFILEPING from here on, only relayed by our buddy —
    // MFC duplicates the whole block (ListenSocket.cpp:1453-1510 vs
    // ClientUDPSocket.cpp:226-308); we answer both through one path. dataIn is left
    // positioned after the hash, which is where the extended info starts.
    if (theApp.uploadQueue)
        theApp.uploadQueue->answerReask(destEP, dataIn, reqFileHash);
}

// ===========================================================================
// processBuddyPing — MFC ListenSocket.cpp OP_BUDDYPING (1401-1418)
//
// Our Kad buddy pings us to keep the connection alive. We verify the
// sender is our buddy, check rate limiting, and reply with OP_BUDDYPONG.
// ===========================================================================

void UpDownClient::processBuddyPing()
{
    UpDownClient* buddy = nullptr;
    if (theApp.clientList)
        buddy = theApp.clientList->getBuddy();

    // Verify: sender must be our buddy, with valid Kad version, not too frequent
    if (buddy != this || m_kadVersion == 0 || !allowIncomingBuddyPingPong())
        return;

    auto packet = std::make_unique<Packet>(OP_BUDDYPONG, 0, OP_EMULEPROT);
    sendPacket(std::move(packet));
    setLastBuddyPingPongTime();
}

// ===========================================================================
// processBuddyPong — MFC ListenSocket.cpp OP_BUDDYPONG (1419-1432)
//
// Our Kad buddy responds to our ping. Just update the timestamp to
// keep the socket timeout from firing.
// ===========================================================================

void UpDownClient::processBuddyPong()
{
    UpDownClient* buddy = nullptr;
    if (theApp.clientList)
        buddy = theApp.clientList->getBuddy();

    if (buddy != this || m_kadVersion == 0)
        return;

    setLastBuddyPingPongTime();
}

// ===========================================================================
// Buddy ping/pong scheduling — MFC srchybrid/UpdownClient.h:180-182
//
// m_lastBuddyPingPongTime holds the *next allowed* time, not the last one, which is what
// lets the two sides of the exchange use different windows off a single field: we may ping
// every 10 minutes, and accept an incoming ping only after 13. Both used to test
// "now - last > 10 min" against a stamp of when it happened, which made them the same
// check — so a buddy pinging on the same 10-minute schedule as us would race our own
// timer instead of being comfortably inside its allowance.
// ===========================================================================

bool UpDownClient::allowIncomingBuddyPingPong() const
{
    return getTickCount() >= m_lastBuddyPingPongTime + MIN2MS(3);
}

// ===========================================================================
// sendBuddyPingPong — MFC BaseClient.cpp SendBuddyPingPong
//
// Whether the 10-minute keep-alive is due. ClientList::processKadList() sends the actual
// OP_BUDDYPING.
// ===========================================================================

bool UpDownClient::sendBuddyPingPong() const
{
    return getTickCount() >= m_lastBuddyPingPongTime;
}

void UpDownClient::setLastBuddyPingPongTime()
{
    m_lastBuddyPingPongTime = getTickCount() + MIN2MS(10);
}

// ===========================================================================
// onExtPacketReceived — dispatch eMule extended protocol packets
// MFC ListenSocket.cpp ProcessExtPacket
// ===========================================================================

void UpDownClient::onExtPacketReceived(const uint8* data, uint32 size, uint8 opcode)
{
    switch (opcode) {
    case OP_EMULEINFO:
        processMuleInfoPacket(data, size);
        sendMuleInfoPacket(true); // answer=true → OP_EMULEINFOANSWER
        onInfoPacketsReceived();
        break;

    case OP_EMULEINFOANSWER:
        processMuleInfoPacket(data, size);
        onInfoPacketsReceived();
        break;

    case OP_COMPRESSEDPART:
        processBlockPacketWithValidation(data, size, true, false);
        break;

    case OP_COMPRESSEDPART_I64:
        processBlockPacketWithValidation(data, size, true, true);
        break;

    case OP_SENDINGPART_I64:
        processBlockPacketWithValidation(data, size, false, true);
        break;

    case OP_REQUESTPARTS_I64:
        processRequestParts(data, size, true);
        break;

    case OP_QUEUERANKING:
        processEmuleQueueRank(data, size);
        break;

    case OP_FILEDESC:
        processMuleCommentPacket(data, size);
        break;

    case OP_REQUESTSOURCES:
    case OP_REQUESTSOURCES2:
        if (thePrefs.wantsSourceExchangeLog())
            logDebug(QStringLiteral("SXRecv: Client source request; %1").arg(userName()));
        if (opcode == OP_REQUESTSOURCES)
            processRequestSources(data, size);
        else
            processRequestSources2(data, size);
        break;

    case OP_ANSWERSOURCES:
        processAnswerSources(data, size);
        break;

    case OP_ANSWERSOURCES2:
        processAnswerSources2(data, size);
        break;

    case OP_PUBLICKEY:
        processPublicKeyPacket(data, size);
        break;

    case OP_SIGNATURE:
        processSignaturePacket(data, size);
        break;

    case OP_SECIDENTSTATE:
        processSecIdentStatePacket(data, size);
        break;

    case OP_REQUESTPREVIEW:
        processPreviewReq(data, size);
        break;

    case OP_PREVIEWANSWER:
        processPreviewAnswer(data, size);
        break;

    case OP_PUBLICIP_REQ: {
        // Tell the peer the address we see it at (MFC srchybrid/ListenSocket.cpp:1336).
        // The answer is 4 bytes, so an IPv6 peer gets none.
        const Address& seen = m_connectAddress.isNull() ? m_userAddress : m_connectAddress;
        if (!seen.isIPv4())
            break;
        auto packet = std::make_unique<Packet>(OP_PUBLICIP_ANSWER, 4, OP_EMULEPROT);
        pokeUInt32(packet->pBuffer, seen.toNetworkUint32());
        sendPacket(std::move(packet));
        break;
    }

    case OP_PUBLICIP_ANSWER:
        processPublicIPAnswer(data, size);
        break;

    case OP_CHANGE_CLIENT_IP:
        // Accepted here too, defensively. The compatibility target sends it under
        // OP_EDONKEYPROT (see onPacketForClient), which is the path that actually fires.
        processChangeClientIP(data, size);
        break;

    case OP_AICHREQUEST:
        processAICHRequest(data, size);
        break;

    case OP_AICHANSWER:
        processAICHAnswer(data, size);
        break;

    case OP_AICHFILEHASHREQ:
        processAICHFileHashRequest(data, size);
        break;

    case OP_AICHFILEHASHANS: {
        // Normally bundled in a multipacket answer; MFC handles the standalone form too
        // (ListenSocket.cpp:1534), and it is the form we send to a non-multipacket peer.
        SafeMemFile io(data, size);
        processAICHFileHash(&io, nullptr, nullptr);
        break;
    }

    case OP_CALLBACK:
        processCallbackPacket(data, size);
        break;

    case OP_REASKCALLBACKTCP:
        processReaskCallbackTCP(data, size);
        break;

    case OP_BUDDYPING:
        processBuddyPing();
        break;

    case OP_BUDDYPONG:
        processBuddyPong();
        break;

    case OP_CHATCAPTCHAREQ: {
        SafeMemFile io(data, size);
        processCaptchaRequest(io);
        break;
    }

    case OP_CHATCAPTCHARES:
        if (size >= 1)
            processCaptchaReqRes(data[0]);
        break;

    case OP_FWCHECKUDPREQ: {
        SafeMemFile io(data, size);
        processFirewallCheckUDPRequest(io);
        break;
    }

    case OP_KAD_FWTCPCHECK_ACK:
        processKadFwTcpCheckAck();
        break;

    case OP_HTTPCACHE:
        processHttpCachePacket(data, size);
        break;

    case OP_MULTIPACKET:
        processMultiPacketLegacy(data, size, false);
        break;

    case OP_MULTIPACKET_EXT:
        processMultiPacketLegacy(data, size, true);
        break;

    case OP_MULTIPACKET_EXT2:
        processMultiPacketExt2(data, size);
        break;

    case OP_MULTIPACKETANSWER:
        processMultiPacketAnswerLegacy(data, size);
        break;

    case OP_MULTIPACKETANSWER_EXT2:
        processMultiPacketAnswer(data, size);
        break;

    case OP_HASHSETREQUEST2:
        sendHashsetPacket(data, size, true);
        break;

    case OP_HASHSETANSWER2:
        processHashSet(data, size, true);
        break;

    default:
        logDebug(QStringLiteral("onExtPacketReceived: unhandled opcode 0x%1 from %2").arg(opcode, 0, 16).arg(userName()));
        break;
    }
}

// ===========================================================================
// onPacketForClient — dispatch standard ED2K protocol packets
// MFC ListenSocket.cpp ProcessPacket
// ===========================================================================

void UpDownClient::onPacketForClient(const uint8* data, uint32 size, uint8 opcode, uint8 protocol)
{
    Q_UNUSED(protocol);

    switch (opcode) {
    case OP_SENDINGPART:
        processBlockPacketWithValidation(data, size, false, false);
        break;

    case OP_ACCEPTUPLOADREQ:
        processAcceptUpload();
        break;

    case OP_CANCELTRANSFER:
        // Remote client cancelled the transfer — remove from upload queue
        if (theApp.uploadQueue)
            theApp.uploadQueue->removeFromUploadQueue(this);
        break;

    case OP_OUTOFPARTREQS:
        // Only ends a running transfer (MFC ListenSocket.cpp:601); in any other state it
        // would let a peer rewrite our view of it.
        if (m_downloadState == DownloadState::Downloading) {
            noteDownloadSessionEndedByPeer();
            setDownloadState(DownloadState::OnQueue);
        }
        break;

    case OP_REQUESTPARTS:
        processRequestParts(data, size, false);
        break;

    case OP_QUEUERANK:
        processEdonkeyQueueRank(data, size);
        break;

    case OP_END_OF_DOWNLOAD:
        // Only for the file it is being served (MFC srchybrid/ListenSocket.cpp:553)
        if (size >= 16 && md4equ(m_reqUpFileId.data(), data)) {
            if (theApp.uploadQueue)
                theApp.uploadQueue->removeFromUploadQueue(this);
        } else if (size >= 16) {
            checkFailedFileIdReqs(data);
        }
        break;

    case OP_CHANGE_CLIENT_ID:
        processChangeClientID(data, size);
        break;

    case OP_CHANGE_CLIENT_IP:
        // The IPv6 counterpart of OP_CHANGE_CLIENT_ID, and it arrives on the same
        // protocol byte: the compatibility target builds it with Packet(opcode, size),
        // whose protocol argument defaults to OP_EDONKEYPROT. We used to accept it only
        // under OP_EMULEPROT and therefore never saw a single one.
        processChangeClientIP(data, size);
        break;

    case OP_CHANGE_SLOT:
        // Slot change notification — no action needed
        break;

    case OP_ASKSHAREDFILES:
        processAskSharedFiles();
        break;

    case OP_ASKSHAREDFILESANSWER:
        processSharedFileList(data, size);
        break;

    case OP_ASKSHAREDDIRS:
        processAskSharedDirs();
        break;

    case OP_ASKSHAREDFILESDIR:
        processAskSharedFilesDir(data, size);
        break;

    case OP_ASKSHAREDDIRSANS:
        processSharedDirsAnswer(data, size);
        break;

    case OP_ASKSHAREDFILESDIRANS:
        processSharedFilesDirAnswer(data, size);
        break;

    case OP_ASKSHAREDDENIEDANS:
        processSharedDenied();
        break;

    case OP_MESSAGE: {
        // MFC srchybrid/ListenSocket.cpp:643-664
        if (size < 2)
            throw FileException("OP_MESSAGE: invalid message packet");
        SafeMemFile io(data, size);
        const uint32 length = io.readUInt16();
        if (length + 2 != size)
            throw FileException("OP_MESSAGE: invalid message packet");
        processChatMessage(io, std::min<uint32>(length, MAX_CLIENT_MSG_LEN));
        break;
    }

    default:
        logDebug(QStringLiteral("onPacketForClient: unhandled opcode 0x%1 from %2").arg(opcode, 0, 16).arg(userName()));
        break;
    }
}

// ===========================================================================
// onHelloReceived — dispatch hello/helloAnswer packets
// ===========================================================================

void UpDownClient::onHelloReceived(const uint8* data, uint32 size, uint8 opcode)
{
    SafeMemFile io(data, size);

    if (opcode == OP_HELLO) {
        if (thePrefs.verbose())
            logDebug(QStringLiteral("onHelloReceived: OP_HELLO from %1:%2")
                         .arg(m_socket ? m_socket->peerAddress().toString() : QStringLiteral("?"))
                         .arg(m_socket ? m_socket->peerPort() : 0));
        // OP_HELLO has a 1-byte hash-size prefix (always 16) before the
        // user hash — must be consumed before processHelloTypePacket,
        // matching processHelloPacket() (MFC BaseClient.cpp:340-355).
        io.readUInt8();
        clearHelloProperties();
        const bool isMule = processHelloTypePacket(io);

        // The peer has now identified itself, so this throwaway can finally be matched
        // against clients we already know — in particular a LowID source sitting in
        // WaitCallback because we asked it, via server or Kad, to dial us.
        // MFC ListenSocket.cpp:262-266.
        if (m_incomingConnection && theApp.clientList) {
            auto* sender = qobject_cast<ClientReqSocket*>(m_socket);
            if (auto* known = theApp.clientList->attachToAlreadyKnown(this, sender)) {
                // The socket now belongs to `known`. Re-parse the hello onto it (MFC
                // re-runs ProcessHelloPacket on the survivor) and let it finish the
                // handshake in our place.
                SafeMemFile again(data, size);
                again.readUInt8();
                known->clearHelloProperties();
                const bool knownIsMule = known->processHelloTypePacket(again);
                if (known->hashType() == static_cast<int>(ClientSoftware::eMule) && !knownIsMule)
                    known->sendMuleInfoPacket(false);
                known->sendHelloAnswer();
                known->connectionEstablished();
                known->onHandshakeCompleted();
                known->onInfoPacketsReceived();
                known->maybeBootstrapKadFromPeer();

                // `this` is now socket-less and redundant. deleteLater() is what makes it
                // safe to retire an object from inside its own signal handler; nothing
                // below may touch `this`.
                theApp.clientList->removeClient(this);
                deleteLater();
                return;
            }
        }

        // Pre-0.42 eMules open with a plain eD2K hello and expect an unsolicited
        // OP_EMULEINFO to learn our capabilities. MFC ListenSocket.cpp:274-275 sends it
        // BEFORE the hello answer. hashType() rather than isEmuleClient(): the latter is
        // also true when m_emuleProtocol is set, which processHelloTypePacket() does for a
        // mule hello — that would defeat the !isMule half of the guard and get us flagged
        // by anti-leech modules, as documented on the OP_HELLOANSWER path below.
        if (hashType() == static_cast<int>(ClientSoftware::eMule) && !isMule)
            sendMuleInfoPacket(false);

        sendHelloAnswer();
        connectionEstablished();   // MFC ListenSocket.cpp:279 — also runs for inbound peers
        onHandshakeCompleted();    // MFC folds this into ConnectionEstablished; see its decl
        onInfoPacketsReceived();
        maybeBootstrapKadFromPeer();   // MFC ListenSocket.cpp:289-290
    } else if (opcode == OP_HELLOANSWER) {
        if (thePrefs.verbose())
            logDebug(QStringLiteral("onHelloReceived: OP_HELLOANSWER from %1:%2")
                         .arg(m_socket ? m_socket->peerAddress().toString() : QStringLiteral("?"))
                         .arg(m_socket ? m_socket->peerPort() : 0));
        m_helloAnswerPending = false;
        (void)processHelloTypePacket(io);   // its tags are read for their side effects

        // Deferred file request goes out first, so the remote processes it with
        // ExtendedRequestsVersion=0 — MFC's packet order, where file requests leave
        // ConnectionEstablished ahead of the hello answer.
        if (m_pendingFileRequest) {
            m_pendingFileRequest = false;
            logDebug(QStringLiteral("onHelloReceived: sending deferred file request after HELLO_ANSWER"));
            sendFileRequest();
        }

        // No OP_EMULEINFO here. MFC sends it from exactly one place — the inbound
        // OP_HELLO path, for an SO_EMULE peer (srchybrid/ListenSocket.cpp:275), which we
        // match above; its OP_HELLOANSWER handler (:214-232) sends nothing. Sending it
        // unprompted was also the riskier half: a mule peer's ProcessMuleInfoPacket
        // overwrites the 0x99 "version came from hello" marker with our legacy version
        // byte, which anti-leech modules read as a forged eMule version and ban us for.
        onInfoPacketsReceived();
        // MFC ListenSocket.cpp:229 calls ConnectionEstablished() here, AFTER
        // InfoPacketsReceived() — the reverse of the OP_HELLO ordering above. This is the
        // point at which an outgoing connection's handshake is actually complete.
        onHandshakeCompleted();
    }
}

// ===========================================================================
// onFileRequestReceived — dispatch file info/status/hashset packets
// ===========================================================================

void UpDownClient::onFileRequestReceived(const uint8* data, uint32 size, uint8 opcode)
{
    switch (opcode) {
    case OP_SETREQFILEID:
        processSetReqFileID(data, size);
        break;

    case OP_REQUESTFILENAME:
        processRequestFileName(data, size);
        break;

    case OP_REQFILENAMEANSWER: {
        // Answer to our file name request (download side)
        // Payload: [16-byte fileHash] [filename string...]
        // processFileInfo() asks for the slot of a single-part file itself; for the
        // others processFileStatus() does once OP_FILESTATUS arrives.
        SafeMemFile io(data, size);
        uint8 fileHash[16];
        io.readHash16(fileHash);
        if (!theApp.downloadQueue || !theApp.downloadQueue->fileByID(fileHash))
            checkFailedFileIdReqs(fileHash);   // MFC ListenSocket.cpp:455
        if (m_reqFile && md4equ(fileHash, m_reqFile->fileHash()))
            processFileInfo(io, m_reqFile);
        break;
    }

    case OP_FILEREQANSNOFIL: {
        // MFC ListenSocket.cpp:421-443
        if (size != 16)
            throw FileException("OP_FILEREQANSNOFIL: wrong packet size");
        PartFile* file = theApp.downloadQueue ? theApp.downloadQueue->fileByID(data) : nullptr;
        if (!file) {
            checkFailedFileIdReqs(data);
            break;
        }
        file->deadSourceList().addDeadSource(deadSourceKey(), hasLowID());
        // It may still have another file we want: try that before dropping it.
        switch (m_downloadState) {
        case DownloadState::Connected:
        case DownloadState::OnQueue:
        case DownloadState::NoNeededParts:
            dontSwapTo(file);
            if (!swapToAnotherFile(QStringLiteral("Source says it doesn't have the file"),
                                   true, true, true, nullptr, false, false))
                theApp.downloadQueue->removeSource(this);
            break;
        default:
            break;
        }
        break;
    }

    case OP_FILESTATUS: {
        // Payload: [16-byte fileHash] [2-byte partCount] [bitmap...]
        SafeMemFile io(data, size);
        uint8 fileHash[16];
        io.readHash16(fileHash);
        if (!theApp.downloadQueue || !theApp.downloadQueue->fileByID(fileHash))
            checkFailedFileIdReqs(fileHash);   // MFC ListenSocket.cpp:470
        // Verify this is for the file we requested
        if (m_reqFile && md4equ(fileHash, m_reqFile->fileHash())) {
            processFileStatus(false, io, m_reqFile);
            logDebug(QStringLiteral("OP_FILESTATUS: partCount=%1 completeSource=%2 downloadState=%3 from %4")
                         .arg(m_partCount).arg(m_completeSource)
                         .arg(static_cast<int>(m_downloadState)).arg(userName()));
        }
        break;
    }

    case OP_HASHSETREQUEST:
        sendHashsetPacket(data, size, false);
        break;

    case OP_HASHSETANSWER:
        processHashSet(data, size, false);
        break;

    default:
        break;
    }
}

// ===========================================================================
// onUploadRequestReceived — handle OP_STARTUPLOADREQ
// ===========================================================================

void UpDownClient::onUploadRequestReceived(const uint8* data, uint32 size)
{
    // MFC ListenSocket.cpp:480-493: exactly a file hash
    if (size != 16)
        return;

    // MFC's one hard handshake gate in the TCP dispatch (srchybrid/ListenSocket.cpp:480):
    // a slot request that arrives while our own HELLO is still unanswered is dropped.
    if (!checkHandshakeFinished())
        return;

    // The packet contains the requested file hash (16 bytes)
    KnownFile* file = findUploadFile(data);

    if (file) {
        setUploadFileID(file);
        if (theApp.uploadQueue)
            theApp.uploadQueue->addClientToQueue(this);
    } else {
        // MFC answers nothing here, it only counts the miss
        logDebug(QStringLiteral("onUploadRequestReceived: file not found for %1")
                     .arg(userName()));
        checkFailedFileIdReqs(data);
    }
}

// ===========================================================================
// Private helpers
// ===========================================================================

// ===========================================================================
// sendChatPacket — one OP_MESSAGE, no session bookkeeping
// ===========================================================================

void UpDownClient::sendChatPacket(const QString& message)
{
    SafeMemFile data;
    const QByteArray utf8 = message.toUtf8();
    data.writeUInt16(static_cast<uint16>(utf8.size()));
    data.write(utf8.constData(), utf8.size());

    auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_MESSAGE);
    safeConnectAndSendPacket(std::move(packet));
}

// ===========================================================================
// sendCaptchaChallenge — hold @p message back until the peer proves it can read
// ===========================================================================

void UpDownClient::sendCaptchaChallenge(const QString& message)
{
    if (!m_supportsCaptcha) {
        // Told once; its messages are dropped either way
        if (m_captchasSent < 1) {
            ++m_captchasSent;
            sendChatPacket(QStringLiteral(
                "In order to avoid spam messages, this user requires you to solve a captcha "
                "before you can send a message to him. However your client does not support "
                "captchas, so you will not be able to chat with this user."));
        }
        return;
    }
    if (m_captchasSent >= 3)   // no more than 3 tries
        return;

    const QString text = generateCaptchaText();
    QByteArray bmp;
    {
        QBuffer buffer(&bmp);
        buffer.open(QIODevice::WriteOnly);
        if (!generateCaptchaImage(text).save(&buffer, "BMP"))
            bmp.clear();
    }
    if (bmp.isEmpty()) {
        logError(QStringLiteral("Failed to create the captcha for %1").arg(statusName()));
        return;
    }

    SafeMemFile data;
    data.writeUInt8(0);   // no tags, for future use
    data.write(bmp.constData(), bmp.size());

    m_captchaPendingMsg = message;
    m_captchaChallenge = text;
    m_chatCaptchaState = ChatCaptchaState::ChallengeSent;
    ++m_captchasSent;

    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_CHATCAPTCHAREQ);
    safeConnectAndSendPacket(std::move(packet));
}

// ===========================================================================
// Captcha text and image
// ===========================================================================

namespace {

constexpr char kCaptchaChars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr int kCaptchaCharCount = sizeof(kCaptchaChars) - 1;

// 5x7 glyphs, one row per byte, in kCaptchaChars order. Drawn by hand because the
// daemon is a QCoreApplication: anything touching QFont aborts there.
constexpr uint8 kCaptchaGlyphs[kCaptchaCharCount][7] = {
    { 0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001 },  // A
    { 0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110 },  // B
    { 0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110 },  // C
    { 0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110 },  // D
    { 0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111 },  // E
    { 0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000 },  // F
    { 0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111 },  // G
    { 0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001 },  // H
    { 0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100 },  // J
    { 0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001 },  // K
    { 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111 },  // L
    { 0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001 },  // M
    { 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001 },  // N
    { 0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000 },  // P
    { 0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101 },  // Q
    { 0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001 },  // R
    { 0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110 },  // S
    { 0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100 },  // T
    { 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110 },  // U
    { 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100 },  // V
    { 0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010 },  // W
    { 0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001 },  // X
    { 0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100 },  // Y
    { 0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111 },  // Z
    { 0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111 },  // 2
    { 0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110 },  // 3
    { 0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010 },  // 4
    { 0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110 },  // 5
    { 0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110 },  // 6
    { 0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000 },  // 7
    { 0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110 },  // 8
    { 0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100 },  // 9
};

} // namespace

QString UpDownClient::generateCaptchaText()
{
    QString text;
    text.reserve(4);
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < 4; ++i)
        text.append(QChar::fromLatin1(kCaptchaChars[rng->bounded(kCaptchaCharCount)]));
    return text;
}

// MFC's shape (srchybrid/CaptchaGenerator.cpp): 32 px for the first letter, 18 for each
// further one, 48 high, two colours — under 1 KB as a BMP, which is what the receiving
// side's size check expects.
QImage UpDownClient::generateCaptchaImage(const QString& text)
{
    constexpr int kLetter = 32;
    constexpr int kCrowded = 18;
    constexpr int kHeight = 48;
    const int width = kLetter + static_cast<int>(text.length()) * kCrowded;

    QImage canvas(width, kHeight, QImage::Format_RGB32);
    if (canvas.isNull())
        return {};
    canvas.fill(Qt::white);

    auto* rng = QRandomGenerator::global();
    for (qsizetype i = 0; i < text.length(); ++i) {
        const char* at = std::strchr(kCaptchaChars, text.at(i).toLatin1());
        if (!at || *at == '\0')
            continue;
        const uint8* glyph = kCaptchaGlyphs[at - kCaptchaChars];

        // Each letter gets its own size, tilt and place
        const double scale = 2.6 + rng->bounded(6) / 10.0;
        const double angle = (rng->bounded(37) - 18) * 3.14159265358979 / 180.0;
        const double cosA = std::cos(angle);
        const double sinA = std::sin(angle);
        const int cx = kLetter / 2 + 4 + static_cast<int>(i) * (kCrowded + 2) + rng->bounded(4);
        const int cy = kHeight / 2 + rng->bounded(7) - 3;

        constexpr int kReach = 22;
        for (int y = std::max(0, cy - kReach); y < std::min(kHeight, cy + kReach); ++y) {
            for (int x = std::max(0, cx - kReach); x < std::min(width, cx + kReach); ++x) {
                // Back into the glyph's own grid
                const double dx = x - cx;
                const double dy = y - cy;
                const int col = static_cast<int>(std::floor((dx * cosA + dy * sinA) / scale + 2.5));
                const int row = static_cast<int>(std::floor((dy * cosA - dx * sinA) / scale + 3.5));
                if (col >= 0 && col < 5 && row >= 0 && row < 7 && (glyph[row] >> (4 - col)) & 1)
                    canvas.setPixel(x, y, qRgb(0, 0, 0));
            }
        }
    }

    // Speckle, so the letters are not clean shapes
    for (int i = 0; i < width * kHeight / 60; ++i)
        canvas.setPixel(rng->bounded(width), rng->bounded(kHeight),
                        rng->bounded(2) ? qRgb(0, 0, 0) : qRgb(255, 255, 255));

    return canvas.convertToFormat(QImage::Format_Mono, Qt::ThresholdDither);
}

// ===========================================================================
// setLastAskedForSourcesTime
// ===========================================================================

void UpDownClient::setLastAskedForSourcesTime()
{
    m_lastAskedForSources = getTickCount();
}

// ===========================================================================
// processRequestSources — handle OP_REQUESTSOURCES v1 (peer requests sources)
// MFC ListenSocket.cpp OP_REQUESTSOURCES case
// ===========================================================================

void UpDownClient::processRequestSources(const uint8* data, uint32 size)
{
    if (!data || size < 16)
        return;

    SafeMemFile io(data, size);
    uint8 fileHash[16];
    io.readHash16(fileHash);

    // Look up in shared files first, then download queue
    KnownFile* file = nullptr;
    if (theApp.sharedFileList)
        file = theApp.sharedFileList->getFileByID(fileHash);
    if (!file && theApp.downloadQueue)
        file = theApp.downloadQueue->fileByID(fileHash);

    if (!file) {
        checkFailedFileIdReqs(fileHash);   // MFC ListenSocket.cpp:1243
        return;
    }

    // Some clients skip the OP_REQUESTFILENAME / OP_SETREQFILEID preamble and ask for
    // sources straight away, so the upload file has to be set from here or the answer is
    // filtered against whatever file was last in context. MFC ListenSocket.cpp:1213-1220.
    // The bundled multipacket form deliberately does not repeat this — its header already
    // set the file.
    setUploadFileID(file);

    // SX1 carries no version byte, so a requested version of 0 is what the shared gate
    // sees; it then falls back to the version the peer announced at handshake.
    answerSourceRequest(file, 0, 0);
}

// ===========================================================================
// processAnswerSources — handle OP_ANSWERSOURCES v1 (peer sends us sources)
// MFC ListenSocket.cpp OP_ANSWERSOURCES case
// ===========================================================================

void UpDownClient::processAnswerSources(const uint8* data, uint32 size)
{
    if (!data || size < 16)
        return;

    SafeMemFile io(data, size);
    uint8 fileHash[16];
    io.readHash16(fileHash);

    if (!theApp.downloadQueue)
        return;

    auto* file = theApp.downloadQueue->fileByID(fileHash);
    if (!file) {
        checkFailedFileIdReqs(fileHash);
        return;
    }
    if (!expectsSourcesFor(file)) {
        logDebug(QStringLiteral("Unrequested source answer from %1 dropped").arg(statusName()));
        return;
    }

    m_lastSourceAnswer = getTickCount();
    file->setLastAnsweredTime();

    file->addClientSources(io, m_sourceExchange1Ver, /*isSX2*/ false, this);
}

// ===========================================================================
// processRequestSources2 — handle OP_REQUESTSOURCES2 (peer requests sources)
// ===========================================================================

void UpDownClient::processRequestSources2(const uint8* data, uint32 size)
{
    if (!data || size < 19) // 1 byte version + 2 bytes options + 16 bytes hash
        return;

    SafeMemFile io(data, size);
    const uint8 version = io.readUInt8();
    const uint16 options = io.readUInt16();

    uint8 fileHash[16];
    io.readHash16(fileHash);

    // Look up in shared files first, then download queue
    KnownFile* file = nullptr;
    if (theApp.sharedFileList)
        file = theApp.sharedFileList->getFileByID(fileHash);
    if (!file && theApp.downloadQueue)
        file = theApp.downloadQueue->fileByID(fileHash);

    if (!file) {
        logDebug(QStringLiteral("processRequestSources2: file not found"));
        checkFailedFileIdReqs(fileHash);   // MFC ListenSocket.cpp:1243
        return;
    }

    setUploadFileID(file);   // see processRequestSources()

    answerSourceRequest(file, version, options);
}

// ===========================================================================
// processAnswerSources2 — handle OP_ANSWERSOURCES2 (peer sends us sources)
// ===========================================================================

void UpDownClient::processAnswerSources2(const uint8* data, uint32 size)
{
    if (!data || size < 17) // 1 byte version + 16 bytes hash
        return;

    SafeMemFile io(data, size);
    const uint8 version = io.readUInt8();

    uint8 fileHash[16];
    io.readHash16(fileHash);

    if (!theApp.downloadQueue)
        return;

    auto* file = theApp.downloadQueue->fileByID(fileHash);
    if (!file) {
        checkFailedFileIdReqs(fileHash);
        return;
    }
    if (!expectsSourcesFor(file)) {
        logDebug(QStringLiteral("Unrequested source answer from %1 dropped").arg(statusName()));
        return;
    }

    m_lastSourceAnswer = getTickCount();
    file->setLastAnsweredTime();

    file->addClientSources(io, version, /*isSX2*/ true, this);
}

// ===========================================================================
// answerSourceRequest — private; shared by every opcode that asks for sources
// MFC ListenSocket.cpp:996-1028
// ===========================================================================

void UpDownClient::answerSourceRequest(KnownFile* file, uint8 requestedVersion,
                                       uint16 requestedOptions)
{
    if (!file)
        return;

    // "Although this shouldn't happen, it's just in case for any Mods that mess with
    // version numbers" — MFC ListenSocket.cpp:1003. A peer that announced only SX1 v1
    // and sent no requested version gets nothing.
    if (requestedVersion == 0 && m_sourceExchange1Ver <= 1)
        return;

    // MFC ListenSocket.cpp:1004-1015, kept expression-for-expression: the latency
    // allowance is added to the elapsed time rather than subtracted from the interval,
    // and the rare-file shortcut applies to part files only.
    const uint64 timePassed =
        getTickCount() - m_lastSourceRequest + CONNECTION_LATENCY;
    const bool neverAskedBefore = (m_lastSourceRequest == 0);
    const bool rareFile = file->isPartFile()
                          && static_cast<PartFile*>(file)->sourceCount() <= RARE_FILE;

    if (!((rareFile && (neverAskedBefore || timePassed > SOURCECLIENTREASKS))
          || neverAskedBefore
          || timePassed > SOURCECLIENTREASKS * MINCOMMONPENALTY))
        return;

    m_lastSourceRequest = getTickCount();

    // ToDo: MFC meters this as AddUpDataOverheadSourceExchange; our sendPacket already
    // books it as "other" overhead, so categorising it here would double-count.
    auto packet = file->createSrcInfoPacket(this, requestedVersion, requestedOptions);
    if (packet)
        sendPacket(std::move(packet));
}

// ===========================================================================
// processAskSharedFiles — respond to OP_ASKSHAREDFILES
// ===========================================================================

void UpDownClient::processAskSharedFiles()
{
    const int access = thePrefs.viewSharedFilesAccess();
    const bool allowed = (access == 2) || (access == 1 && m_friend != nullptr);

    if (!allowed) {
        // An empty list, not the denial opcode: a client old enough to send this
        // request may not know it (MFC ListenSocket.cpp:672-694).
        logDebug(QStringLiteral("Denied shared file browse request from %1").arg(userName()));
        SafeMemFile empty;
        empty.writeUInt32(0);
        sendPacket(std::make_unique<Packet>(empty, OP_EDONKEYPROT, OP_ASKSHAREDFILESANSWER));
        return;
    }

    if (!theApp.sharedFileList)
        return;

    // Collect shared files, skipping large files if peer doesn't support them
    const bool largePeer = supportsLargeFiles();
    std::vector<KnownFile*> files;
    theApp.sharedFileList->forEachFile([&](KnownFile* file) {
        if (!file->isLargeFile() || largePeer)
            files.push_back(file);
    });

    SafeMemFile response;
    response.writeUInt32(static_cast<uint32>(files.size()));

    for (KnownFile* file : files)
        writeOfferedFile(response, *file);

    auto packet = std::make_unique<Packet>(response, OP_EDONKEYPROT, OP_ASKSHAREDFILESANSWER);
    sendPacket(std::move(packet));

    logDebug(QStringLiteral("Sent %1 shared files to %2").arg(files.size()).arg(userName()));
}

// ===========================================================================
// processAskSharedDirs — respond to OP_ASKSHAREDDIRS
// ===========================================================================

void UpDownClient::processAskSharedDirs()
{
    const int access = thePrefs.viewSharedFilesAccess();
    const bool allowed = (access == 2) || (access == 1 && m_friend != nullptr);

    if (!allowed) {
        logDebug(QStringLiteral("Denied shared dirs browse request from %1").arg(userName()));
        auto packet = std::make_unique<Packet>(OP_ASKSHAREDDENIEDANS, 0);
        packet->prot = OP_EDONKEYPROT;
        sendPacket(std::move(packet));
        return;
    }

    sendSharedDirectories();
}

// ===========================================================================
// processAskSharedFilesDir — respond to OP_ASKSHAREDFILESDIR
// ===========================================================================

void UpDownClient::processAskSharedFilesDir(const uint8* data, uint32 size)
{
    const int access = thePrefs.viewSharedFilesAccess();
    const bool allowed = (access == 2) || (access == 1 && m_friend != nullptr);

    if (!allowed) {
        logDebug(QStringLiteral("Denied shared files dir request from %1").arg(userName()));
        auto packet = std::make_unique<Packet>(OP_ASKSHAREDDENIEDANS, 0);
        packet->prot = OP_EDONKEYPROT;
        sendPacket(std::move(packet));
        return;
    }

    if (!data || size == 0 || !theApp.sharedFileList)
        return;

    SafeMemFile io(data, size);
    const QString reqDir = io.readString(m_unicodeSupport);

    // The name is one we handed out, or one of the two fixed ones
    // (MFC ListenSocket.cpp:736-763).
    const bool largePeer = supportsLargeFiles();
    std::vector<KnownFile*> matchedFiles;
    if (reqDir == QLatin1StringView(OP_INCOMPLETE_SHARED_FILES)) {
        if (theApp.downloadQueue) {
            for (PartFile* partFile : theApp.downloadQueue->files())
                if (partFile->status(true) == PartFileStatus::Ready)
                    matchedFiles.push_back(partFile);
        }
    } else if (reqDir == QLatin1StringView(OP_OTHER_SHARED_FILES)) {
        matchedFiles = theApp.sharedFileList->singleSharedFilesForBrowse();
    } else if (const QString dir = theApp.sharedFileList->dirNameByPseudo(reqDir); !dir.isEmpty()) {
        matchedFiles = theApp.sharedFileList->filesInDirectory(dir);
        std::erase_if(matchedFiles, [](const KnownFile* file) { return file->isPartFile(); });
    } else {
        logDebug(QStringLiteral("View shared files: no directory behind \"%1\" — sending an empty list")
                     .arg(reqDir));
    }
    if (!largePeer)
        std::erase_if(matchedFiles, [](const KnownFile* file) { return file->isLargeFile(); });

    // the name goes back as it came
    SafeMemFile response;
    response.writeString(reqDir, peerStringMode());
    response.writeUInt32(static_cast<uint32>(matchedFiles.size()));

    for (KnownFile* file : matchedFiles)
        writeOfferedFile(response, *file);

    auto packet = std::make_unique<Packet>(response, OP_EDONKEYPROT, OP_ASKSHAREDFILESDIRANS);
    sendPacket(std::move(packet));
}

// ===========================================================================
// writeOfferedFile — one file record for a browsing client
// ===========================================================================

UTF8Mode UpDownClient::peerStringMode() const
{
    return m_unicodeSupport ? UTF8Mode::Raw : UTF8Mode::None;
}

// MFC CSharedFileList::CreateOfferedFilePacket with pClient (SharedFileList.cpp:919-1071).
void UpDownClient::writeOfferedFile(SafeMemFile& out, KnownFile& file) const
{
    // No address unless we can be reached at it.
    uint32 clientID = 0;
    uint16 clientPort = 0;
    if (theApp.isConnected() && !theApp.isFirewalled()) {
        clientID = theApp.getID();
        clientPort = theApp.advertisedTcpPort();
    }
    out.writeHash16(file.fileHash());
    out.writeUInt32(clientID);
    out.writeUInt16(clientPort);

    const uint32 emuleVersion = isEmuleClient() ? std::max(m_clientVersion, uint32{1}) : 0;
    const std::vector<Tag> tags = SharedFileList::offeredTagsForClient(file, emuleVersion);
    out.writeUInt32(static_cast<uint32>(tags.size()));
    const bool newTags = emuleVersion >= makeClientVersion(0, 42, 7);
    for (const Tag& tag : tags) {
        if (newTags)
            tag.writeNewEd2kTag(out, peerStringMode());
        else
            tag.writeTagToFile(out, peerStringMode());
    }
}

// ===========================================================================
// processSharedDirsAnswer — handle OP_ASKSHAREDDIRSANS
// ===========================================================================

void UpDownClient::processSharedDirsAnswer(const uint8* data, uint32 size)
{
    if (m_fileListRequested != 1) {
        logDebug(QStringLiteral("processSharedDirsAnswer: unrequested response from %1").arg(userName()));
        return;
    }

    if (!data || size < 4)
        return;

    SafeMemFile io(data, size);
    const uint32 dirCount = io.readUInt32();

    if (dirCount == 0) {
        m_fileListRequested = 0;
        logStatusInfo(QStringLiteral("User %1 shares no files").arg(statusName()));
        return;
    }

    // Request files for each directory
    m_fileListRequested = static_cast<int>(dirCount);
    for (uint32 i = 0; i < dirCount; ++i) {
        const QString dirName = io.readString(m_unicodeSupport);

        SafeMemFile reqData;
        reqData.writeString(dirName, peerStringMode());

        auto packet = std::make_unique<Packet>(reqData, OP_EDONKEYPROT, OP_ASKSHAREDFILESDIR);
        sendPacket(std::move(packet));
    }
}

// ===========================================================================
// processSharedFilesDirAnswer — handle OP_ASKSHAREDFILESDIRANS
// ===========================================================================

void UpDownClient::processSharedFilesDirAnswer(const uint8* data, uint32 size)
{
    if (m_fileListRequested <= 0) {
        logDebug(QStringLiteral("processSharedFilesDirAnswer: unrequested response from %1").arg(userName()));
        return;
    }

    if (!data || size < 4)
        return;

    SafeMemFile io(data, size);
    const QString dirName = io.readString(m_unicodeSupport);

    // The rest is the file list; processSharedFileList() counts the answer.
    const auto pos = static_cast<uint32>(io.position());
    processSharedFileList(data + pos, size - pos, dirName, true);
}

// ===========================================================================
// processSharedDenied — handle OP_ASKSHAREDDENIEDANS
// ===========================================================================

void UpDownClient::processSharedDenied()
{
    // Ends this request only (MFC ListenSocket.cpp:833-839): the peer may allow
    // it later, e.g. once we are its friend.
    m_fileListRequested = 0;
    logStatusWarning(QStringLiteral("User %1 (%2) denied access to list of shared directories/files")
                         .arg(statusName()).arg(m_userIDHybrid));
}

// ===========================================================================
// processChangeClientIP — handle OP_CHANGE_CLIENT_IP
// ===========================================================================

void UpDownClient::flushPendingIPChange()
{
    if (!m_sendIPPending)
        return;

    // Clear unconditionally. If the peer cannot be told now it is either gone or not
    // IPv6-capable, and re-queueing would leave the flag set forever.
    m_sendIPPending = false;

    if (!m_supportsIPv6 || !m_socket || !m_socket->isConnected())
        return;

    // The advertise gate, not the confidence gate: if the server probed our address and
    // found it unreachable we must not push it to peers either.
    if (!theApp.shouldAdvertisePublicIPv6())
        return;

    const Address ourIPv6 = theApp.publicIPv6();
    if (!ourIPv6.isIPv6())
        return;

    auto packet = std::make_unique<Packet>(OP_CHANGE_CLIENT_IP, 16, OP_EDONKEYPROT);
    std::memcpy(packet->pBuffer, ourIPv6.ipv6Bytes().data(), 16);
    sendPacket(std::move(packet));

    logDebug(QStringLiteral("OP_CHANGE_CLIENT_IP to %1: our IPv6 is now %2")
                 .arg(userName(), ourIPv6.toString()));
}

void UpDownClient::processChangeClientIP(const uint8* data, uint32 size)
{
    // Payload is 16 raw IPv6 bytes, network order, with no tag wrapper. Anything shorter
    // is not a truncated address we can use, it is a different packet.
    if (!data || size < 16)
        return;

    const Address announced = Address::fromIPv6Bytes(data);
    if (!announced.isPublicIP()) {
        logDebug(QStringLiteral("OP_CHANGE_CLIENT_IP from %1: ignoring non-public address %2")
                     .arg(userName(), announced.toString()));
        return;
    }

    m_userIPv6 = announced;
    m_openIPv6 = true;
    sourceIdentityChanged();

    // Only re-point the dial address if we were already going to use IPv6 for this peer;
    // an IPv4 connection in progress must not be redirected mid-flight.
    if (m_connectAddress.isIPv6())
        m_connectAddress = m_userIPv6;

    logDebug(QStringLiteral("OP_CHANGE_CLIENT_IP from %1: new IPv6 %2")
                 .arg(userName(), announced.toString()));
}

// ===========================================================================
// processAICHFileHashRequest — MFC ListenSocket.cpp:1545-1571
// ===========================================================================

void UpDownClient::processAICHFileHashRequest(const uint8* data, uint32 size)
{
    SafeMemFile io(data, size);
    uint8 fileHash[16];
    io.readHash16(fileHash);

    KnownFile* file = findUploadFile(fileHash);
    if (!file) {
        checkFailedFileIdReqs(fileHash);
        return;
    }

    // A peer with file identifiers gets the AICH hash inside the identifier instead.
    if (!isSupportingAICH() || !file->fileIdentifier().hasAICHHash())
        return;

    SafeMemFile dataOut;
    dataOut.writeHash16(fileHash);
    file->fileIdentifier().getAICHHash().write(dataOut);

    auto packet = std::make_unique<Packet>(dataOut, OP_EMULEPROT, OP_AICHFILEHASHANS);
    sendPacket(std::move(packet));
}

// ===========================================================================
// processChangeClientID — MFC ListenSocket.cpp:604-636
// ===========================================================================

void UpDownClient::processChangeClientID(const uint8* data, uint32 size)
{
    SafeMemFile io(data, size);
    const uint32 newUserID = io.readUInt32();      // ed2k ID, wire order
    const uint32 newServerIP = io.readUInt32();    // network order

    Server* newServer = theApp.serverList
        ? theApp.serverList->getServerByIP(newServerIP)
        : nullptr;

    if (isLowID(newUserID)) {
        // A Low ID is only meaningful together with the server that issued it, so an
        // unknown server means we cannot use the ID either.
        if (!newServer)
            return;
        setUserIDHybrid(newUserID);
        setServerAddress(Address::fromNetworkOrder(newServerIP));
        setServerPort(newServer->port());
    } else if (Address::fromNetworkOrder(newUserID) == m_userAddress) {
        // A High ID is the peer's own IPv4 — believe it only if it is the address we are
        // actually talking to. m_userIDHybrid keeps High IDs in host order.
        setUserIDHybrid(ntohl(newUserID));
        if (newServer) {
            setServerAddress(Address::fromNetworkOrder(newServerIP));
            setServerPort(newServer->port());
        }
    } else {
        logDebug(QStringLiteral("OP_CHANGE_CLIENT_ID from %1: unknown contents (id=%2)")
                     .arg(userName(), ipstr(newUserID)));
    }
}

// ===========================================================================
// statusName — who a user-facing log line talks about
// ===========================================================================

QString UpDownClient::statusName() const
{
    // A peer we never shook hands with has no name yet; the address still tells them apart
    if (!m_username.isEmpty())
        return m_username;
    return QStringLiteral("%1:%2").arg(m_userAddress.toString()).arg(m_userPort);
}

// ===========================================================================
// expectsSourcesFor — is a source answer for this file one we asked for?
// ===========================================================================

bool UpDownClient::expectsSourcesFor(const PartFile* file) const
{
    // Never asked this client for sources: nothing to answer
    if (m_lastAskedForSources == 0)
        return false;
    // We ask for m_reqFile; an A4AF swap may have moved it since
    return file == m_reqFile
        || std::ranges::find(m_otherRequests, file) != m_otherRequests.end()
        || std::ranges::find(m_otherNoNeeded, file) != m_otherNoNeeded.end();
}

} // namespace eMule
