#include "nntp/NntpSocket.h"

#include "nntp/NntpCommand.h"
#include "utils/Log.h"

#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace eMule::usenet {

namespace {

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
}

void NntpSocket::connectToServer(const NewsServer& server)
{
    if (m_state != State::Disconnected) {
        logWarning(QStringLiteral("NNTP: already connected to %1, ignoring connect to %2")
                       .arg(m_server.displayName(), server.displayName()));
        return;
    }
    if (!server.isValid()) {
        fail(NntpError::ConnectFailed, QStringLiteral("Server has no host name"));
        return;
    }

    m_server = server;
    m_failed = false;

    if (!m_socket) {
        m_socket = new QSslSocket(this);
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

    applyTlsConfiguration();

    m_state = State::Connecting;
    logInfo(QStringLiteral("NNTP: connecting to %1:%2%3")
                .arg(m_server.host)
                .arg(m_server.port)
                .arg(m_server.tlsMode == TlsMode::None ? QString{}
                                                       : QStringLiteral(" (TLS)")));

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
    if (m_state != State::Ready) {
        logWarning(QStringLiteral("NNTP: %1 not ready, dropping command")
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
    sendLine(command->requestLine());
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
    disarmTimers();
    m_state = State::Disconnected;
    if (m_socket)
        m_socket->abort();
}

bool NntpSocket::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void NntpSocket::setReadRateLimit(qint64 bytesPerSecond)
{
    m_readRateLimit = std::max<qint64>(0, bytesPerSecond);

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
    m_readBudget = m_readRateLimit * kRefillIntervalMs / 1000;
    m_refillTimer->start(kRefillIntervalMs);
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

    const auto err = m_socket->error() == QAbstractSocket::SslHandshakeFailedError
                         ? NntpError::TlsFailed
                     : m_socket->error() == QAbstractSocket::RemoteHostClosedError
                         ? NntpError::Disconnected
                         : NntpError::ConnectFailed;
    fail(err, m_socket->errorString());
}

void NntpSocket::onSslErrors(const QList<QSslError>& errors)
{
    switch (m_server.certVerification) {
    case CertVerification::None:
        // The user asked for this explicitly. Say so once rather than silently.
        logWarning(QStringLiteral("NNTP: %1 — ignoring %2 certificate error(s), "
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

    m_state = State::Disconnected;
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
    if (m_state == State::Ready)
        close();
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

    while (m_socket->canReadLine()) {
        // A limit of 0 means unlimited — read everything the socket holds.
        if (m_readRateLimit > 0 && m_readBudget <= 0) {
            // Out of budget. The refill timer calls back into drain(), so the
            // buffered remainder is not stranded waiting for a readyRead that
            // will never come for bytes already delivered.
            return;
        }

        QByteArray raw = m_socket->readLine();
        m_readBudget -= raw.size();

        // Strip CRLF / LF. Everything downstream works on the bare line.
        while (raw.endsWith('\n') || raw.endsWith('\r'))
            raw.chop(1);

        if (m_state == State::CommandBody)
            handleBodyLine(raw);
        else
            handleStatusLine(raw);

        if (m_state == State::Disconnected)
            return;
    }
}

void NntpSocket::handleStatusLine(const QByteArray& raw)
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
        // 400 and 502 at the greeting almost always mean "too many connections"
        // or "account blocked", which back the whole server off rather than
        // failing just this article.
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
            fail(NntpError::ServerUnavailable, line);
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
            fail(NntpError::AuthFailed, line);
        }
        break;

    case State::AuthPassSent:
        if (code == kAuthAccepted)
            enterReady();
        else if (code == kAuthRejected || code == kAuthOutOfSequence || code == kCommandUnavailable)
            fail(NntpError::AuthFailed, line);
        else
            fail(NntpError::ProtocolError, QStringLiteral("AUTHINFO PASS: %1").arg(line));
        break;

    case State::CommandStatus: {
        NntpCommand* cmd = m_command;
        if (!cmd) {
            fail(NntpError::ProtocolError, QStringLiteral("Unsolicited response: %1").arg(line));
            break;
        }
        cmd->onStatus(code, text);
        if (!cmd->failed() && cmd->hasBodyFor(code)) {
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
        m_state = State::Disconnected;
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

void NntpSocket::handleBodyLine(const QByteArray& raw)
{
    // A lone "." ends the block. "..", and any other leading-dot line, is a
    // stuffed line whose first dot the server added (RFC 3977 §3.1.1).
    if (raw == ".") {
        finishCommand();
        return;
    }

    armResponseTimer();

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

    logInfo(QStringLiteral("NNTP: %1 ready").arg(m_server.displayName()));
    emit ready();
}

void NntpSocket::finishCommand()
{
    NntpCommand* cmd = m_command;
    m_command = nullptr;
    m_state = State::Ready;
    if (m_responseTimer)
        m_responseTimer->stop();
    if (m_idleTimer && m_idleTimeoutMs > 0)
        m_idleTimer->start(m_idleTimeoutMs);

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
    if (NntpCommand* cmd = std::exchange(m_command, nullptr)) {
        cmd->fail(error, text);
        cmd->onComplete();
        emit commandFinished(cmd);
    }

    logWarning(QStringLiteral("NNTP: %1 — %2: %3")
                   .arg(m_server.displayName(), describeNntpError(error), text));

    const bool wasConnected = m_state != State::Disconnected;
    m_state = State::Disconnected;
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

} // namespace eMule::usenet
