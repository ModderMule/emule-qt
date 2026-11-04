#include "net/I2pRouter.h"

#include "utils/Log.h"
#include "utils/PathUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>

namespace eMule {

namespace {

#ifdef Q_OS_WIN
const QString kBinaryName = QStringLiteral("emuleqt-i2pd.exe");
#else
const QString kBinaryName = QStringLiteral("emuleqt-i2pd");
#endif

/// Writes @p text to @p path unless the file is there already.
void writeOnce(const QString& path, const QByteArray& text)
{
    if (QFileInfo::exists(path))
        return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(text);
}

} // namespace

I2pRouter::I2pRouter(QObject* parent)
    : QObject(parent)
    , m_binary(findBinary())
{
    m_pollTimer.setInterval(kPollMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &I2pRouter::poll);
    m_restartTimer.setSingleShot(true);
    connect(&m_restartTimer, &QTimer::timeout, this, &I2pRouter::launch);
    connect(&m_proc, &SidecarProcess::lineRead, this, &I2pRouter::onLine);
    connect(&m_proc, &SidecarProcess::finished, this, &I2pRouter::onFinished);
    connect(&m_proc, &SidecarProcess::failedToStart, this, [this] {
        fail(QStringLiteral("%1 did not start").arg(m_binary));
        onFinished(-1, false, false);
    });
}

I2pRouter::~I2pRouter()
{
    // m_proc waits for i2pd, then kills it; SIGTERM first so it saves its netDb
    m_wanted = false;
    m_proc.disconnect(this);
    m_proc.stop(kStopGraceMs);
}

QString I2pRouter::findBinary()
{
    return findBundledFile("EMULE_I2PD", {kBinaryName, QStringLiteral("../libexec/emuleqt/") + kBinaryName},
                           QStringLiteral("i2pd/") + kBinaryName);
}

QString I2pRouter::findCertificates(const QString& binary)
{
    if (binary.isEmpty())
        return {};
    const QDir dir = QFileInfo(binary).absoluteDir();
    // bundles: i2pd-certificates beside the binary or in Resources/share; build tree: certificates/
    for (const QString& rel : {QStringLiteral("i2pd-certificates"), QStringLiteral("../Resources/i2pd-certificates"),
                               QStringLiteral("../share/emuleqt/i2pd-certificates"), QStringLiteral("certificates")}) {
        if (const QString p = dir.filePath(rel); QFileInfo(p).isDir())
            return QDir::cleanPath(p);
    }
    return {};
}

QStringList I2pRouter::arguments(const Config& config, quint16 samPort, const QString& certsDir)
{
    const QDir data(config.dataDir);
    QStringList a{
        QStringLiteral("--datadir=") + QDir::toNativeSeparators(config.dataDir),
        // the user may add options there; the command line wins
        QStringLiteral("--conf=") + QDir::toNativeSeparators(data.filePath(QStringLiteral("i2pd.conf"))),
        // no default tunnels: an empty file and folder
        QStringLiteral("--tunconf=") + QDir::toNativeSeparators(data.filePath(QStringLiteral("tunnels.conf"))),
        QStringLiteral("--tunnelsdir=") + QDir::toNativeSeparators(data.filePath(QStringLiteral("tunnels.d"))),
        QStringLiteral("--log=stdout"),
        QStringLiteral("--loglevel=warn"),
        // stay our child: no fork, and on Windows no service. Our Windows build has no tray
        // window (USE_WIN32_APP=no in ed3k-protocol/i2pd's emuleqt-release.yml)
        QStringLiteral("--daemon=false"),
        QStringLiteral("--service=false"),
        QStringLiteral("--port=%1").arg(config.routerPort),
        QStringLiteral("--share=%1").arg(std::clamp(config.share, 0, 100)),
        // the app's PortMapper maps the router port
        QStringLiteral("--upnp.enabled=false"),
        QStringLiteral("--sam.enabled=true"),
        QStringLiteral("--sam.address=127.0.0.1"),
        QStringLiteral("--sam.port=%1").arg(samPort),
        // no other client service: nothing else listens
        QStringLiteral("--http.enabled=false"),
        QStringLiteral("--httpproxy.enabled=false"),
        QStringLiteral("--socksproxy.enabled=false"),
        QStringLiteral("--bob.enabled=false"),
        QStringLiteral("--i2cp.enabled=false"),
        QStringLiteral("--i2pcontrol.enabled=false"),
    };
    if (!certsDir.isEmpty())
        a << QStringLiteral("--certsdir=") + QDir::toNativeSeparators(certsDir);
    if (!config.address4.isEmpty())
        a << QStringLiteral("--address4=") + config.address4;
    if (!config.address6.isEmpty())
        a << QStringLiteral("--address6=") + config.address6;
    return a;
}

bool I2pRouter::available() const
{
    return !m_binary.isEmpty();
}

void I2pRouter::start(const Config& config)
{
    const bool changed = !(config == m_config);
    m_config = config;
    m_wanted = true;
    if (m_proc.running()) {
        if (!changed)
            return;
        logTorrent(QStringLiteral("I2P: router settings changed, restarting it"));
        // onFinished launches again
        m_pollTimer.stop();
        m_proc.stop(kStopGraceMs);
        return;
    }
    if (m_restartTimer.isActive() && !changed)
        return;
    m_restartTimer.stop();
    m_restartDelayMs = kFirstRestartMs;
    launch();
}

void I2pRouter::stop()
{
    m_wanted = false;
    m_restartTimer.stop();
    m_pollTimer.stop();
    if (m_probe) {
        m_probe->abort();
        m_probe->deleteLater();
        m_probe = nullptr;
    }
    m_proc.stop(kStopGraceMs);
    m_samPort = 0;
    m_error.clear();
    setState(State::Stopped);
}

void I2pRouter::launch()
{
    if (!m_wanted || m_proc.running())
        return;
    m_binary = findBinary();
    if (m_binary.isEmpty()) {
        fail(QStringLiteral("no bundled router (emuleqt-i2pd) in this build"));
        return;
    }
    if (m_config.dataDir.isEmpty() || !QDir().mkpath(m_config.dataDir)) {
        fail(QStringLiteral("I2P data folder %1 not writable").arg(m_config.dataDir));
        return;
    }
    const QDir data(m_config.dataDir);
    writeOnce(data.filePath(QStringLiteral("i2pd.conf")),
              "# i2pd options of eMuleQt's bundled router. The app passes its own on the command line\n"
              "# (SAM, ports, services off), which win over this file.\n");
    writeOnce(data.filePath(QStringLiteral("tunnels.conf")), "# no tunnels: libtorrent opens its own over SAM\n");
    data.mkpath(QStringLiteral("tunnels.d"));
    const QString certs = findCertificates(m_binary);
    if (certs.isEmpty())
        logTorrentWarning(QStringLiteral("I2P: no reseed certificates beside %1; a first start may not find the network")
                              .arg(m_binary));
    m_samPort = freeLoopbackPort();
    if (m_samPort == 0) {
        fail(QStringLiteral("no free loopback port for the SAM bridge"));
        return;
    }
    m_error.clear();
    setState(State::Starting);
    logTorrent(QStringLiteral("I2P: starting the router, port %1, SAM on 127.0.0.1:%2").arg(m_config.routerPort).arg(m_samPort));
    m_proc.start(m_binary, arguments(m_config, m_samPort, certs), m_config.dataDir);
    m_launchedAt.start();
    m_pollTimer.start();
}

void I2pRouter::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

void I2pRouter::poll()
{
    if (m_state != State::Starting) {
        m_pollTimer.stop();
        return;
    }
    if (m_launchedAt.elapsed() > kReadyTimeoutMs) {
        fail(QStringLiteral("the SAM bridge did not open within %1 s").arg(kReadyTimeoutMs / 1000));
        m_proc.stop(kStopGraceMs);
        return;
    }
    if (m_probe)
        return;   // one try at a time
    m_probe = new QTcpSocket(this);
    QTcpSocket* probe = m_probe;
    const auto done = [this, probe](bool ok) {
        if (probe != m_probe)
            return;
        m_probe = nullptr;
        probe->abort();
        probe->deleteLater();
        if (!ok || m_state != State::Starting)
            return;
        m_pollTimer.stop();
        m_restartDelayMs = kFirstRestartMs;
        logTorrent(QStringLiteral("I2P: router ready after %1 s").arg(m_launchedAt.elapsed() / 1000));
        setState(State::Ready);
    };
    connect(probe, &QTcpSocket::connected, this, [done] { done(true); });
    connect(probe, &QTcpSocket::errorOccurred, this, [done] { done(false); });
    probe->connectToHost(QHostAddress::LocalHost, m_samPort);
}

void I2pRouter::onLine(const QByteArray& line)
{
    if (line.isEmpty())
        return;
    // "12:00:00@123/warn - text"
    const QString text = QString::fromUtf8(line);
    const qsizetype slash = text.indexOf(QLatin1Char('/'));
    const qsizetype dash = text.indexOf(QStringLiteral(" - "));
    const QString level = slash >= 0 && dash > slash ? text.mid(slash + 1, dash - slash - 1) : QString();
    const QString msg = QStringLiteral("I2P router: ") + (dash >= 0 ? text.mid(dash + 3) : text);
    if (level == QLatin1StringView("error") || level == QLatin1StringView("critical"))
        logTorrentWarning(msg);
    else
        logTorrentDebug(msg);
}

void I2pRouter::onFinished(int exitCode, bool crashed, bool expected)
{
    m_pollTimer.stop();
    m_samPort = 0;
    if (!m_wanted) {
        setState(State::Stopped);
        return;
    }
    if (expected && m_state != State::Failed) {
        // a restart for new settings
        launch();
        return;
    }
    if (m_state != State::Failed)
        fail(QStringLiteral("the router exited (code %1%2)").arg(exitCode).arg(crashed ? QStringLiteral(", crashed") : QString()));
    logTorrentWarning(QStringLiteral("I2P: router restarts in %1 s").arg(m_restartDelayMs / 1000));
    m_restartTimer.start(m_restartDelayMs);
    m_restartDelayMs = std::min(m_restartDelayMs * 2, kMaxRestartMs);
}

void I2pRouter::fail(const QString& error)
{
    // once per cause: applyBtConfig calls start() often
    if (m_state != State::Failed || m_error != error)
        logTorrentWarning(QStringLiteral("I2P: ") + error);
    m_error = error;
    setState(State::Failed);
}

quint16 I2pRouter::freeLoopbackPort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    return probe.serverPort();
}

} // namespace eMule
