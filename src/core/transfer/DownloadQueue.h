#pragma once

/// @file DownloadQueue.h
/// @brief Download queue manager — port of MFC CDownloadQueue.
///
/// Manages in-progress downloads (PartFile objects), provides file lookup,
/// add/remove, priority sorting, periodic processing, source management
/// with IPFilter/dead-source/dedup checks, and server UDP source queries.

#include "client/ClientStateDefs.h"
#include "kademlia/Kademlia.h"
#include "net/Address.h"
#include "utils/EntityList.h"
#include "utils/Types.h"

#include <QObject>
#include <QStringList>

#include <deque>
#include <memory>
#include <vector>

class tst_DownloadQueue;  // fwd-decl for the white-box unit-test friend below

namespace eMule {

class ClientList;
class HostResolver;
class IPFilter;
class KnownFileList;
class PartFile;
class SafeMemFile;
class Server;
class ServerConnect;
class SharedFileList;
class UpDownClient;
struct ED2KLinkSource;

/// ED2K user ID standing in for "this source has no usable IPv4". A value below
/// 0x01000000 reads back as a LowID, which is exactly the semantics we want; Kad buddy
/// sources use the same marker.
inline constexpr uint32 kNoIPv4SourceId = 1;

/// Identity and capability hints a source ingress may carry beyond address and port.
/// Namespace scope rather than nested in DownloadQueue: a nested type's default member
/// initializers are not yet parsed where addVettedSource() names `{}` as a default argument.
struct SourceHints {
    uint32       serverIP       = 0;        ///< network byte order
    uint16       serverPort     = 0;
    const uint8* userHash       = nullptr;  ///< 16 bytes, or null
    uint8        connectOptions = 0;        ///< bits setConnectOptions() decodes
    uint16       kadPort        = 0;
    uint16       udpPort        = 0;
};

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

    /// Re-register every eligible part file with SharedFileList.
    ///
    /// SharedFileList::reload() clears its map, so a re-scan of the shared directories
    /// would otherwise silently drop every part file and leave us unadvertised as a
    /// partial source until the next part completed. MFC re-adds them for the same
    /// reason, from CSharedFileList::FindSharedFiles
    /// (srchybrid/DownloadQueue.cpp:68-75, srchybrid/SharedFileList.cpp:551).
    void addPartFilesToShare();

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
    /// The file that owns the Kad source search @p id, if any.
    /// MFC CDownloadQueue::GetFileByKadFileSearchID (DownloadQueue.cpp:439).
    [[nodiscard]] PartFile* fileByKadFileSearchID(uint32 id) const;
    [[nodiscard]] bool isFileExisting(const uint8* hash) const;
    [[nodiscard]] const std::vector<PartFile*>& files() const { return items(); }

    // -- Source management (basic) --------------------------------------------

    bool checkAndAddSource(PartFile* file, UpDownClient* source);
    void removeSource(UpDownClient* source);

    /// Vet one peer address the way every untrusted source ingress must: isGoodIP, the IP
    /// filter and the ban list. Returns a null Address when the address is unusable.
    [[nodiscard]] Address vetPeerAddress(const Address& addr) const;

    /// Build, inject and dial one peer source that has already been address-vetted.
    ///
    /// Owns the checks shared by every ingress — the firewalled-LowID drop and the
    /// per-file source cap — plus client construction and the connect attempt.
    /// @param ed2kUserId network-order IPv4 for a HighID, the raw low value for a LowID,
    ///                   or kNoIPv4SourceId when only IPv6 is usable.
    bool addVettedSource(PartFile* file, uint32 ed2kUserId, const Address& v6, uint16 port,
                         SourceFrom from, const SourceHints& hints = {});

    /// Seed @p file with the source hints carried by an eD2K link.
    ///
    /// IP literals are added immediately; hostnames are resolved asynchronously (A and
    /// AAAA) and the file is re-looked-up by hash when the answer arrives, so a
    /// meanwhile-removed download is handled safely. Link text is untrusted, so the
    /// number of DNS lookups one link may trigger is capped (kMaxLinkDnsSources).
    void addLinkSources(PartFile* file, const std::vector<ED2KLinkSource>& sources);

