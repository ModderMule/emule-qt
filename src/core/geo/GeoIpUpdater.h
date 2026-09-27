#pragma once

/// @file GeoIpUpdater.h
/// @brief Downloads GeoLite2-Country.mmdb with the user's MaxMind credentials.
///
/// MFC/MorphXT fetched GeoIPCountryCSV.zip from a public URL; MaxMind retired that,
/// and GeoLite2 needs an account ID + license key sent as HTTP Basic auth. The
/// license key must never reach a log line, so nothing here prints the request.
///
/// Releases also ship a copy in config/; start() adopts it when its build is newer
/// than the live one, so neither a release nor a download ever rolls the other back.
///
/// Schedule: an hourly tick updates when the database is missing or the last
/// successful check is a week old. A failure blocks automatic retries for 6 h —
/// GeoLite2 caps downloads per account per day.

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace eMule {

class IP2Country;

class GeoIpUpdater : public QObject {
    Q_OBJECT

public:
    using Callback = std::function<void(bool ok, const QString& message)>;

    GeoIpUpdater(IP2Country* ip2Country, const QString& configDir, QObject* parent = nullptr);
    ~GeoIpUpdater() override;

    /// Adopt a newer bundled database, open the database and arm the schedule.
    void start();
    void stop();

    /// Force a download now, ignoring the schedule and the failure back-off.
    /// @p done fires once with the outcome (also when no credentials are set).
    void updateNow(Callback done = {});

    [[nodiscard]] bool isRunning() const { return m_reply != nullptr; }
    [[nodiscard]] QString databasePath() const;

    /// Tests point this at a local server; defaults to the MaxMind permalink.
    void setDownloadUrl(const QUrl& url) { m_url = url; }

    /// Where the release's config/ lives; empty disables adopting. Defaults to the
    /// first existing AppConfig::bundleCandidates() entry.
    void setBundleDir(const QString& dir) { m_bundleDir = dir; }

    /// Update if due (missing db / week-old check) and not backing off.
    void checkSchedule();

signals:
    /// A new database was installed and is now active.
    void databaseChanged();
    void updateFinished(bool ok, const QString& message);

private:
    bool adoptBundledDatabase();
    void startRequest(const QUrl& url, bool withCredentials);
    void finish(bool ok, const QString& message);
    void onReplyFinished();
    [[nodiscard]] bool installDatabase(const QByteArray& mmdb, QString& error);

    IP2Country* m_ip2Country = nullptr;
    QString m_configDir;
    std::optional<QString> m_bundleDir;
    QUrl m_url;
    QTimer m_timer;
    QNetworkAccessManager* m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    std::vector<Callback> m_waiting;
    QDateTime m_lastFailure;
    int m_redirects = 0;
};

} // namespace eMule
