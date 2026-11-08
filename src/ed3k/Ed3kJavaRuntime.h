#pragma once

/// @file Ed3kJavaRuntime.h
/// @brief Finds a Java runtime for the hoster resolver, or downloads one (docs/hoster.md).
///
/// Probe order: the pref, the downloaded runtime under <config>/ed3k/java/jre, $JAVA_HOME,
/// macOS java_home, `java` on PATH, the usual install folders (Windows: also the registry).
/// Each candidate runs `java -XshowSettings:properties -version`; it must be Java 17 or newer
/// and of the daemon's CPU architecture. With none found, Eclipse Temurin 21 (JRE) is fetched
/// from api.adoptium.net through the user's proxy, its SHA-256 checked, and unpacked: the
/// Windows zip in-process (ArchiveReader), the tar.gz elsewhere with the system `tar`, which
/// keeps exec bits and links. The fresh runtime alone is probed then; failing, the state is
/// Failed (not Missing), so it is not fetched again and again.
///
/// Shared (`sharedPluginDir`): the runtime lives in <sharedPluginDir>/java/jre for every
/// instance; one fetches it (java/.install.lock), the others wait and take it. An instance's
/// own runtime is moved there, or dropped once the shared one runs. A read-only shared folder
/// is still probed; a fetch then goes to the instance's own folder.

#ifdef EMULE_HAVE_HOSTER_JD

#include <QByteArray>
#include <QLockFile>
#include <QNetworkProxy>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QThread;

namespace eMule { class HttpStreamDownload; }

namespace eMule::ed3k {

struct JavaInfo {
    QString java;      ///< the program
    int feature = 0;   ///< 17, 21, ...
    QString arch;      ///< normalised: "arm64", "x86_64"
    QString vendor;
};

class Ed3kJavaRuntime : public QObject {
    Q_OBJECT

public:
    static constexpr int kMinFeature = 17;
    static constexpr int kDownloadFeature = 21;

    enum class State { Unknown, Probing, Ready, Missing, Downloading, Unpacking, Failed };

    /// @p baseDir: the ed3k state dir; the runtime goes to <baseDir>/java/jre. @p legacyDir: a
    /// runtime fetched by an older version (<eMuleQt>/runtime/jre), moved in by locate()
    explicit Ed3kJavaRuntime(QString baseDir, QString legacyDir = {}, QObject* parent = nullptr);
    ~Ed3kJavaRuntime() override;

    /// Probes in order (async); stateChanged() when done. @p prefPath first when set.
    /// False (nothing done) while a probe, download or unpack runs.
    bool locate(const QString& prefPath);
    /// Fetches Temurin into <baseDir>/java/jre, then probes it. Refused (false) while
    /// not allowed: only a user who switched hoster downloads on gets a runtime fetched.
    bool download();
    /// @p dir: <sharedPluginDir>; empty: per instance. @p writable: a fetch may go there.
    /// Takes effect with the next locate(); ignored while a fetch runs
    void setSharedDir(const QString& dir, bool writable);
    /// Off: download() is refused and a running one is stopped (its files removed).
    void setAllowed(bool allowed);
    [[nodiscard]] bool allowed() const { return m_allowed; }
    void setProxy(const QNetworkProxy& proxy) { m_proxy = proxy; }
    /// Tests: look again for another instance's fetch this often; default 10 s
    void setLockRetryMs(int ms) { m_lockRetry.setInterval(ms); }

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] std::optional<JavaInfo> runtime() const { return m_info; }
    /// The error of Failed, else empty.
    [[nodiscard]] QString error() const { return m_error; }
    /// 0..100 while Downloading.
    [[nodiscard]] int progress() const { return m_progress; }
    /// Where a fetched runtime goes: the shared folder when writable, else the instance's
    [[nodiscard]] QString runtimeDir() const;

    // -- pure parts, for tests ----------------------------------------------

    /// From the stderr of `java -XshowSettings:properties -version`.
    [[nodiscard]] static std::optional<JavaInfo> parseProperties(const QByteArray& out, const QString& java);
    /// "aarch64", "arm64" → "arm64"; "amd64", "x86_64", "x64" → "x86_64".
    [[nodiscard]] static QString normaliseArch(const QString& arch);
    /// The architecture of this process, normalised.
    [[nodiscard]] static QString hostArch();
    /// The candidates in probe order (existing files only, no duplicates).
    [[nodiscard]] QStringList candidates(const QString& prefPath) const;
    struct Package {
        QString link;
        QString checksum;   ///< SHA-256 hex
        QString name;
        qint64 size = 0;
    };
    /// The first package of an api.adoptium.net /v3/assets/latest answer.
    [[nodiscard]] static std::optional<Package> parseAdoptium(const QByteArray& json);
    /// The query URL for this OS and architecture.
    [[nodiscard]] static QString adoptiumUrl();

signals:
    void stateChanged();

private:
    void probeNext();
    void probeFinished(const QString& java, const QByteArray& out);
    void setState(State s, const QString& error = {});
    void onMetadata();
    void onPackageDone(bool ok);
    void unpack(const QString& archive);
    void unpackDone(bool ok);
    /// <tmp>/<home> moved to runtimeDir(); an old runtime moved aside first, so a locked one
    /// (in use on Windows) stays whole. False: Failed set
    bool installTree(const QString& home);
    /// The legacy runtime moved to runtimeDir() when there is none yet; its empty parent removed
    void adoptLegacy();
    /// Shared: the instance's own runtime moved to the shared folder when that has none
    void adoptInstance();
    /// Shared runtime running: the instance's own copy goes (moved aside first: Windows)
    void dropInstance();
    /// False: another instance fetches into the shared folder; retried by m_lockRetry
    bool takeInstallLock();
    void onLockRetry();
    [[nodiscard]] QString instanceRuntimeDir() const;
    [[nodiscard]] QString sharedRuntimeDir() const;
    /// java under an unpacked JDK/JRE tree (Contents/Home on macOS).
    [[nodiscard]] static QString javaIn(const QString& home);
    void abortTransfer();

    QString m_baseDir;
    QString m_legacyDir;
    QString m_sharedDir;
    bool m_sharedWritable = false;
    std::unique_ptr<QLockFile> m_installLock;   ///< held while fetching into the shared folder
    QTimer m_lockRetry;
    bool m_allowed = false;
    State m_state = State::Unknown;
    std::optional<JavaInfo> m_info;
    QString m_error;
    int m_progress = 0;
    QStringList m_queue;
    QPointer<QProcess> m_probe;
    QNetworkProxy m_proxy{QNetworkProxy::DefaultProxy};
    std::unique_ptr<QNetworkAccessManager> m_nam;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<HttpStreamDownload> m_dl;
    Package m_package;
    QPointer<QProcess> m_tar;
    QPointer<QThread> m_unpackThread;   ///< Windows: the zip read in-process
    QString m_archive;                  ///< while Unpacking
    bool m_verifyInstall = false;       ///< probing our own fresh install only
};

} // namespace eMule::ed3k

#endif // EMULE_HAVE_HOSTER_JD
