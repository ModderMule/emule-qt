#pragma once

/// @file DownloadQueue.h
/// @brief Download queue manager — port of MFC CDownloadQueue.
///
/// Manages in-progress downloads (PartFile objects), provides file lookup,
/// add/remove, priority sorting, periodic processing, source management
/// with IPFilter/dead-source/dedup checks, and server UDP source queries.

#include "utils/EntityList.h"
#include "utils/Types.h"

#include <QObject>
#include <QStringList>

#include <deque>
#include <vector>

class tst_DownloadQueue;  // fwd-decl for the white-box unit-test friend below

namespace eMule {

class ClientList;
class Endpoint;
class IPFilter;
class KnownFileList;
class PartFile;
class SafeMemFile;
class Server;
class ServerConnect;
class SharedFileList;
class UpDownClient;

class DownloadQueue : public EntityList<PartFile> {
    Q_OBJECT

    // White-box access for the unit test (drives the private global-UDP-source
    // rotation state machine deterministically).
    friend class ::tst_DownloadQueue;

public:
    explicit DownloadQueue(QObject* parent = nullptr);
    ~DownloadQueue() override;

    DownloadQueue(const DownloadQueue&) = delete;
    DownloadQueue& operator=(const DownloadQueue&) = delete;

    // -- Init — scan temp dirs for .part.met files ----------------------------

    void init(const QStringList& tempDirs);

    // -- File management ------------------------------------------------------

    void addDownload(PartFile* file, bool paused = false);
    bool addDownloadFromED2KLink(const QString& link, const QString& tempDir,
                                  uint32 category = 0, bool paused = false);
    void removeFile(PartFile* file);
    void deleteAll();
    [[nodiscard]] int fileCount() const { return count(); }

    // -- Lookup ---------------------------------------------------------------

    [[nodiscard]] PartFile* fileByID(const uint8* hash) const;
    [[nodiscard]] PartFile* fileByIndex(int index) const;
    [[nodiscard]] bool isFileExisting(const uint8* hash) const;
    [[nodiscard]] const std::vector<PartFile*>& files() const { return items(); }

    // -- Source management (basic) --------------------------------------------

    bool checkAndAddSource(PartFile* file, UpDownClient* source);
    void removeSource(UpDownClient* source);

    /// Add a Kad-discovered file source. Finds the matching PartFile by hash
    /// and stores the source info for later connection.
    /// sourceType: 1/4=non-firewalled, 3/5=firewalled+buddy, 6=direct UDP callback.
    void addKadSourceResult(uint32 searchID, const uint8* fileHash,
                            uint32 ip, uint16 tcpPort,
                            uint32 buddyIP, uint16 buddyPort, uint8 buddyCrypt,
                            uint8 sourceType, const uint8* buddyHash,
                            const uint8* clientHash, uint16 udpPort);

    /// Store a Kad "notes" search result (filename + rating + comment) on the
    /// matching file — an in-progress download or an already-completed known file.
    /// Dedups by the note publisher's source ID and persists the result.
    void addKadNoteResult(const uint8* fileHash, const uint8* publisherId,
                          const QString& name, uint8 rating, const QString& comment);

    /// Process OP_FOUNDSOURCES / OP_FOUNDSOURCES_OBFU from the connected server.
    void addServerSourceResult(const uint8* data, uint32 size, bool obfuscated);

    /// Process an OP_GLOBFOUNDSOURCES (0x9B) UDP reply, which may pack several
    /// files' source blocks in one datagram. Sources are attributed to the
    /// answering server (`from`). Port of the OP_GLOBFOUNDSOURCES case in
    /// CUDPSocket::ProcessPacket.
    void addUDPGlobalSources(const uint8* data, uint32 size, const Endpoint& from);

    // -- Queue operations -----------------------------------------------------

    void startNextFile(int category = -1);
    void sortByPriority();
    void process();

    // -- Category management --------------------------------------------------

    void setCatStatus(uint32 category, bool paused);

    // -- List integration -----------------------------------------------------

