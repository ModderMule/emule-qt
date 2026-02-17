/// @file UpDownClient.cpp
/// @brief UpDownClient implementation — identity, state, Compare, version, handshake,
///        connection management, secure identity, chat, preview, firewall.
///
/// Ported from MFC srchybrid/BaseClient.cpp + DownloadClient.cpp.

#include "client/UpDownClient.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/DeadSourceList.h"
#include "app/AppContext.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "friends/Friend.h"
#include "friends/FriendList.h"
#include "ipfilter/IPFilter.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadUDPListener.h"
#include "net/ClientReqSocket.h"
#include "net/EMSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "transfer/UploadQueue.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"
#include "utils/TimeUtils.h"
#include "utils/Opcodes.h"

#include <QBuffer>
#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QRandomGenerator>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>  // ntohl
#endif

#include <cstring>
#include <random>

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
        m_connectIP = ed2kID ? userId : ntohl(userId);

    m_serverIP = serverIP;
    m_serverPort = serverPort;
}

UpDownClient::~UpDownClient()
{
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
    setIP(0);

    m_serverIP = 0;
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

    m_lastSignatureIP = 0;
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

    m_buddyIP = 0;
    m_lastBuddyPingPongTime = static_cast<uint32>(getTickCount());
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
    m_lastRefreshedULDisplay = static_cast<uint32>(getTickCount());

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
            m_uploadTime = static_cast<uint32>(getTickCount());
        }

        m_uploadState = state;
        emit uploadStateChanged(state);
    }
}

void UpDownClient::setDownloadState(DownloadState state)
{
    if (state != m_downloadState) {
        // Clear rate data when leaving Downloading
        if (m_downloadState == DownloadState::Downloading) {
            m_downDatarate = 0;
            m_downDataRateMS = 0;
            m_sumForAvgDownDataRate = 0;
            m_averageDDR.clear();
        }

        m_downloadState = state;
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
        if (userIP() != 0 && userIP() == other->userIP()) {
            if (userPort() != 0 && userPort() == other->userPort())
                return true;
            if (kadPort() != 0 && kadPort() == other->kadPort())
                return true;
        }
        // Same low ID on same server
        if (userIDHybrid() != 0 && userIDHybrid() == other->userIDHybrid()
            && serverIP() != 0 && serverIP() == other->serverIP()
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
        if (userIP() != 0 && other->userIP() != 0) {
            if (userIP() == other->userIP())
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
        if (connectIP() != 0) {
            return QStringLiteral("%1@%2 (%3) '%4' (%5,%6/%7/%8)")
                .arg(userIDHybrid())
                .arg(ipstr(serverIP()))
                .arg(ipstr(connectIP()))
                .arg(userName())
                .arg(dbgGetFullClientSoftVer())
                .arg(dbgGetDownloadState())
                .arg(dbgGetUploadState())
                .arg(dbgGetKadState());
        }
        return QStringLiteral("%1@%2 '%3' (%4,%5/%6/%7)")
            .arg(userIDHybrid())
            .arg(ipstr(serverIP()))
            .arg(userName())
            .arg(dbgGetFullClientSoftVer())
            .arg(dbgGetDownloadState())
            .arg(dbgGetUploadState())
            .arg(dbgGetKadState());
    }

    Q_UNUSED(formatIP);
    return QStringLiteral("%1 '%2' (%3,%4/%5/%6)")
        .arg(ipstr(connectIP()))
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
    const uint32 tagCount = data.readUInt32();

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
                m_buddyIP = tag.intValue();
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

        default:
            // Check for string-named "pr" tag (eDonkeyHybrid marker)
            if (!tag.nameId() && tag.name() == QByteArrayLiteral("pr"))
                bPrTag = true;
            break;
        }
    }

    m_userPort = nUserPort;

    // Read server info
    m_serverIP = data.readUInt32();
    m_serverPort = data.readUInt16();

    // Check for trailing client identification bytes
    if (data.position() < data.length()) {
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
            setIP(htonl(addr.toIPv4Address()));
    }

    // Add peer's server to our server list if not already known
    if (theApp.serverList && m_serverIP != 0 && m_serverPort != 0) {
        if (!theApp.serverList->findByIPTcp(m_serverIP, m_serverPort)) {
            auto newServer = std::make_unique<Server>(m_serverIP, m_serverPort);
            theApp.serverList->addServer(std::move(newServer));
        }
    }

    // Credits lookup
    if (theApp.clientCredits)
        setCredits(theApp.clientCredits->getCredit(m_userHash.data()));

    // Friend linking
    if (theApp.friendList)
        m_friend = theApp.friendList->searchFriend(m_userHash.data(), m_userIP, m_userPort);

    // High-ID conversion: if not low-ID, convert userIDHybrid to match userIP
    if (m_userIP != 0) {
        if (!hasLowID() || m_userIDHybrid == 0 || m_userIDHybrid == m_userIP)
            m_userIDHybrid = ntohl(m_userIP);
    }

    // Set info packets received flag
    m_infoPacketsReceived |= InfoPacketState::EDonkeyProtPack;

    // If CT_EMULE_VERSION was received, mark eMule protocol
    if (bIsMule) {
        m_emuleProtocol = true;
        m_infoPacketsReceived |= InfoPacketState::EMuleProtPack;
    }

    if (bPrTag)
        m_isHybrid = true;

    initClientSoftwareVersion();

    if (m_isHybrid)
        m_sharedDirectories = true;

    return bIsMule;
}

