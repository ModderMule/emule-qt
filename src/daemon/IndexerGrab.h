#pragma once

/// @file IndexerGrab.h
/// @brief Start an indexer search; fetch one result and hand it to its engine:
///        .torrent/magnet to Torrents, .nzb to the Usenet queue. IPC and REST API share both.

#include "IndexerQuery.h"
#include "IndexerResult.h"

#include <QByteArray>
#include <QCborMap>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace eMule {

/// A magnet URI handed back as text (magnet-only torznab row or a /dl redirect).
[[nodiscard]] bool isMagnetPayload(const QByteArray& payload);
/// A .torrent (bencoded dictionary) rather than an .nzb. An aggregator's rows
/// can be either kind, so the payload decides, not the account.
[[nodiscard]] bool isTorrentPayload(const QByteArray& payload);

/// The row as GUI and API see it: no download URL, it carries the API key.
[[nodiscard]] QCborMap indexerResultToCbor(const indexer::IndexerResult& result, const QString& expression);

struct IndexerStartResult {
    quint32 id = 0;           ///< 0 on failure
    QString error;
    bool noIndexer = false;   ///< no enabled account serves the network
};

/// Validate @p query (IMDb id to digits, something to search for) and start it on
/// every enabled account of the network (@p torrents: torznab), @p only when named.
[[nodiscard]] IndexerStartResult startIndexerSearch(indexer::IndexerQuery query, bool torrents,
                                                    const QStringList& only);

struct IndexerGrabRequest {
    quint32 searchId = 0;          ///< 0: a restored tab, the row comes from the grab cache
    QString resultId;
    bool force = false;            ///< add even though downloaded before
    int category = 0;
    int priority = 0;              ///< Usenet -2..2
    bool paused = false;
    QList<int> keptFiles;          ///< empty = all
    QHash<QString, QStringList> keptEntries;
    QString entryPassword;
};

struct IndexerGrabOutcome {
    enum class Target { None, Torrent, Usenet };
    Target target = Target::None;  ///< None: fetch failed or no engine
    bool ok = false;
    QString id;                    ///< torrent id or Usenet item id
    QString error;
    int usenetOutcome = 3;         ///< usenet::UsenetAddOutcome; Failed
};

/// @p done runs once, later. Counts the Usenet add for the statistics.
void grabIndexerResult(const IndexerGrabRequest& request,
                       std::function<void(const IndexerGrabOutcome&)> done);

} // namespace eMule
