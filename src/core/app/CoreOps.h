#pragma once

/// @file CoreOps.h
/// @brief User actions on the running core, behind one implementation.
///
/// What the GUI does over IPC and what a REST or MCP client does must be the same
/// thing: same checks, same error. Each function works on theApp and reports an
/// HTTP-style code, which both transports already speak.

#include "utils/Types.h"

#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

namespace eMule {

class PartFile;
class Server;

namespace ops {

struct Status {
    int code = 200;
    QString message;

    [[nodiscard]] bool ok() const { return code >= 200 && code < 300; }
    [[nodiscard]] static Status fail(int c, const QString& m) { return {c, m}; }
};

// --- Downloads ---------------------------------------------------------------

/// The queued download named by a 32-digit hex hash; null and @p st set otherwise.
[[nodiscard]] PartFile* findDownload(const QString& hashHex, Status& st);

Status pauseDownload(const QString& hashHex);
Status resumeDownload(const QString& hashHex);
/// Stop: sources are dropped and the file is no longer asked for; data is kept.
Status stopDownload(const QString& hashHex);
/// Cancel: the download and its part files are deleted.
Status cancelDownload(const QString& hashHex);
void cancelDownload(PartFile* file);

/// @p priority: PR_VERYLOW..PR_HIGH; ignored when @p isAuto.
Status setDownloadPriority(const QString& hashHex, uint8 priority, bool isAuto);
Status renameDownload(const QString& hashHex, const QString& newName);
Status setDownloadCategory(const QString& hashHex, uint32 category);

/// Removes completed entries from the list (the files stay). Empty @p only = all.
/// @return how many were removed, or -1 without a queue.
int clearCompletedDownloads(const QSet<QString>& only);

struct AddOutcome {
    Status status;
    QString hash;           ///< of the file the link names
    bool added = false;     ///< false: it was already queued
};

/// Queues the file an ed2k:// file link names.
[[nodiscard]] AddOutcome addDownloadFromLink(const QString& link, qint64 category, bool paused);

/// Queues a search result, carrying over what the result knows (sources, AICH).
/// @p link wins over hash/name/size when it is an ed2k link. Without @p paused the
/// "add new files paused" option decides (MFC AddSearchToDownload).
[[nodiscard]] AddOutcome addDownloadFromSearch(const QString& hashHex, const QString& fileName,
                                               uint64 fileSize, const QString& link,
                                               qint64 category, uint32 searchID,
                                               std::optional<bool> paused = std::nullopt);

// --- Servers -----------------------------------------------------------------

struct AddServerOutcome {
    Status status;
    bool added = false;     ///< false with an ok status: it was already listed
    bool lanFiltered = false;
};

/// @p address is an IP literal or a host name. @p otherFamilyAddress: the second
/// address of a dual-stack server, may be empty.
[[nodiscard]] AddServerOutcome addServer(const QString& address, uint16 port, const QString& name,
                                         const QString& otherFamilyAddress = {});

/// The listed server at @p address : @p port, or null.
[[nodiscard]] Server* findServer(const QString& address, uint16 port);
Status removeServer(const QString& address, uint16 port);

/// Connects to @p server, or to any server when null.
Status connectToServer(Server* server);
Status disconnectFromServer();

/// Merges a downloaded server.met into the list. @p done gets the number added.
void importServerMetFromUrl(const QString& url,
                            std::function<void(const Status&, int added)> done);

// --- Kad -----------------------------------------------------------------------

/// Starts Kad; with @p host and @p port it bootstraps from that node.
Status startKad(const QString& host = {}, uint16 port = 0);
Status stopKad();
/// Replaces nodes.dat with the downloaded file and (re)starts Kad from it.
void importKadNodesFromUrl(const QString& url, std::function<void(const Status&)> done);

/// Re-runs the port-mapping probe and the Kad firewall check.
Status recheckFirewall();

// --- Shared files ------------------------------------------------------------------

/// Rescans the shared directories, or only re-reads media tags.
Status reloadSharedFiles(bool metaDataOnly = false);
/// Stores the shared directory list and rescans. Every entry must be an existing directory.
Status setSharedDirectories(const QStringList& dirs);

} // namespace ops
} // namespace eMule
