#pragma once

/// @file UpDownClient.h
/// @brief Network peer representation — identity, state, handshake, transfer.
///
/// Ported from MFC CUpDownClient (srchybrid/UpDownClient.h).
/// Phase 1: identity, state management, Compare(), version detection.
/// Phase 2: hello handshake (OP_Hello/OP_HelloAnswer), eMule info exchange,
///          comment packet, and handshake helpers.
/// Phase 3: connection management, secure identity, chat, preview, firewall,
///          upload methods, download methods.

#include "client/ClientStateDefs.h"
#include "client/ClientStructs.h"
#include "net/Address.h"
#include "utils/Types.h"

#include <QCborArray>
#include <QImage>
#include <QObject>
#include <QString>
#include <QByteArray>

#include <array>
#include <deque>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

namespace eMule {

// Forward declarations
class AICHHash;
class ClientCredits;
class ClientReqSocket;
class EMSocket;
class Friend;
class KnownFile;
class PartFile;
class AbstractFile;
class Packet;
class SafeMemFile;

// ---------------------------------------------------------------------------
// Supporting types
// ---------------------------------------------------------------------------

/// Build a comparable version number from major.minor.update.
/// Matches MFC MAKE_CLIENT_VERSION(mjr, min, upd).
constexpr uint32 makeClientVersion(uint32 major, uint32 minor, uint32 update)
{
    return ((major * 100 + minor) * 10 + update) * 100;
}

// ---------------------------------------------------------------------------
// UpDownClient
// ---------------------------------------------------------------------------

class UpDownClient : public QObject {
    Q_OBJECT

public:
    // -- Construction / destruction ------------------------------------------

    explicit UpDownClient(QObject* parent = nullptr);

    /// Construct from a source: port, user ID, server address, ed2kID flag.
    /// Matches MFC CUpDownClient(CPartFile*, uint16, uint32, uint32, uint16, bool).
    UpDownClient(uint16 port, uint32 userId, uint32 serverIP,
                 uint16 serverPort, PartFile* reqFile = nullptr,
                 bool ed2kID = false, QObject* parent = nullptr);

    ~UpDownClient() override;

    // Non-copyable (QObject)
    UpDownClient(const UpDownClient&) = delete;
    UpDownClient& operator=(const UpDownClient&) = delete;

    // -- Identity getters / setters -----------------------------------------

    [[nodiscard]] const uint8* userHash() const { return m_userHash.data(); }
    void setUserHash(const uint8* hash);
    [[nodiscard]] bool hasValidHash() const;
    [[nodiscard]] int hashType() const;

    [[nodiscard]] const QString& userName() const { return m_username; }
    void setUserName(const QString& name) { m_username = name; }

    // Address-based accessors
    [[nodiscard]] const Address& userAddress() const    { return m_userAddress; }
    [[nodiscard]] const Address& connectAddress() const { return m_connectAddress; }
    [[nodiscard]] const Address& serverAddress() const  { return m_serverAddress; }
    [[nodiscard]] const Address& buddyAddress() const   { return m_buddyAddress; }

    void setUserAddress(const Address& a)    { m_userAddress = a; m_connectAddress = a; }
    void setConnectAddress(const Address& a) { m_connectAddress = a; }
    void setServerAddress(const Address& a)  { m_serverAddress = a; }
    void setBuddyAddress(const Address& a)   { m_buddyAddress = a; }

    // IPv6 — the peer's advertised public IPv6 (from CT_MOD_IP_V6 in hello / SX / Kad).
    // m_openIPv6 latches true once we learn a reachable IPv6, letting the connection
    // logic prefer IPv6 even for an IPv4-LowID peer.
    [[nodiscard]] const Address& userIPv6() const  { return m_userIPv6; }
    void setUserIPv6(const Address& a)             { m_userIPv6 = a; }
    [[nodiscard]] const Address& buddyIPv6() const { return m_buddyIPv6; }
    void setBuddyIPv6(const Address& a)            { m_buddyIPv6 = a; }
    [[nodiscard]] bool openIPv6() const            { return m_openIPv6; }
    void setOpenIPv6(bool v)                       { m_openIPv6 = v; }
    [[nodiscard]] bool supportsIPv6() const        { return m_supportsIPv6; }
    [[nodiscard]] bool supportsExtendedXS() const  { return m_supportsExtendedXS; }
    void setSupportsExtendedXS(bool s)             { m_supportsExtendedXS = s; }
    /// Peer promises to skip ExtSX tags it does not know rather than discarding the whole
    /// answer, so it is safe to send it CT_EMULE_USERHASH / CT_EMULE_CONOPTS.
    [[nodiscard]] bool supportsExtSXSkipTags() const { return m_supportsExtSXSkipTags; }
    void setSupportsExtSXSkipTags(bool s)            { m_supportsExtSXSkipTags = s; }

    /// Our own public IPv6 changed — remember to tell this peer. Deliberately lazy: the
    /// packet goes out the next time we walk this client anyway (upload queue or source
    /// list), so a temporary-address rotation cannot fan a write out to every open socket
    /// at once. Mirrors the compatibility target's TrigReask/m_bSendIP pair.
    void markSendIPPending()                       { m_sendIPPending = true; }
    [[nodiscard]] bool sendIPPending() const       { return m_sendIPPending; }
    void flushPendingIPChange();

    /// Which family the traffic to this peer actually travels over — the live socket
    /// wins, since that is what carries the data; the addresses we would dial are the
    /// fallback. Distinct from supportsIPv6()/openIPv6(), which only say the peer *has*
    /// an IPv6 we could use. Used for upload-slot fairness and the per-IPv6 queue limit.
    [[nodiscard]] bool isIPv6Connection() const;

    [[nodiscard]] uint32 userIDHybrid() const { return m_userIDHybrid; }
    void setUserIDHybrid(uint32 id) { m_userIDHybrid = id; }
    [[nodiscard]] bool hasLowID() const;

