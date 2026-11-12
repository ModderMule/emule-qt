#pragma once

/// @file I2pRouter.h
/// @brief The own I2P router (i2pd, `emuleqt-i2pd`) for BitTorrent's btI2pMode 1.
///
/// Not shipped: I2pdInstaller fetches the pinned release into <config>/I2P/bin the first
/// time it is wanted (or when a newer one is pinned). Runs i2pd as a child with its data under <config>/I2P, SAM on a free loopback port,
/// every other client service off. Ready once the SAM port answers; libtorrent gets the
/// bridge only then. Opt-in: an HTTP proxy for browsers, loopback by default. A crash restarts it with a growing pause.

#include "net/I2pdInstaller.h"
#include "utils/SidecarProcess.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class QTcpSocket;

namespace eMule {

class I2pRouter : public QObject {
    Q_OBJECT

public:
    /// Values are IPC (GetBtStatus i2pRouter): append only.
    enum class State : quint8 {
        Stopped = 0,
        Starting = 1,
        Ready = 2,
        Failed = 3,
        Downloading = 4,
    };

    struct Config {
        /// <config>/I2P: router.keys, netDb, addressbook, i2pd.conf
        QString dataDir;
        /// NTCP2/SSU2 port
        quint16 routerPort = 0;
        /// percent of the bandwidth for others' tunnels
        int share = 50;
        /// bind selection; empty = any
        QString address4;
        QString address6;
        /// HTTP proxy for browsers; port 0 = off. Bind from preferences.yml, default loopback
        QString browserProxyAddress = QStringLiteral("127.0.0.1");
        quint16 browserProxyPort = 0;
        /// i2pd webconsole; port 0 = off. Basic auth when the password is set
        QString statusPageAddress = QStringLiteral("127.0.0.1");
        quint16 statusPagePort = 0;
        QString statusPageUser;
        QString statusPagePassword;

        bool operator==(const Config&) const = default;
    };

    static constexpr int kReadyTimeoutMs = 90 * 1000;
    static constexpr int kPollMs = 500;
    static constexpr int kStopGraceMs = 5000;
    /// hosts.txt feeds of the bundled router's address book; i2pd.conf may override.
    /// i2pd fetches one random feed per 12 h. identiguy.i2p dropped: no lease set (#168)
    static inline const QStringList kAddressBookSubscriptions{
        QStringLiteral("http://reg.i2p/hosts.txt"),
        QStringLiteral("http://stats.i2p/cgi-bin/newhosts.txt"),
        QStringLiteral("http://notbob.i2p/hosts.txt"),
    };
    struct SeedHost {
        const char* name;
        const char* b32;   // without ".b32.i2p"
    };
    /// Names merged into i2pd's address book before each start (#168): a fresh router
    /// fetches its first book ~3-20 min after start, and that one (78 hosts) lacks most
    /// trackers; the feeds above follow one per 12 h. Feed hosts + trackers of ngosang's
    /// trackers_all_i2p and i2psnark. From notbob/Java I2P hosts.txt, 2026-11-10
    static constexpr SeedHost kSeedHosts[] = {
        {"reg.i2p", "shx5vqsw7usdaunyzr2qmes2fq37oumybpudrd4jjj4e4vk4uusa"},
        {"stats.i2p", "kqypgjpjwrphnzebod5ev3ts2vtii6e5tntrg4rnfijqc7rypldq"},
        {"notbob.i2p", "nytzrhrjjfsutowojvxi7hphesskpqqr65wpistz6wa7cpajhp7a"},
        {"btracker.i2p", "btrackrqkjp6kgelov5a3uxisis77ofxqt5nvy5hvvtoybjpmq4q"},
        {"freetracker.i2p", "px7uban46x62n3iekxtt4qtdzhee6qu4j5xpbbyaqiruvva2mjha"},
        {"kyu.i2p", "vyqfcpq3cx5svfnr77annk73zfrhumcjeswbnllfxlpjb4564sna"},
        {"opentracker.dg2.i2p", "w7tpbzncbcocrqtwwm3nezhnnsw4ozadvi2hmvzdhrqzfxfum7wa"},
        {"opentracker.fattydove.i2p", "svece3bxv4vqlt2zuut5ww4ztkwunfcnab55pmnjjb6zfei3noha"},
        {"opentracker.localcache.i2p", "trackfmu3by6nhkibnw5exyhibjrvxl6k3e4y54wmjy4bvqgs3ha"},
        {"opentracker.r4sas.i2p", "punzipidirfqspstvzpj6gb4tkuykqp6quurj6e23bgxcxhdoe7q"},
        {"opentracker.simp.i2p", "wc4sciqgkceddn6twerzkfod6p2npm733p7z3zwsjfzhc4yulita"},
        {"opentracker.skank.i2p", "by7luzwhx733fhc5ug2o75dcaunblq2ztlshzd7qvptaoa73nqua"},
        {"surfsup.i2p", "surfsupqyw6a3zegz7ia7ez7pkz7nbuiffqzpeewqlghj7jetmgq"},
        {"tracker.gellegery.i2p", "bvdwskacx5pnldseo6gw7ccuadvsbma5hzoth4athvpsm3jxhdra"},
        {"tracker.insulaocculta.i2p", "2rv2kch37xsn2fqdz6huyevq76njop4inumscnspklh3ufo7mkwa"},
        {"tracker.queke.i2p", "j4nrlimeg2loq5sfslb3c6nmnvbucet4gyrmog66igxrk5l5fxla"},
        {"tracker2.postman.i2p", "6a4kxkg5wp33p25qqhgwl6sj4yh4xuf5b3p3qldwgclebchm3eea"},
        {"yet-another-public-tracker.i2p", "ybi4axxg4bduwsenutmsynz2plfdb4jzr2vhapvprvzcxskx2foa"},
    };
    static constexpr int kFirstRestartMs = 5000;
    static constexpr int kMaxRestartMs = 5 * 60 * 1000;

