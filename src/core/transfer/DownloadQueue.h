#pragma once

/// @file DownloadQueue.h
/// @brief Download queue manager — port of MFC CDownloadQueue.
///
/// Manages in-progress downloads (PartFile objects), provides file lookup,
/// add/remove, priority sorting, periodic processing, source management
/// with IPFilter/dead-source/dedup checks, and server UDP source queries.

#include "utils/Types.h"

#include <QObject>
#include <QStringList>

#include <vector>

namespace eMule {

class ClientList;
class IPFilter;
class KnownFileList;
class PartFile;
class ServerConnect;
class SharedFileList;
class UpDownClient;

class DownloadQueue : public QObject {
    Q_OBJECT

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
    [[nodiscard]] int fileCount() const { return static_cast<int>(m_fileList.size()); }

    // -- Lookup ---------------------------------------------------------------

    [[nodiscard]] PartFile* fileByID(const uint8* hash) const;
    [[nodiscard]] PartFile* fileByIndex(int index) const;
    [[nodiscard]] bool isFileExisting(const uint8* hash) const;
    [[nodiscard]] const std::vector<PartFile*>& files() const { return m_fileList; }

    // -- Source management (basic) --------------------------------------------

    bool checkAndAddSource(PartFile* file, UpDownClient* source);
    void removeSource(UpDownClient* source);

    /// Add a Kad-discovered file source. Finds the matching PartFile by hash
    /// and stores the source info for later connection.
    /// sourceType: 1/2=High-ID (direct TCP), 4=Low-ID (Kad buddy callback).
    void addKadSourceResult(uint32 searchID, const uint8* fileHash,
                            uint32 ip, uint16 tcpPort,
                            uint32 buddyIP, uint16 buddyPort, uint8 buddyCrypt,
                            uint8 sourceType, const uint8* buddyHash,
                            const uint8* clientHash);

    /// Process OP_FOUNDSOURCES / OP_FOUNDSOURCES_OBFU from the connected server.
    void addServerSourceResult(const uint8* data, uint32 size, bool obfuscated);

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

signals:
    void fileAdded(eMule::PartFile* file);
    void fileRemoved(eMule::PartFile* file);
    void fileCompleted(eMule::PartFile* file);

private:
    void onDownloadCompleted(PartFile* file);
    void connectPartFileSignals(PartFile* file);
    void distributeDownloadLimit();

    std::vector<PartFile*> m_fileList;
    SharedFileList* m_sharedFileList = nullptr;
    KnownFileList* m_knownFileList = nullptr;
    IPFilter* m_ipFilter = nullptr;
    ClientList* m_clientList = nullptr;
    ServerConnect* m_serverConnect = nullptr;
    uint32 m_datarate = 0;
    uint32 m_udCounter = 0;
    uint32 m_lastUDPSourceRequestTime = 0;
    uint32 m_lastServerSourceRequestTime = 0;
    uint32 m_lastKademliaFileRequest = 0;
};

} // namespace eMule
