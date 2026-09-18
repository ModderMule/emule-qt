/// @file UsenetBridge.cpp
/// @brief The daemon's one implementation of each Usenet request — implementation.

#include "UsenetBridge.h"

#include "IndexerQuery.h"
#include "UsenetSession.h"
#include "nzb/NzbFile.h"
#include "nzb/NzbUrlFetch.h"
#include "post/UsenetUnpacker.h"
#include "queue/UsenetHealth.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCborArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

namespace eMule::UsenetBridge {

namespace {

/// These sentences used to be tr() calls in IpcClientHandler; keeping the context
/// keeps their translations.
QString trIpc(const char* text)
{
    return QCoreApplication::translate("eMule::IpcClientHandler", text);
}

} // namespace

usenet::UsenetQueue* queue()
{
    return usenet::theUsenetSession ? usenet::theUsenetSession->queue() : nullptr;
}

QCborMap itemToCbor(const usenet::UsenetQueueItem& item)
{
    // Preview is container-aware since phase 6b, and only the queue's streaming
    // index knows whether a `.rXX` is a mappable stored volume. Absent a queue
    // (never, in the daemon) the answer degrades to the phase 6a predicate.
    auto* q = queue();
    const QList<usenet::UsenetQueue::FileView> views =
        q ? q->fileViews(item.id) : QList<usenet::UsenetQueue::FileView>{};

    QCborArray files;
    for (int i = 0; i < item.files.size() && i < item.nzb.files.size(); ++i) {
        const auto& st = item.files.at(i);
        const auto& info = item.nzb.files.at(i);

        const auto preview =
            q ? q->previewability(item.id, i)
              : usenet::UsenetQueue::PreviewInfo{
                    item.isFilePreviewable(i) && st.availableEnd() > 0, {}};

        // PAR2's name, then the article's own =ybegin, then the subject: for an
        // obfuscated post the subject carries nothing readable, and for a fully
        // obfuscated one neither does =ybegin.
        QString name = item.bestFileName(i);
        if (name.isEmpty())
            name = info.subject;

        int done = 0;
        for (qsizetype b = 0; b < st.done.size(); ++b) {
            if (st.done.testBit(b))
                ++done;
        }
        const int total = int(info.segments.size());

        QCborMap fileMap{
            {QStringLiteral("name"),      name},
            {QStringLiteral("size"),      static_cast<qint64>(st.declaredSize > 0
                                              ? st.declaredSize : info.encodedBytes())},
            {QStringLiteral("percent"),   total > 0 ? done * 100 / total : 0},
            {QStringLiteral("finalPath"), st.finalPath},
            {QStringLiteral("isPar2"),    info.isPar2()},
            {QStringLiteral("missingSegments"), st.missingSegments},
            // The position in the NZB, which is what the preview URL addresses.
            // Sent explicitly rather than left as the array index, so a future
            // filtered list cannot silently shift it.
            {QStringLiteral("index"),     i},
            // Whether a Preview action should be offered at all. Decided here,
            // not in the GUI: it needs the file's real (post-yEnc) name, the
            // ED2K type table, and how far the *contiguous* prefix has got —
            // none of which the GUI has.
            {QStringLiteral("previewable"), preview.previewable},
            // Why not, when not. A stored RAR set streams; a compressed or
            // encrypted one never will, and the GUI shows this instead of
            // leaving the user with an unexplained greyed-out entry.
            {QStringLiteral("previewNote"), preview.note},
        };

        // What the file is doing and where its articles stand. The map rides
        // only on files that are not uniform, so a 250 ms push stays small.
        const usenet::UsenetQueue::FileView view =
            i < views.size() ? views.at(i) : usenet::UsenetQueue::FileView{};
        fileMap.insert(QStringLiteral("state"), int(view.state));
        fileMap.insert(QStringLiteral("skipped"), st.skipped);
        if (!view.segmentMap.isEmpty())
            fileMap.insert(QStringLiteral("segmentMap"), view.segmentMap);
        files.append(fileMap);
    }

    int missing = 0;
    for (const auto& st : item.files) {
        if (!st.isSkipped())
            missing += st.missingSegments;
    }

    // What the release actually put on disk. `files` cannot answer this: an
    // unpacked release publishes the extracted members, which are not NZB files.
    // Empty until the item completes, so a running queue pays nothing for it.
    //
    // relPath is resolved here rather than in the GUI because the daemon's
    // incoming directory is the authoritative one — against a remote core the
    // GUI's own copy is somebody else's setting.
    const QDir incoming(thePrefs.incomingDir());
    QCborArray publishedFiles;
    for (const QString& path : item.publishedPaths) {
        const QFileInfo fi(path);
        QString rel = incoming.relativeFilePath(path);
        // A result that escapes the incoming root cannot be addressed by the
        // browse route at all; say so with an empty string rather than a
        // traversal the web server would refuse anyway.
        if (rel.startsWith(QLatin1String("..")) || QDir::isAbsolutePath(rel))
            rel.clear();
        publishedFiles.append(QCborMap{
            {QStringLiteral("name"),    fi.fileName()},
            {QStringLiteral("path"),    path},
            {QStringLiteral("relPath"), rel},
            {QStringLiteral("size"),    static_cast<qint64>(fi.size())},
        });
    }

    return QCborMap{
        {QStringLiteral("id"),              item.id},
        {QStringLiteral("name"),            item.name},
        {QStringLiteral("status"),          int(item.status)},
        {QStringLiteral("statusText"),      usenet::describeUsenetItemStatus(item.status)},
        {QStringLiteral("priority"),        item.priority},
        {QStringLiteral("category"),        item.category},
        {QStringLiteral("percent"),         item.percentComplete()},
        {QStringLiteral("totalBytes"),      static_cast<qint64>(item.totalEncodedBytes())},
        {QStringLiteral("decodedBytes"),    static_cast<qint64>(item.decodedBytes())},
        {QStringLiteral("segmentCount"),    item.segmentCount()},
        {QStringLiteral("doneSegments"),    item.doneSegmentCount()},
        {QStringLiteral("missingSegments"), missing},
        {QStringLiteral("error"),           item.error},
        // Whether a password is set, never the password itself — the same
        // one-way contract GetNewsServers keeps for provider accounts.
        {QStringLiteral("hasPassword"),     !item.nzb.password.isEmpty()},
        // The release is password-protected and no password we have opens it.
        // A flag rather than a match on `error`, which is translated.
        {QStringLiteral("passwordRequired"), item.passwordRequired},
        // Post-processing progress. Separate from `percent`, which is segment
        // counts: a repair or an unpack moves no segments at all, so a single
        // figure would sit frozen at 100% for the whole of it.
        {QStringLiteral("postPercent"),     item.postPercent},
        {QStringLiteral("postDetail"),      item.postDetail},
        // Why a queued item is standing still — an allowance spent, today.
        // Separate from `error`, which means the download failed.
        {QStringLiteral("stalledReason"),   item.stalledReason},
        // How much of the release looks obtainable, combining what the NZB never
        // listed with what no account still holds. **-1 means not assessed**,
        // which the GUI must not render as 100.
        {QStringLiteral("healthPercent"),   item.healthPercent},
        {QStringLiteral("healthMissingBytes"),
                                            static_cast<qint64>(item.healthMissingBytes)},
        {QStringLiteral("healthRecoveryBytes"),
                                            static_cast<qint64>(item.healthRecoveryBytes)},
        // False means the figure is the NZB's own arithmetic and no server was
        // asked — a different statement from the same number with a probe behind it.
        {QStringLiteral("healthProbed"),    item.healthProbed},
        {QStringLiteral("files"),           files},
        {QStringLiteral("publishedFiles"),  publishedFiles},
    };
}

QCborMap itemDetailsToCbor(const usenet::UsenetQueueItem& item)
{
    // Built on itemToCbor() rather than beside it, so the details view and the
    // list can never disagree about a field they share. What is added here is the
    // per-file detail that would be wasteful on a 250 ms push.
    QCborMap out = itemToCbor(item);

    // Re-emit `files` with the detail fields merged in, keeping every key the
    // queue row already carries (percent, previewable, previewNote, index, ...).
    const QCborArray base = out.value(QStringLiteral("files")).toArray();
    QCborArray files;
    for (int i = 0; i < item.nzb.files.size(); ++i) {
        const usenet::NzbFileInfo& info = item.nzb.files.at(i);
        const usenet::UsenetFileState& st =
            i < item.files.size() ? item.files.at(i) : usenet::UsenetFileState{};

        QCborMap f = i < base.size() ? base.at(i).toMap() : QCborMap{};

        int done = 0;
        for (qsizetype b = 0; b < st.done.size(); ++b) {
            if (st.done.testBit(b))
                ++done;
        }

        QCborArray groups;
        for (const QString& g : info.groups)
            groups.append(g);

        f.insert(QStringLiteral("subject"),      info.subject);
        f.insert(QStringLiteral("poster"),       info.poster);
        f.insert(QStringLiteral("date"),         static_cast<qint64>(info.date));
        f.insert(QStringLiteral("groups"),       groups);
        f.insert(QStringLiteral("segmentCount"), int(info.segments.size()));
        f.insert(QStringLiteral("doneSegments"), done);
        // What the subject's (n/m) counter claims, and what it claims the NZB
        // never listed. Distinct from missingSegments, which is what no server
        // would serve — one is the indexer's shortfall, the other the network's.
        f.insert(QStringLiteral("partsTotal"),        info.partsTotal);
        f.insert(QStringLiteral("nzbMissingSegments"), info.missingSegmentCount());
        f.insert(QStringLiteral("encodedBytes"),  static_cast<qint64>(info.encodedBytes()));
        f.insert(QStringLiteral("declaredSize"),  static_cast<qint64>(st.declaredSize));
        f.insert(QStringLiteral("decodedBytes"),  static_cast<qint64>(st.decodedBytes));
        f.insert(QStringLiteral("par2Blocks"),    info.par2RecoveryBlocks());
        f.insert(QStringLiteral("requestedPar2"), item.requestedPar2.contains(i));
        f.insert(QStringLiteral("finalized"),     st.finalized);
        f.insert(QStringLiteral("tempPath"),      st.tempPath);

        files.append(f);
    }
    out.insert(QStringLiteral("files"), files);
    return out;
}

QCborMap archiveEntriesToCbor(const QString& itemId, int fileIndex)
{
    auto* q = queue();
    if (!q)
        return QCborMap{{QStringLiteral("status"), 0}};

    const auto listing = q->listArchiveEntries(itemId, fileIndex);

    QCborArray entries;
    for (const auto& e : listing.entries) {
        entries.append(QCborMap{
            {QStringLiteral("entry"),    e.entry},
            {QStringLiteral("name"),     e.name},
            {QStringLiteral("size"),     e.size},
            {QStringLiteral("playable"), e.playable},
            {QStringLiteral("note"),     e.note},
        });
    }

    return QCborMap{
        {QStringLiteral("status"),  int(listing.status)},
        {QStringLiteral("note"),    listing.note},
        {QStringLiteral("entries"), entries},
    };
}

void insertDownloadSplit(QCborMap& stats)
{
    // Stopped or not yet constructed reads as unthrottled, which is what it is.
    const usenet::UsenetSession::DownloadSplit split =
        usenet::theUsenetSession ? usenet::theUsenetSession->lastSplit()
                                 : usenet::UsenetSession::DownloadSplit{thePrefs.maxDownload()};
    stats.insert(QStringLiteral("maxDownloadKb"), static_cast<qint64>(split.ceilingKb));
    stats.insert(QStringLiteral("usenetLimitKb"), split.usenetKb());
    stats.insert(QStringLiteral("ed2kBudgetKb"), split.ed2kKb());
}

bool categoryExists(int category)
{
    return category >= 0 && category < int(thePrefs.categoryCount());
}

void noteAdd(usenet::UsenetAddOrigin origin, usenet::UsenetAddOutcome outcome)
{
    if (auto* q = queue())
        q->stats().noteAdd(origin, outcome);
}

AddResult addNzbData(const QByteArray& data, const QString& name,
                     const usenet::UsenetAddOptions& options, usenet::UsenetAddOrigin origin)
{
    AddResult r;
    auto* q = queue();
    if (!q) {
        r.error = trIpc("The Usenet engine is not running.");
        return r;
    }

    r.attempted = true;
    r.itemId = q->addNzb(data, name, r.error, options, &r.outcome);
    noteAdd(origin, r.outcome);

    // A user-facing refusal: every surface shows it as a sentence.
    if (r.itemId.isEmpty() && r.error.isEmpty())
        r.error = trIpc("The NZB could not be read.");
    return r;
}

void addNzbUrl(QObject* context, const QString& urlText, const usenet::UsenetAddOptions& options,
               std::function<void(const AddResult&)> done)
{
    const QUrl url(urlText.trimmed(), QUrl::StrictMode);
    if (const QString why = usenet::NzbUrlFetch::rejectReason(url); !why.isEmpty()) {
        AddResult r;
        r.error = why;
        done(r);
        return;
    }

    usenet::NzbUrlFetch::fetch(context, url,
                               [options, done = std::move(done)]
                               (const usenet::NzbUrlFetch::Result& result) {
        // A fetch failure and a parse failure are different problems and the
        // user fixes them differently: one means the link or the network is
        // wrong, the other means the link was fine and what came back was not an
        // NZB — an indexer's HTML login page, most often. Qt embeds the request
        // URL in its own error strings, so the key rides out with them unless
        // this is redacted.
        if (!result.ok()) {
            AddResult r;
            r.error = indexer::redactApiKey(result.error);
            done(r);
            return;
        }

        // An empty name is deliberate and useful: addNzb() then falls back to the
        // NZB's own <meta type="name">, which is a better answer than anything
        // an API-style URL could have told us.
        done(addNzbData(result.data, result.name, options, usenet::UsenetAddOrigin::Url));
    });
}

QString recheck(const QString& itemId)
{
    auto* q = queue();
    if (!q)
        return trIpc("The Usenet engine is not running.");

    // Not an error about the release: the item may be downloading, may already
    // be checking, or there may be no account to ask. A probe that cannot run
    // produces no verdict, which is the whole safety property.
    return q->recheckItem(itemId) ? QString()
                                  : trIpc("This download cannot be checked right now.");
}

QCborMap inspectNzb(const QByteArray& data, const QString& name, QString& error)
{
    usenet::NzbInfo nzb;
    if (!usenet::NzbFile::parse(data, nzb, error))
        return {};
    if (nzb.isEmpty()) {
        error = QCoreApplication::translate("eMule::usenet::UsenetQueue",
                                            "The NZB contains no files.");
        return {};
    }

    QCborArray files;
    for (int i = 0; i < nzb.files.size(); ++i) {
        const usenet::NzbFileInfo& info = nzb.files.at(i);
        // The same key the queue widens a skip by, so the dialog can check a
        // whole set at once and agree with what the daemon will do.
        const auto position = usenet::UsenetUnpacker::volumePositionOf(info.fileName);
        files.append(QCborMap{
            {QStringLiteral("index"),  i},
            {QStringLiteral("name"),   info.fileName.isEmpty() ? info.subject : info.fileName},
            {QStringLiteral("size"),   static_cast<qint64>(info.encodedBytes())},
            {QStringLiteral("isPar2"), info.isPar2()},
            {QStringLiteral("setKey"), position.index >= 0 ? position.baseName : QString()},
        });
    }
    return QCborMap{
        {QStringLiteral("name"),  name.isEmpty() ? nzb.name : name},
        {QStringLiteral("files"), files},
    };
}

void setEnginePaused(bool paused)
{
    // Saved first: the queue's signal reaches every client, and one that reads
    // GetPreferences in reaction must see the new value.
    if (thePrefs.usenetPaused() != paused) {
        thePrefs.setUsenetPaused(paused);
        thePrefs.save();
    }
    if (auto* q = queue())
        q->setEnginePaused(paused);
}

int applyCategoryAction(int category, UsenetWebCategoryAction action)
{
    auto* q = queue();
    if (!q || !categoryExists(category))
        return -1;

    // Snapshot the ids first: Cancel removes items while we walk, and items()
    // hands out pointers into the very list it is about to mutate.
    QStringList ids;
    for (const auto* item : q->items()) {
        // Index 0 is "All" and means every release, categorised or not — the
        // same thing the "All" tab shows.
        if (category == 0 || item->category == category)
            ids.append(item->id);
    }

    int acted = 0;
    for (const QString& id : std::as_const(ids)) {
        switch (action) {
        case UsenetWebCategoryAction::Pause:
            acted += q->pauseItem(id) ? 1 : 0;
            break;
        case UsenetWebCategoryAction::Resume:
            // Bulk: a category-wide resume must not wave through a release a
            // check stopped, which nobody looked at.
            acted += q->resumeItem(id, usenet::UsenetQueue::ResumeIntent::Bulk) ? 1 : 0;
            break;
        case UsenetWebCategoryAction::Cancel:
            acted += q->removeItem(id, /*deleteFiles*/ true) ? 1 : 0;
            break;
        }
    }

    const QString verb = action == UsenetWebCategoryAction::Pause    ? QStringLiteral("Pause")
                         : action == UsenetWebCategoryAction::Resume ? QStringLiteral("Resume")
                                                                     : QStringLiteral("Cancel");
    logInfo(QStringLiteral("Usenet: %1 applied to %2 item(s) in \"%3\"")
                .arg(verb, QString::number(acted), thePrefs.category(category).displayName()));
    return acted;
}

} // namespace eMule::UsenetBridge