    [[nodiscard]] uint16 userPort() const { return m_userPort; }
    void setUserPort(uint16 port) { m_userPort = port; }

    [[nodiscard]] uint16 serverPort() const { return m_serverPort; }
    void setServerPort(uint16 port) { m_serverPort = port; }

    [[nodiscard]] uint16 udpPort() const { return m_udpPort; }
    void setUDPPort(uint16 port) { m_udpPort = port; }
    [[nodiscard]] uint16 kadPort() const { return m_kadPort; }
    void setKadPort(uint16 port) { m_kadPort = port; }

    [[nodiscard]] uint16 buddyPort() const { return m_buddyPort; }
    void setBuddyPort(uint16 port) { m_buddyPort = port; }

    [[nodiscard]] const uint8* buddyID() const { return m_buddyID.data(); }
    void setBuddyID(const uint8* id);
    [[nodiscard]] bool hasValidBuddyID() const { return m_buddyIDValid; }

    // Socket / credits / friend
    [[nodiscard]] EMSocket* socket() const { return m_socket; }
    void setSocket(EMSocket* s) { m_socket = s; }

    [[nodiscard]] ClientCredits* credits() const { return m_credits; }
    void setCredits(ClientCredits* c) { m_credits = c; }

    [[nodiscard]] Friend* friendPtr() const { return m_friend; }
    void setFriendPtr(Friend* f) { m_friend = f; }

    // -- State machine getters / setters ------------------------------------

    [[nodiscard]] ClientSoftware clientSoft() const { return m_clientSoft; }

    [[nodiscard]] UploadState uploadState() const { return m_uploadState; }
    void setUploadState(UploadState state);

    [[nodiscard]] DownloadState downloadState() const { return m_downloadState; }
    [[nodiscard]] bool isDownloading() const { return m_downloadState == DownloadState::Downloading; }
    [[nodiscard]] uint32 downStartTime() const { return m_downStartTime; }
    void setDownloadState(DownloadState state);

    [[nodiscard]] ChatState chatState() const { return m_chatState; }
    void setChatState(ChatState state) { m_chatState = state; }

    [[nodiscard]] KadState kadState() const { return m_kadState; }
    void setKadState(KadState state) { m_kadState = state; }

    [[nodiscard]] SecureIdentState secureIdentState() const { return m_secureIdentState; }
    void setSecureIdentState(SecureIdentState state) { m_secureIdentState = state; }

    [[nodiscard]] SourceFrom sourceFrom() const { return m_sourceFrom; }
    void setSourceFrom(SourceFrom src) { m_sourceFrom = src; }

    [[nodiscard]] ChatCaptchaState chatCaptchaState() const { return m_chatCaptchaState; }
    void setChatCaptchaState(ChatCaptchaState state) { m_chatCaptchaState = state; }

    [[nodiscard]] ConnectingState connectingState() const { return m_connectingState; }
    void setConnectingState(ConnectingState state) { m_connectingState = state; }

    [[nodiscard]] InfoPacketState infoPacketsReceived() const { return m_infoPacketsReceived; }
    void setInfoPacketsReceived(InfoPacketState state) { m_infoPacketsReceived = state; }

    // -- Protocol capability queries ----------------------------------------

    [[nodiscard]] bool isEmuleClient() const { return m_emuleProtocol || hashType() == static_cast<int>(ClientSoftware::eMule); }
    [[nodiscard]] bool extProtocolAvailable() const { return m_emuleProtocol; }
    [[nodiscard]] bool supportMultiPacket() const {
        return m_multiPacket && !m_testDisableMultiPacket;
    }
    [[nodiscard]] bool supportExtMultiPacket() const {
        return m_extMultiPacket && !m_testDisableMultiPacket;
    }
    [[nodiscard]] bool supportsLargeFiles() const { return m_supportsLargeFiles; }
    [[nodiscard]] bool supportsFileIdentifiers() const {
        return m_supportsFileIdent && !m_testDisableMultiPacket;
    }
    void setTestDisableMultiPacket(bool disable) { m_testDisableMultiPacket = disable; }
    /// Peer understands OP_ASKSHAREDDIRS, i.e. its shares can be browsed per directory
    /// rather than as one flat list. Set from the hello for hybrids and eMule >= 0.28.
    [[nodiscard]] bool supportsSharedDirectories() const { return m_sharedDirectories; }
    void setSupportsSharedDirectories(bool v) { m_sharedDirectories = v; }
    [[nodiscard]] bool supportsUDP() const { return m_udpPort != 0 && m_udpVer != 0; }
    [[nodiscard]] bool supportsCryptLayer() const { return m_supportsCryptLayer; }
    [[nodiscard]] bool requestsCryptLayer() const { return m_requestsCryptLayer; }
    [[nodiscard]] bool requiresCryptLayer() const { return m_requiresCryptLayer; }
    /// A direct UDP callback needs a reachable Kad endpoint and a hash to address it
    /// with; without both the callback goes nowhere and we'd just stall on it.
    [[nodiscard]] bool supportsDirectUDPCallback() const
    {
        return m_directUDPCallback && hasValidHash() && m_kadPort != 0;
    }
    [[nodiscard]] bool unicodeSupport() const { return m_unicodeSupport; }

    [[nodiscard]] uint8 dataCompVer() const { return m_dataCompVer; }
    [[nodiscard]] uint8 udpVer() const { return m_udpVer; }
    [[nodiscard]] uint8 sourceExchange1Ver() const { return m_sourceExchange1Ver; }
    void setSourceExchange1Ver(uint8 v) { m_sourceExchange1Ver = v; }
    [[nodiscard]] bool supportsSourceExchange2() const { return m_supportsSourceEx2; }
    void setSupportsSourceExchange2(bool s) { m_supportsSourceEx2 = s; }
    [[nodiscard]] uint8 extendedRequestsVer() const { return m_extendedRequestsVer; }
    [[nodiscard]] uint8 acceptCommentVer() const { return m_acceptCommentVer; }
    [[nodiscard]] uint8 compatibleClient() const { return m_compatibleClient; }
    [[nodiscard]] uint8 kadVersion() const { return m_kadVersion; }
    void setKadVersion(uint8 v) { m_kadVersion = v; }
    [[nodiscard]] uint8 emuleVersion() const { return m_emuleVersion; }