// ===========================================================================
// sendHelloPacket — MFC BaseClient.cpp:890-910
// ===========================================================================

void UpDownClient::sendHelloPacket()
{
    if (!m_socket)
        return;

    SafeMemFile data;
    data.writeUInt8(16); // userhash size
    sendHelloTypePacket(data);

    auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_HELLO);
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

    auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_HELLOANSWER);
    m_socket->sendPacket(std::move(packet));
    m_helloAnswerPending = false;
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
    data.writeUInt16(thePrefs.port());

    // Build tags
    constexpr uint32 tagCount = 6;
    data.writeUInt32(tagCount);

    // CT_NAME — our nickname
    Tag(CT_NAME, thePrefs.nick()).writeTagToFile(data, UTF8Mode::Raw);

    // CT_VERSION — eDonkey version
    Tag(CT_VERSION, static_cast<uint32>(EDONKEYVERSION)).writeTagToFile(data);

    // CT_EMULE_UDPPORTS — (kadPort << 16) | udpPort
    uint16 kadPortVal = 0;
    if (auto* kadPrefs = kad::Kademlia::getInstancePrefs())
        kadPortVal = kadPrefs->internKadPort();
    const uint32 udpPorts = (static_cast<uint32>(kadPortVal) << 16) | thePrefs.udpPort();
    Tag(CT_EMULE_UDPPORTS, udpPorts).writeTagToFile(data);

    // CT_EMULE_MISCOPTIONS1 — capability bits
    const uint32 miscOpts1 =
        (static_cast<uint32>(1) << 29) | // AICH version = 1
        (static_cast<uint32>(1) << 28) | // Unicode
        (static_cast<uint32>(4) << 24) | // UDP version
        (static_cast<uint32>(1) << 20) | // Data compression
        (static_cast<uint32>(2) << 16) | // Secure ident
        (static_cast<uint32>(SOURCEEXCHANGE2_VERSION) << 12) | // Source exchange
        (static_cast<uint32>(2) <<  8) | // Extended requests
        (static_cast<uint32>(1) <<  4) | // Comments
        (static_cast<uint32>(0) <<  3) | // Peer cache (not supported)
        (static_cast<uint32>(0) <<  2) | // No view shared
        (static_cast<uint32>(1) <<  1) | // Multi packet
        (static_cast<uint32>(1) <<  0);  // Preview
    Tag(CT_EMULE_MISCOPTIONS1, miscOpts1).writeTagToFile(data);

    // CT_EMULE_MISCOPTIONS2 — more capability bits
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
        (static_cast<uint32>(1) << 12) | // Direct UDP callback
        (static_cast<uint32>(1) << 13);  // File identifiers
    Tag(CT_EMULE_MISCOPTIONS2, miscOpts2).writeTagToFile(data);

    // CT_EMULE_VERSION — (compatClient << 24) | (majVer << 17) | (minVer << 10) | (upVer << 7)
    const uint32 emuleVer =
        (static_cast<uint32>(0) << 24) | // compatible client = 0 (eMule)
        (static_cast<uint32>(EMULE_VERSION_MAJOR) << 17) |
        (static_cast<uint32>(EMULE_VERSION_MINOR) << 10) |
        (static_cast<uint32>(EMULE_VERSION_PATCH) <<  7);
    Tag(CT_EMULE_VERSION, emuleVer).writeTagToFile(data);

    // Write server info
    if (theApp.serverConnect && theApp.serverConnect->isConnected()) {
        if (auto* srv = theApp.serverConnect->currentServer()) {
            data.writeUInt32(srv->ip());
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
    data.writeUInt8(static_cast<uint8>(EMULE_VERSION_MAJOR)); // eMule version byte
    data.writeUInt8(EMULE_PROTOCOL); // protocol version

    constexpr uint32 tagCount = 7;
    data.writeUInt32(tagCount);

    Tag(ET_COMPRESSION, static_cast<uint32>(1)).writeTagToFile(data);
    Tag(ET_UDPVER, static_cast<uint32>(4)).writeTagToFile(data);
    Tag(ET_UDPPORT, static_cast<uint32>(thePrefs.udpPort())).writeTagToFile(data);
    Tag(ET_SOURCEEXCHANGE, static_cast<uint32>(SOURCEEXCHANGE2_VERSION)).writeTagToFile(data);
    Tag(ET_COMMENTS, static_cast<uint32>(1)).writeTagToFile(data);
    Tag(ET_EXTENDEDREQUEST, static_cast<uint32>(2)).writeTagToFile(data);

    // ET_FEATURES — crypto + preview bits
    const uint32 features =
        (static_cast<uint32>(thePrefs.cryptLayerSupported() ? 3 : 0)) | // sec ident (bits 0-1)
        (static_cast<uint32>(1) << 7); // preview (bit 7)
    Tag(ET_FEATURES, features).writeTagToFile(data);

    const uint8 opcode = answer ? OP_EMULEINFOANSWER : OP_EMULEINFO;
    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, opcode);
    m_socket->sendPacket(std::move(packet));
}

// ===========================================================================
// processMuleInfoPacket — MFC BaseClient.cpp:734-887
// ===========================================================================

