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
#include <QElapsedTimer>
#include <QHash>
#include <QList>
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
    /// socket does not take ownership. A call while a command is running
    /// pipelines it: written at once, answered in order. Refused and logged
    /// when not connected and authenticated.
    void sendCommand(NntpCommand* command);

    /// Whether sendCommand() would take a command now (ready or running one).
    [[nodiscard]] bool acceptsCommands() const
    {
        return m_state == State::Ready || m_state == State::CommandStatus
            || m_state == State::CommandBody;
    }

    /// The command whose response is being read, or nullptr.
    [[nodiscard]] const NntpCommand* currentCommand() const { return m_command; }

    /// Commands sent behind the current one, not yet answering.
    [[nodiscard]] int pipelinedCount() const { return int(m_pipeline.size()); }

    /// Wire bytes of the current command's body drained so far.
    [[nodiscard]] qint64 commandBodyBytes() const { return m_bodyBytes; }

    /// Smoothed time from a command to its status line on an idle connection.
    [[nodiscard]] qint64 responseLatencyMs() const { return m_latencyMs; }

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

    /// The newsgroup in effect once everything already sent has run; empty when
    /// none or unknown. Lets a fetcher skip a GROUP round trip per article.
    [[nodiscard]] const QString& selectedGroup() const { return m_selectedGroup; }
    void setSelectedGroup(const QString& group) { m_selectedGroup = group; }

    /// Bytes Qt holds for this connection not yet drained. Bounded while limited.
    [[nodiscard]] qint64 bufferedBytes() const;

    /// Per-layer receive buffer cap for the current limit; 0 = unbounded.
    [[nodiscard]] qint64 readBufferCapBytes() const;

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

    /// A drain() pass consumed lines while a command runs. Same thread only.
    void bodyProgress();

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
    void applyBufferCaps();

    void applyTlsConfiguration();
    void sendLine(QByteArrayView line);
    void drain();
    void handleStatusLine(QByteArrayView raw);
    void handleBodyLine(QByteArrayView raw);
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
    QList<NntpCommand*> m_pipeline;   ///< sent, answered after m_command
    qint64 m_bodyBytes = 0;
    QElapsedTimer m_latencyClock;     ///< valid while an idle-line command awaits status
    qint64 m_latencyMs = 250;
    bool m_latencySampled = false;

    QTimer* m_responseTimer = nullptr;
    QTimer* m_idleTimer = nullptr;
    QTimer* m_refillTimer = nullptr;

    int m_responseTimeoutMs = 60'000;
    int m_idleTimeoutMs = 0;

    qint64 m_readRateLimit = 0;
    QByteArray m_in;            ///< read off the socket, not yet parsed from m_inPos on
    qsizetype m_inPos = 0;
    bool m_draining = false;    ///< drain() is on the stack; a nested call returns
    QString m_selectedGroup;    ///< see selectedGroup(); reset with the connection
    qint64 m_kernelBufferCap = 0;   ///< SO_RCVBUF last set by applyBufferCaps(); 0 = OS default
    qint64 m_readBudget = 0;
    qint64 m_bytesRead = 0;

    bool m_failed = false;

    /// QSslSocket said connected: through a proxy, the tunnel is up.
    bool m_transportUp = false;

    /// In the open-connection registry, under this account id.
    bool m_counted = false;
    QString m_countedAccount;
};

} // namespace eMule::usenet