    /// Upper bound on hostname lookups triggered by a single link.
    static constexpr int kMaxLinkDnsSources = 4;

    /// Add a Kad-discovered file source. Finds the matching PartFile by hash
    /// and stores the source info for later connection.
    /// sourceType: 1/4=non-firewalled, 3/5=firewalled+buddy, 6=direct UDP callback.
    void addKadSourceResult(const kad::Kademlia::KadSourceResult& result);

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

    /// UDP file re-asks sent this session, and how many of them went unanswered.
    /// MFC CDownloadQueue::AddUDPFileReasks / AddFailedUDPFileReasks
    /// (srchybrid/DownloadQueue.h:114-117). A re-ask is charged as failed at the head of
    /// UpDownClient::askForDownload(), i.e. when we fall back to TCP while the datagram
    /// is still outstanding. Session counters, like their neighbours above — the
    /// statistics reset deliberately leaves them running.
    void addUDPFileReasks() { ++m_udpFileReasks; }
    [[nodiscard]] uint32 udpFileReasks() const { return m_udpFileReasks; }
    void addFailedUDPFileReasks() { ++m_failedUDPFileReasks; }
    [[nodiscard]] uint32 failedUDPFileReasks() const { return m_failedUDPFileReasks; }

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
    // S3a inline-sentinel counterpart: build a source from an inline IPv6 (16 bytes).
    void addServerSourceClientIPv6(PartFile* file, const uint8* ipv6, uint16 port,
                                   bool obfuscated, uint8 cryptFlags,
                                   const uint8* userHash, bool hasHash);

    // Shared source construction for the server and link paths. Vetting stays with the
    // callers, since each ingress trusts its input differently.
    // @param ed2kUserId  ED2K user ID: network-order IPv4 for a HighID, the raw low
    //                    value for a LowID, or 1 (the LowID marker) when there is no
    //                    usable IPv4 at all.
    // @param v6          IPv6 user address, or null. When @p ed2kUserId is the marker,
    //                    userAddress/connectAddress point at it so the peer is dialed
    //                    over IPv6 directly.
    UpDownClient* makeSourceClient(PartFile* file, uint32 ed2kUserId, const Address& v6,
                                   uint16 port, SourceFrom from,
                                   uint32 srvIP = 0, uint16 srvPort = 0);

    // One vetted peer source from a link (either or both families).
    void addLinkPeerSource(PartFile* file, const Address& v4, const Address& v6, uint16 port);
    // One HTTP source from an `s=<url>` link parameter.
    void addLinkUrlSource(PartFile* file, const ED2KLinkSource& source);

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

    HostResolver* m_hostResolver = nullptr;   // created on first hostname source
    SharedFileList* m_sharedFileList = nullptr;
    KnownFileList* m_knownFileList = nullptr;
    IPFilter* m_ipFilter = nullptr;
    ClientList* m_clientList = nullptr;
    ServerConnect* m_serverConnect = nullptr;
    uint32 m_datarate = 0;
    uint32 m_successfulDownCount = 0;
    uint32 m_failedDownCount = 0;
    uint32 m_udpFileReasks = 0;         // m_nUDPFileReasks
    uint32 m_failedUDPFileReasks = 0;   // m_nFailedUDPFileReasks
    uint64 m_totalDownTime = 0;  // seconds
    std::deque<TransferredData> m_averageDRList;  // 10-second averaging window
    uint32 m_udCounter = 0;
    uint32 m_lastKademliaFileRequest = 0;

    // Global-UDP-source rotation cursors (port of CDownloadQueue members).
    Server*   m_curUdpServer = nullptr;       // cur_udpserver — current pass cursor (non-owning)
    PartFile* m_lastUdpFile = nullptr;        // m_lastfile — file cursor within the current server
    uint32    m_lastUdpSearchTime = 0;        // m_lastudpsearchtime — 0 ⇒ start a new pass now
    uint32    m_searchedServers = 0;          // m_iSearchedServers — servers covered this pass
    uint32    m_requestsSentToServer = 0;     // m_cRequestsSentToServer — per-server batch counter
};

} // namespace eMule