void UpDownClient::processMuleInfoPacket(const uint8* data, uint32 size)
{
    SafeMemFile file(data, size);

    const uint8 emuleVersion = file.readUInt8();
    const uint8 protVersion = file.readUInt8();

    // Must be our protocol
    if (protVersion != EMULE_PROTOCOL)
        return;

    // Set implicit version-based capabilities for old clients
    if (emuleVersion >= 0x20) {
        m_sourceExchange1Ver = 1;
    }
    if (emuleVersion >= 0x24) {
        m_dataCompVer = 1;
        m_acceptCommentVer = 1;
    }
    if (emuleVersion >= 0x26) {
        m_extendedRequestsVer = 1;
    }

    // Read tags
    const uint32 tagCount = file.readUInt32();

    for (uint32 i = 0; i < tagCount; ++i) {
        Tag tag(file, true);

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

    initClientSoftwareVersion();
}

// ===========================================================================
// processMuleCommentPacket — MFC BaseClient.cpp:1054-1090 (simplified)
// ===========================================================================

void UpDownClient::processMuleCommentPacket(const uint8* data, uint32 size)
{
    SafeMemFile file(data, size);

    m_fileRating = file.readUInt8();

    const uint32 commentLen = file.readUInt32();
    if (commentLen > 0) {
        m_fileComment = file.readString(true, commentLen);
        if (m_fileComment.length() > MAXFILECOMMENTLEN)
            m_fileComment.truncate(MAXFILECOMMENTLEN);
    } else {
        m_fileComment.clear();
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

    m_socket->sendPacket(std::move(packet));
    return true;
}

// ===========================================================================
// checkHandshakeFinished
// ===========================================================================

bool UpDownClient::checkHandshakeFinished() const
{
    return (m_infoPacketsReceived & InfoPacketState::Both) == InfoPacketState::Both;
}

// ===========================================================================
// Phase 3 — Connection Management
// Ported from MFC BaseClient.cpp lines 1238-1581
// ===========================================================================

// ===========================================================================
// tryToConnect — MFC BaseClient.cpp:1238-1478
// ===========================================================================

bool UpDownClient::tryToConnect(bool ignoreMaxCon)
{
    Q_UNUSED(ignoreMaxCon);

    if (m_connectingState != ConnectingState::None) {
        // Already connecting
        return true;
    }

    const uint32 curTick = static_cast<uint32>(getTickCount());
    if ((curTick - m_lastTriedToConnect) < MIN2MS(1)) {
        // Too soon since last attempt
        return false;
    }
    m_lastTriedToConnect = curTick;

    // IP filter check
    if (theApp.ipFilter && theApp.ipFilter->isFiltered(m_connectIP))
        return false;

    // Socket limit check
    if (theApp.listenSocket && theApp.listenSocket->tooManySockets())
        return false;

    if (m_socket && m_socket->isConnected()) {
        // Already connected — just send what's needed
        connectionEstablished();
        return true;
    }

    // Direct TCP connection path for high-ID clients
    if (m_connectIP == 0 && !hasLowID()) {
        m_connectIP = ntohl(m_userIDHybrid);
    }

    if (m_connectIP != 0) {
        // Direct TCP connection
        m_connectingState = ConnectingState::DirectTCP;
        connect();
        return true;
    }

    // Low-ID client — need callback via server
    if (hasLowID() && theApp.serverConnect && theApp.serverConnect->isConnected()) {
        // Check if the source is on the same server we're connected to
        if (theApp.serverConnect->isLocalServer(m_serverIP, m_serverPort)) {
            // Send callback request via server
            SafeMemFile data;
            data.writeUInt32(m_userIDHybrid);
            auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_CALLBACKREQUEST);
            theApp.serverConnect->sendPacket(std::move(packet));
            m_connectingState = ConnectingState::ServerCallback;
            return true;
        }
    }

    // Kademlia callback path — use buddy if available
    if (hasLowID() && m_kadVersion >= KADEMLIA_VERSION2_47a
        && m_buddyIP != 0 && m_buddyPort != 0)
    {
        m_connectingState = ConnectingState::KadCallback;
        // Build Kademlia callback packet via buddy
        SafeMemFile data;
        data.writeUInt16(m_userPort); // our port
        data.writeHash16(m_userHash.data()); // target client hash
        // Send the buddy connection request
        // This will be forwarded by the buddy to the target client
        auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_CALLBACK);
        sendPacket(std::move(packet));
        return true;
    }

    qDebug() << "tryToConnect: no viable connection path for" << userName();
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
        qDebug() << "connect: failed to create socket for" << userName();
        delete reqSocket;
        m_connectingState = ConnectingState::None;
        return;
    }

    setSocket(reqSocket);

    // Register with ListenSocket for connection tracking
    if (theApp.listenSocket)
        theApp.listenSocket->addSocket(reqSocket);

    // Connect socket signals
    QObject::connect(reqSocket, &ClientReqSocket::clientDisconnected,
                     this, [this](const QString& reason) {
        disconnected(reason, true);
    });

    // Initiate TCP connection
    const QHostAddress addr(ntohl(m_connectIP));
    reqSocket->connectToHost(addr, m_userPort);
    reqSocket->waitForOnConnect();

    m_connectingState = ConnectingState::DirectTCP;
}

// ===========================================================================
// connectionEstablished — MFC BaseClient.cpp:1499-1581
// ===========================================================================