    explicit I2pRouter(QObject* parent = nullptr);
    ~I2pRouter() override;

    /// $EMULE_I2PD (a router of the user's or a test's); empty when unset or missing.
    [[nodiscard]] static QString findBinary();
    /// The download folder under @p dataDir.
    [[nodiscard]] static QString installDir(const QString& dataDir);
    /// The reseed certificates shipped beside @p binary; empty when none.
    [[nodiscard]] static QString findCertificates(const QString& binary);
    /// i2pd's command line for @p config.
    [[nodiscard]] static QStringList arguments(const Config& config, quint16 samPort, const QString& certsDir);
    /// Appends kSeedHosts names missing from <dataDir>/addressbook/addresses.csv; a name
    /// already there keeps its address. Router must not run. Returns the count added.
    static int seedAddressBook(const QString& dataDir);

    /// A router can be had: $EMULE_I2PD, or a release to download for this OS/CPU.
    [[nodiscard]] bool available() const;

    /// Runs the router with @p config; restarts it when the config changed. Idempotent.
    void start(const Config& config);
    /// Ends it (SIGTERM: i2pd quits at once, transit tunnels dropped). Idempotent.
    void stop();

    [[nodiscard]] State state() const { return m_state; }
    /// SAM port while Starting or Ready; 0 otherwise.
    [[nodiscard]] quint16 samPort() const { return m_samPort; }
    /// Why it is Failed (or the last crash); English.
    [[nodiscard]] QString lastError() const { return m_error; }
    /// 0..100 while Downloading
    [[nodiscard]] int downloadProgress() const { return m_installer.progress(); }

signals:
    void stateChanged();
    /// downloadProgress() moved, while Downloading
    void downloadProgressChanged();

private:
    void launch();
    void onInstalled(bool ok);
    void setState(State s);
    void poll();
    void onLine(const QByteArray& line);
    void onFinished(int exitCode, bool crashed, bool expected);
    void fail(const QString& error);
    /// A free loopback TCP port for SAM (released again before i2pd binds it).
    [[nodiscard]] static quint16 freeLoopbackPort();
    /// Ends an i2pd still on @p dataDir from an earlier core that died or hung (#129):
    /// it holds i2pd.pid's lock, so ours would not start. Only ours use that folder.
    static void stopStaleRouter(const QString& dataDir);

    SidecarProcess m_proc;
    Config m_config;
    QString m_binary;
    bool m_wanted = false;
    State m_state = State::Stopped;
    quint16 m_samPort = 0;
    QString m_error;
    QTimer m_pollTimer;
    QTimer m_restartTimer;
    QElapsedTimer m_launchedAt;
    int m_restartDelayMs = kFirstRestartMs;
    QTcpSocket* m_probe = nullptr;
    I2pdInstaller m_installer;
    /// the pin whose download failed this run: an older install is used then
    QString m_failedRelease;
};

} // namespace eMule
