/// @file IndexerGrab.cpp
/// @brief Indexer result → its engine — implementation.

#include "IndexerGrab.h"

#include "BtBridge.h"
#include "IndexerClient.h"
#include "IndexerSearchList.h"
#include "UsenetBridge.h"
#include "UsenetSession.h"
#include "queue/UsenetQueue.h"

#include <QCoreApplication>

#include <algorithm>
#include <optional>

namespace eMule {

namespace {

void addTorrentPayload(const QByteArray& payload, const IndexerGrabRequest& request,
                       const std::function<void(const IndexerGrabOutcome&)>& done)
{
    const QCborMap added = isMagnetPayload(payload)
                               ? BtBridge::addMagnetUri(QString::fromUtf8(payload), request.paused)
                               : BtBridge::addTorrentData(payload, request.paused, request.keptFiles);
    const int outcome = int(added.value(QStringLiteral("outcome")).toInteger(4));
    IndexerGrabOutcome out;
    out.target = IndexerGrabOutcome::Target::Torrent;
    out.ok = outcome == 0 || outcome == 1;   // added, or already there
    if (out.ok)
        out.id = added.value(QStringLiteral("id")).toString();
    else
        out.error = added.value(QStringLiteral("message")).toString();
    done(out);
}

void addNzbPayload(const QByteArray& payload, const QString& name, const QString& password,
                   const IndexerGrabRequest& request,
                   const std::optional<indexer::IndexerSearchList::GrabSource>& source,
                   const std::function<void(const IndexerGrabOutcome&)>& done)
{
    if (!usenet::theUsenetSession || !usenet::theUsenetSession->queue()) {
        done({.error = QCoreApplication::translate("eMule::IpcClientHandler", "The Usenet engine is not running.")});
        return;
    }
    // A grab is a person clicking Download, so the feed's password arrives as
    // a Manual one and outranks the NZB's own metadata: the indexer knows
    // the release, the NZB says whatever its poster wrote.
    usenet::UsenetOrigin origin;
    if (source) {
        origin.indexerName = source->indexerName;
        origin.guid = source->guid;
        origin.title = source->title;
        origin.failureUrl = indexer::IndexerClient::failureUrlFor(source->downloadUrl)
                                .toString(QUrl::FullyEncoded);
    }
    QString addError;
    usenet::UsenetAddOutcome outcome = usenet::UsenetAddOutcome::Failed;
    const QString itemId = usenet::theUsenetSession->queue()->addNzb(
        payload, name, addError,
        {.force = request.force, .password = password.isEmpty() ? request.entryPassword : password,
         .category = request.category, .priority = request.priority, .paused = request.paused,
         .keptFiles = request.keptFiles, .keptEntries = request.keptEntries, .origin = origin},
        &outcome);
    UsenetBridge::noteAdd(usenet::UsenetAddOrigin::IndexerGrab, outcome);
    done({.target = IndexerGrabOutcome::Target::Usenet, .ok = !itemId.isEmpty(), .id = itemId,
          .error = addError, .usenetOutcome = int(outcome)});
}

} // namespace

bool isMagnetPayload(const QByteArray& payload)
{
    return payload.startsWith("magnet:");
}

bool isTorrentPayload(const QByteArray& payload)
{
    qsizetype i = 0;
    while (i < payload.size() && QChar::isSpace(uchar(payload.at(i))))
        ++i;
    return i < payload.size() && payload.at(i) == 'd';
}

IndexerStartResult startIndexerSearch(indexer::IndexerQuery query, bool torrents, const QStringList& only)
{
    if (!indexer::theIndexerSearchList)
        return {.error = QCoreApplication::translate("eMule::IpcClientHandler", "The daemon is still starting.")};
    query.text = query.text.trimmed();
    query.season = query.season.trimmed();
    query.episode = query.episode.trimmed();
    query.imdbId.removeIf([](QChar c) { return !c.isDigit(); });
    query.minSize = std::max<qint64>(0, query.minSize);
    query.maxSize = std::max<qint64>(0, query.maxSize);
    if (query.mode.isEmpty())
        query.mode = indexer::kModeSearch.toString();
    if (query.text.isEmpty() && query.categories.isEmpty() && query.imdbId.isEmpty())
        return {.error = QCoreApplication::translate("eMule::IpcClientHandler", "Enter something to search for.")};

    const IndexerKind wanted = torrents ? IndexerKind::Torznab : IndexerKind::Newznab;
    if (!indexer::theIndexerSearchList->hasEnabledAccount(wanted)) {
        return {.error = torrents
                             ? QCoreApplication::translate("eMule::IpcClientHandler", "No torrent indexer is configured.")
                             : QCoreApplication::translate("eMule::IpcClientHandler", "No Usenet indexer is configured."),
                .noIndexer = true};
    }
    QString error;
    const quint32 id = indexer::theIndexerSearchList->startSearch(query, wanted, only, error);
    return {.id = id, .error = id ? QString() : error};
}

void grabIndexerResult(const IndexerGrabRequest& request,
                       std::function<void(const IndexerGrabOutcome&)> done)
{
    // the Usenet engine is checked once the payload says it is an .nzb
    if (!indexer::theIndexerSearchList) {
        done({.error = QCoreApplication::translate("eMule::IpcClientHandler", "No indexer is configured.")});
        return;
    }
    // the row, for a replacement NZB should this one fail
    auto source = indexer::theIndexerSearchList->grabSource(request.searchId, request.resultId);
    indexer::theIndexerSearchList->grab(
        request.searchId, request.resultId,
        [request, source = std::move(source), done = std::move(done)](
            bool ok, const QByteArray& payload, const QString& name, const QString& password,
            const QString& error) {
        if (!ok || payload.isEmpty()) {
            done({.error = error.isEmpty() ? QCoreApplication::translate("eMule::IpcClientHandler", "The indexer returned no NZB.") : error});
            return;
        }
        // Torznab row: to the Torrents tab
        if (isTorrentPayload(payload) || isMagnetPayload(payload))
            addTorrentPayload(payload, request, done);
        else
            addNzbPayload(payload, name, password, request, source, done);
    });
}

} // namespace eMule