void UpDownClient::connectionEstablished()
{
    m_connectingState = ConnectingState::None;

    // Flush waiting packets
    for (auto& packet : m_waitingPackets) {
        if (m_socket)
            m_socket->sendPacket(std::move(packet));
    }
    m_waitingPackets.clear();

    // Handle state transitions based on what we were trying to do
    if (m_downloadState == DownloadState::Connecting) {
        setDownloadState(DownloadState::Connected);
    }

    if (m_uploadState == UploadState::Connecting) {
        // Send hello if needed
        if (!checkHandshakeFinished()) {
            sendHelloPacket();
        }
    }

    // Kademlia state handling
    switch (m_kadState) {
    case KadState::ConnectingFwCheck:
        setKadState(KadState::ConnectedFwCheck);
        break;
    case KadState::ConnectingBuddy:
    case KadState::IncomingBuddy:
        setKadState(KadState::ConnectedBuddy);
        break;
    case KadState::ConnectingFwCheckUDP:
        setKadState(KadState::FwCheckUDP);
        sendFirewallCheckUDPRequest();
        break;
    default:
        break;
    }

    // Chat UI callback
    if (m_chatState == ChatState::Connecting) {
        setChatState(ChatState::Chatting);
        emit chatStateChanged();
    }
}

// ===========================================================================
// disconnected — MFC BaseClient.cpp:1101-1233
// ===========================================================================

bool UpDownClient::disconnected(const QString& reason, bool fromSocket)
{
    Q_UNUSED(fromSocket);

    qDebug() << "Client disconnected:" << userName() << "reason:" << reason;

    m_connectingState = ConnectingState::None;

    // Save session stats
    if (m_uploadState == UploadState::Uploading) {
        setUploadState(UploadState::None);
        if (theApp.uploadQueue)
            theApp.uploadQueue->removeFromUploadQueue(this);
    } else if (m_uploadState == UploadState::Connecting) {
        setUploadState(UploadState::None);
    }

    if (m_downloadState == DownloadState::Downloading ||
        m_downloadState == DownloadState::Connected ||
        m_downloadState == DownloadState::Connecting ||
        m_downloadState == DownloadState::WaitCallback ||
        m_downloadState == DownloadState::WaitCallbackKad ||
        m_downloadState == DownloadState::ReqHashSet)
    {
        // Add to dead source list
        if (theApp.clientList) {
            DeadSourceKey key;
            key.hash = m_userHash;
            key.serverIP = m_serverIP;
            key.userID = m_userIDHybrid;
            key.port = m_userPort;
            key.kadPort = m_kadPort;
            theApp.clientList->globalDeadSourceList.addDeadSource(key, hasLowID());
        }
        setDownloadState(DownloadState::None);
    }

    // Clear pending block requests on disconnect
    clearDownloadBlockRequests();

    // Reset chat state
    if (m_chatState == ChatState::Connecting)
        setChatState(ChatState::UnableToConnect);

    // Update friend's last-seen info
    if (m_friend) {
        m_friend->setLastUsedIP(m_connectIP);
        m_friend->setLastUsedPort(m_userPort);
        m_friend->setLastSeen(std::time(nullptr));
    }

    // GUI refresh
    emit updateDisplayedInfoRequested();

    m_sentCancelTransfer = false;

    return true;
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
    m_aichRequested = false;
    m_hashsetRequestingMD4 = false;
    m_hashsetRequestingAICH = false;
}

// ===========================================================================
// infoPacketsReceived — called after handshake complete
// ===========================================================================

void UpDownClient::onInfoPacketsReceived()
{
    if (m_supportSecIdent != 0 && m_credits) {
        sendSecIdentStatePacket();
    }
    m_failedFileIdReqs = 0;
}

// ===========================================================================
// isBanned
// ===========================================================================

bool UpDownClient::isBanned() const
{
    if (theApp.clientList && theApp.clientList->isBannedClient(m_connectIP))
        return true;
    return m_uploadState == UploadState::Banned;
}

// ===========================================================================
// processEmuleQueueRank
// ===========================================================================

void UpDownClient::processEmuleQueueRank(const uint8* data, uint32 size)
{
    if (size < 2)
        return;

    SafeMemFile file(data, size);
    const uint16 rank = file.readUInt16();
    setRemoteQueueRank(rank);
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
    setRemoteQueueRank(rank);
    checkQueueRankFlood();
}

// ===========================================================================
// checkQueueRankFlood
// ===========================================================================

void UpDownClient::checkQueueRankFlood()
{
    m_unaskQueueRankRecv++;
    if (m_unaskQueueRankRecv >= 3) {
        m_unaskQueueRankRecv = 0;
        // Possible flood — log warning
        qDebug() << "Queue rank flood detected from" << userName();
    }
}

// ===========================================================================
// requestSharedFileList
// ===========================================================================

void UpDownClient::requestSharedFileList()
{
    if (!m_socket)
        return;

    if (m_noViewSharedFiles) {
        qDebug() << "Client" << userName() << "doesn't allow viewing shared files";
        return;
    }

    auto packet = std::make_unique<Packet>(OP_ASKSHAREDFILES, 0);
    packet->prot = OP_EDONKEYPROT;
    sendPacket(std::move(packet));
    m_fileListRequested++;
}

// ===========================================================================
// processSharedFileList
// ===========================================================================

void UpDownClient::processSharedFileList(const uint8* data, uint32 size, const QString& dir)
{
    Q_UNUSED(dir);

    if (m_fileListRequested == 0) {
        qDebug() << "processSharedFileList: unrequested response from" << userName();
        return;
    }

    m_fileListRequested = 0;

    if (!data || size == 0)
        return;

    // Process the shared file list through SearchList
    if (theApp.searchList) {
        // Process as a TCP search result packet (shared file list has the same format)
        theApp.searchList->processSearchAnswer(data, size,
                                                m_unicodeSupport,
                                                m_serverIP, m_serverPort);
    }
}

