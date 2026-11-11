#pragma once

/// @file UsenetArticleGrab.h
/// @brief A few articles of an NZB, fetched outside the queue: the base of
///        search Details › Images and › NFO.
///
/// Own small pool (kLanes per server), so the queue's connection budget is not
/// touched beyond that. Each article lands at its real offset in targetPath();
/// received() says what arrived. Every byte read from a server is reported by
/// bytesUsed(), so the caller can charge it to the account's meter.

#include "nzb/NzbInfo.h"
#include "nntp/NntpError.h"
#include "prefs/NewsServer.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QNetworkProxy>
#include <QObject>
#include <QString>
#include <QTemporaryDir>

#include <functional>
#include <memory>
#include <vector>

namespace eMule::usenet {

class ArticleFetcher;
class ArticleWriter;
class NntpServerPool;
class NntpSocket;

class UsenetArticleGrab : public QObject {
    Q_OBJECT

public:
    static constexpr int kLanes = 3;   ///< connections per server, articles in flight

    /// @p servers: disabled ones (e.g. over quota) are not asked.
    UsenetArticleGrab(QList<NewsServer> servers, const QNetworkProxy& proxy, QObject* parent = nullptr);
    ~UsenetArticleGrab() override;

signals:
    /// Raw bytes read from @p accountId's server (article + handshake).
    void bytesUsed(const QString& accountId, qint64 bytes);

protected:
    /// One article.
    struct Job {
        int file = -1;
        int segment = -1;
    };

    /// Shared start checks; false = a note was set in @p note and nothing runs.
    [[nodiscard]] bool canStart(QString& note) const;
    /// Every segment of @p file.
    [[nodiscard]] QList<Job> allSegments(int file) const;
    void fetchJobs(QList<Job> jobs, std::function<void()> done);
    /// Where @p file's articles are written. Default: a file per NZB index in m_dir.
    [[nodiscard]] virtual QString targetPath(int file) const;
    /// An article arrived; @p fetcher still holds its facts.
    virtual void articleDone(const Job& job, const ArticleFetcher& fetcher);
    /// Decoded bytes of @p file that arrived; @p contiguous = readable from 0.
    qint64 received(int file, qint64* contiguous = nullptr) const;
    /// @p file as far as it arrived, empty when nothing did.
    [[nodiscard]] QByteArray readReceived(int file) const;
    /// Stop: connections back to the pool, nothing more runs. True the first time.
    bool stop();
    [[nodiscard]] bool isStopped() const { return m_done; }

    QTemporaryDir m_dir;
    NzbInfo m_nzb;

private:
    /// One connection working through the jobs.
    struct Lane {
        std::unique_ptr<ArticleFetcher> fetcher;
        std::unique_ptr<ArticleWriter> writer;
        NntpSocket* socket = nullptr;
        Job job;
        int level = 0;
        int retries = 0;
        bool busy = false;   ///< connecting or fetching
    };

    void runLane(Lane& lane);
    void onFetchFinished(Lane& lane, NntpError error, const QString& text);
    void kickIdleLanes();
    void dropSocket(Lane& lane, bool reusable);
    void chargeBytes(NntpSocket* socket);
    [[nodiscard]] bool laneHasServer(int level) const;

    std::unique_ptr<NntpServerPool> m_pool;
    std::vector<std::unique_ptr<Lane>> m_lanes;
    QList<Job> m_jobs;
    std::function<void()> m_jobsDone;
    QHash<int, QMap<qint64, qint64>> m_ranges;   ///< file -> decoded offset -> length
    int m_missing = 0;
    bool m_done = false;
};

} // namespace eMule::usenet