    // Version fields — set by hello packet processing
    [[nodiscard]] uint32 clientVersion() const { return m_clientVersion; }
    void setEmuleVersion(uint8 v) { m_emuleVersion = v; }
    void setClientVersion(uint32 v) { m_clientVersion = v; }
    void setCompatibleClient(uint8 v) { m_compatibleClient = v; }
    void setIsHybrid(bool v) { m_isHybrid = v; }
    void setIsMLDonkey(bool v) { m_isMLDonkey = v; }
    void setEmuleProtocol(bool v) { m_emuleProtocol = v; }

    /// Decode connect option bits from server callback or Kad.
    void setConnectOptions(uint8 options, bool encryption, bool callback);

    // -- Key methods (Phase 1, fully implemented) ---------------------------

    /// Detect client software type and build version string.
    /// Full port from MFC InitClientSoftwareVersion().
    void initClientSoftwareVersion();

    /// Compare this client with another for duplicate detection.
    /// Full port from MFC CUpDownClient::Compare().
    [[nodiscard]] bool compare(const UpDownClient* other, bool ignoreUserHash = false) const;

    /// Reset protocol fields before processing a new hello packet.
    void clearHelloProperties();

    // -- Session tracking ---------------------------------------------------

    [[nodiscard]] uint64 sessionUp() const { return m_curSessionUp; }
    void resetSessionUp() { m_curSessionUp = 0; }

    [[nodiscard]] uint64 sessionDown() const { return m_curSessionDown; }
    void resetSessionDown() { m_curSessionDown = 0; }

    [[nodiscard]] uint64 sessionPayloadDown() const { return m_curSessionPayloadDown; }

    [[nodiscard]] uint64 queueSessionPayloadUp() const { return m_curQueueSessionPayloadUp; }
    void addQueueSessionPayloadUp(uint64 bytes) { m_curQueueSessionPayloadUp += bytes; }
    void resetQueueSessionPayloadUp() { m_curQueueSessionPayloadUp = 0; }

    [[nodiscard]] uint64 transferredUp() const { return m_transferredUp; }
    [[nodiscard]] uint64 transferredDown() const { return m_transferredDown; }

    // -- Chat / message counting --------------------------------------------

    [[nodiscard]] uint8 messagesReceived() const { return m_messagesReceived; }
    void incMessagesReceived() { if (m_messagesReceived < 255) ++m_messagesReceived; }

    [[nodiscard]] uint8 messagesSent() const { return m_messagesSent; }
    void incMessagesSent() { if (m_messagesSent < 255) ++m_messagesSent; }

    [[nodiscard]] bool isSpammer() const { return m_isSpammer; }
    void setSpammer(bool v) { m_isSpammer = v; }

    [[nodiscard]] bool messageFiltered() const { return m_messageFiltered; }
    void setMessageFiltered(bool v) { m_messageFiltered = v; }

    // -- Upload tracking accessors ------------------------------------------

    [[nodiscard]] uint32 askedCount() const { return m_askedCount; }
    void setAskedCount(uint32 c) { m_askedCount = c; }
    void incAskedCount() { ++m_askedCount; }

    [[nodiscard]] uint32 lastUpRequest() const { return m_lastUpRequest; }
    void setLastUpRequest(uint32 t) { m_lastUpRequest = t; }

    [[nodiscard]] uint32 slotNumber() const { return m_slotNumber; }
    void setSlotNumber(uint32 s) { m_slotNumber = s; }

    [[nodiscard]] const uint8* reqUpFileId() const { return m_reqUpFileId.data(); }
    void setReqUpFileId(const uint8* id);

    [[nodiscard]] uint32 upDatarate() const { return m_upDatarate; }

    [[nodiscard]] uint16 upPartCount() const { return m_upPartCount; }
    [[nodiscard]] const std::vector<uint8>& upPartStatus() const { return m_upPartStatus; }
    void setUpPartStatus(const std::vector<uint8>& status)
    {
        m_upPartStatus = status;
        m_upPartCount = static_cast<uint16>(status.size());
    }
    [[nodiscard]] uint16 upCompleteSourcesCount() const { return m_upCompleteSourcesCount; }
    void setUpCompleteSourcesCount(uint16 c) { m_upCompleteSourcesCount = c; }

    [[nodiscard]] bool collectionUploadSlot() const { return m_collectionUploadSlot; }
    void setCollectionUploadSlot(bool v) { m_collectionUploadSlot = v; }

    [[nodiscard]] uint32 getUpStartTimeDelay() const;

    // -- Download tracking accessors ----------------------------------------

    [[nodiscard]] PartFile* reqFile() const { return m_reqFile; }
    void setReqFile(PartFile* f) { m_reqFile = f; }

    [[nodiscard]] uint32 remoteQueueRank() const { return m_remoteQueueRank; }

    [[nodiscard]] bool remoteQueueFull() const { return m_remoteQueueFull; }
    void setRemoteQueueFull(bool v) { m_remoteQueueFull = v; }

    [[nodiscard]] bool completeSource() const { return m_completeSource; }
    void setCompleteSource(bool v) { m_completeSource = v; }

    [[nodiscard]] uint16 partCount() const { return m_partCount; }
    [[nodiscard]] const QString& clientFilename() const { return m_clientFilename; }
    void setClientFilename(const QString& name) { m_clientFilename = name; }