// ===========================================================================
// checkFailedFileIdReqs
// ===========================================================================

void UpDownClient::checkFailedFileIdReqs(const uint8* fileHash)
{
    Q_UNUSED(fileHash);

    m_failedFileIdReqs++;
    if (m_failedFileIdReqs > BADCLIENTBAN) {
        ban(QStringLiteral("Too many failed file ID requests"));
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
    if (size < 4)
        return;

    SafeMemFile file(data, size);
    const uint32 ip = file.readUInt32();

    thePrefs.setPublicIP(ip);
    m_needOurPublicIP = false;
}

// ===========================================================================
// sendSharedDirectories
// ===========================================================================

void UpDownClient::sendSharedDirectories()
{
    if (!m_socket)
        return;

    // Send empty shared directories response
    SafeMemFile data;
    data.writeUInt32(0); // 0 directories

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
        sendPacket(std::move(packet));
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
// shouldReceiveCryptUDPPackets
// ===========================================================================

bool UpDownClient::shouldReceiveCryptUDPPackets() const
{
    return m_supportsCryptLayer && m_kadVersion >= KADEMLIA_VERSION8_49b;
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

QString UpDownClient::uploadStateDisplayString() const
{
    return dbgGetUploadState(m_uploadState);
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
    packet->pBuffer[0] = keyLen;
    std::memcpy(packet->pBuffer + 1, theApp.clientCredits->publicKey(), keyLen);
    sendPacket(std::move(packet));

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

    // Signature requires a valid challenge from the peer
    if (m_credits->cryptRndChallengeFrom == 0) {
        qDebug() << "sendSignaturePacket: no challenge available for" << userName();
        return;
    }

    // Determine signature version and IP challenge
    uint32 challengeIP = 0;
    uint8 chaIPKind = kCryptCipNoneClient;

    // v2 signatures include IP to prevent replay across IPs
    if (m_supportSecIdent > 1) {
        if (theApp.serverConnect) {
            uint32 myID = theApp.serverConnect->clientID();
            if (myID == 0 || theApp.serverConnect->isLowID()) {
                challengeIP = theApp.serverConnect->localIP();
                chaIPKind = kCryptCipLocalClient;
            } else {
                challengeIP = myID;
                chaIPKind = kCryptCipRemoteClient;
            }
        }
    }

    uint8 sig[200];
    uint8 sigLen = theApp.clientCredits->createSignature(
        m_credits, sig, sizeof(sig), challengeIP, chaIPKind);

    if (sigLen == 0) {
        qDebug() << "sendSignaturePacket: signature creation failed for" << userName();
        return;
    }

    // Build OP_SIGNATURE packet: [sigLen:1][sigData:sigLen] + optional [ipKind:1] for v2
    const uint32 packetSize = (chaIPKind != kCryptCipNoneClient) ? 1 + sigLen + 1 : 1 + sigLen;
    auto packet = std::make_unique<Packet>(OP_SIGNATURE, packetSize);
    packet->prot = OP_EMULEPROT;
    packet->pBuffer[0] = sigLen;
    std::memcpy(packet->pBuffer + 1, sig, sigLen);

    if (chaIPKind != kCryptCipNoneClient)
        packet->pBuffer[1 + sigLen] = chaIPKind;

    sendPacket(std::move(packet));
    m_secureIdentState = SecureIdentState::AllRequestsSend;
}

// ===========================================================================
// processPublicKeyPacket
// ===========================================================================

void UpDownClient::processPublicKeyPacket(const uint8* data, uint32 size)
{
    if (!data || size < 1 || !m_credits)
        return;

    const uint8 keyLen = data[0];
    if (keyLen + 1 > size || keyLen == 0)
        return;

    m_credits->setSecureIdent(data + 1, keyLen);
}

// ===========================================================================
// processSignaturePacket
// ===========================================================================

void UpDownClient::processSignaturePacket(const uint8* data, uint32 size)
{
    if (!data || size < 1 || !m_credits)
        return;

    // Prevent duplicate signatures from the same IP
    if (m_lastSignatureIP == m_connectIP) {
        qDebug() << "processSignaturePacket: duplicate signature from" << userName();
        return;
    }

    const uint8 sigLen = data[0];
    if (sigLen + 1 > size || sigLen == 0 || sigLen > 200)
        return;

    // Must have their public key
    if (m_credits->secIDKeyLen() == 0) {
        qDebug() << "processSignaturePacket: no public key stored for" << userName();
        return;
    }

    // Must have a valid challenge
    if (m_credits->cryptRndChallengeFor == 0) {
        qDebug() << "processSignaturePacket: no challenge for" << userName();
        return;
    }

    m_lastSignatureIP = m_connectIP;

    if (!theApp.clientCredits || !theApp.clientCredits->cryptoAvailable())
        return;

    // Determine signature version from packet: v2 has an extra byte after signature
    uint8 chaIPKind = kCryptCipNoneClient;
    if (static_cast<uint32>(sigLen + 1 + 1) <= size)
        chaIPKind = data[1 + sigLen];

    theApp.clientCredits->verifyIdent(m_credits, data + 1, sigLen, m_connectIP, chaIPKind);
}

// ===========================================================================
// sendSecIdentStatePacket
// ===========================================================================

void UpDownClient::sendSecIdentStatePacket()
{
    if (!m_socket || !extProtocolAvailable())
        return;

    SafeMemFile data;

    uint8 state;
    if (!m_credits) {
        state = static_cast<uint8>(SecureIdentState::Unavailable);
    } else if (m_credits->currentIdentState(m_connectIP) == IdentState::Identified) {
        state = static_cast<uint8>(SecureIdentState::AllRequestsSend);
    } else if (m_credits->secIDKeyLen() > 0) {
        // We already have their public key, just need a signature
        state = static_cast<uint8>(SecureIdentState::SignatureNeeded);
    } else {
        // We need both their public key and a signature
        state = static_cast<uint8>(SecureIdentState::KeyAndSigNeeded);
    }

    data.writeUInt8(state);

    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_SECIDENTSTATE);
    sendPacket(std::move(packet));
}

// ===========================================================================
// processSecIdentStatePacket
// ===========================================================================

void UpDownClient::processSecIdentStatePacket(const uint8* data, uint32 size)
{
    if (!data || size < 1)
        return;

    const uint8 state = data[0];

    switch (state) {
    case static_cast<uint8>(SecureIdentState::KeyAndSigNeeded):
        sendPublicKeyPacket();
        sendSignaturePacket();
        break;
    case static_cast<uint8>(SecureIdentState::SignatureNeeded):
        sendSignaturePacket();
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

    const IdentState state = m_credits->currentIdentState(m_connectIP);
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
    Q_UNUSED(length);

    const QString message = data.readString(true);

    if (message.isEmpty())
        return;

    // Apply filters
    if (m_isSpammer) {
        qDebug() << "Chat message from spammer" << userName() << "blocked";
        return;
    }

    // Rate limit: max 255 messages
    if (m_messagesReceived >= 255)
        return;

    incMessagesReceived();

    // Friends-only filter
    if (thePrefs.msgOnlyFriends() && !m_friend) {
        qDebug() << "Chat message from non-friend" << userName() << "blocked (msgOnlyFriends)";
        return;
    }

    // Secure-only filter
    if (thePrefs.msgSecure() && !hasPassedSecureIdent(false)) {
        qDebug() << "Chat message from unverified" << userName() << "blocked (msgSecure)";
        return;
    }

    // Basic keyword spam filter
    static const QStringList spamKeywords = {
        QStringLiteral("http://"), QStringLiteral("https://"),
        QStringLiteral("www."), QStringLiteral(".com/"),
        QStringLiteral("download free"), QStringLiteral("click here"),
    };
    for (const auto& keyword : spamKeywords) {
        if (message.contains(keyword, Qt::CaseInsensitive)) {
            m_isSpammer = true;
            qDebug() << "Spam detected from" << userName() << ":" << message;
            return;
        }
    }

    // Captcha challenge for first message from unknown clients
    if (m_supportsCaptcha && m_messagesReceived == 1 && !m_friend
        && m_chatCaptchaState == ChatCaptchaState::None)
    {
        // Generate a simple captcha image using Qt
        const QString captchaText = generateCaptchaText();
        m_captchaChallenge = captchaText;
        m_captchaPendingMsg = message;

        QImage captchaImg = generateCaptchaImage(captchaText);
        if (!captchaImg.isNull()) {
            // Encode as BMP for wire format (eMule captcha uses BMP)
            QByteArray bmpData;
            QBuffer buffer(&bmpData);
            buffer.open(QIODevice::WriteOnly);
            captchaImg.save(&buffer, "BMP");
            buffer.close();

            if (!bmpData.isEmpty()) {
                SafeMemFile data;
                // Write captcha tag with BMP data
                data.writeUInt8(static_cast<uint8>(bmpData.size() & 0xFF));
                data.write(bmpData.constData(), bmpData.size());

                auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_CHATCAPTCHAREQ);
                sendPacket(std::move(packet));
                m_chatCaptchaState = ChatCaptchaState::ChallengeSent;
                return;
            }
        }
    }

    // GUI display
    emit chatMessageReceived(m_username, message);
}

// ===========================================================================
// sendChatMessage
// ===========================================================================

void UpDownClient::sendChatMessage(const QString& message)
{
    if (!m_socket || message.isEmpty())
        return;

    // Handle captcha state machine
    if (m_chatCaptchaState == ChatCaptchaState::CaptchaRecv) {
        m_chatCaptchaState = ChatCaptchaState::SolutionSent;
    }

    SafeMemFile data;
    const QByteArray utf8 = message.toUtf8();
    data.writeUInt16(static_cast<uint16>(utf8.size()));
    data.write(utf8.constData(), utf8.size());

    auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_MESSAGE);
    sendPacket(std::move(packet));

    incMessagesSent();
}

// ===========================================================================
// processCaptchaRequest
// ===========================================================================

void UpDownClient::processCaptchaRequest(SafeMemFile& data)
{
    if (m_chatCaptchaState != ChatCaptchaState::None) {
        m_chatCaptchaState = ChatCaptchaState::None;
        return;
    }

    // Read BMP image data size
    const uint32 imgSize = data.readUInt8();
    if (imgSize == 0 || imgSize > 4096) {
        qDebug() << "processCaptchaRequest: invalid image size" << imgSize;
        m_chatCaptchaState = ChatCaptchaState::None;
        return;
    }

    // Read image data
    std::vector<uint8> imgData(imgSize);
    data.read(imgData.data(), imgSize);

    // Decode BMP using QImage
    QImage captchaImg;
    if (!captchaImg.loadFromData(imgData.data(), imgSize, "BMP")) {
        qDebug() << "processCaptchaRequest: failed to decode BMP captcha";
        m_chatCaptchaState = ChatCaptchaState::None;
        return;
    }

    // Validate image dimensions (reasonable captcha size)
    if (captchaImg.height() < 10 || captchaImg.height() > 50
        || captchaImg.width() < 10 || captchaImg.width() > 150)
    {
        qDebug() << "processCaptchaRequest: invalid captcha dimensions"
                 << captchaImg.width() << "x" << captchaImg.height();
        m_chatCaptchaState = ChatCaptchaState::None;
        return;
    }

    m_chatCaptchaState = ChatCaptchaState::CaptchaRecv;

    // Emit signal so the GUI can show the captcha to the user
    emit captchaRequestReceived(m_username, captchaImg);
}

// ===========================================================================
// processCaptchaReqRes
// ===========================================================================

void UpDownClient::processCaptchaReqRes(uint8 status)
{
    switch (status) {
    case 0: // Captcha solved correctly
        m_chatCaptchaState = ChatCaptchaState::CaptchaSolved;
        break;
    default: // Captcha failed
        m_chatCaptchaState = ChatCaptchaState::None;
        m_captchasSent++;
        break;
    }
}

// ===========================================================================
// Phase 3 — Preview
// Ported from MFC BaseClient.cpp lines 2059-2167
// ===========================================================================

// ===========================================================================
// sendPreviewRequest
// ===========================================================================

void UpDownClient::sendPreviewRequest(const AbstractFile& file)
{
    if (!m_socket || !m_supportsPreview)
        return;

    if (m_previewReqPending)
        return;

    m_previewReqPending = true;

    // Build and send OP_REQUESTPREVIEW with file hash
    SafeMemFile data;
    data.writeHash16(file.fileHash());

    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_REQUESTPREVIEW);
    sendPacket(std::move(packet));
}

