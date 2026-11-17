#pragma once

/// @file Ed3kDeclineUrls.h
/// @brief Decline lists fetched from URLs the operator trusts (declined-records.md §2 "Shared
/// lists": operator choice, never default-on; pref ed3k.declineUrls, empty by default).
///
/// - Fetched at start(), when the URLs change and every 24 h (HttpFileDownload, 4 MiB at most).
/// - Each one cached as <dir>/decline-url-<n>.txt (n = its place in the list), first line
///   "# url: <url>": a cache of another URL at that place is not used. A failed fetch is logged
///   and the last cache kept.
/// - merged(): every usable cache parsed (parseDeclineList) and merged; Ed3kSession adds it to
///   decline.txt's lists.

#include "Ed3kDecline.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <vector>

namespace eMule {
class HttpDownloadJob;
}

namespace eMule::ed3k {

class Ed3kDeclineUrls : public QObject {
    Q_OBJECT

public:
    static constexpr std::uint64_t kMaxBytes = 4ull * 1024 * 1024;
    static constexpr int kRefreshMs = 24 * 3600 * 1000;

    /// The last fetch of one URL, for the GUI.
    struct Status {
        QString url;
        qint64 fetchedAt = 0;   ///< ms since the epoch: the cache in use; 0: none
        qint64 triedAt = 0;     ///< ms: the last fetch ended; 0: not tried this run
        QString error;          ///< of the last fetch; empty: it worked
        int entries = 0;        ///< lines the cache in use lists
        bool fetching = false;
    };

    explicit Ed3kDeclineUrls(QObject* parent = nullptr);
    ~Ed3kDeclineUrls() override;

    /// Folder of decline.txt; the caches go there. Read again at once.
    void setDir(const QString& dir);
    /// The pref; a change fetches the list again (while started). Empty and blank lines dropped.
    void setUrls(const QStringList& urls);
    [[nodiscard]] const QStringList& urls() const { return m_urls; }
    /// Fetches every URL now and every 24 h.
    void start();
    /// Ends the fetches and the timer; the caches stay in use.
    void stop();
    [[nodiscard]] bool isStarted() const { return m_started; }
    /// Every URL fetched again now.
    void fetchAll();

    /// The cached lists of the URLs set, merged.
    [[nodiscard]] const DeclineList& merged() const { return m_merged; }
    [[nodiscard]] const std::vector<Status>& status() const { return m_status; }
    /// decline-url-<n>.txt
    [[nodiscard]] static QString cacheName(int n);

signals:
    /// merged() changed (a fetch, a new URL list or folder).
    void listsChanged();
    /// A fetch started or ended.
    void statusChanged();

private:
    void fetch(int n);
    void onFetched(int n, const QString& url, bool ok, const QByteArray& data, const QString& error);
    /// The caches read again into merged() and the status; listsChanged when merged() changed.
    void reload();
    /// Caches past the list's end deleted.
    void dropStaleCaches() const;
    [[nodiscard]] QString cachePath(int n) const;

    QString m_dir;
    QStringList m_urls;
    std::vector<Status> m_status;
    std::vector<QPointer<HttpDownloadJob>> m_jobs;
    DeclineList m_merged;
    QTimer m_timer;
    bool m_started = false;
};

} // namespace eMule::ed3k
