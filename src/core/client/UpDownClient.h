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
#include "utils/Types.h"

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

struct TransferredData {
    uint32 dataLen = 0;
    uint32 timestamp = 0;  // getTickCount() value
};

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

    [[nodiscard]] uint32 userIP() const { return m_userIP; }
    void setIP(uint32 ip) { m_userIP = ip; m_connectIP = ip; }

    [[nodiscard]] uint32 connectIP() const { return m_connectIP; }
    void setConnectIP(uint32 ip) { m_connectIP = ip; }

    [[nodiscard]] uint32 userIDHybrid() const { return m_userIDHybrid; }
    void setUserIDHybrid(uint32 id) { m_userIDHybrid = id; }
    [[nodiscard]] bool hasLowID() const;

    [[nodiscard]] uint16 userPort() const { return m_userPort; }
    void setUserPort(uint16 port) { m_userPort = port; }

    [[nodiscard]] uint32 serverIP() const { return m_serverIP; }
    void setServerIP(uint32 ip) { m_serverIP = ip; }
    [[nodiscard]] uint16 serverPort() const { return m_serverPort; }
    void setServerPort(uint16 port) { m_serverPort = port; }

    [[nodiscard]] uint16 udpPort() const { return m_udpPort; }
    void setUDPPort(uint16 port) { m_udpPort = port; }
    [[nodiscard]] uint16 kadPort() const { return m_kadPort; }
    void setKadPort(uint16 port) { m_kadPort = port; }

    [[nodiscard]] uint32 buddyIP() const { return m_buddyIP; }
    void setBuddyIP(uint32 ip) { m_buddyIP = ip; }
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
    [[nodiscard]] bool supportMultiPacket() const { return m_multiPacket; }
    [[nodiscard]] bool supportExtMultiPacket() const { return m_extMultiPacket; }
    [[nodiscard]] bool supportsLargeFiles() const { return m_supportsLargeFiles; }
    [[nodiscard]] bool supportsFileIdentifiers() const { return m_supportsFileIdent; }
    [[nodiscard]] bool supportsUDP() const { return m_udpPort != 0 && m_udpVer != 0; }
    [[nodiscard]] bool supportsCryptLayer() const { return m_supportsCryptLayer; }
    [[nodiscard]] bool requestsCryptLayer() const { return m_requestsCryptLayer; }
    [[nodiscard]] bool requiresCryptLayer() const { return m_requiresCryptLayer; }
    [[nodiscard]] bool supportsDirectUDPCallback() const { return m_directUDPCallback; }
    [[nodiscard]] bool unicodeSupport() const { return m_unicodeSupport; }

    [[nodiscard]] uint8 dataCompVer() const { return m_dataCompVer; }
    [[nodiscard]] uint8 udpVer() const { return m_udpVer; }
    [[nodiscard]] uint8 sourceExchange1Ver() const { return m_sourceExchange1Ver; }
    [[nodiscard]] bool supportsSourceExchange2() const { return m_supportsSourceEx2; }
    [[nodiscard]] uint8 extendedRequestsVer() const { return m_extendedRequestsVer; }
    [[nodiscard]] uint8 acceptCommentVer() const { return m_acceptCommentVer; }
    [[nodiscard]] uint8 compatibleClient() const { return m_compatibleClient; }
    [[nodiscard]] uint8 kadVersion() const { return m_kadVersion; }
    void setKadVersion(uint8 v) { m_kadVersion = v; }
    [[nodiscard]] uint8 emuleVersion() const { return m_emuleVersion; }

    // Version fields — set by hello packet processing
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

    [[nodiscard]] uint32 slotNumber() const { return m_slotNumber; }
    void setSlotNumber(uint32 s) { m_slotNumber = s; }

    [[nodiscard]] const uint8* reqUpFileId() const { return m_reqUpFileId.data(); }
    void setReqUpFileId(const uint8* id);

    [[nodiscard]] uint16 upPartCount() const { return m_upPartCount; }
    [[nodiscard]] const std::vector<uint8>& upPartStatus() const { return m_upPartStatus; }
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
    [[nodiscard]] uint32 waitStartTime() const;
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
    virtual void sendCancelTransfer();
    void startDownload();
    void sendHashSetRequest();
    [[nodiscard]] uint32 calculateDownloadRate();
    virtual void checkDownloadTimeout();
    [[nodiscard]] uint16 availablePartCount() const;
    [[nodiscard]] bool isPartAvailable(uint32 part) const;
    void setRemoteQueueRank(uint32 rank, bool updateDisplay = false);
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
    [[nodiscard]] bool isSwapSuspended(const PartFile* file,
                                       bool allowShortReaskTime = false,
                                       bool fileIsNNP = false) const;
    [[nodiscard]] uint32 timeUntilReask() const;
    [[nodiscard]] uint32 timeUntilReask(const PartFile* file) const;
    [[nodiscard]] uint32 lastAskedTime(const PartFile* file = nullptr) const;
    void setLastAskedTime();
    void updateDisplayedInfo(bool force = false);

    // -- Source exchange (SX2) ------------------------------------------------

    void processRequestSources2(const uint8* data, uint32 size);
    void processAnswerSources2(const uint8* data, uint32 size);

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

signals:
    void uploadStateChanged(UploadState newState);
    void downloadStateChanged(DownloadState newState);
    void chatMessageReceived(const QString& fromUser, const QString& message);
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
    void processMultiPacketAnswer(const uint8* data, uint32 size);

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
    uint32 m_connectIP = 0;
    uint32 m_userIP = 0;
    uint16 m_userPort = 0;
    uint32 m_serverIP = 0;
    uint16 m_serverPort = 0;
    uint16 m_udpPort = 0;
    uint16 m_kadPort = 0;
    uint32 m_buddyIP = 0;
    uint16 m_buddyPort = 0;
    std::array<uint8, 16> m_buddyID{};
    bool m_buddyIDValid = false;

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
    bool m_hashsetRequestingAICH = false;

    // -- Multi-bit fields (from MFC bitfields > 1 bit) ----------------------
    uint8 m_unaskQueueRankRecv = 0;    // was :2
    uint8 m_failedFileIdReqs = 0;       // was :4
    uint8 m_supportsAICH = 0;           // was :3

    // -- Pointers -----------------------------------------------------------
    EMSocket* m_socket = nullptr;
    ClientCredits* m_credits = nullptr;
    Friend* m_friend = nullptr;

    // -- Timestamps ---------------------------------------------------------
    uint32 m_lastSignatureIP = 0;
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