// ===========================================================================
// sendPreviewAnswer
// ===========================================================================

void UpDownClient::sendPreviewAnswer(const KnownFile* file)
{
    if (!m_socket)
        return;

    SafeMemFile data;
    data.writeHash16(m_reqUpFileId.data());

    if (!file || file->filePath().isEmpty()) {
        // No file or no path — send empty preview
        data.writeUInt8(0);
        auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_PREVIEWANSWER);
        sendPacket(std::move(packet));
        return;
    }

    // Try to generate preview frames from the file using Qt
    // For video files, we could use QMediaPlayer in the future.
    // For now, attempt to load as an image file (covers image previews).
    QImage previewImg(file->filePath());
    if (previewImg.isNull()) {
        // Not an image file or failed to load — send empty preview
        data.writeUInt8(0);
        auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_PREVIEWANSWER);
        sendPacket(std::move(packet));
        return;
    }

    // Scale to reasonable preview size
    previewImg = previewImg.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Encode as PNG
    QByteArray pngData;
    QBuffer buffer(&pngData);
    buffer.open(QIODevice::WriteOnly);
    previewImg.save(&buffer, "PNG");
    buffer.close();

    if (pngData.isEmpty()) {
        data.writeUInt8(0);
        auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_PREVIEWANSWER);
        sendPacket(std::move(packet));
        return;
    }

    // Write 1 frame
    data.writeUInt8(1);
    data.writeUInt32(static_cast<uint32>(pngData.size()));
    data.write(pngData.constData(), pngData.size());

    auto packet = std::make_unique<Packet>(data, OP_EMULEPROT, OP_PREVIEWANSWER);
    sendPacket(std::move(packet));
}

