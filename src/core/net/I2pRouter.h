#pragma once

/// @file I2pRouter.h
/// @brief The bundled I2P router (i2pd, `emuleqt-i2pd`) for BitTorrent's btI2pMode 1.
///
/// Runs i2pd as a child with its data under <config>/I2P, SAM on a free loopback port,
/// every other client service off. Ready once the SAM port answers; libtorrent gets the
/// bridge only then. A crash restarts it with a growing pause.

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

        bool operator==(const Config&) const = default;
    };

    static constexpr int kReadyTimeoutMs = 90 * 1000;
    static constexpr int kPollMs = 500;
    static constexpr int kStopGraceMs = 5000;
    static constexpr int kFirstRestartMs = 5000;
    static constexpr int kMaxRestartMs = 5 * 60 * 1000;

    explicit I2pRouter(QObject* parent = nullptr);
    ~I2pRouter() override;

    /// `emuleqt-i2pd` next to the binary (or $EMULE_I2PD, or a build tree's i2pd/).
    /// Empty: not bundled.
    [[nodiscard]] static QString findBinary();
    /// The reseed certificates shipped beside @p binary; empty when none.
    [[nodiscard]] static QString findCertificates(const QString& binary);
    /// i2pd's command line for @p config.
    [[nodiscard]] static QStringList arguments(const Config& config, quint16 samPort, const QString& certsDir);

    /// A bundled binary exists.
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

signals:
    void stateChanged();

private:
    void launch();
    void setState(State s);
    void poll();
    void onLine(const QByteArray& line);
    void onFinished(int exitCode, bool crashed, bool expected);
    void fail(const QString& error);
    /// A free loopback TCP port for SAM (released again before i2pd binds it).
    [[nodiscard]] static quint16 freeLoopbackPort();

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
};

} // namespace eMule
