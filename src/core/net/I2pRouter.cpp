#include "net/I2pRouter.h"

#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <chrono>
#include <thread>

#ifndef Q_OS_WIN
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace eMule {

namespace {

/// Writes @p text to @p path unless the file is there already.
void writeOnce(const QString& path, const QByteArray& text)
{
    if (QFileInfo::exists(path))
        return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(text);
}

/// True when the user's i2pd.conf sets address book subscriptions itself.
bool confSetsSubscriptions(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    static const QRegularExpression re(QStringLiteral(R"(^\s*subscriptions\s*=)"),
                                       QRegularExpression::MultilineOption);
    return re.match(QString::fromUtf8(f.readAll())).hasMatch();
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
    connect(&m_installer, &I2pdInstaller::finished, this, &I2pRouter::onInstalled);
    connect(&m_installer, &I2pdInstaller::progressChanged, this, &I2pRouter::downloadProgressChanged);
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
    const QString env = qEnvironmentVariable("EMULE_I2PD");
    return !env.isEmpty() && QFileInfo(env).isFile() ? QFileInfo(env).absoluteFilePath() : QString();
}

QString I2pRouter::installDir(const QString& dataDir)
{
    return QDir(dataDir).filePath(QStringLiteral("bin"));
}

QString I2pRouter::findCertificates(const QString& binary)
{
    if (binary.isEmpty())
        return {};
    const QDir dir = QFileInfo(binary).absoluteDir();
    // the release archive: certificates/ beside the program
    for (const QString& rel : {QStringLiteral("certificates"), QStringLiteral("i2pd-certificates")}) {
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
        // no --daemon/--service: off by default, and switches without a value (i2pd exits on
        // "--daemon=false"). Our Windows build has no tray window either (USE_WIN32_APP=no in
        // ed3k-protocol/i2pd's emuleqt-release.yml)
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
    // .i2p tracker names resolve only from the address book: i2pd's default subscribes
    // reg.i2p alone, which misses many trackers → SAM "invalid key" (report 38)
    if (!confSetsSubscriptions(data.filePath(QStringLiteral("i2pd.conf"))))
        a << QStringLiteral("--addressbook.subscriptions=") + kAddressBookSubscriptions.join(QLatin1Char(','));
    if (!certsDir.isEmpty())
        a << QStringLiteral("--certsdir=") + QDir::toNativeSeparators(certsDir);
    if (!config.address4.isEmpty())
        a << QStringLiteral("--address4=") + config.address4;
    if (!config.address6.isEmpty())
        a << QStringLiteral("--address6=") + config.address6;
    return a;
}

int I2pRouter::seedAddressBook(const QString& dataDir)
{
    // i2pd's index: "name,b32" lines; names resolve from it alone (lease set by hash)
    const QDir book(QDir(dataDir).filePath(QStringLiteral("addressbook")));
    if (!book.mkpath(QStringLiteral(".")))
        return 0;
    QFile f(book.filePath(QStringLiteral("addresses.csv")));
    QByteArray have;
    if (f.exists()) {
        if (!f.open(QIODevice::ReadOnly))
            return 0;
        have = f.readAll();
        f.close();
    }
    QSet<QByteArray> names;
    for (const QByteArray& line : have.split('\n'))
        if (const qsizetype comma = line.indexOf(','); comma > 0)
            names.insert(line.left(comma).trimmed());
    QByteArray add;
    int count = 0;
    for (const SeedHost& h : kSeedHosts) {
        if (names.contains(QByteArray(h.name)))
            continue;
        add += QByteArray(h.name) + ',' + h.b32 + '\n';
        ++count;
    }
    if (count == 0)
        return 0;
    if (!have.isEmpty() && !have.endsWith('\n'))
        add.prepend('\n');
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append))
        return 0;
    return f.write(add) == add.size() ? count : 0;
}

bool I2pRouter::available() const
{
    return !m_binary.isEmpty() || I2pdInstaller::supported();
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
    m_installer.abort();
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
    if (!m_wanted || m_proc.running() || m_installer.running())
        return;
    if (m_config.dataDir.isEmpty() || !QDir().mkpath(m_config.dataDir)) {
        fail(QStringLiteral("I2P data folder %1 not writable").arg(m_config.dataDir));
        return;
    }
    m_binary = findBinary();
    if (m_binary.isEmpty()) {
        m_installer.setDir(installDir(m_config.dataDir));
        const bool fallback = !m_installer.binary().isEmpty() && m_failedRelease == I2pdInstaller::pinnedAsset().release;
        if (!m_installer.current() && !fallback) {
            if (!I2pdInstaller::supported()) {
                fail(QStringLiteral("no I2P router release for this system; use an external router"));
                return;
            }
            m_error.clear();
            if (m_installer.install())
                setState(State::Downloading);
            return;
        }
        m_binary = m_installer.binary();
    }
    const QDir data(m_config.dataDir);
    writeOnce(data.filePath(QStringLiteral("i2pd.conf")),
              "# i2pd options of eMuleQt's bundled router. The app passes its own on the command line\n"
              "# (SAM, ports, services off), which win over this file.\n");
    writeOnce(data.filePath(QStringLiteral("tunnels.conf")), "# no tunnels: libtorrent opens its own over SAM\n");
    data.mkpath(QStringLiteral("tunnels.d"));
    stopStaleRouter(m_config.dataDir);
    if (const int seeded = seedAddressBook(m_config.dataDir); seeded > 0)
        logTorrent(QStringLiteral("I2P: %1 tracker names added to the router's address book").arg(seeded));
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
    logTorrent(QStringLiteral("I2P: starting the router, port %1, SAM on 127.0.0.1:%2").arg(m_config.routerPort).arg(m_samPort));
    m_proc.start(m_binary, arguments(m_config, m_samPort, certs), m_config.dataDir);
    m_launchedAt.start();
    m_pollTimer.start();
    // last: stateChanged re-enters start() (applyBtConfig), which must see it running.
    // A synchronous failedToStart has set Failed already
    if (m_proc.running())
        setState(State::Starting);
}

void I2pRouter::onInstalled(bool ok)
{
    if (!m_wanted)
        return;
    if (ok) {
        m_failedRelease.clear();
        launch();
        return;
    }
    const QString why = QStringLiteral("router download failed: ") + m_installer.error();
    if (!m_installer.binary().isEmpty()) {
        // a newer pin that did not come: the older router still works
        logTorrentWarning(QStringLiteral("I2P: %1; starting the installed older one").arg(why));
        m_failedRelease = I2pdInstaller::pinnedAsset().release;
        launch();
        return;
    }
    fail(why);
    logTorrentWarning(QStringLiteral("I2P: next download try in %1 s").arg(m_restartDelayMs / 1000));
    m_restartTimer.start(m_restartDelayMs);
    m_restartDelayMs = std::min(m_restartDelayMs * 2, kMaxRestartMs);
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
    // macOS 27 emits connected() for a refused connect too, with no peer: check the port
    connect(probe, &QTcpSocket::connected, this,
            [done, probe, port = m_samPort] { done(probe->peerPort() == port); });
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

void I2pRouter::stopStaleRouter(const QString& dataDir)
{
#ifdef Q_OS_WIN
    Q_UNUSED(dataDir);
#else
    // i2pd lockf()s its pid file: F_GETLK names the holder
    const QByteArray path = QFile::encodeName(QDir(dataDir).filePath(QStringLiteral("i2pd.pid")));
    const int fd = ::open(path.constData(), O_RDWR);
    if (fd < 0)
        return;
    const auto holder = [fd]() -> pid_t {
        struct flock fl{};
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        if (::fcntl(fd, F_GETLK, &fl) != 0 || fl.l_type == F_UNLCK)
            return 0;
        return fl.l_pid;
    };
    pid_t pid = holder();
    if (pid > 0 && pid != ::getpid()) {
        logTorrentWarning(QStringLiteral("I2P: a router left by an earlier run still runs (pid %1), stopping it").arg(pid));
        ::kill(pid, SIGTERM);
        // i2pd quits on SIGTERM in about a second; killed after 5
        for (int i = 0; i < 50 && holder() == pid; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (holder() == pid) {
            ::kill(pid, SIGKILL);
            for (int i = 0; i < 20 && holder() == pid; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    ::close(fd);
#endif
}

} // namespace eMule