    void setSharedFileList(SharedFileList* sfl) { m_sharedFileList = sfl; }
    void setKnownFileList(KnownFileList* kfl) { m_knownFileList = kfl; }
    void setIPFilter(IPFilter* filter) { m_ipFilter = filter; }
    void setClientList(ClientList* cl) { m_clientList = cl; }
    void setServerConnect(ServerConnect* sc) { m_serverConnect = sc; }

    // -- Kad file request rate-limiter ----------------------------------------

    [[nodiscard]] bool doKademliaFileRequest() const;
    void setLastKademliaFileRequest();

    // -- Stats ----------------------------------------------------------------

    [[nodiscard]] uint32 datarate() const { return m_datarate; }
    [[nodiscard]] bool hasActiveTransfers() const;
    [[nodiscard]] uint32 successfulDownloadCount() const { return m_successfulDownCount; }
    [[nodiscard]] uint32 failedDownloadCount() const { return m_failedDownCount; }
    [[nodiscard]] uint32 averageDownTime() const;

signals:
    void fileAdded(eMule::PartFile* file);
    void fileRemoved(eMule::PartFile* file);
    void fileCompleted(eMule::PartFile* file);

private:
    void onDownloadCompleted(PartFile* file);
    void connectPartFileSignals(PartFile* file);

    // Shared server-source parsing (TCP OP_FOUNDSOURCES and UDP OP_GLOBFOUNDSOURCES).
    // parseServerSourceBlock reads one [count][source...] block starting at `offset`
    // (the count byte) and returns the offset past it; addServerSourceClient runs the
    // per-source validation and construction.
    uint32 parseServerSourceBlock(PartFile* file, const uint8* data, uint32 size,
                                  uint32 offset, bool obfuscated,
                                  uint32 srvIP, uint16 srvPort);
    void addServerSourceClient(PartFile* file, uint32 userId, uint16 port,
                               bool obfuscated, uint8 cryptFlags,
                               const uint8* userHash, bool hasHash,
                               uint32 srvIP, uint16 srvPort);

    // Global UDP source acquisition — port of CDownloadQueue::SendNextUDPPacket &
    // friends. Walks the server list ONCE per pass (non-wrapping getSuccServer),
    // batching several files' hashes into one OP_GLOBGETSOURCES(2) datagram per
    // server, skipping the connected server + dead servers, then idles until the
    // next UDPSERVERREASKTIME. Driven from process(); state carried in the
    // m_curUdpServer / m_lastUdpFile cursors below.
    bool sendNextUDPPacket();
    bool sendGlobGetSourcesUDPPacket(SafeMemFile& data, bool ext2Packet,
                                     uint32 nFiles, uint32 nIncludedLargeFiles);
    [[nodiscard]] bool isMaxFilesPerUDPServerPacketReached(uint32 nFiles,
                                                           uint32 nIncludedLargeFiles) const;
    void stopUDPRequests();

    // EntityList hooks — emit the queue's signals + run side-effects on add/remove.
    void onEntityAdded(PartFile* file) override;
    void onEntityRemoved(PartFile* file) override;

    SharedFileList* m_sharedFileList = nullptr;
    KnownFileList* m_knownFileList = nullptr;
    IPFilter* m_ipFilter = nullptr;
    ClientList* m_clientList = nullptr;
    ServerConnect* m_serverConnect = nullptr;
    uint32 m_datarate = 0;
    uint32 m_successfulDownCount = 0;
    uint32 m_failedDownCount = 0;
    uint64 m_totalDownTime = 0;  // seconds
    std::deque<TransferredData> m_averageDRList;  // 10-second averaging window
    uint32 m_udCounter = 0;
    uint32 m_lastUDPSourceRequestTime = 0;
    uint32 m_lastKademliaFileRequest = 0;

    // Global-UDP-source rotation cursors (port of CDownloadQueue members).
    Server*   m_curUdpServer = nullptr;       // cur_udpserver — current pass cursor (non-owning)
    PartFile* m_lastUdpFile = nullptr;        // m_lastfile — file cursor within the current server
    uint32    m_lastUdpSearchTime = 0;        // m_lastudpsearchtime — 0 ⇒ start a new pass now
    uint32    m_searchedServers = 0;          // m_iSearchedServers — servers covered this pass
    uint32    m_requestsSentToServer = 0;     // m_cRequestsSentToServer — per-server batch counter
};

} // namespace eMule
