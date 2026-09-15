#pragma once

/// @file NntpSocket.h
/// @brief Async NNTP transport: connect, TLS, greeting, authentication.
///
/// A new socket rather than eMule's. `EMSocket` derives from
/// `EncryptedStreamSocket`, which implements eMule's *own* RC4 obfuscation
/// handshake, not TLS — there is no path through it to
/// QSslSocket::startClientEncryption(). It also speaks length-prefixed eMule
/// packets where NNTP is CRLF-delimited text, and it registers with the upload
/// bandwidth throttler, which throttles sending in a download-dominant
/// protocol. Reusing it would mean disabling every part of it.
///
/// The model is `core/net/SmtpClient`, which is already a bare QSslSocket
/// running a line-oriented state machine over both implicit TLS and STARTTLS.
/// Two things are deliberately *not* copied from it: it has no timeouts at all
/// (a server that stops answering after its greeting wedges the object
/// permanently), and it decides implicit-vs-STARTTLS from a hardcoded
/// `port == 465`. Both are fixed here.
///
/// This class owns transport, greeting and authentication only. Everything
/// after "ready" is an NntpCommand — see NntpCommand.h for why that split is
/// load-bearing.

#include "nntp/NewsServer.h"
#include "nntp/NntpError.h"

#include <QByteArray>
#include <QHash>
#include <QNetworkProxy>
#include <QObject>
#include <QSslError>
#include <QString>

class QSslSocket;
class QTimer;

namespace eMule::usenet {

class NntpCommand;

class NntpSocket : public QObject {
    Q_OBJECT

public:
    explicit NntpSocket(QObject* parent = nullptr);
    ~NntpSocket() override;

    /// Connect and authenticate against @p server. Emits ready() or failed().
    /// The QSslSocket is created here, so calling this from the thread that
    /// owns the object is what puts the socket in the right event loop.
    void connectToServer(const NewsServer& server);

    /// Route the next connectToServer() through @p proxy. NoProxy by default,
    /// explicitly, so an application-wide proxy never applies unasked. A refusal
    /// by the proxy itself fails with NntpError::ProxyFailed.
    void setProxy(const QNetworkProxy& proxy) { m_proxy = proxy; }
    [[nodiscard]] const QNetworkProxy& proxy() const { return m_proxy; }

    /// Send @p command. It must outlive the commandFinished() signal; the
    /// socket does not take ownership. One at a time — a second call while a
    /// command is running is refused and logged.
    void sendCommand(NntpCommand* command);

    /// Polite close: QUIT, then disconnect. Safe to call when not connected.
    void close();

    /// Drop the connection immediately, without QUIT.
    void abort();

    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] bool isReady() const { return m_state == State::Ready; }
    [[nodiscard]] const NewsServer& server() const { return m_server; }

    /// Cap on inbound payload, in bytes per second. 0 = unlimited, matching
    /// eMuleQt's convention everywhere else (MFC says UNLIMITED; this codebase
    /// says 0, and reading the raw field instead of the accessor is how a
    /// limit of "unlimited" silently becomes a limit of zero).
    ///
    /// The drain loop stops when the budget runs out and re-arms a refill
    /// timer. That re-arm is the whole point: a readyRead slot that consumes a
    /// bounded amount and does *not* schedule itself again leaves the rest of
    /// the data sitting in the socket until the peer's keep-alive fires, which
    /// is exactly the stall that was once diagnosed in EMSocket.
    void setReadRateLimit(qint64 bytesPerSecond);
    [[nodiscard]] qint64 readRateLimit() const { return m_readRateLimit; }

    /// Inbound bytes since the last call, then zeroed.
    ///
    /// Everything off the wire — greeting, auth, status lines, article bodies,
    /// dot-stuffing — because that is what a provider meters, and it is the
    /// figure the quota accounting spends. Decrypted, so TLS record framing and
    /// TCP headers are not in it; the count reads a few percent under a
    /// provider's own.
    ///
    /// A *take* rather than a running total on purpose: a pooled connection
    /// serves many articles in sequence and each caller wants the delta since
    /// it last looked, which is also what stops two jobs charging the same
    /// bytes twice.
    [[nodiscard]] qint64 takeBytesRead();

    /// Wire bytes read by every NntpSocket in the process, monotonic, from any
    /// thread. Counted as lines are consumed — when the read budget is charged
    /// too — so a rate taken from it is live, where decoded bytes only arrive a
    /// whole article at a time. Everything NNTP spends of the line is in it:
    /// downloads, health probes, the Options page's Test button.
    [[nodiscard]] static qint64 totalWireBytesRead();

    /// Authenticated connections open right now, per NewsServer::accountId and
    /// in total, from any thread. Like totalWireBytesRead() it counts every
    /// socket, the Test button's included. With no idle timeout a pooled
    /// connection stays open between articles, so this is "open", not "busy".
    [[nodiscard]] static QHash<QString, int> openConnectionsByAccount();
    [[nodiscard]] static int openConnectionCount();

    /// How long to wait for a response before giving up. Default 60 s, matching
    /// NZBGet's ServerPool timeout.
    void setResponseTimeout(int ms);

    /// How long an authenticated, idle connection is held before it is closed.
    /// 0 disables. Providers drop idle connections on their own schedule; going
    /// first keeps the count they see accurate.
    void setIdleTimeout(int ms);

signals:
    /// Connected, TLS established if requested, and authenticated.
    void ready();

    /// The command handed to sendCommand() is finished. Check its failed().
    void commandFinished(eMule::usenet::NntpCommand* command);

    /// The connection failed. Always followed by disconnected().
    void failed(eMule::usenet::NntpError error, const QString& text);

    void disconnected();

private:
    /// Transport + auth only. Command state lives in NntpCommand.
    enum class State : quint8 {
        Disconnected,
        Connecting,
        Greeting,
        StartTlsSent,
        ModeReaderSent,
        AuthUserSent,
        AuthPassSent,
        Ready,
        CommandStatus,   ///< command written, awaiting its status line
        CommandBody,     ///< streaming a dot-terminated block
        QuitSent,
    };

    void onReadyRead();
    void onSocketError();
    void onSslErrors(const QList<QSslError>& errors);
    void onEncrypted();
    void onSocketDisconnected();
    void onResponseTimeout();
    void onIdleTimeout();
    void onReadBudgetRefill();

    void applyTlsConfiguration();
    void sendLine(QByteArrayView line);
    void drain();
    void handleStatusLine(const QByteArray& raw);
    void handleBodyLine(const QByteArray& raw);
    void beginAuthOrReady();
    void enterReady();
    void finishCommand();
    void fail(NntpError error, const QString& text);
    void armResponseTimer();
    void disarmTimers();
    void enterDisconnected();
    void markOpen();
    void markClosed();

    NewsServer m_server;
    QNetworkProxy m_proxy{QNetworkProxy::NoProxy};
    QSslSocket* m_socket = nullptr;
    State m_state = State::Disconnected;

    NntpCommand* m_command = nullptr;

    QTimer* m_responseTimer = nullptr;
    QTimer* m_idleTimer = nullptr;
    QTimer* m_refillTimer = nullptr;

    int m_responseTimeoutMs = 60'000;
    int m_idleTimeoutMs = 0;

    qint64 m_readRateLimit = 0;
    qint64 m_readBudget = 0;
    qint64 m_bytesRead = 0;

    bool m_failed = false;

    /// In the open-connection registry, under this account id.
    bool m_counted = false;
    QString m_countedAccount;
};

} // namespace eMule::usenet
