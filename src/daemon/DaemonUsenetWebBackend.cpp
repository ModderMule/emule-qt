/// @file DaemonUsenetWebBackend.cpp
/// @brief The web server's view of the Usenet queue — implementation.

#include "DaemonUsenetWebBackend.h"
#include "UsenetBridge.h"

#include "queue/UsenetHealth.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"

#include "prefs/Preferences.h"

#include <QCoreApplication>
#include <QPointer>

namespace eMule {

namespace {

usenet::UsenetAddOptions toOptions(const UsenetWebAddOptions& o)
{
    return {.source = usenet::UsenetAddSource::Manual, .force = o.force,
            .password = o.password, .category = o.category, .priority = o.priority,
            .paused = o.paused};
}

UsenetWebAddResult toWeb(const UsenetBridge::AddResult& r)
{
    UsenetWebAddResult out;
    out.itemId = r.itemId;
    out.error = r.error;
    // Without an attempt there is no outcome; Invalid is the honest reading of a
    // bad link or a fetch that returned no NZB.
    out.outcome = r.attempted ? static_cast<UsenetWebAddOutcome>(int(r.outcome))
                              : UsenetWebAddOutcome::Invalid;
    return out;
}

} // namespace

bool DaemonUsenetWebBackend::available() const
{
    return UsenetBridge::queue() != nullptr;
}

QCborArray DaemonUsenetWebBackend::queue() const
{
    QCborArray out;
    if (auto* q = UsenetBridge::queue()) {
        for (const auto* item : q->items())
            out.append(UsenetBridge::itemToCbor(*item));
    }
    return out;
}

bool DaemonUsenetWebBackend::contains(const QString& id) const
{
    auto* q = UsenetBridge::queue();
    return q && q->findItem(id) != nullptr;
}

QCborMap DaemonUsenetWebBackend::details(const QString& id) const
{
    auto* q = UsenetBridge::queue();
    const auto* item = q ? q->findItem(id) : nullptr;
    return item ? UsenetBridge::itemDetailsToCbor(*item) : QCborMap{};
}

QCborMap DaemonUsenetWebBackend::archiveEntries(const QString& id, int fileIndex)
{
    return UsenetBridge::archiveEntriesToCbor(id, fileIndex);
}

QCborMap DaemonUsenetWebBackend::downloadSplit() const
{
    QCborMap out;
    UsenetBridge::insertDownloadSplit(out);
    auto* q = UsenetBridge::queue();
    out.insert(QStringLiteral("rate"), q ? q->currentRate() : qint64(0));
    out.insert(QStringLiteral("paused"), thePrefs.usenetPaused());
    return out;
}

bool DaemonUsenetWebBackend::categoryExists(int category) const
{
    return UsenetBridge::categoryExists(category);
}

void DaemonUsenetWebBackend::setEnginePaused(bool paused)
{
    UsenetBridge::setEnginePaused(paused);
}

QString DaemonUsenetWebBackend::setFilesSkipped(const QString& id, const QList<int>& files,
                                                bool skipped)
{
    auto* q = UsenetBridge::queue();
    if (!q)
        return QCoreApplication::translate("eMule::IpcClientHandler", "Usenet engine unavailable");
    return q->setFilesSkipped(id, files, skipped);
}

bool DaemonUsenetWebBackend::pause(const QString& id)
{
    auto* q = UsenetBridge::queue();
    return q && q->pauseItem(id);
}

bool DaemonUsenetWebBackend::resume(const QString& id)
{
    auto* q = UsenetBridge::queue();
    return q && q->resumeItem(id, usenet::UsenetQueue::ResumeIntent::User);
}

bool DaemonUsenetWebBackend::remove(const QString& id, bool deleteFiles)
{
    auto* q = UsenetBridge::queue();
    return q && q->removeItem(id, deleteFiles);
}

bool DaemonUsenetWebBackend::setPriority(const QString& id, int priority)
{
    auto* q = UsenetBridge::queue();
    return q && q->setItemPriority(id, priority);
}

bool DaemonUsenetWebBackend::setCategory(const QString& id, int category)
{
    auto* q = UsenetBridge::queue();
    return q && UsenetBridge::categoryExists(category) && q->setItemCategory(id, category);
}

bool DaemonUsenetWebBackend::setPassword(const QString& id, const QString& password)
{
    auto* q = UsenetBridge::queue();
    return q && q->setItemPassword(id, password);
}

QString DaemonUsenetWebBackend::recheck(const QString& id)
{
    return UsenetBridge::recheck(id);
}

int DaemonUsenetWebBackend::applyCategoryAction(int category, UsenetWebCategoryAction action)
{
    return UsenetBridge::applyCategoryAction(category, action);
}

UsenetWebAddResult DaemonUsenetWebBackend::addNzb(const QByteArray& data, const QString& name,
                                                  const UsenetWebAddOptions& options)
{
    if (data.isEmpty()) {
        UsenetWebAddResult r;
        r.error = QCoreApplication::translate("eMule::IpcClientHandler", "The NZB file is empty.");
        r.outcome = UsenetWebAddOutcome::Invalid;
        return r;
    }
    return toWeb(UsenetBridge::addNzbData(data, name, toOptions(options),
                                          usenet::UsenetAddOrigin::File));
}

void DaemonUsenetWebBackend::addNzbUrl(const QString& url, const UsenetWebAddOptions& options,
                                       std::function<void(const UsenetWebAddResult&)> done)
{
    if (m_nzbUrlFetchesInFlight >= kMaxNzbUrlFetches) {
        UsenetWebAddResult r;
        r.busy = true;
        r.error = QCoreApplication::translate(
            "eMule::IpcClientHandler",
            "Too many NZB downloads are already running — try again in a moment.");
        done(r);
        return;
    }
    ++m_nzbUrlFetchesInFlight;

    // The fetch takes seconds and the daemon may restart the web server, or stop,
    // inside them.
    QPointer<QObject> guard(&m_fetchContext);
    UsenetBridge::addNzbUrl(&m_fetchContext, url, toOptions(options),
                            [this, guard, done = std::move(done)](const UsenetBridge::AddResult& r) {
        if (!guard)
            return;
        --m_nzbUrlFetchesInFlight;
        done(toWeb(r));
    });
}

} // namespace eMule