    [[nodiscard]] uint32 downAskedCount() const { return m_downAskedCount; }
    void incDownAskedCount() { ++m_downAskedCount; }

    // -- Misc accessors -----------------------------------------------------

    [[nodiscard]] bool friendSlot() const { return m_friendSlot; }
    void setFriendSlot(bool v) { m_friendSlot = v; }

    [[nodiscard]] bool gplEvildoer() const { return m_gplEvildoer; }
    void setGPLEvildoer(bool v) { m_gplEvildoer = v; }

    [[nodiscard]] bool helloAnswerPending() const { return m_helloAnswerPending; }
    void setHelloAnswerPending(bool v) { m_helloAnswerPending = v; }

    /// True while this client's current socket was accepted rather than dialled.
    /// The inbound and outbound handshakes differ in ways connectionEstablished() and the
    /// OP_HELLO handler both have to know about: an accepted peer has already sent its
    /// hello (so we must not send one) and will never send us an OP_HELLOANSWER (so a
    /// deferred file request would never fire). Analogous to MFC's `bNewClient`
    /// (srchybrid/ListenSocket.cpp:237).
    [[nodiscard]] bool incomingConnection() const { return m_incomingConnection; }

    [[nodiscard]] bool addNextConnect() const { return m_addNextConnect; }
    void setAddNextConnect(bool v) { m_addNextConnect = v; }

    [[nodiscard]] uint16 lastPartAsked() const { return m_lastPartAsked; }
    void setLastPartAsked(uint16 p) { m_lastPartAsked = p; }

    [[nodiscard]] uint8 supportSecIdent() const { return m_supportSecIdent; }
    void setSupportSecIdent(uint8 v) { m_supportSecIdent = v; }

    [[nodiscard]] const QString& clientSoftwareStr() const { return m_clientSoftwareStr; }
    [[nodiscard]] const QString& modVersion() const { return m_modVersion; }
    void setModVersion(const QString& v) { m_modVersion = v; }

    [[nodiscard]] const QString& fileComment() const { return m_fileComment; }
    void setFileComment(const QString& c) { m_fileComment = c; }
    [[nodiscard]] uint8 fileRating() const { return m_fileRating; }
    void setFileRating(uint8 r) { m_fileRating = r; }
    [[nodiscard]] bool commentDirty() const { return m_commentDirty; }
    void setCommentDirty(bool v) { m_commentDirty = v; }

    [[nodiscard]] uint32 searchID() const { return m_searchID; }
    void setSearchID(uint32 id) { m_searchID = id; }

    [[nodiscard]] int fileListRequested() const { return m_fileListRequested; }
    void setFileListRequested(int v) { m_fileListRequested = v; }

    [[nodiscard]] uint16 showDR() const { return m_showDR; }
    void setShowDR(uint16 v) { m_showDR = v; }

    [[nodiscard]] bool sentCancelTransfer() const { return m_sentCancelTransfer; }

    [[nodiscard]] const std::vector<uint8>& partStatus() const { return m_partStatus; }

    // -- Source exchange timestamps -------------------------------------------

    [[nodiscard]] uint32 lastSourceRequestTime() const { return m_lastSourceRequest; }
    void setLastSourceRequestTime(uint32 t) { m_lastSourceRequest = t; }
    [[nodiscard]] uint32 lastSourceAnswerTime() const { return m_lastSourceAnswer; }
    void setLastSourceAnswerTime(uint32 t) { m_lastSourceAnswer = t; }
    [[nodiscard]] uint32 lastAskedForSourcesTime() const { return m_lastAskedForSources; }
    void setLastAskedForSourcesTime();

    // -- Debug strings ------------------------------------------------------

    [[nodiscard]] QString dbgGetClientInfo(bool formatIP = false) const;
    [[nodiscard]] QString dbgGetFullClientSoftVer() const;
    [[nodiscard]] static QString dbgGetUploadState(UploadState state);
    [[nodiscard]] static QString dbgGetDownloadState(DownloadState state);
    [[nodiscard]] static QString dbgGetKadState(KadState state);

    // Convenience overloads using current state
    [[nodiscard]] QString dbgGetUploadState() const { return dbgGetUploadState(m_uploadState); }
    [[nodiscard]] QString dbgGetDownloadState() const { return dbgGetDownloadState(m_downloadState); }
    [[nodiscard]] QString dbgGetKadState() const { return dbgGetKadState(m_kadState); }

    // -- Phase 2 — hello handshake ------------------------------------------

    /// Process an incoming OP_HELLO packet. Returns true if peer is eMule-compatible.
    bool processHelloPacket(const uint8* data, uint32 size);

    /// Process an incoming OP_HELLOANSWER packet. Returns true if peer is eMule-compatible.
    bool processHelloAnswer(const uint8* data, uint32 size);

    /// Send our OP_HELLO packet to the peer.
    virtual void sendHelloPacket();

    /// Send our OP_HELLOANSWER packet to the peer.
    void sendHelloAnswer();

    // -- Phase 2 — mule info exchange ---------------------------------------

    /// Send our eMule info packet. If answer=true, sends OP_EMULEINFOANSWER.
    void sendMuleInfoPacket(bool answer);

    /// Process an incoming eMule info packet.
    void processMuleInfoPacket(const uint8* data, uint32 size);

    /// Process an incoming file comment packet (OP_FILEDESC).
    void processMuleCommentPacket(const uint8* data, uint32 size);

    // -- Phase 2 — helpers --------------------------------------------------

    /// Send a packet via the client socket. Returns false if no socket.
    bool sendPacket(std::unique_ptr<Packet> packet, bool verifyConnection = false);

    /// Returns true when both eDonkey and eMule info packets have been received.
    [[nodiscard]] bool checkHandshakeFinished() const;

    // -- Phase 3 — connection management ------------------------------------

    virtual bool tryToConnect(bool ignoreMaxCon = false);
    virtual void connectionEstablished();

