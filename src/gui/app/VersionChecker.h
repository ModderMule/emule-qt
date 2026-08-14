#pragma once

/// @file VersionChecker.h
/// @brief HTTP-based version checker — fetches latest version from emule-qt.org.

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
    /// Everything emuleqt-version.json advertises.
    ///
    /// Fields the document omits stay empty; @ref error is the only thing that makes
    /// the manifest unusable. A missing or placeholder download entry is normal — the
    /// manifest announces a release before every platform's binary exists.
    struct Manifest {
        QString latest;        ///< "latest", e.g. "0.2.0"
        QDate   date;          ///< "date" (ISO-8601 instant), invalid when absent/malformed
        QString releaseNotes;  ///< "releaseNotes"
        QString minVersion;    ///< "minVersion"
        QString downloadUrl;   ///< this platform's "downloads" entry, empty when unusable
        QString error;         ///< why the document could not be used; empty on success

        [[nodiscard]] bool isValid() const { return error.isEmpty(); }
    };

    /// Parse a manifest document. Pure: no network, no preferences, no widgets.
    [[nodiscard]] static Manifest parse(const QByteArray& json);

    /// True when @p remote is a strictly higher version than @p local. Compares
    /// numerically, so 0.10.0 beats 0.9.0; unparseable input is never newer.
    [[nodiscard]] static bool isNewer(const QString& remote, const QString& local);

    /// Key this build looks itself up under in the manifest's "downloads" map —
    /// "mac-arm64", "windows-x64", … Empty on an OS/CPU combination the manifest
    /// has no entry for, in which case no download URL is ever offered.
    [[nodiscard]] static QString platformKey();

    /// URL the check fetches. Overridable through EMULEQT_VERSION_MANIFEST_URL so the
    /// dialogs can be exercised against a fixture; it only ever decides which message
    /// box appears, never what gets downloaded.
    [[nodiscard]] static QString manifestUrl();

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

Q_DECLARE_METATYPE(eMule::VersionChecker::Manifest)
