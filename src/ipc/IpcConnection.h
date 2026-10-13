#pragma once

/// @file IpcConnection.h
/// @brief QObject wrapping QTcpSocket with length-prefixed CBOR framing.
///
/// Handles read buffering and frame assembly. Emits messageReceived()
/// for each complete IPC message decoded from the stream.
///
/// Both directions are bounded. Reading dispatches for a few milliseconds and then
/// returns to the event loop, and of the state pushes still waiting it keeps only
/// the newest per kind. Writing holds state pushes back from a peer that is not
/// reading and drops the peer once its backlog is past repair.

#include "IpcMessage.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>

#include <deque>
#include <memory>
#include <vector>

namespace eMule::Ipc {

class IpcConnection : public QObject {
    Q_OBJECT

public:
    /// Take ownership of an existing connected socket.
    explicit IpcConnection(QTcpSocket* socket, QObject* parent = nullptr);
    ~IpcConnection() override;

    /// What became of a message handed to sendMessage() or sendPush().
    enum class SendResult {
        Sent,
        Held,       ///< state push kept back until the peer catches up
        Overflow,   ///< backlog past the hard limit: connection dropped
        Closed,
    };

    /// Send a message over the connection. Past the hard write limit the peer is
    /// taken for stuck: protocolError() and the socket is aborted.
    SendResult sendMessage(const IpcMessage& msg);

    /// Send a push. A state push (snapshotPushKey) to a peer whose backlog is over
    /// the soft limit is held instead — newest per key — and goes out once the
    /// backlog has drained. Everything else is sendMessage().
    SendResult sendPush(const IpcMessage& msg);

    /// Bytes of unsent backlog allowed before state pushes are held (@p soft) and
    /// before the peer is dropped (@p hard). 0 turns a limit off, the default.
    void setWriteLimits(qint64 soft, qint64 hard);

    /// Bytes written but not yet taken by the OS.
    [[nodiscard]] qint64 pendingWriteBytes() const;

    /// State pushes currently held back.
    [[nodiscard]] int heldPushes() const { return static_cast<int>(m_held.size()); }

    /// How long one pass may dispatch before it yields to the event loop.
    /// 0 dispatches everything at once.
    void setDispatchBudgetMs(int ms) { m_budgetMs = ms; }

    /// Collapse queued state pushes on the read side: an undispatched one is
    /// replaced by a newer with the same snapshotPushKey. Off by default.
    void setCoalesceIncoming(bool on) { m_coalesceIncoming = on; }

    /// Frames replaced by a newer one before they were dispatched.
    [[nodiscard]] quint64 coalescedFrames() const { return m_coalesced; }

    /// How long a handler may sit in a nested event loop, with data waiting, before
    /// dispatchBlocked() says so.
    void setStallNoticeMs(int ms) { m_stallTimer.setInterval(ms); }

    /// Received data is waiting to be dispatched: the peer is alive, we are behind.
    [[nodiscard]] bool hasPendingInput() const;

    /// Drop the connection at once, unsent data included.
    void abort();

    /// Returns true if the underlying socket is connected.
    [[nodiscard]] bool isConnected() const;

    /// Close the connection.
    void close();

    /// Access the underlying socket (e.g. for peer address).
    [[nodiscard]] QTcpSocket* socket() const;

    /// Enable AES-256-CBC encryption for all subsequent messages.
    void setEncryptionKey(const QByteArray& key);

    /// Returns true if encryption is active.
    [[nodiscard]] bool isEncrypted() const;

signals:
    /// Emitted for each complete IPC message received.
    void messageReceived(const eMule::Ipc::IpcMessage& msg);

    /// Emitted when the connection is closed or an error occurs — after the last
    /// message the peer sent ahead of it has been dispatched.
    void disconnected();

    /// Emitted on protocol errors (e.g. oversized frame).
    void protocolError(const QString& reason);

    /// Bytes arrived from the peer, dispatched yet or not.
    void activity();

    /// Data is waiting while a messageReceived handler sits in a nested event loop
    /// (a modal dialog): nothing is dispatched until it returns. Once per stall.
    void dispatchBlocked();

private slots:
    void onReadyRead();
    void onBytesWritten();
    void drain();
    void onStallCheck();
    void onDisconnected();
    void onErrorOccurred(QAbstractSocket::SocketError error);

private:
    struct Queued {
        IpcMessage msg;
        quint64 key = 0;    ///< snapshotPushKey, 0 = never replaced
        bool live = true;   ///< false once a newer frame replaced it
    };

    /// Move complete frames from m_readBuffer to m_queue.
    void decodeFrames(const QElapsedTimer& clock, int budgetMs);
    /// A frame that cannot be trusted: protocolError(), close, take no more bytes.
    void closeInput(const QString& reason);
    /// Emit disconnected(), or leave it to drain() while input is still waiting.
    void reportDisconnect();
    void enqueue(IpcMessage msg);
    [[nodiscard]] bool hasCompleteFrame() const;
    void scheduleDrain();
    void flushHeld();

    QTcpSocket* m_socket;  // Owned by this object (parented)
    QByteArray m_readBuffer;

    /// Decoded, not yet dispatched. Position i holds sequence m_queueBase + i.
    std::deque<Queued> m_queue;
    qint64 m_queueBase = 0;
    QHash<quint64, qint64> m_queuedKeys;   ///< key -> sequence of its live entry
    quint64 m_coalesced = 0;
    int m_budgetMs = 15;
    bool m_coalesceIncoming = false;
    bool m_drainQueued = false;
    bool m_blockedReported = false;

    /// State pushes kept back from a slow peer, oldest first, one per key.
    std::vector<std::pair<quint64, IpcMessage>> m_held;
    qint64 m_softWriteLimit = 0;
    qint64 m_hardWriteLimit = 0;

    /// True while drain() is dispatching. A slot connected to messageReceived may
    /// open a modal dialog; nothing may be dispatched from inside its event loop.
    /// QAbstractSocket does not emit readyRead recursively either, so what arrives
    /// meanwhile waits in the socket and drain() collects it on the way out.
    bool m_dispatching = false;

    /// Runs only while drain() does, so it can only fire from a nested event loop
    /// inside a handler: that is the stall dispatchBlocked() reports.
    QTimer m_stallTimer;
    QByteArray m_encryptionKey;  // Empty = no encryption
    bool m_disconnectEmitted = false;
    /// The socket is gone but queued messages are not through yet.
    bool m_disconnectPending = false;
    /// Set by closeInput() and abort(): bytes still arriving are thrown away.
    bool m_inputClosed = false;
};

} // namespace eMule::Ipc