    /// The three post-hello actions from MFC's ConnectionEstablished
    /// (BaseClient.cpp:1550-1573): the pending re-ask, the upload-slot activation,
    /// and the deferred shared-file-list request.
    ///
    /// This is the other half of connectionEstablished(), split out because the two
    /// run at different moments in this port. MFC calls ConnectionEstablished() only
    /// once the hello exchange has happened (ListenSocket.cpp:229 for OP_HELLOANSWER,
    /// :280 for OP_HELLO, BaseClient.cpp:1278 for a socket that was already up),
    /// whereas connectionEstablished() here is wired to the socket's connected signal
    /// — it runs as soon as TCP is up, which is where it sends OP_HELLO. Running these
    /// three blocks there would put OP_ACCEPTUPLOADREQ and OP_ASKSHAREDFILES on the
    /// wire before our own hello, and would fire the file-list request a second time
    /// when the hello answer arrived. So this is invoked from MFC's three real trigger
    /// points instead.
    void onHandshakeCompleted();

    /// Bootstrap Kad off a peer that just told us its Kad port (MFC
    /// ListenSocket.cpp:289-290, :866, :1057). Must run AFTER the hello has been
    /// parsed — clearHelloProperties() zeroes m_kadPort and m_kadVersion.
    void maybeBootstrapKadFromPeer();

    virtual bool disconnected(const QString& reason, bool fromSocket = false);
    void connect();
    virtual void onSocketConnected(int errorCode);

    /// Wire signal connections for an incoming (already-accepted) socket.
    /// Mirrors the signal wiring in connect() but skips connectToHost/encryption.
    void wireIncomingSocket(ClientReqSocket* socket);

    // -- Phase 3 — protocol utility -----------------------------------------

    void requestSharedFileList();
    void processSharedFileList(const uint8* data, uint32 size, const QString& dir = {});
    void processEmuleQueueRank(const uint8* data, uint32 size);
    void processEdonkeyQueueRank(const uint8* data, uint32 size);
    void checkQueueRankFlood();
    void resetFileStatusInfo();
    void onInfoPacketsReceived();
    [[nodiscard]] bool isBanned() const;
    void checkFailedFileIdReqs(const uint8* fileHash);
    void sendPublicIPRequest();
    void processPublicIPAnswer(const uint8* data, uint32 size);
    /// OP_CHANGE_CLIENT_IP — the peer's new public IPv6, 16 raw bytes, no tag wrapper.
    /// Reachable from both protocol dispatchers; see the opcode comment in Opcodes.h.
    void processChangeClientIP(const uint8* data, uint32 size);
    void sendSharedDirectories();
    bool safeConnectAndSendPacket(std::unique_ptr<Packet> packet);
    [[nodiscard]] bool isObfuscatedConnectionEstablished() const;
    [[nodiscard]] bool shouldReceiveCryptUDPPackets() const;
    [[nodiscard]] uint8 getUnicodeSupport() const;
    [[nodiscard]] QString downloadStateDisplayString() const;
    [[nodiscard]] QString uploadStateDisplayString() const;

    // -- Phase 3 — secure identity ------------------------------------------

    void sendPublicKeyPacket();
    void sendSignaturePacket();
    void processPublicKeyPacket(const uint8* data, uint32 size);
    void processSignaturePacket(const uint8* data, uint32 size);
    void sendSecIdentStatePacket();
    void processSecIdentStatePacket(const uint8* data, uint32 size);
    [[nodiscard]] bool hasPassedSecureIdent(bool passIfUnavailable) const;

    // -- Phase 3 — chat -----------------------------------------------------

    void processChatMessage(SafeMemFile& data, uint32 length);
    void sendChatMessage(const QString& message);
    void processCaptchaRequest(SafeMemFile& data);
    void processCaptchaReqRes(uint8 status);

    // -- Phase 3 — preview --------------------------------------------------

    void sendPreviewRequest(const AbstractFile& file);
    void sendPreviewAnswer(const KnownFile* file);
    void processPreviewReq(const uint8* data, uint32 size);
    void processPreviewAnswer(const uint8* data, uint32 size);

    // -- Phase 3 — firewall -------------------------------------------------

    void sendFirewallCheckUDPRequest();
    void processFirewallCheckUDPRequest(SafeMemFile& data);
    void processKadFwTcpCheckAck();

    // -- Phase 3 — buddy / callback -----------------------------------------

    void processCallbackPacket(const uint8* data, uint32 size);
    void processReaskCallbackTCP(const uint8* data, uint32 size);
    void processBuddyPing();
    void processBuddyPong();
    [[nodiscard]] bool allowIncomingBuddyPingPong() const;
    [[nodiscard]] bool sendBuddyPingPong() const;
    void setLastBuddyPingPongTime();

    // -- Phase 3 — upload (UploadClient.cpp) --------------------------------

    virtual uint32 score(bool sysValue = false, bool isDownloading = false,
                         bool onlyBaseValue = false) const;
    [[nodiscard]] float getCombinedFilePrioAndCredit() const;
    bool processExtendedInfo(SafeMemFile& data, KnownFile* file);
    void setUploadFileID(KnownFile* newReqFile);
    void addReqBlock(Requested_Block_Struct* reqBlock);
    void updateUploadingStatisticsData();
    void sendOutOfPartReqsAndAddToWaitingQueue();
    void flushSendBlocks();
    void sendHashsetPacket(const uint8* data, uint32 size, bool fileIdentifiers);
    void sendRankingInfo();
    void sendCommentInfo(const KnownFile* file);
    void addRequestCount(const uint8* fileID);
    void unBan();
    void ban(const QString& reason = {});
    /// Record one abuse strike against this peer's ADDRESS and ban on the second.
    /// Shared by every flood detector; the counter survives this object's destruction,
    /// so reconnecting does not wipe a strike. See ClientList::trackBadRequest.
    void registerBadRequest(const QString& reason);
    [[nodiscard]] uint32 waitStartTime() const;
    [[nodiscard]] uint32 getWaitTimeDelay() const;
    void setWaitStartTime();
    void clearWaitStartTime();
    [[nodiscard]] EMSocket* getFileUploadSocket() const;
    [[nodiscard]] bool isUpPartAvailable(uint32 part) const;
    [[nodiscard]] KnownFile* uploadFile() const { return m_uploadFile; }

