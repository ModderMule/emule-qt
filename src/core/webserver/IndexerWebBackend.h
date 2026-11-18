#pragma once

/// @file IndexerWebBackend.h
/// @brief What the REST API may ask of the newznab/torznab indexer searches.
///
/// Core never links the indexer module, so the /api/v1/indexer routes talk to this
/// interface and DaemonApp installs the implementation — the HosterWebBackend seam.
/// Rows are the StartIndexerSearch push rows, so the GUI and the API read one vocabulary.

#include <QCborArray>
#include <QCborMap>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace eMule {

class IndexerWebBackend {
public:
    virtual ~IndexerWebBackend() = default;

    /// False before the daemon built the indexer session.
    [[nodiscard]] virtual bool available() const = 0;

    struct Query {
        QString text;
        QList<int> categories;   ///< newznab category ids
        QString mode;            ///< "search", "tvsearch", "movie"; empty = search
        QString season;
        QString episode;
        QString imdbId;          ///< digits; "tt" dropped
        qint64 minSize = 0;      ///< bytes; 0 = no bound
        qint64 maxSize = 0;
        QStringList indexers;    ///< account names; empty = all enabled
        bool torrents = false;   ///< torznab instead of newznab
    };
    struct StartResult {
        quint32 id = 0;          ///< 0 on failure
        QString error;
        bool noIndexer = false;  ///< no enabled account serves this network
    };
    virtual StartResult start(const Query& query) = 0;

    /// Every search: id, network, query, running, accountsDone, accountsTotal, count.
    [[nodiscard]] virtual QCborArray searches() const = 0;
    /// One search's summary as in searches(); nullopt: unknown id.
    [[nodiscard]] virtual std::optional<QCborMap> search(quint32 id) const = 0;
    /// Its rows; nullopt: unknown id.
    [[nodiscard]] virtual std::optional<QCborArray> results(quint32 id) const = 0;
    virtual bool stop(quint32 id) = 0;
    virtual bool remove(quint32 id) = 0;

    struct GrabOptions {
        bool force = false;      ///< add even though downloaded before
        int category = 0;
        int priority = 0;        ///< Usenet -2..2
        bool paused = false;
    };
    struct GrabResult {
        bool ok = false;
        QString target;          ///< "usenet", "torrent"; empty when nothing was added
        QString id;              ///< Usenet item or torrent id
        QString error;
        int usenetOutcome = -1;  ///< UsenetWebAddOutcome when target is "usenet"
    };
    /// Fetch the row's .nzb/.torrent/magnet and add it. @p done runs once, later.
    virtual void grab(quint32 searchId, const QString& resultId, const GrabOptions& options,
                      std::function<void(const GrabResult&)> done) = 0;
};

} // namespace eMule
