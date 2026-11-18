#pragma once

/// @file Ed3kFfmpegTool.h
/// @brief Finds ffmpeg + ffprobe for the hoster resolver, or downloads ours (docs/hoster.md,
/// docs/ffmpeg-tools.local.md).
///
/// JD's HLS/DASH plugins skip a link with FFMPEG_MISSING unless ffmpeg and ffprobe are there;
/// JD's own installer is not in the resolver. So the daemon gives the paths:
/// - Probe order: our download (<baseDir>/ffmpeg), next to the application, PATH, the usual
///   install folders (Homebrew /opt/homebrew/bin: an app started from Finder has no PATH to it;
///   winget, scoop, chocolatey). Both binaries must sit in one folder.
/// - None: the resolver asks (needTools) when a link needs them; download() reads the update
///   manifest's "ffmpeg" entry of this platform, fetches the zip (size + SHA-256 checked),
///   unpacks it in-process, moves both binaries to <baseDir>/ffmpeg and runs `ffmpeg -version`.
/// - A failure is logged to the hoster log and blocks a new fetch for kRetrySecs.
/// ToDo: sharedPluginDir (one copy for all instances, as the Java runtime)

#ifdef EMULE_HAVE_HOSTER_JD

#include <QDateTime>
#include <QNetworkProxy>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <memory>

class QProcess;
class QThread;

namespace eMule {
class GuardedNetworkAccessManager;
class HttpDownloadJob;
class HttpStreamDownload;
struct VersionManifestInfo;
}

namespace eMule::ed3k {

class Ed3kFfmpegTool : public QObject {
    Q_OBJECT

public:
    /// ffmpeg + ffprobe zipped are ~60-150 MB
    static constexpr qint64 kMaxZipBytes = 400LL * 1024 * 1024;
    /// after a failed fetch: no new one before this
    static constexpr qint64 kRetrySecs = 10 * 60;
    static constexpr int kVerifyTimeoutMs = 90000;   ///< first start: virus scan

    enum class State { Unknown, Ready, Missing, Downloading, Unpacking, Failed };

    /// @p baseDir: the ed3k state dir; downloads go to <baseDir>/ffmpeg
    explicit Ed3kFfmpegTool(QString baseDir, QObject* parent = nullptr);
    ~Ed3kFfmpegTool() override;

    /// Probes the candidates (file checks only, synchronous). Ready or Missing; ignored while
    /// fetching
    void locate();
    /// Fetches ffmpeg per the manifest. False: refused (not allowed, a failure under
    /// kRetrySecs ago); error() says why. True: running (or already)
    bool download();
    /// Off: download() refused, a running one stopped
    void setAllowed(bool allowed);
    void setProxy(const QNetworkProxy& proxy) { m_proxy = proxy; }
    /// Tests: a local manifest; default VersionManifest::manifestUrl()
    void setManifestUrl(const QUrl& url) { m_manifestUrl = url; }

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] bool busy() const { return m_state == State::Downloading || m_state == State::Unpacking; }
    [[nodiscard]] QString ffmpeg() const { return m_ffmpeg; }
    [[nodiscard]] QString ffprobe() const { return m_ffprobe; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] int progress() const { return m_progress; }
    /// <baseDir>/ffmpeg
    [[nodiscard]] QString toolDir() const;

    /// Folders probed in order (existing or not); @p appDir: the application's
    [[nodiscard]] QStringList candidateDirs(const QString& appDir) const;
    /// "ffmpeg.exe" on Windows, else @p name
    [[nodiscard]] static QString exe(const QString& name);

signals:
    void stateChanged();

private:
    void onManifest(const QByteArray& json);
    void onZipDone(bool ok);
    void unpack(const QString& zip);
    void unpackDone(const QString& error);
    void verify();
    void setState(State s, const QString& error = {});
    void fail(const QString& error);
    void abortTransfer();
    /// Moves ffmpeg + ffprobe found under @p tree into toolDir(). Empty: done, else the error
    [[nodiscard]] QString installFrom(const QString& tree);

    QString m_baseDir;
    bool m_allowed = false;
    State m_state = State::Unknown;
    QString m_ffmpeg;
    QString m_ffprobe;
    QString m_error;
    int m_progress = 0;
    QString m_version;   ///< of the zip being fetched
    QDateTime m_failedAt;
    QUrl m_manifestUrl;
    QNetworkProxy m_proxy{QNetworkProxy::DefaultProxy};
    QPointer<HttpDownloadJob> m_job;
    std::unique_ptr<GuardedNetworkAccessManager> m_nam;
    std::unique_ptr<HttpStreamDownload> m_dl;
    QPointer<QThread> m_unpackThread;
    QPointer<QProcess> m_verify;
    QString m_zip;   ///< while unpacking
};

} // namespace eMule::ed3k

#endif // EMULE_HAVE_HOSTER_JD
