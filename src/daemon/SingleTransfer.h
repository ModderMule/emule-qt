#pragma once

/// @file SingleTransfer.h
/// @brief Single transfer mode (ed3k only), per network: btSingleTransfer, usenetSingleTransfer,
/// ed3kHosterSingleTransfer. The network's own window is hidden (GUI); its jobs joined with
/// ed3k are rows of the Transfers list (GetEd3kDownloads), controlled through
/// Ed3kDownloadControl; its other jobs are paused while the mode lasts and resumed after.
///
/// A row is an Ed3kDownloadsBridge row with `root` = "<tag>|<owner>" (tag torrent, usenet,
/// hoster; owner as the join keys it) and `network` = tag. Free of the ed3k module: without
/// it nothing is added.

#include <QCborArray>
#include <QCborMap>
#include <QString>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kSession;
} // namespace ed3k

namespace Ipc {
class IpcMessage;
} // namespace Ipc

namespace SingleTransfer {

/// The state of a row, as Ed3kDownloadInfo::State
enum class RowState : int { Running = 0, Paused = 1, Checking = 2, Complete = 3, Error = 4 };

/// What a network bridge knows of one job, for transferRow().
struct Job {
    QString name;
    qint64 size = 0;
    qint64 done = 0;
    qint64 rate = 0;
    int sources = 0;
    int transferring = 0;
    RowState state = RowState::Running;
    int priority = 1;   ///< ed3k's: 0 low, 1 normal, 2 high, 3 very high, 4 very low
    int category = 0;
    QString path;
    qint64 addedOn = 0;
    QString error;
};

/// Set by DaemonApp, null when ed3k is torn down. @p broadcast sends PushEd3kDownloads while
/// rows of the mode are listed (their rates change without an ed3k event).
void setSession(ed3k::Ed3kSession* session, std::function<void(const Ipc::IpcMessage&)> broadcast = {});

/// "<tag>|<owner>"
[[nodiscard]] QString keyOf(const QString& tag, const QString& owner);
/// Whether @p key names a job of a network (keyOf), not an ed3k root.
[[nodiscard]] bool isKey(const QString& key);

/// An Ed3kDownloadsBridge row of job @p owner of network @p tag; @p joined false: an
/// unjoined torrent done (seeding), listed though not joined
[[nodiscard]] QCborMap transferRow(const QString& tag, const QString& owner, const Job& job,
                                   bool joined = true);

/// The rows of every network in the mode: its jobs joined with ed3k, and its unjoined
/// torrents done (they seed, the mode does not pause them).
void appendRows(QCborArray& downloads);

/// Ed3kDownloadControl @p action (0 pause, 1 resume, 2 cancel, 3 priority, 4 category,
/// 7 cancel deleting) on the job of @p key. False: not done, or no such job.
bool control(const QString& key, int action, qint64 value);

/// Pauses the unjoined jobs of the networks in the mode; resumes those it paused once the
/// mode ends (or they joined). Runs on a timer while a session is set; tests call it.
void enforce();

} // namespace SingleTransfer
} // namespace eMule