    // -- Phase 3 — download (DownloadClient.cpp) ----------------------------

    virtual bool askForDownload();
    virtual void sendFileRequest();
    void sendStartupLoadReq();
    void processFileInfo(SafeMemFile& data, PartFile* file);
    void processFileStatus(bool udpPacket, SafeMemFile& data, PartFile* file);
    void processHashSet(const uint8* data, uint32 size, bool fileIdentifiers);
    void processAcceptUpload();
    bool addRequestForAnotherFile(PartFile* file);
    void clearDownloadBlockRequests();
    void createBlockRequests(int blockCount);
    virtual void sendBlockRequests();
    virtual void processBlockPacket(const uint8* data, uint32 size,
                                    bool packed, bool i64Offsets);
    void processBlockPacketWithValidation(const uint8* data, uint32 size,
                                          bool packed, bool i64Offsets);
    virtual void sendCancelTransfer();
    void startDownload();
    void sendHashSetRequest();
    [[nodiscard]] uint32 calculateDownloadRate();
    [[nodiscard]] uint32 downDatarate() const { return m_downDatarate; }
    virtual void checkDownloadTimeout();
    [[nodiscard]] uint16 availablePartCount() const;
    [[nodiscard]] bool isPartAvailable(uint32 part) const;
    void setRemoteQueueRank(uint32 rank, bool updateDisplay = false);
    [[nodiscard]] bool reaskPending() const { return m_reaskPending; }
    void setReaskPending(bool v) { m_reaskPending = v; }
    void udpReaskACK(uint16 newQR);
    void udpReaskFNF();
    void udpReaskForDownload();
    [[nodiscard]] bool isSourceRequestAllowed() const;
    [[nodiscard]] bool isSourceRequestAllowed(PartFile* partFile,
                                              bool sourceExchangeCheck = false) const;
    [[nodiscard]] bool isValidSource() const;
    bool swapToAnotherFile(const QString& reason, bool ignoreNoNeeded,
                           bool ignoreSuspensions, bool removeCompletely,
                           PartFile* toFile = nullptr, bool allowSame = true,
                           bool isAboutToAsk = false);
    void dontSwapTo(PartFile* file);
    void removeFileFromOtherLists(PartFile* file);
    [[nodiscard]] bool isSwapSuspended(const PartFile* file,
                                       bool allowShortReaskTime = false,
                                       bool fileIsNNP = false) const;
    [[nodiscard]] uint32 timeUntilReask() const;
    [[nodiscard]] uint32 timeUntilReask(const PartFile* file) const;
    [[nodiscard]] uint32 lastAskedTime(const PartFile* file = nullptr) const;
    void setLastAskedTime();
    void updateDisplayedInfo(bool force = false);

    // -- Source exchange -------------------------------------------------------

    void processRequestSources(const uint8* data, uint32 size);   // v1
    void processAnswerSources(const uint8* data, uint32 size);    // v1
    void processRequestSources2(const uint8* data, uint32 size);  // v2
    void processAnswerSources2(const uint8* data, uint32 size);   // v2

    // AICH
    [[nodiscard]] bool isSupportingAICH() const { return m_supportsAICH > 0; }
    [[nodiscard]] const AICHHash* reqFileAICHHash() const;
    [[nodiscard]] bool isAICHReqPending() const { return m_aichRequested; }
    void sendAICHRequest(PartFile* forFile, uint16 part);
    void processAICHAnswer(const uint8* data, uint32 size);
    void processAICHRequest(const uint8* data, uint32 size);
    void processAICHFileHash(SafeMemFile& data, PartFile* file);

    [[nodiscard]] virtual bool isEd2kClient() const { return true; }
    [[nodiscard]] bool isUrlClient() const { return !isEd2kClient(); }

    [[nodiscard]] const std::list<Pending_Block_Struct*>& pendingBlocks() const { return m_pendingBlocks; }
    [[nodiscard]] uint64 lastBlockOffset() const { return m_lastBlockOffset; }

signals:
    void uploadStateChanged(UploadState newState);
    void downloadStateChanged(DownloadState newState);
    void chatMessageReceived(const QString& fromUser, const QString& message);
    void sharedFileListReceived(const QByteArray& userHash,
                                const QString& userName,
                                const QCborArray& files);
    void captchaRequestReceived(const QString& fromUser, const QImage& captchaImage);
    void previewAnswerReceived(const std::array<uint8, 16>& fileHash,
                               const std::vector<QImage>& images);
    void chatStateChanged();
    void updateDisplayedInfoRequested();

private slots:
    void onExtPacketReceived(const uint8* data, uint32 size, uint8 opcode);
    void onPacketForClient(const uint8* data, uint32 size, uint8 opcode, uint8 protocol);
    void onHelloReceived(const uint8* data, uint32 size, uint8 opcode);
    void onFileRequestReceived(const uint8* data, uint32 size, uint8 opcode);
    void onUploadRequestReceived(const uint8* data, uint32 size);

protected:
    void accumulateDownBytes(uint32 bytes) { m_downDataRateMS += bytes; }

private:
    void init();

    // -- Phase 2 — hello/muleInfo internals ---------------------------------
    bool processHelloTypePacket(SafeMemFile& data);
    void sendHelloTypePacket(SafeMemFile& data);
    void checkForGPLEvildoer();

