#pragma once

/// @file VersionChecker.h
/// @brief HTTP-based version checker — fetches latest version from emule-qt.org.

#include "app/VersionManifest.h"   // src/core

#include <QDate>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace eMule {

class VersionChecker : public QObject {
    Q_OBJECT

public:
    /// Everything emuleqt-version.json advertises (core/app/VersionManifest.h).
    using Manifest = VersionManifestInfo;

    /// Parse a manifest document. Pure: no network, no preferences, no widgets.
    [[nodiscard]] static Manifest parse(const QByteArray& json) { return VersionManifest::parse(json); }

    /// True when @p remote is a strictly higher version than @p local. Compares
    /// numerically, so 0.10.0 beats 0.9.0; unparseable input is never newer.
    [[nodiscard]] static bool isNewer(const QString& remote, const QString& local)
    {
        return VersionManifest::isNewer(remote, local);
    }

    /// Key this build looks itself up under in the manifest's "downloads" map.
    [[nodiscard]] static QString platformKey() { return VersionManifest::platformKey(); }

    /// URL the check fetches; EMULEQT_VERSION_MANIFEST_URL overrides it.
    [[nodiscard]] static QString manifestUrl() { return VersionManifest::manifestUrl(); }

    explicit VersionChecker(QObject* parent = nullptr);

    /// Seed the interval bookkeeping with the persisted timestamp (0 = never checked).
    ///
    /// Where that timestamp is stored is the owner's business — MainWindow reads it
    /// from UiState and writes it back on @ref checkCompleted. Keeping it out of here
    /// leaves this class testable without dragging in the GUI's state file.
    void setLastCheck(int64_t secs) { m_lastCheck = secs; }

    /// Check now, whatever the versionCheckEnabled setting and the interval say, and
    /// report the outcome either way. Tools -> Links -> Version Check.
    void checkNow();

    /// Check only when enabled and the configured interval has elapsed since the last
    /// completed check. Silent unless there is a new version.
    void checkIfDue();

    /// Run checkIfDue() now and once an hour after that. Idempotent — safe to call on
    /// every IPC reconnect. Hourly is fine granularity for a 1-14 day interval, and
    /// the comparison is wall-clock, so sleeping the machine cannot skip a check.
    void startPeriodicChecks();

signals:
    /// @p manual distinguishes the menu item from the interval timer: an automatic
    /// check that finds nothing must not interrupt the user.
    void newVersionAvailable(const eMule::VersionChecker::Manifest& info, bool manual);
    void upToDate(const QString& currentVersion, bool manual);
    void checkFailed(const QString& reason, bool manual);

    /// A check completed and produced a usable manifest. Carries the epoch seconds to
    /// record as the last check; a failed fetch deliberately does not emit, so an
    /// offline session retries on the next tick instead of waiting out the interval.
    void checkCompleted(qint64 whenSecs);

private:
    void start(bool manual);
    void onReplyFinished(QNetworkReply* reply);

    QNetworkAccessManager* m_nam = nullptr;
    QTimer* m_periodicTimer = nullptr;
    int64_t m_lastCheck = 0;
    bool m_inFlight = false;
};

} // namespace eMule

