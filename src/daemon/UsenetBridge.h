#pragma once

/// @file UsenetBridge.h
/// @brief The daemon's one implementation of each Usenet request.
///
/// IPC and the web server (page and REST API) answer the same questions about
/// the Usenet queue. Their wire formats differ; what a row contains, how an add
/// is counted and how a category-wide action walks the queue must not. Before
/// this file each lived inline in an IpcClientHandler handler.
///
/// Header stays free of the Usenet module, like IpcClientHandler.h: the enums are
/// forward-declared with their fixed underlying type.

#include "webserver/UsenetWebBackend.h"

#include <QCborMap>
#include <QString>

#include <functional>

class QObject;

namespace eMule {

namespace usenet {
class UsenetQueue;
class UsenetQueueItem;
struct UsenetAddOptions;
enum class UsenetAddOutcome : quint8;
enum class UsenetAddOrigin : quint8;
} // namespace usenet

namespace UsenetBridge {

/// The queue, or null before the daemon built the engine / after it tore it down.
[[nodiscard]] usenet::UsenetQueue* queue();

/// One GetUsenetQueue row. Also what PushUsenetQueueItem carries.
[[nodiscard]] QCborMap itemToCbor(const usenet::UsenetQueueItem& item);

/// The row plus per-file detail: subjects, poster, groups, article tallies.
[[nodiscard]] QCborMap itemDetailsToCbor(const usenet::UsenetQueueItem& item);

/// ListUsenetArchiveEntries. Asks for the header bytes it is missing.
[[nodiscard]] QCborMap archiveEntriesToCbor(const QString& itemId, int fileIndex);

/// `maxDownloadKb`, `usenetLimitKb`, `ed2kBudgetKb` into @p stats.
void insertDownloadSplit(QCborMap& stats);

[[nodiscard]] bool categoryExists(int category);

/// Count an intake attempt for the statistics. Every add path calls this once.
void noteAdd(usenet::UsenetAddOrigin origin, usenet::UsenetAddOutcome outcome);

struct AddResult {
    QString itemId;
    QString error;
    usenet::UsenetAddOutcome outcome = static_cast<usenet::UsenetAddOutcome>(3); // Failed

    /// addNzb() ran. False for a rejected URL, a failed fetch or a missing
    /// engine — none of which has an outcome to report.
    bool attempted = false;
};

/// addNzb(), counted. A refusal always carries a sentence.
[[nodiscard]] AddResult addNzbData(const QByteArray& data, const QString& name,
                                   const usenet::UsenetAddOptions& options,
                                   usenet::UsenetAddOrigin origin);

/// Validate, fetch, then addNzbData(). @p done runs exactly once. The caller
/// caps concurrency and guards its own lifetime.
void addNzbUrl(QObject* context, const QString& url, const usenet::UsenetAddOptions& options,
               std::function<void(const AddResult&)> done);

/// Empty when the probe started, else why not.
[[nodiscard]] QString recheck(const QString& itemId);

/// InspectNzb: the files of an .nzb, without queueing it. Empty with @p error
/// set when it does not parse.
[[nodiscard]] QCborMap inspectNzb(const QByteArray& data, const QString& name, QString& error);

/// Pause, resume (Bulk intent) or cancel every release in @p category, 0 being
/// all of them. Returns how many it acted on, -1 for an unknown category.
int applyCategoryAction(int category, UsenetWebCategoryAction action);

/// The engine-wide pause: persist `usenet.paused`, then apply it to the queue.
void setEnginePaused(bool paused);

} // namespace UsenetBridge
} // namespace eMule
