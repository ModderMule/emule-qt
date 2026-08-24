#include "pch.h"
/// @file UdpMappingBackend.cpp
/// @brief Shared machinery for the two UDP/5351 protocols.

#include "portmap/UdpMappingBackend.h"
#include "net/LocalIPv6.h"
#include "portmap/PortMapWire.h"
#include "utils/Log.h"

#include <QNetworkDatagram>

#include <algorithm>

namespace eMule {

namespace {

constexpr uint16 kServerPort = 5351;

/// Bounded discovery ladder: send, then retry at 250 ms and 750 ms, giving up
/// around 1.75 s. Well inside PortMapper's 3 s probe deadline.
constexpr int kProbeAttempts = 3;
constexpr int kProbeBaseDelayMs = 250;

/// Live requests get a little more patience than discovery, but nothing like
/// the RFC schedules — a mapping that needs 128 s to appear is not useful.
constexpr int kRequestAttempts = 4;
constexpr int kRequestBaseDelayMs = 400;

constexpr int kMaxDatagramSize = 1500;

[[nodiscard]] int backoffMs(int base, int attempt)
{
    return base * (1 << std::min(attempt, 4));
}

} // namespace

UdpMappingBackend::UdpMappingBackend(QObject* parent)
    : PortMapBackend(parent)
{
}

UdpMappingBackend::~UdpMappingBackend()
{
    // A transaction can still be in flight, and its socket has our slots attached. Detach
    // them before the transaction vector deletes the sockets inline, so a notification that
    // arrives mid-teardown cannot reach a half-destroyed backend. This is retire() minus the
    // deleteLater: we are going away, and deferring here would leak whenever the event loop
    // is already gone.
    for (auto& transaction : m_transactions) {
        if (transaction->timer)
            transaction->timer->disconnect(this);
        if (transaction->socket)
            transaction->socket->disconnect(this);
    }
    closeChannels();
}

// ---------------------------------------------------------------------------
// PortMapBackend
// ---------------------------------------------------------------------------

void UdpMappingBackend::probe(int timeoutMs)
{
    Q_UNUSED(timeoutMs)

    m_probing = true;
    m_probeAnswered = false;
    m_probesOutstanding = 0;

    const bool haveV4 = openChannel(m_v4, PortMapFamily::IPv4);
    const bool haveV6 = supports(PortMapFamily::IPv6)
                        && openChannel(m_v6, PortMapFamily::IPv6);

    if (!haveV4 && !haveV6) {
        // No default route means "do not send" — never a guessed gateway.
        finishProbe(false, QStringLiteral("no usable default gateway"));
        return;
    }

    // Two passes on purpose. transmit() can fail synchronously — a refused write, or no
    // socket at all — and that failure decrements m_probesOutstanding and calls
    // finishProbe(). Creating and counting both families first means a dead v4 channel
    // cannot drive the count to zero while v6 is still unsent: finishProbe() clears
    // m_probing, so the v6 answer would afterwards be discarded and the whole protocol
    // written off as unsupported even though it works over IPv6.
    std::vector<int> probeIds;
    for (Channel* channel : {&m_v4, &m_v6}) {
        if (!channel->open)
            continue;
        QByteArray payload = encodeProbe(*channel);
        if (payload.isEmpty())
            continue;
        const PortMapFamily family = channel == &m_v4 ? PortMapFamily::IPv4
                                                      : PortMapFamily::IPv6;
        Transaction* transaction = startTransaction(family, std::move(payload), kProbeAttempts);
        transaction->isProbe = true;
        probeIds.push_back(transaction->id);
        ++m_probesOutstanding;
    }

    if (m_probesOutstanding == 0) {
        finishProbe(false, QStringLiteral("nothing to probe"));
        return;
    }

    // By id, never by pointer: an earlier transmit() in this loop can erase a later
    // transaction — the sibling family shares nothing, but shutdown() reached from a
    // synchronous failure would take both.
    for (const int id : probeIds) {
        if (Transaction* transaction = findTransaction(id))
            transmit(*transaction);
    }
}

void UdpMappingBackend::requestMapping(const PortMapRequest& request, uint32 lifetimeSecs)
{
    Channel* channel = channelFor(request.family);
    if (channel == nullptr || !channel->open) {
        PortMapping mapping;
        mapping.request = request;
        emit mappingResult(mapping, false, QStringLiteral("no gateway for this family"));
        return;
    }

    Transaction* transaction = startTransaction(request.family, QByteArray(), kRequestAttempts);
    transaction->request = request;
    transaction->lifetimeSecs = lifetimeSecs;
    transaction->payload = encodeMap(*channel, request, lifetimeSecs, *transaction);

    if (transaction->payload.isEmpty()) {
        failTransaction(*transaction, QStringLiteral("could not encode request"));
        return;
    }
    transmit(*transaction);
}

void UdpMappingBackend::releaseMapping(const PortMapping& mapping)
{
    Channel* channel = channelFor(mapping.request.family);
    if (channel == nullptr || !channel->open)
        return;

    QByteArray payload = encodeDelete(*channel, mapping);
    if (payload.isEmpty())
        return;

    // Fire and forget on a throwaway socket. The dev FRITZ!Box never
    // acknowledges a PCP delete even though the request matches RFC 6887
    // section 15.1 exactly, so waiting for a reply would stall every shutdown by
    // the full ladder. The finite lease is the real cleanup mechanism; this is
    // the polite early release.
    //
    // The socket dies at the end of this scope, right after write() buffers the datagram,
    // which looks like the trap ClientReqSocket::disconnect() flushes for
    // (ClientReqSocket.cpp:66-73) — but it is not one: closing a QAbstractSocket pushes its
    // pending write out first, and tst_UdpMappingBackend::releaseSendsTheDatagram holds that
    // down. No flush here, so shutdown cannot stall on a router that never answers.
    if (auto socket = makeSocket(mapping.request.family))
        socket->write(payload);
}

void UdpMappingBackend::shutdown()
{
    for (auto& transaction : m_transactions)
        retire(*transaction);
    m_transactions.clear();
    m_probing = false;
    m_probesOutstanding = 0;
    closeChannels();
}

UdpMappingBackend::Channel* UdpMappingBackend::channelFor(PortMapFamily family)
{
    return family == PortMapFamily::IPv6 ? &m_v6 : &m_v4;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

bool UdpMappingBackend::openChannel(Channel& channel, PortMapFamily family)
{
    if (channel.open)
        return true;

    const auto addressFamily = family == PortMapFamily::IPv6 ? Address::Family::IPv6
                                                             : Address::Family::IPv4;
    const auto candidates = defaultGateways(addressFamily);
    if (candidates.empty())
        return false;

    // Only relevant for IPv6, where the source address must be our GUA.
    const Address preferredSource = family == PortMapFamily::IPv6
                                        ? selectPreferredIPv6(scanLocalIPv6())
                                        : Address{};
    if (family == PortMapFamily::IPv6 && !preferredSource.isIPv6())
        return false;

    for (const GatewayCandidate& gateway : candidates) {
        // A throwaway socket, purely to learn which source address the kernel
        // picks for this gateway. Every real request gets its own socket.
        QUdpSocket probe;
        if (family == PortMapFamily::IPv6
            && !probe.bind(preferredSource.toQHostAddress(), 0)) {
            continue;
        }
        probe.connectToHost(gateway.toQHostAddress(), kServerPort);
        if (probe.state() != QAbstractSocket::ConnectedState && !probe.waitForConnected(200))
            continue;

        channel.gateway = gateway;
        channel.localAddress = Address::fromQHostAddress(probe.localAddress());
        channel.open = true;
        logDebug(QStringLiteral("%1: talking to %2 from %3")
                     .arg(name(), gateway.toString(), channel.localAddress.toString()));
        return true;
    }
    return false;
}

void UdpMappingBackend::closeChannels()
{
    for (Channel* channel : {&m_v4, &m_v6}) {
        channel->open = false;
        channel->localAddress = Address{};
    }
}

std::unique_ptr<QUdpSocket> UdpMappingBackend::makeSocket(PortMapFamily family)
{
    Channel* channel = channelFor(family);
    if (channel == nullptr || !channel->open)
        return nullptr;

    auto socket = std::make_unique<QUdpSocket>();
    if (family == PortMapFamily::IPv6) {
        // Bind to the GUA before connecting. The v6 next hop is link-local, so
        // the kernel would otherwise source from a link-local address and the
        // router answers NOT_AUTHORIZED to everything, ANNOUNCE included.
        if (!socket->bind(channel->localAddress.toQHostAddress(), 0))
            return nullptr;
    }
    socket->connectToHost(channel->gateway.toQHostAddress(), kServerPort);
    if (socket->state() != QAbstractSocket::ConnectedState && !socket->waitForConnected(200))
        return nullptr;
    return socket;
}

void UdpMappingBackend::onTransactionReadyRead(int transactionId)
{
    // Nothing here may hold a Transaction* across a call that can emit, because two of the
    // calls in this loop destroy this very transaction:
    //
    //  * receiveDatagram() reports a failed read through setErrorAndEmit(), so
    //    errorOccurred fires *inside* it and onTransactionError() erases us. On Windows a
    //    connected UDP socket that collected an ICMP port-unreachable fails its read with
    //    WSAECONNRESET, which Qt maps to ConnectionRefusedError — the exact condition in
    //    issue #5, where a router with nothing on UDP/5351 killed the daemon.
    //  * decodeReply() is a subclass hook that emits: PcpBackend publishes
    //    externalAddressLearned and mappingsInvalidated from inside it, and those reach
    //    PortMapper, CoreSession and theApp.setPublicIP().
    //
    // So the transaction is re-resolved by id after every such call. This is not
    // theoretical hardening — the old `while (transaction->socket->hasPendingDatagrams())`
    // re-read a freed Transaction whose socket unique_ptr had already been release()d.
    for (;;) {
        Transaction* transaction = findTransaction(transactionId);
        if (transaction == nullptr || !transaction->socket)
            return;

        // Held across receiveDatagram() only to detect that the transaction moved on to a
        // different socket while we were inside it; never dereferenced after that check.
        QUdpSocket* const socket = transaction->socket.get();
        if (!socket->hasPendingDatagrams())
            return;

        const QNetworkDatagram datagram = socket->receiveDatagram(kMaxDatagramSize);

        transaction = findTransaction(transactionId);
        if (transaction == nullptr || transaction->socket.get() != socket)
            return;   // the read failed and retired us, or a retransmit superseded it

        const QByteArray payload = datagram.data();
        if (payload.isEmpty())
            continue;

        // The connected socket already enforces the 5-tuple, but check anyway:
        // both RFCs require discarding anything not from the gateway we asked,
        // and this is what stands between an off-path attacker and an
        // attacker-chosen external endpoint being published into Kad.
        Channel* channel = channelFor(transaction->family);
        const Address sender = Address::fromQHostAddress(datagram.senderAddress());
        if (channel != nullptr && !sender.isNull() && !(sender == channel->gateway.address))
            continue;
        if (datagram.senderPort() > 0 && datagram.senderPort() != int(kServerPort))
            continue;

        const auto bytes = portmap::asBytes(payload);

        if (transaction->isProbe) {
            // A version mismatch means the router speaks the other protocol; the
            // sibling backend is probing concurrently and will pick it up, so
            // fail fast rather than working through the ladder.
            const bool mismatch = isVersionMismatch(bytes);
            if (!mismatch)
                inspectProbeReply(bytes);
            // inspectProbeReply() emits as well — PcpBackend reports a server restart
            // through mappingsInvalidated from in there — so count this probe off only
            // while it is still ours to count, or m_probesOutstanding goes negative.
            if (findTransaction(transactionId) == nullptr)
                return;
            eraseTransaction(transactionId);
            --m_probesOutstanding;
            if (mismatch) {
                if (m_probesOutstanding <= 0 && !m_probeAnswered)
                    finishProbe(false, QStringLiteral("router speaks another version"));
            } else if (!m_probeAnswered) {
                m_probeAnswered = true;
                finishProbe(true, QString());
            }
            return;
        }

        PortMapping mapping;
        mapping.request = transaction->request;
        mapping.method = method();
        bool ok = false;
        QString error;
        const bool matched = decodeReply(bytes, *transaction, mapping, ok, error);

        // decodeReply() emits, so `transaction` may already be gone. Everything below
        // works from `mapping` and the id, and the pointer is deliberately not reused.
        if (findTransaction(transactionId) == nullptr)
            return;
        if (!matched)
            continue;   // not this transaction's reply

        eraseTransaction(transactionId);
        emit mappingResult(mapping, ok, error);
        return;
    }
}

void UdpMappingBackend::onTransactionError(int transactionId)
{
    Transaction* transaction = findTransaction(transactionId);
    if (transaction == nullptr || !transaction->socket)
        return;
    // An ICMP port-unreachable surfaces here on a connected UDP socket, which is
    // the fastest possible "nothing is listening" — much better than waiting out
    // the retransmit ladder.
    //
    // Windows-only in practice, and that is why this whole function had no coverage:
    // macOS never raises the error at all. A connected QUdpSocket written to a closed
    // loopback port there yields hundreds of thousands of readyRead notifications and
    // exactly zero errorOccurred, so every caller of this slot is reached from Qt's
    // Windows socket engine and nowhere else. Issue #5 crashed here.
    if (transaction->socket->error() != QAbstractSocket::ConnectionRefusedError)
        return;

    const bool wasProbe = transaction->isProbe;
    logDebug(QStringLiteral("%1: gateway refused the connection (nothing on UDP/%2)")
                 .arg(name())
                 .arg(kServerPort));

    if (wasProbe) {
        eraseTransaction(transactionId);
        if (--m_probesOutstanding <= 0 && !m_probeAnswered)
            finishProbe(false, QStringLiteral("port unreachable"));
        return;
    }
    failTransaction(*transaction, QStringLiteral("gateway refused the connection"));
}

UdpMappingBackend::Transaction* UdpMappingBackend::findTransaction(int id)
{
    const auto it = std::find_if(m_transactions.begin(), m_transactions.end(),
                                 [id](const std::unique_ptr<Transaction>& t) {
                                     return t->id == id;
                                 });
    return it == m_transactions.end() ? nullptr : it->get();
}

UdpMappingBackend::Transaction* UdpMappingBackend::startTransaction(PortMapFamily family,
                                                                    QByteArray payload,
                                                                    int maxAttempts)
{
    auto transaction = std::make_unique<Transaction>();
    transaction->id = m_nextTransactionId++;
    transaction->family = family;
    transaction->payload = std::move(payload);
    transaction->maxAttempts = maxAttempts;

    transaction->timer = std::make_unique<QTimer>();
    transaction->timer->setSingleShot(true);
    const int id = transaction->id;
    connect(transaction->timer.get(), &QTimer::timeout, this, [this, id] {
        if (Transaction* t = findTransaction(id))
            transmit(*t);
    });

    m_transactions.push_back(std::move(transaction));
    return m_transactions.back().get();
}

void UdpMappingBackend::transmit(Transaction& transaction)
{
    if (transaction.attempt >= transaction.maxAttempts) {
        if (transaction.isProbe) {
            eraseTransaction(transaction.id);
            if (--m_probesOutstanding <= 0 && !m_probeAnswered)
                finishProbe(false, QStringLiteral("no reply"));
            return;
        }
        // Worth spelling out: a router that answered our probe but ignores a MAP
        // is usually refusing because it already holds a mapping for that port
        // under a different owner. PCP servers are supposed to say
        // NOT_AUTHORIZED (RFC 6887 section 11.3); some firmware just goes quiet,
        // which is otherwise impossible to diagnose from the outside.
        failTransaction(transaction,
                        QStringLiteral("no reply from gateway — it may already hold a "
                                       "mapping for this port from another client"));
        return;
    }

    // A fresh socket, and therefore a fresh source port, for every attempt.
    // Over IPv6 the FRITZ!Box answers only the first request per source port and
    // ignores the rest, so reusing one socket would make every retransmission —
    // and every later request — silently disappear.
    //
    // The previous attempt's socket has to go out through the event loop, exactly like
    // retire() does. It is still connected to our readyRead/error handlers and still
    // reachable by a late reply from the gateway, and we are called from that transaction's
    // own timer callback — so letting the unique_ptr assignment below destroy it inline is
    // the use-after-free retire() was written to avoid. It crashed tst_PortMapLive in
    // QAbstractSocketPrivate::canReadNotification(), a read notification delivered into
    // freed memory about two seconds in, i.e. on the first retransmit.
    retireSocket(transaction);
    transaction.socket = makeSocket(transaction.family);
    if (!transaction.socket) {
        if (transaction.isProbe) {
            eraseTransaction(transaction.id);
            if (--m_probesOutstanding <= 0 && !m_probeAnswered)
                finishProbe(false, QStringLiteral("could not open a socket"));
            return;
        }
        failTransaction(transaction, QStringLiteral("could not open a socket"));
        return;
    }

    const int id = transaction.id;
    connect(transaction.socket.get(), &QUdpSocket::readyRead, this,
            [this, id] { onTransactionReadyRead(id); });
    connect(transaction.socket.get(), &QUdpSocket::errorOccurred, this,
            [this, id](QAbstractSocket::SocketError) { onTransactionError(id); });

    // Arm the retransmit before writing, not after. write() on a connected UDP socket can
    // report a queued ICMP port-unreachable synchronously — errorOccurred is emitted from
    // inside it — and onTransactionError() then erases this transaction and release()s the
    // very timer being started. Doing the bookkeeping first makes the write the last
    // statement that touches @p transaction, so nothing can be touched after it dies.
    // Ordering is otherwise free: the timer is single-shot, only fires from the event loop,
    // its callback re-resolves the transaction by id, and retire() stops and deletes it if
    // the write does take the transaction down.
    const int delay = backoffMs(transaction.isProbe ? kProbeBaseDelayMs : kRequestBaseDelayMs,
                                transaction.attempt);
    ++transaction.attempt;
    transaction.timer->start(delay);

    transaction.socket->write(transaction.payload);
}

void UdpMappingBackend::finishProbe(bool supported, const QString& detail)
{
    if (!m_probing)
        return;
    m_probing = false;
    emit probeFinished(supported, detail);
}

void UdpMappingBackend::failTransaction(Transaction& transaction, const QString& error)
{
    // Everything is copied out first: eraseTransaction() below destroys @p transaction, so
    // the reference is dangling from that point on and the id must not be read through it.
    const int id = transaction.id;
    PortMapping mapping;
    mapping.request = transaction.request;
    mapping.method = method();

    eraseTransaction(id);
    emit mappingResult(mapping, false, error);
}

void UdpMappingBackend::eraseTransaction(int id)
{
    const auto it = std::find_if(m_transactions.begin(), m_transactions.end(),
                                 [id](const std::unique_ptr<Transaction>& t) {
                                     return t->id == id;
                                 });
    if (it == m_transactions.end())
        return;
    retire(**it);
    m_transactions.erase(it);
}

void UdpMappingBackend::retire(Transaction& transaction)
{
    // Hand the socket and timer to the event loop instead of destroying them
    // here. eraseTransaction() is routinely reached from inside the socket's own
    // readyRead handler or the timer's own timeout, and deleting a QObject while
    // executing one of its slots is a use-after-free.
    if (transaction.timer) {
        transaction.timer->stop();
        transaction.timer->disconnect(this);
        transaction.timer.release()->deleteLater();
    }
    retireSocket(transaction);
}

void UdpMappingBackend::retireSocket(Transaction& transaction)
{
    if (!transaction.socket)
        return;
    transaction.socket->disconnect(this);
    transaction.socket->close();
    transaction.socket.release()->deleteLater();
}


} // namespace eMule