// ===========================================================================
// processPreviewReq
// ===========================================================================

void UpDownClient::processPreviewReq(const uint8* data, uint32 size)
{
    if (!data || size < 16)
        return;

    // Look up file in shared files by hash
    KnownFile* file = nullptr;
    if (theApp.sharedFileList)
        file = theApp.sharedFileList->getFileByID(data);

    // Send preview answer (possibly empty if file not found)
    sendPreviewAnswer(file);
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

    const uint8 frameCount = file.readUInt8();
    if (frameCount == 0) {
        qDebug() << "processPreviewAnswer: remote sent 0 frames";
        return;
    }

    // Look up the search file for this hash
    SearchFile* searchFile = nullptr;
    if (theApp.searchList)
        searchFile = theApp.searchList->searchFileByHash(fileHash.data(), m_searchID);

    std::vector<QImage> previewImages;
    previewImages.reserve(frameCount);

    for (uint8 i = 0; i < frameCount; ++i) {
        const uint32 imgSize = file.readUInt32();
        if (imgSize == 0 || imgSize > size) {
            qDebug() << "processPreviewAnswer: frame" << i << "size exceeds packet";
            break;
        }

        std::vector<uint8> imgData(imgSize);
        file.read(imgData.data(), imgSize);

        // Decode PNG using QImage (replaces CxImage)
        QImage image;
        if (image.loadFromData(imgData.data(), imgSize, "PNG") && !image.isNull()) {
            previewImages.push_back(std::move(image));
        } else {
            qDebug() << "processPreviewAnswer: failed to decode frame" << i;
        }
    }

    if (!previewImages.empty())
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
    if (m_kadState != KadState::FwCheckUDP)
        return;

    if (!kad::Kademlia::instance() || !kad::Kademlia::instance()->isRunning()) {
        setKadState(KadState::None);
        return;
    }

    // Cancel if the client has other active connections or insufficient Kad version
    if (m_uploadState != UploadState::None || m_downloadState != DownloadState::None
        || m_chatState != ChatState::None
        || m_kadVersion <= KADEMLIA_VERSION5_48a || kadPort() == 0)
    {
        kad::UDPFirewallTester::setUDPFWCheckResult(false, true, ntohl(m_connectIP), 0);
        setKadState(KadState::None);
        return;
    }

    auto* kadPrefs = kad::Kademlia::getInstancePrefs();
    if (!kadPrefs) {
        setKadState(KadState::None);
        return;
    }

    SafeMemFile data;
    data.writeUInt16(kadPrefs->internKadPort());
    data.writeUInt16(kadPrefs->externalKadPort());
    data.writeUInt32(kad::KadPrefs::getUDPVerifyKey(m_connectIP));

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
        qDebug() << "processFirewallCheckUDPRequest: Kad not running, ignoring";
        return;
    }

    // If we already know this client, the result might be biased
    const bool errorAlreadyKnown =
        m_uploadState != UploadState::None
        || m_downloadState != DownloadState::None
        || m_chatState != ChatState::None
        || (kad::Kademlia::getInstanceRoutingZone()
            && kad::Kademlia::getInstanceRoutingZone()->getContact(ntohl(m_connectIP), 0, false) != nullptr);

    const uint16 remoteInternPort = data.readUInt16();
    const uint16 remoteExternPort = data.readUInt16();
    const uint32 senderKey = data.readUInt32();

    if (remoteInternPort == 0) {
        qDebug() << "processFirewallCheckUDPRequest: intern port == 0";
        return;
    }

    if (senderKey == 0)
        qDebug() << "processFirewallCheckUDPRequest: sender key == 0";

    auto* udpListener = kadInstance->getUDPListener();

    // Send test packet to internal port
    SafeMemFile testPacket1;
    testPacket1.writeUInt8(static_cast<uint8>(errorAlreadyKnown ? 1 : 0));
    testPacket1.writeUInt16(remoteInternPort);
    udpListener->sendPacket(testPacket1, KADEMLIA2_FIREWALLUDP, ntohl(m_connectIP),
                            remoteInternPort,
                            kad::KadUDPKey(senderKey, thePrefs.publicIP()), nullptr);

    // If external port differs, test that too (PAT router scenario)
    if (remoteExternPort != 0 && remoteExternPort != remoteInternPort) {
        SafeMemFile testPacket2;
        testPacket2.writeUInt8(static_cast<uint8>(errorAlreadyKnown ? 1 : 0));
        testPacket2.writeUInt16(remoteExternPort);
        udpListener->sendPacket(testPacket2, KADEMLIA2_FIREWALLUDP, ntohl(m_connectIP),
                                remoteExternPort,
                                kad::KadUDPKey(senderKey, thePrefs.publicIP()), nullptr);
    }

    qDebug() << "Answered UDP firewall check request from" << dbgGetClientInfo();
}

