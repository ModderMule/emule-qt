#include "nntp/NntpSocket.h"

#include "nntp/NntpCommand.h"
#include "utils/Log.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QScopeGuard>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <utility>

namespace eMule::usenet {

namespace {

/// NntpSocket::totalWireBytesRead(). Sockets live on the worker threads and the
/// reader is the queue's tick on the daemon thread, hence atomic; relaxed, since
/// nothing is ordered against it.
std::atomic<qint64> g_wireBytesRead{0};

/// Open authenticated connections, for statistics. Written by the worker
/// threads on connect and close only, so a mutex costs nothing that matters.
/// Never destroyed: a socket may still close during static destruction.
struct OpenConnections {
    QMutex mutex;
    QHash<QString, int> byAccount;
    int total = 0;
};

OpenConnections& openConnections()
{
    static auto* registry = new OpenConnections;
    return *registry;
}

/// Refill granularity for the read budget. Ten slices a second is fine-grained
/// enough that a limited connection does not arrive in visible bursts, and
/// coarse enough not to add a wakeup floor — macOS budgets ~150 wakeups/s per
/// process, and the upload throttler's 1 ms poll once spent 843 of them.
constexpr int kRefillIntervalMs = 100;

/// How much unused credit the budget may carry, in refill ticks.
///
/// A socket spends most of a high-latency link's time waiting: a command round
/// trip to a provider across an ocean is ~200 ms, i.e. two whole ticks during
/// which there is nothing to read. Assigning the budget each tick throws that
/// credit away, so the connection can never average the rate it was given — the
/// limiter silently delivers a fraction of its own setting. Accumulating repays
/// the wait; the cap stops a long idle from turning into an unbounded burst.
constexpr int kBurstTicks = 10;

/// Read-buffer cap for a limited socket, per layer: one burst's worth, rounded
/// up to 16 KiB steps so small rate shifts don't re-apply it. Without it the
/// buffers take whole articles at link speed and the wire sees bursts then
/// silence. Bounded, a full buffer closes the TCP window and the provider sends
/// at the limited rate. 64 KiB floor: 16 KiB measured live at 40 % of the limit,
/// the TCP window stuck near zero over a 220 ms round trip.
/// 0 = unbounded when unlimited.
[[nodiscard]] qint64 readBufferCap(qint64 bytesPerSecond)
{
    if (bytesPerSecond <= 0)
        return 0;
    constexpr qint64 kStep = 16 * 1024;
    const qint64 burst = bytesPerSecond * kRefillIntervalMs / 1000 * kBurstTicks;
    return std::clamp<qint64>((burst + kStep - 1) / kStep * kStep, 64 * 1024, 1024 * 1024);
}

/// NNTP status codes this class acts on. Everything else is passed to the
/// running command or reported as a protocol error.
constexpr int kGreetingPostingOk    = 200;
constexpr int kGreetingPostingNo    = 201;
constexpr int kClosing              = 205;
constexpr int kTlsProceed           = 382;
constexpr int kServiceUnavailable   = 400;
constexpr int kAuthAccepted         = 281;
constexpr int kAuthPasswordRequired = 381;
constexpr int kAuthRejected         = 481;
constexpr int kAuthOutOfSequence    = 482;
constexpr int kCommandUnavailable   = 502;

/// Whether a refusal names the account's connection limit. Providers disagree on
/// the code (400/502 at the greeting, 502 or 481 at AUTHINFO) but not on the
/// words, so the text decides, as in SABnzbd. A wrong password says neither.
bool saysTooManyConnections(const QString& text)
{
    return text.contains(QLatin1String("connection"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("too many"), Qt::CaseInsensitive);
}

/// AUTHINFO refusal: the connection limit, or the credentials.
NntpError authRefusal(const QString& text)
{
    return saysTooManyConnections(text) ? NntpError::TooManyConnections
                                        : NntpError::AuthFailed;
}

} // namespace

NntpSocket::NntpSocket(QObject* parent)
    : QObject(parent)
{
}

NntpSocket::~NntpSocket()
{
    // The socket is a child, so Qt would delete it anyway — but a queued
    // readyRead against a half-destroyed NntpSocket is a crash, so cut the
    // signals first.
    if (m_socket)
        m_socket->disconnect(this);
    markClosed();
}

void NntpSocket::connectToServer(const NewsServer& server)
{
    if (m_state != State::Disconnected) {
        logUsenetWarning(QStringLiteral("NNTP: already connected to %1, ignoring connect to %2")
                       .arg(m_server.displayName(), server.displayName()));
        return;
    }
    if (!server.isValid()) {
        fail(NntpError::ConnectFailed, QStringLiteral("Server has no host name"));
        return;
    }

    m_server = server;
    m_failed = false;
    m_transportUp = false;
    m_in.clear();
    m_inPos = 0;
    m_selectedGroup.clear();
    m_kernelBufferCap = 0;   // a new OS socket starts at the default

    if (!m_socket) {
        m_socket = new QSslSocket(this);
        connect(m_socket, &QSslSocket::connected,     this, [this] {
            m_transportUp = true;
            applyBufferCaps();   // SO_RCVBUF needs the OS socket
        });
        connect(m_socket, &QSslSocket::readyRead,     this, &NntpSocket::onReadyRead);
        connect(m_socket, &QSslSocket::errorOccurred, this, &NntpSocket::onSocketError);
        connect(m_socket, &QSslSocket::sslErrors,     this, &NntpSocket::onSslErrors);
        connect(m_socket, &QSslSocket::encrypted,     this, &NntpSocket::onEncrypted);
        connect(m_socket, &QSslSocket::disconnected,  this, &NntpSocket::onSocketDisconnected);
    }
    if (!m_responseTimer) {
        m_responseTimer = new QTimer(this);
        m_responseTimer->setSingleShot(true);
        connect(m_responseTimer, &QTimer::timeout, this, &NntpSocket::onResponseTimeout);
    }

    // Every connect, not only the first: a reused NntpSocket may have been given
    // a different route since.
    m_socket->setProxy(m_proxy);
    applyBufferCaps();
    applyTlsConfiguration();

    m_state = State::Connecting;
    logUsenetDebug(QStringLiteral("NNTP: connecting to %1:%2%3%4")
                .arg(m_server.host)
                .arg(m_server.port)
                .arg(m_server.tlsMode == TlsMode::None ? QString{}
                                                       : QStringLiteral(" (TLS)"))
                .arg(m_proxy.type() == QNetworkProxy::NoProxy
                         ? QString{}
                         : QStringLiteral(" via proxy %1:%2")
                               .arg(m_proxy.hostName()).arg(m_proxy.port())));

    // Implicit TLS negotiates before the greeting; STARTTLS and cleartext both
    // start as plain TCP. This is the explicit form of what SmtpClient decides
    // from `port == 465` — NNTP uses 563 and 443, and STARTTLS runs on 119.
    if (m_server.tlsMode == TlsMode::Implicit)
        m_socket->connectToHostEncrypted(m_server.host, m_server.port);
    else
        m_socket->connectToHost(m_server.host, m_server.port);

    m_state = State::Greeting;
    armResponseTimer();
}

void NntpSocket::sendCommand(NntpCommand* command)
{
    if (!command)
        return;

    // Pipelined (RFC 3977 §3.5): written now, answered in order after the
    // running command. Keeps the wire busy while the current body drains.
    if (m_state == State::CommandStatus || m_state == State::CommandBody) {
        m_pipeline.append(command);
        sendLine(command->requestLine());
        return;
    }

    if (m_state != State::Ready) {
        logUsenetWarning(QStringLiteral("NNTP: %1 not ready, dropping command")
                       .arg(m_server.displayName()));
        command->fail(NntpError::ProtocolError, QStringLiteral("Connection not ready"));
        command->onComplete();
        emit commandFinished(command);
        return;
    }

    if (m_idleTimer)
        m_idleTimer->stop();

    m_command = command;
    m_state = State::CommandStatus;
    m_bodyBytes = 0;
    sendLine(command->requestLine());
    m_latencyClock.start();   // only a command on an idle line times the server
    armResponseTimer();
}

void NntpSocket::close()
{
    if (!m_socket || m_state == State::Disconnected)
        return;

    disarmTimers();

    if (m_state == State::Ready) {
        m_state = State::QuitSent;
        sendLine(QByteArrayLiteral("QUIT"));
        armResponseTimer();
    } else {
        abort();
    }
}

void NntpSocket::abort()
{
    // No callbacks: the owner is tearing this connection down on purpose.
    m_command = nullptr;
    m_pipeline.clear();
    disarmTimers();
    enterDisconnected();
    if (m_socket)
        m_socket->abort();
}

bool NntpSocket::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void NntpSocket::setReadRateLimit(qint64 bytesPerSecond)
{
    bytesPerSecond = std::max<qint64>(0, bytesPerSecond);

    // Re-applied on every lease, every release and every bandwidth-split tick,
    // nearly always with the figure already in force. Resetting the budget each
    // time gave a drained socket a free refill per call — measured live, the
    // engine ran ~12 % over its cap — and threw away credit banked while waiting
    // on a round trip, which is the whole point of kBurstTicks.
    if (bytesPerSecond == m_readRateLimit)
        return;
    m_readRateLimit = bytesPerSecond;
    if (m_socket)
        applyBufferCaps();

    if (m_readRateLimit == 0) {
        if (m_refillTimer)
            m_refillTimer->stop();
        m_readBudget = 0;
        // Whatever the socket is already holding was never going to arrive on
        // its own — readyRead does not fire again for bytes already buffered.
        drain();
        return;
    }

    if (!m_refillTimer) {
        m_refillTimer = new QTimer(this);
        connect(m_refillTimer, &QTimer::timeout, this, &NntpSocket::onReadBudgetRefill);
    }

    const qint64 perTick = m_readRateLimit * kRefillIntervalMs / 1000;
    if (m_refillTimer->isActive()) {
        // A new figure on a limited socket: keep what it has banked, or owes,
        // within the new burst cap. The running timer carries on at the new rate.
        m_readBudget = std::min(m_readBudget, perTick * kBurstTicks);
    } else {
        m_readBudget = perTick;
        m_refillTimer->start(kRefillIntervalMs);
    }
}

qint64 NntpSocket::bufferedBytes() const
{
    return (m_socket ? m_socket->bytesAvailable() : 0) + (m_in.size() - m_inPos);
}

qint64 NntpSocket::readBufferCapBytes() const
{
    return readBufferCap(m_readRateLimit);
}

qint64 NntpSocket::totalWireBytesRead()
{
    return g_wireBytesRead.load(std::memory_order_relaxed);
}

QHash<QString, int> NntpSocket::openConnectionsByAccount()
{
    auto& reg = openConnections();
    QMutexLocker lock(&reg.mutex);
    return reg.byAccount;
}

int NntpSocket::openConnectionCount()
{
    auto& reg = openConnections();
    QMutexLocker lock(&reg.mutex);
    return reg.total;
}

qint64 NntpSocket::takeBytesRead()
{
    const qint64 spent = m_bytesRead;
    m_bytesRead = 0;
    return spent;
}

void NntpSocket::setResponseTimeout(int ms)
{
    m_responseTimeoutMs = ms;
}

void NntpSocket::setIdleTimeout(int ms)
{
    m_idleTimeoutMs = ms;
    if (m_idleTimeoutMs <= 0 && m_idleTimer)
        m_idleTimer->stop();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void NntpSocket::onReadyRead()
{
    drain();
}

void NntpSocket::onSocketError()
{
    // Qt reports a clean remote close as RemoteHostClosedError. During QUIT
    // that is the expected end of the conversation, not a failure.
    if (m_state == State::QuitSent || m_state == State::Disconnected)
        return;

    const QAbstractSocket::SocketError code = m_socket->error();
    bool proxyFault = false;
    switch (code) {
    // The proxy's own refusal, named as such: the queue waits for a proxy and
    // must never back a provider off, or book an article missing, over one.
    case QAbstractSocket::ProxyAuthenticationRequiredError:
    case QAbstractSocket::ProxyConnectionRefusedError:
    case QAbstractSocket::ProxyConnectionClosedError:
    case QAbstractSocket::ProxyConnectionTimeoutError:
    case QAbstractSocket::ProxyNotFoundError:
    case QAbstractSocket::ProxyProtocolError:
        proxyFault = true;
        break;
    // Darwin 27 answers the retry-connect of a refused socket with EISCONN, so
    // Qt's SOCKS engine "reaches" a dead proxy and fails later with a plain
    // NetworkError/RemoteHostClosed. Before the tunnel is up, that's the proxy.
    case QAbstractSocket::NetworkError:
    case QAbstractSocket::RemoteHostClosedError:
        proxyFault = m_proxy.type() != QNetworkProxy::NoProxy && !m_transportUp;
        break;
    default:
        break;
    }
    if (proxyFault) {
        fail(NntpError::ProxyFailed, QStringLiteral("proxy %1:%2: %3")
                                         .arg(m_proxy.hostName())
                                         .arg(m_proxy.port())
                                         .arg(m_socket->errorString()));
        return;
    }

    const auto err = code == QAbstractSocket::SslHandshakeFailedError ? NntpError::TlsFailed
                   : code == QAbstractSocket::RemoteHostClosedError   ? NntpError::Disconnected
                                                                      : NntpError::ConnectFailed;
    fail(err, m_socket->errorString());
}

void NntpSocket::onSslErrors(const QList<QSslError>& errors)
{
    switch (m_server.certVerification) {
    case CertVerification::None:
        // The user asked for this explicitly. Say so once rather than silently.
        logUsenetWarning(QStringLiteral("NNTP: %1 — ignoring %2 certificate error(s), "
                                  "verification is disabled for this server")
                       .arg(m_server.displayName())
                       .arg(errors.size()));
        m_socket->ignoreSslErrors();
        return;

    case CertVerification::Minimal: {
        // Chain must validate; a hostname mismatch is tolerated. Providers
        // routinely front many brands with one certificate.
        QList<QSslError> ignorable;
        for (const auto& e : errors) {
            if (e.error() == QSslError::HostNameMismatch)
                ignorable.append(e);
        }
        if (ignorable.size() == errors.size()) {
            m_socket->ignoreSslErrors(ignorable);
            return;
        }
        break;
    }

    case CertVerification::Strict:
        break;
    }

    QStringList texts;
    texts.reserve(errors.size());
    for (const auto& e : errors)
        texts.append(e.errorString());
    fail(NntpError::TlsFailed, texts.join(QStringLiteral("; ")));
}

void NntpSocket::onEncrypted()
{
    if (m_state != State::StartTlsSent)
        return;

    // RFC 4642: after a successful upgrade the session resets to its initial
    // state, so authentication has to happen again on the encrypted channel —
    // which is the entire point of doing it.
    beginAuthOrReady();
}

void NntpSocket::onSocketDisconnected()
{
    const bool wasQuit = m_state == State::QuitSent;
    disarmTimers();

    if (!wasQuit && m_state != State::Disconnected && !m_failed)
        fail(NntpError::Disconnected, QStringLiteral("Server closed the connection"));

    enterDisconnected();
    emit disconnected();
}

void NntpSocket::onResponseTimeout()
{
    fail(NntpError::Timeout,
         QStringLiteral("No response from %1 within %2 s")
             .arg(m_server.displayName())
             .arg(m_responseTimeoutMs / 1000));
}

void NntpSocket::onIdleTimeout()
{
    if (m_state == State::Ready) {
        logUsenetDebug(QStringLiteral("NNTP: %1 idle for %2 s, closing")
                           .arg(m_server.displayName()).arg(m_idleTimeoutMs / 1000));
        close();
    }
}

void NntpSocket::onReadBudgetRefill()
{
    const qint64 perTick = m_readRateLimit * kRefillIntervalMs / 1000;
    m_readBudget = std::min(m_readBudget + perTick, perTick * kBurstTicks);
    drain();
}

void NntpSocket::applyTlsConfiguration()
{
    auto conf = m_socket->sslConfiguration();
    conf.setPeerVerifyMode(m_server.certVerification == CertVerification::None
                               ? QSslSocket::VerifyNone
                               : QSslSocket::VerifyPeer);
    m_socket->setSslConfiguration(conf);
    m_socket->setPeerVerifyName(m_server.host);
}

void NntpSocket::sendLine(QByteArrayView line)
{
    if (!m_socket)
        return;
    QByteArray out;
    out.reserve(line.size() + 2);
    out.append(line);
    out.append("\r\n", 2);
    m_socket->write(out);
}

void NntpSocket::drain()
{
    if (!m_socket)
        return;
    // A handler can reach drain() again (setReadRateLimit on a lease or release).
    // The outer loop is still holding views into m_in and carries on reading
    // anyway, so the nested call has nothing to add.
    if (m_draining)
        return;
    m_draining = true;

    // Published once per call rather than per line: one atomic add per read
    // burst instead of one per ~128-byte yEnc line.
    qint64 drained = 0;
    bool bodyConsumed = false;
    // Last: a bodyProgress() slot may pipeline another command.
    const auto progress = qScopeGuard([this, &drained, &bodyConsumed] {
        m_draining = false;
        // Once per read, not per line: QTimer::start() per line was ~800k timer
        // re-registrations a second at full speed.
        if (bodyConsumed && m_state == State::CommandBody)
            armResponseTimer();
        if (drained > 0 && m_command && m_state != State::Disconnected)
            emit bodyProgress();
    });
    const auto publish = qScopeGuard(
        [&drained] { g_wireBytesRead.fetch_add(drained, std::memory_order_relaxed); });

    qsizetype pos = m_inPos;
    const auto keepPos = qScopeGuard([this, &pos] { m_inPos = pos; });

    while (m_state != State::Disconnected) {
        if (pos < m_in.size()) {
            const QByteArrayView unread(m_in.constData() + pos, m_in.size() - pos);

            // Block path: the command takes the raw, still dot-stuffed bytes and
            // says where its body ended. What follows is the next pipelined
            // status line, parsed below on this same pass.
            if (m_state == State::CommandBody && m_command && m_command->wantsRawBody()) {
                bool ended = false;
                const qsizetype used = m_command->onBodyData(unread, ended);
                pos += used;
                m_bodyBytes += used;
                bodyConsumed = bodyConsumed || used > 0;
                if (ended) {
                    finishCommand();
                    continue;
                }
                // Not ended means it took everything (partial lines it buffers
                // itself), so all that is left to do is read more.
            } else if (const qsizetype nl = unread.indexOf('\n'); nl >= 0) {
                QByteArrayView line = unread.first(nl);
                pos += nl + 1;
                // Strip CR. Everything downstream works on the bare line.
                while (line.endsWith('\r'))
                    line.chop(1);

                if (m_state == State::CommandBody) {
                    m_bodyBytes += line.size() + 2;
                    bodyConsumed = true;
                    handleBodyLine(line);
                } else {
                    handleStatusLine(line);
                }
                continue;
            }
        }

        // Only here, with no views into m_in outstanding, may it move.
        if (pos > 0) {
            m_in.remove(0, pos);
            pos = 0;
        }

        // Take everything we may: only a read that empties Qt's buffer makes
        // QSslSocket decrypt more. A limit of 0 means unlimited.
        const qint64 available = m_socket->bytesAvailable();
        if (available <= 0)
            break;
        qint64 want = available;
        if (m_readRateLimit > 0) {
            // Out of budget. The refill timer calls back into drain(), so the
            // buffered remainder is not stranded waiting for a readyRead that
            // will never come for bytes already delivered.
            if (m_readBudget <= 0)
                break;
            want = std::min(want, m_readBudget);
        }
        const qsizetype old = m_in.size();
        m_in.resize(old + want);
        const qint64 got = m_socket->read(m_in.data() + old, want);
        m_in.resize(old + std::max<qint64>(0, got));
        if (got <= 0)
            break;
        if (m_readRateLimit > 0)
            m_readBudget -= got;
        m_bytesRead += got;   // the wire count
        drained += got;
    }
}

void NntpSocket::handleStatusLine(QByteArrayView raw)
{
    const QString line = QString::fromLatin1(raw);
    bool codeOk = false;
    const int code = line.left(3).toInt(&codeOk);
    if (!codeOk) {
        fail(NntpError::ProtocolError,
             QStringLiteral("Unparseable response: %1").arg(line));
        return;
    }
    const QString text = line.mid(4).trimmed();

    if (m_responseTimer)
        m_responseTimer->stop();

    switch (m_state) {
    case State::Greeting:
        // 200 posting allowed, 201 posting prohibited — both fine for reading.
        // 400 and 502 at the greeting mean "too many connections" (the pool
        // stops growing) or "account blocked" (the whole server backs off),
        // never a fault of the article.
        if (code == kGreetingPostingOk || code == kGreetingPostingNo) {
            if (m_server.tlsMode == TlsMode::StartTls) {
                m_state = State::StartTlsSent;
                sendLine(QByteArrayLiteral("STARTTLS"));
                armResponseTimer();
            } else {
                m_state = State::ModeReaderSent;
                sendLine(QByteArrayLiteral("MODE READER"));
                armResponseTimer();
            }
        } else if (code == kServiceUnavailable || code == kCommandUnavailable) {
            fail(saysTooManyConnections(text) ? NntpError::TooManyConnections
                                              : NntpError::ServerUnavailable,
                 line);
        } else {
            fail(NntpError::ProtocolError, QStringLiteral("Unexpected greeting: %1").arg(line));
        }
        break;

    case State::StartTlsSent:
        if (code == kTlsProceed) {
            // onEncrypted() picks the sequence back up. Nothing may be written
            // between here and the handshake completing.
            m_socket->startClientEncryption();
            armResponseTimer();
        } else {
            fail(NntpError::TlsFailed, QStringLiteral("STARTTLS refused: %1").arg(line));
        }
        break;

    case State::ModeReaderSent:
        // 200/201 is the answer; 500 means the server has no reader/transit
        // distinction, which is fine and not an error.
        if (code == kGreetingPostingOk || code == kGreetingPostingNo || code >= 500)
            beginAuthOrReady();
        else
            fail(NntpError::ProtocolError, QStringLiteral("MODE READER failed: %1").arg(line));
        break;

    case State::AuthUserSent:
        if (code == kAuthPasswordRequired) {
            m_state = State::AuthPassSent;
            const QByteArray line = QByteArrayLiteral("AUTHINFO PASS ") + m_server.pass.toUtf8();
            sendLine(line);
            armResponseTimer();
        } else if (code == kAuthAccepted) {
            // Some providers authenticate on the user name alone.
            enterReady();
        } else {
            fail(authRefusal(text), line);
        }
        break;

    case State::AuthPassSent:
        if (code == kAuthAccepted)
            enterReady();
        else if (code == kAuthRejected || code == kAuthOutOfSequence || code == kCommandUnavailable)
            fail(authRefusal(text), line);
        else
            fail(NntpError::ProtocolError, QStringLiteral("AUTHINFO PASS: %1").arg(line));
        break;

    case State::CommandStatus: {
        NntpCommand* cmd = m_command;
        if (!cmd) {
            fail(NntpError::ProtocolError, QStringLiteral("Unsolicited response: %1").arg(line));
            break;
        }
        if (m_latencyClock.isValid()) {
            const qint64 ms = m_latencyClock.elapsed();
            m_latencyClock.invalidate();
            m_latencyMs = m_latencySampled ? (m_latencyMs * 3 + ms) / 4 : ms;
            m_latencySampled = true;
        }
        cmd->onStatus(code, text);
        if (!cmd->failed() && cmd->hasBodyFor(code)) {
            m_bodyBytes = 0;
            m_state = State::CommandBody;
            armResponseTimer();
        } else {
            finishCommand();
        }
        break;
    }

    case State::QuitSent:
        // 205 is the polite answer; anything else still means we are done.
        Q_UNUSED(kClosing);
        enterDisconnected();
        m_socket->disconnectFromHost();
        break;

    case State::Ready:
        // Nothing was asked for. A server volunteering a line here is broken,
        // and continuing would desynchronise every later response.
        fail(NntpError::ProtocolError, QStringLiteral("Unsolicited response: %1").arg(line));
        break;

    case State::Disconnected:
    case State::Connecting:
        break;
    }
}

void NntpSocket::handleBodyLine(QByteArrayView raw)
{
    // A lone "." ends the block. "..", and any other leading-dot line, is a
    // stuffed line whose first dot the server added (RFC 3977 §3.1.1).
    if (raw == ".") {
        finishCommand();
        return;
    }

    if (m_command) {
        QByteArrayView line{raw};
        if (line.startsWith('.'))
            line = line.sliced(1);
        m_command->onBodyLine(line);
    }
}

void NntpSocket::beginAuthOrReady()
{
    if (m_server.user.isEmpty()) {
        enterReady();
        return;
    }
    m_state = State::AuthUserSent;
    const QByteArray line = QByteArrayLiteral("AUTHINFO USER ") + m_server.user.toUtf8();
    sendLine(line);
    armResponseTimer();
}

void NntpSocket::enterReady()
{
    m_state = State::Ready;
    markOpen();
    if (m_responseTimer)
        m_responseTimer->stop();

    if (m_idleTimeoutMs > 0) {
        if (!m_idleTimer) {
            m_idleTimer = new QTimer(this);
            m_idleTimer->setSingleShot(true);
            connect(m_idleTimer, &QTimer::timeout, this, &NntpSocket::onIdleTimeout);
        }
        m_idleTimer->start(m_idleTimeoutMs);
    }

    logUsenetDebug(QStringLiteral("NNTP: %1 ready").arg(m_server.displayName()));
    emit ready();
}

void NntpSocket::finishCommand()
{
    NntpCommand* cmd = m_command;
    m_command = nullptr;
    m_bodyBytes = 0;
    m_latencyClock.invalidate();
    if (!m_pipeline.isEmpty()) {
        // Next answer is already on its way, maybe already buffered: drain()
        // carries on parsing it as this command's status.
        m_command = m_pipeline.takeFirst();
        m_state = State::CommandStatus;
        armResponseTimer();
    } else {
        m_state = State::Ready;
        if (m_responseTimer)
            m_responseTimer->stop();
        if (m_idleTimer && m_idleTimeoutMs > 0)
            m_idleTimer->start(m_idleTimeoutMs);
    }

    if (cmd) {
        cmd->onComplete();
        emit commandFinished(cmd);
    }
}

void NntpSocket::fail(NntpError error, const QString& text)
{
    if (m_failed)
        return;
    m_failed = true;

    disarmTimers();

    // A command in flight learns why it died before the socket goes away, so
    // the scheduler sees the real reason rather than a generic disconnect.
    // Pipelined ones too, in order.
    QList<NntpCommand*> dying = std::exchange(m_pipeline, {});
    if (NntpCommand* cmd = std::exchange(m_command, nullptr))
        dying.prepend(cmd);
    for (NntpCommand* cmd : std::as_const(dying)) {
        cmd->fail(error, text);
        cmd->onComplete();
        emit commandFinished(cmd);
    }

    logUsenetWarning(QStringLiteral("NNTP: %1 — %2: %3")
                   .arg(m_server.displayName(), describeNntpError(error), text));

    const bool wasConnected = m_state != State::Disconnected;
    enterDisconnected();
    emit failed(error, text);

    if (m_socket && wasConnected)
        m_socket->abort();
}

void NntpSocket::armResponseTimer()
{
    if (m_responseTimer && m_responseTimeoutMs > 0)
        m_responseTimer->start(m_responseTimeoutMs);
}

void NntpSocket::disarmTimers()
{
    if (m_responseTimer)
        m_responseTimer->stop();
    if (m_idleTimer)
        m_idleTimer->stop();
    if (m_refillTimer)
        m_refillTimer->stop();
}

void NntpSocket::enterDisconnected()
{
    m_state = State::Disconnected;
    m_selectedGroup.clear();
    markClosed();
}

void NntpSocket::markOpen()
{
    if (m_counted)
        return;
    m_counted = true;
    m_countedAccount = m_server.accountId;

    auto& reg = openConnections();
    QMutexLocker lock(&reg.mutex);
    ++reg.byAccount[m_countedAccount];
    ++reg.total;
}

void NntpSocket::markClosed()
{
    if (!m_counted)
        return;
    m_counted = false;

    auto& reg = openConnections();
    QMutexLocker lock(&reg.mutex);
    if (auto it = reg.byAccount.find(m_countedAccount); it != reg.byAccount.end()
        && --it.value() <= 0)
        reg.byAccount.erase(it);
    --reg.total;
}

void NntpSocket::applyBufferCaps()
{
    // Both layers. Qt's cap alone leaves the kernel's auto-tuned receive buffer
    // (MBs on macOS/Linux/Windows) to take whole articles at link speed: every
    // connection bursts at once, then all go quiet while the bucket drains them.
    // SO_RCVBUF via Qt maps to setsockopt on every OS; Linux doubles the value.
    const qint64 cap = readBufferCap(m_readRateLimit);
    m_socket->setReadBufferSize(cap);
    if (m_socket->state() != QAbstractSocket::ConnectedState || cap == m_kernelBufferCap)
        return;
    // Shrinking the kernel buffer shrinks the TCP window under a sender, and
    // rates shift on every sibling lease. Only grow, or shrink when 2x too big.
    const bool grow = m_kernelBufferCap == 0 || cap == 0 || cap > m_kernelBufferCap;
    if (!grow && cap * 2 > m_kernelBufferCap)
        return;
    if (cap > 0) {
        m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, int(cap));
    } else {
        // Auto-tuning can't be switched back on; a large fixed buffer is next best.
        m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,
                                  4 * 1024 * 1024);
    }
    m_kernelBufferCap = cap;
}

} // namespace eMule::usenet
