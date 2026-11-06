#pragma once

/// @file UsenetRetryGrabber.h
/// @brief A failed Usenet job from an indexer fetches another release of it.
///
/// SABnzbd's new_nzb_on_failure, plus a fallback. When a job that came from an
/// indexer grab fails (usenetRetryOtherNzb on):
///   1. the indexer's `X-DNZB-Failure` link, when it sent one with the .nzb:
///      reports the failure and answers a replacement NZB;
///   2. otherwise a quiet search of every enabled Usenet indexer for the same
///      episode (UsenetReleaseName) or the same title, and the newest result
///      not tried yet, of a similar size.
/// Up to kMaxRetries replacements per chain. Never for a missing password: a
/// replacement would need one just the same.
///
/// Lives in the daemon because it is where eMule::Usenet and eMule::Indexer
/// meet, like the feed sink in DaemonApp.

#include <QObject>
#include <QSet>
#include <QString>

namespace eMule {

namespace indexer {
class IndexerClient;
struct IndexerResult;
}
namespace usenet {
class UsenetSession;
}

class UsenetRetryGrabber : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxRetries = 3;

    explicit UsenetRetryGrabber(usenet::UsenetSession* session, QObject* parent = nullptr);
    ~UsenetRetryGrabber() override;

private:
    /// What a retry needs of the failed item, copied: it may be removed meanwhile.
    struct Job {
        QString itemId;
        QString itemName;
        QString indexerName;
        QString title;
        QString failureUrl;
        QStringList triedGuids;
        int retries = 0;
        qint64 size = 0;
        int category = 0;
        int priority = 0;
    };

    void onItemFinished(const QString& id, bool success, const QString& message);
    void tryFailureUrl(const Job& job);
    void search(const Job& job);
    void pickAndGrab(const Job& job, QList<indexer::IndexerResult> rows);
    void grabNext(const Job& job, QList<indexer::IndexerResult> candidates);
    /// false = refused (e.g. same articles); the caller tries the next one
    bool add(const Job& job, const QByteArray& nzb, const QString& name, const QString& password,
             const QString& guid, const QString& indexerName, const QString& failureUrl);
    void finish(const Job& job, const QString& outcome);

    usenet::UsenetSession* m_session = nullptr;
    indexer::IndexerClient* m_client = nullptr;
    QSet<QString> m_running;   ///< item ids with a retry out
};

} // namespace eMule