// ===========================================================================
// Private helpers
// ===========================================================================

// ===========================================================================
// generateCaptchaText — random 4-character alphanumeric string
// ===========================================================================

QString UpDownClient::generateCaptchaText()
{
    static constexpr char chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    static constexpr int charCount = sizeof(chars) - 1;

    QString text;
    text.reserve(4);
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < 4; ++i)
        text.append(QChar::fromLatin1(chars[rng->bounded(charCount)]));
    return text;
}

// ===========================================================================
// generateCaptchaImage — renders captcha text onto a small BMP-compatible image
// ===========================================================================

QImage UpDownClient::generateCaptchaImage(const QString& text)
{
    constexpr int width = 120;
    constexpr int height = 40;

    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    if (!painter.isActive())
        return {};

    auto* rng = QRandomGenerator::global();

    // Draw noise lines
    for (int i = 0; i < 6; ++i) {
        QPen pen(QColor::fromRgb(rng->bounded(200), rng->bounded(200), rng->bounded(200)));
        pen.setWidth(1);
        painter.setPen(pen);
        painter.drawLine(rng->bounded(width), rng->bounded(height),
                         rng->bounded(width), rng->bounded(height));
    }

    // Draw each character with slight rotation and offset
    QFont font(QStringLiteral("Arial"), 20, QFont::Bold);
    painter.setFont(font);

    const int charWidth = width / (text.length() + 1);
    for (int i = 0; i < text.length(); ++i) {
        painter.save();
        const int x = charWidth * (i + 1) - charWidth / 2;
        const int y = height / 2 + rng->bounded(10) - 5;
        painter.translate(x, y);
        painter.rotate(rng->bounded(30) - 15);
        painter.setPen(QColor::fromRgb(rng->bounded(100), rng->bounded(100), rng->bounded(100)));
        painter.drawText(0, 0, text.mid(i, 1));
        painter.restore();
    }

    // Draw noise dots
    for (int i = 0; i < 40; ++i) {
        painter.setPen(QColor::fromRgb(rng->bounded(256), rng->bounded(256), rng->bounded(256)));
        painter.drawPoint(rng->bounded(width), rng->bounded(height));
    }

    painter.end();
    return image;
}

} // namespace eMule
