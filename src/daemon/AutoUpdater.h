#pragma once

/// @file AutoUpdater.h
/// @brief `emulecored --autoupdate[=hours]`: fetch, verify and install new releases.
///
/// Polls emuleqt-version.json at the version-check interval (or the hours given
/// on the command line), downloads this platform's package, checks size + SHA-256
/// and hands it to the emuleqt-updater helper. The core then quits; the helper
/// installs, starts the core again with the same arguments and reopens every
/// local GUI that was attached.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace eMule {

class HttpStreamDownload;

class AutoUpdater : public QObject {
    Q_OBJECT

public:
    /// A GUI to reopen after the update.
    struct Gui {
        qint64 pid = 0;
        QStringList relaunch;   ///< [exe, args...]
    };

    /// What the updater needs from the daemon; defaults work without one (tests).
    struct Hooks {
        std::function<std::vector<Gui>()> localGuis;                       ///< attached local GUIs
        std::function<void(const QString& version)> announce;              ///< PushCoreUpdating
        std::function<bool(const QString& program, const QStringList& args)> launch; ///< detached start
        std::function<void()> quit;                                        ///< clean core shutdown
    };

    /// @p stateDir holds autoupdate.json, the package and the helper copy.
    /// @p hoursOverride from --autoupdate=N; 0 = the version-check interval.
    AutoUpdater(QString stateDir, int hoursOverride, QObject* parent = nullptr);
    ~AutoUpdater() override;

    void setHooks(Hooks hooks) { m_hooks = std::move(hooks); }
    /// Caller-owned; default is a GuardedNetworkAccessManager of our own.
    void setNetworkAccessManager(QNetworkAccessManager* nam) { m_nam = nam; }
    void setCurrentVersion(const QString& v) { m_currentVersion = v; }
    void setAppDir(const QString& dir) { m_appDir = dir; }
    void setCoreArguments(const QStringList& argv) { m_coreArgv = argv; }
    void setFirstCheckDelayMs(int ms) { m_firstDelayMs = ms; }

    /// Read the last helper result, then check once the first delay passed and hourly after.
    void start();
    /// Check now, ignoring the interval (still skips repeatedly failed versions).
    void checkNow();

    // -- pure helpers --------------------------------------------------------

    /// QCommandLineParser has no optional values: `--autoupdate=N` and
    /// `--autoupdate N` (N numeric) become `--autoupdate --autoupdate-hours N`.
    [[nodiscard]] static QStringList normalizeArgs(const QStringList& args);
    /// argv minus argv[0] and --hold-connect: what the restarted core gets.
    [[nodiscard]] static QStringList relaunchArgs(const QStringList& argv);
    /// 1..720 h; out of range = 0 (use the pref).
    [[nodiscard]] static int clampHours(int hours);
    [[nodiscard]] static bool isDue(qint64 lastCheck, qint64 now, qint64 intervalSecs);
    /// Manifest key: on Windows "<key>-installer" for an Inno install.
    [[nodiscard]] static QString packageKey(const QString& platformKey, bool innoInstall);
    /// "dmg", "inno" or "archive"; empty for an unknown file type.
    [[nodiscard]] static QString packageFormat(const QString& fileName);
    /// The .app bundle on macOS, else the program dir.
    [[nodiscard]] static QString installRoot(const QString& appDir);
    /// Running inside Docker/Podman: update by pulling the image instead.
    [[nodiscard]] static bool inContainer();
    /// emuleqt-updater[.exe]
    [[nodiscard]] static QString helperName();

signals:
    /// Progress for logs and tests: "checking", "uptodate", "downloading",
    /// "launching", "failed", "skipped".
    void stageChanged(const QString& stage, const QString& detail);

private:
    void checkIfDue();
    void fetchManifest();
    void onManifest(QNetworkReply* reply);
    void onDownloaded(bool ok);
    void handOver();
    void readResult();
    void recordFailure(const QString& version, const QString& why);
    void loadState();
    void saveState() const;
    [[nodiscard]] qint64 intervalSecs() const;
    [[nodiscard]] QString statePath() const;
    void setStage(const QString& stage, const QString& detail = {});

    QString m_stateDir;
    int m_hoursOverride = 0;
    Hooks m_hooks;
    QNetworkAccessManager* m_nam = nullptr;
    std::unique_ptr<QNetworkAccessManager> m_ownNam;
    std::unique_ptr<HttpStreamDownload> m_download;
    QPointer<QNetworkReply> m_reply;
    QTimer* m_tick = nullptr;
    QString m_currentVersion;
    QString m_appDir;
    QStringList m_coreArgv;
    int m_firstDelayMs = 60'000;

    // persisted
    qint64 m_lastCheck = 0;
    QString m_failedVersion;
    int m_failedCount = 0;
    QString m_pendingPackage;   ///< handed to the helper; removed on the next start

    // current attempt
    qint64 m_retryAfter = 0;    ///< failure back-off (not persisted)
    QString m_version;
    QString m_package;
    bool m_busy = false;
};

} // namespace eMule