    // -- Phase 3 — shared file browse handlers --------------------------------
    void processAskSharedFiles();
    void processAskSharedDirs();
    void processAskSharedFilesDir(const uint8* data, uint32 size);
    void processSharedDirsAnswer(const uint8* data, uint32 size);
    void processSharedFilesDirAnswer(const uint8* data, uint32 size);
    void processSharedDenied();

    // -- Phase 3 — upload-side packet handlers --------------------------------
    void processRequestParts(const uint8* data, uint32 size, bool i64Offsets);
    void processSetReqFileID(const uint8* data, uint32 size);
    void processRequestFileName(const uint8* data, uint32 size);
    void processMultiPacketExt2(const uint8* data, uint32 size);
    void processMultiPacketLegacy(const uint8* data, uint32 size, bool hasFileSize);
    void processMultiPacketAnswer(const uint8* data, uint32 size);
    void processMultiPacketAnswerLegacy(const uint8* data, uint32 size);

    /// Rate-limit and answer a source request, whatever opcode carried it: the standalone
    /// OP_REQUESTSOURCES/OP_REQUESTSOURCES2, or the same two as multipacket sub-opcodes.
    /// MFC ListenSocket.cpp:996-1028 served all of them from one code path; keeping one
    /// here is what stops the standalone and bundled forms from drifting apart.
    void answerSourceRequest(KnownFile* file, uint8 requestedVersion, uint16 requestedOptions);

    // Helpers for upload-side file lookup
    KnownFile* findUploadFile(const uint8* fileHash) const;
    void sendFileNotFound(const uint8* fileHash);
    void sendFileStatus(const uint8* fileHash, KnownFile* file);

    // -- Phase 3 — private helpers ------------------------------------------
    int filePrioAsNumber() const;
    void clearPendingBlockRequest(const Pending_Block_Struct* pending);
    bool doSwap(PartFile* swapTo, bool removeCompletely, const QString& reason);
    bool swapToRightFile(PartFile* swapTo, PartFile* curFile, bool ignoreSuspensions,
                         bool swapToIsNNP, bool curFileIsNNP,
                         bool& wasSkippedDueToSrcExch,
                         bool aggressiveSwapping = false);
    bool isInNoNeededList(const PartFile* file) const;
    bool recentlySwappedForSourceExchange() const;
    void setSwapForSourceExchangeTick();
    int unzip(Pending_Block_Struct* block, const uint8* zipped, uint32 lenZipped,
              uint8** unzipped, uint32* lenUnzipped, int recursion = 0);

    // Captcha helpers
    [[nodiscard]] static QString generateCaptchaText();
    [[nodiscard]] static QImage generateCaptchaImage(const QString& text);

    // -----------------------------------------------------------------------
    // Member variables (~180+, mapped from MFC CUpDownClient)
    // -----------------------------------------------------------------------

    // -- Identity -----------------------------------------------------------
    std::array<uint8, 16> m_userHash{};
    QString m_username;
    uint32 m_userIDHybrid = 0;
    Address m_connectAddress;
    Address m_userAddress;
    uint16 m_userPort = 0;
    Address m_serverAddress;
    uint16 m_serverPort = 0;
    uint16 m_udpPort = 0;
    uint16 m_kadPort = 0;
    Address m_buddyAddress;
    uint16 m_buddyPort = 0;
    std::array<uint8, 16> m_buddyID{};
    bool m_buddyIDValid = false;
    Address m_userIPv6;              // peer's advertised public IPv6
    Address m_buddyIPv6;             // serving buddy's public IPv6

    // -- Protocol version ---------------------------------------------------
    uint32 m_clientVersion = 0;
    uint8 m_emuleVersion = 0;
    uint8 m_dataCompVer = 0;
    uint8 m_udpVer = 0;
    uint8 m_sourceExchange1Ver = 0;
    uint8 m_extendedRequestsVer = 0;
    uint8 m_acceptCommentVer = 0;
    uint8 m_compatibleClient = 0;
    uint8 m_kadVersion = 0;
    bool m_emuleProtocol = false;
    bool m_isHybrid = false;
    bool m_isMLDonkey = false;
    bool m_multiPacket = false;
    bool m_unicodeSupport = false;

    // -- State machines -----------------------------------------------------
    ClientSoftware m_clientSoft = ClientSoftware::Unknown;
    UploadState m_uploadState = UploadState::None;
    DownloadState m_downloadState = DownloadState::None;
    ChatState m_chatState = ChatState::None;
    KadState m_kadState = KadState::None;
    SecureIdentState m_secureIdentState = SecureIdentState::Unavailable;
    SourceFrom m_sourceFrom = SourceFrom::Server;
    ChatCaptchaState m_chatCaptchaState = ChatCaptchaState::None;
    ConnectingState m_connectingState = ConnectingState::None;
    InfoPacketState m_infoPacketsReceived = InfoPacketState::None;

    // -- Upload tracking ----------------------------------------------------
    uint64 m_transferredUp = 0;
    uint64 m_curSessionUp = 0;
    uint64 m_curQueueSessionPayloadUp = 0;
    uint64 m_addedPayloadQueueSession = 0;
    uint32 m_uploadTime = 0;
    uint32 m_lastUpRequest = 0;
    uint32 m_askedCount = 0;
    uint32 m_slotNumber = 0;
    std::array<uint8, 16> m_reqUpFileId{};
    std::vector<uint8> m_upPartStatus;
    uint16 m_upPartCount = 0;
    uint16 m_upCompleteSourcesCount = 0;
    uint32 m_upDatarate = 0;
    uint64 m_sumForAvgUpDataRate = 0;
    std::deque<TransferredData> m_averageUDR;
    bool m_collectionUploadSlot = false;

    // -- Download tracking --------------------------------------------------
    PartFile* m_reqFile = nullptr;
    uint64 m_transferredDown = 0;
    uint64 m_curSessionDown = 0;
    uint64 m_curSessionPayloadDown = 0;
    uint64 m_lastBlockOffset = UINT64_MAX;
    uint32 m_downStartTime = 0;
    uint32 m_lastBlockReceived = 0;
    uint32 m_downAskedCount = 0;
    uint32 m_remoteQueueRank = 0;
    uint32 m_downDatarate = 0;
    uint32 m_downDataRateMS = 0;
    uint64 m_sumForAvgDownDataRate = 0;
    std::deque<TransferredData> m_averageDDR;
    std::vector<uint8> m_partStatus;
    uint16 m_partCount = 0;
    bool m_remoteQueueFull = false;
    bool m_completeSource = false;
    bool m_reaskPending = false;
    bool m_udpPending = false;
    bool m_transferredDownMini = false;
    uint32 m_totalUDPPackets = 0;
    uint32 m_failedUDPPackets = 0;
    QString m_clientFilename;

    // -- Capability flags (converted from MFC bitfields) --------------------
    bool m_hashsetRequestingMD4 = false;
    bool m_sharedDirectories = false;
    bool m_sentCancelTransfer = false;
    bool m_noViewSharedFiles = false;
    bool m_supportsPreview = false;
    bool m_previewReqPending = false;
    bool m_previewAnsPending = false;
    bool m_isSpammer = false;
    bool m_messageFiltered = false;
    bool m_peerCache = false;
    bool m_queueRankPending = false;
    bool m_needOurPublicIP = false;
    bool m_aichRequested = false;
    bool m_sentOutOfPartReqs = false;
    bool m_supportsLargeFiles = false;
    bool m_extMultiPacket = false;
    bool m_requestsCryptLayer = false;
    bool m_supportsCryptLayer = false;
    bool m_requiresCryptLayer = false;
    bool m_supportsSourceEx2 = false;
    bool m_supportsCaptcha = false;
    bool m_directUDPCallback = false;
    bool m_supportsFileIdent = false;
    bool m_supportsIPv6 = false;        // peer set CT_MOD_MISCOPTIONS bit 2
    bool m_openIPv6 = false;            // peer has a reachable public IPv6
    bool m_supportsExtendedXS = false;  // peer set CT_MOD_MISCOPTIONS bit 0
    bool m_supportsExtSXSkipTags = false; // peer set CT_MOD_MISCOPTIONS bit 5
    bool m_sendIPPending = false;       // owe this peer an OP_CHANGE_CLIENT_IP
    bool m_hashsetRequestingAICH = false;
    bool m_testDisableMultiPacket = false;

    // -- Multi-bit fields (from MFC bitfields > 1 bit) ----------------------
    uint8 m_unaskQueueRankRecv = 0;    // was :2
    uint8 m_failedFileIdReqs = 0;       // was :4
    uint8 m_supportsAICH = 0;           // was :3

    // -- Pointers -----------------------------------------------------------
    EMSocket* m_socket = nullptr;
    ClientCredits* m_credits = nullptr;
    Friend* m_friend = nullptr;

    // -- Timestamps ---------------------------------------------------------
    Address m_lastSignatureAddress;
    uint32 m_lastSourceRequest = 0;
    uint32 m_lastSourceAnswer = 0;
    uint32 m_lastAskedForSources = 0;
    uint32 m_lastBuddyPingPongTime = 0;
    uint32 m_lastRefreshedDLDisplay = 0;
    uint32 m_lastRefreshedULDisplay = 0;
    uint32 m_randomUpdateWait = 0;
    uint32 m_lastTriedToConnect = 0;
    uint32 m_lastSwapForSourceExchangeTick = 0;

    // -- Chat / Comment -----------------------------------------------------
    QString m_fileComment;
    uint8 m_fileRating = 0;
    uint8 m_messagesReceived = 0;
    uint8 m_messagesSent = 0;
    uint8 m_captchasSent = 0;

    // -- URL download -------------------------------------------------------
    QByteArray m_urlPath;
    uint64 m_reqStart = 0;
    uint64 m_reqEnd = 0;
    uint64 m_urlStartPos = UINT64_MAX;

    // -- Misc ---------------------------------------------------------------
    uint32 m_searchID = 0;
    int m_fileListRequested = 0;
    uint8 m_supportSecIdent = 0;
    bool m_friendSlot = false;
    bool m_commentDirty = false;
    bool m_gplEvildoer = false;
    bool m_helloAnswerPending = false;
    bool m_pendingFileRequest = false;
    bool m_incomingConnection = false;
    bool m_addNextConnect = false;
    bool m_sourceExchangeSwapped = false;
    bool m_secIdentSent = false;
    uint16 m_lastPartAsked = UINT16_MAX;
    uint16 m_showDR = 0;
    QString m_clientSoftwareStr;
    QString m_modVersion;
    QString m_helloInfo;
    QString m_muleInfo;
    QString m_captchaChallenge;
    QString m_captchaPendingMsg;

    // -- Phase 3 — new member variables -------------------------------------

    // Download block management
    std::list<Pending_Block_Struct*> m_pendingBlocks;

    // Upload file request tracking
    std::list<Requested_File_Struct*> m_requestedFiles;

    // A4AF (Ask for Another File) lists
    std::list<PartFile*> m_otherRequests;
    std::list<PartFile*> m_otherNoNeeded;

    // Swap suspension tracking
    struct FileStamp { PartFile* file = nullptr; uint32 timestamp = 0; };
    std::list<FileStamp> m_dontSwap;

    // Per-file reask times
    std::unordered_map<const PartFile*, uint32> m_fileReaskTimes;

    // Packets queued before connection established
    std::list<std::unique_ptr<Packet>> m_waitingPackets;

    // Upload file pointer (typed)
    KnownFile* m_uploadFile = nullptr;

    // Upload block queue
    std::list<Requested_Block_Struct*> m_blockRequests;
    std::list<Requested_Block_Struct*> m_doneBlocks;
};

} // namespace eMule
