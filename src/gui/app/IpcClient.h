#pragma once

/// @file IpcClient.h
/// @brief GUI-side IPC client — connects to the daemon and provides
///        request/response + push event handling.
///
/// Connects to the daemon's TCP IPC server, performs the handshake,
/// and provides a request/callback interface for GUI panels.

#include "IpcConnection.h"
#include "IpcMessage.h"

#include <QHostAddress>
#include <QObject>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <unordered_map>

namespace eMule {

class IpcClient : public QObject {
    Q_OBJECT

public:
    explicit IpcClient(QObject* parent = nullptr);
    ~IpcClient() override;

    /// Connect to the daemon at the given address and port.
    /// Enables auto-reconnect: on failure or disconnect the client will
    /// keep retrying with exponential backoff (1s → 2s → 4s … 30s).
    void connectToDaemon(const QHostAddress& address, uint16_t port);

    /// Connect by hostname (DNS resolved by QTcpSocket internally).
    void connectToDaemon(const QString& host, uint16_t port);

    /// Disconnect from the daemon and stop auto-reconnect.
    void disconnectFromDaemon();

    /// Returns true if connected and handshake is complete.
    [[nodiscard]] bool isConnected() const;

    /// Returns true if connected to a loopback address (localhost).
    [[nodiscard]] bool isLocalConnection() const;

    /// Recommended polling interval: 500ms for localhost, configurable for remote.
    [[nodiscard]] int pollingInterval() const;

    /// Set the remote polling interval (from preferences). Default 1500ms.
    void setRemotePollingMs(int ms);

    /// Set the auth token for non-localhost connections.
    void setAuthToken(const QString& token);

    /// Return the daemon host address as a string (hostname or IP).
    [[nodiscard]] QString daemonHost() const {
        return m_hostname.isEmpty() ? m_address.toString() : m_hostname;
    }

    /// Return the daemon port.
    [[nodiscard]] uint16_t daemonPort() const { return m_port; }

    static constexpr int LocalPollingMs  = 500;

    /// Callback type for request responses.
    using ResponseCallback = std::function<void(const Ipc::IpcMessage&)>;

    /// Send a request and register a callback for the response.
    /// Returns the sequence ID used for this request.
    ///
    /// The callback runs exactly once while this client lives: with the reply, or — when
    /// the connection drops first — with a default-constructed message, which reads as
    /// a failure (fieldBool(0) is false). Callbacks must check before using the payload;
    /// IpcMessage::isValid() tells a dropped connection from a daemon refusal.
    int sendRequest(Ipc::IpcMessage msg, ResponseCallback callback = nullptr);

    /// Forget the callback of request @p seqId; a reply that still arrives is ignored.
    void cancelRequest(int seqId);

    /// Fan one request per hash out to the daemon, then run @p onAllDone once the last
    /// reply lands. @p build makes the message for a hash, so callers can attach their own
    /// payload (a priority, a category, …).
    ///
    /// @p onAllDone runs at most once, and is skipped when @p context has been destroyed
    /// by then. A request that cannot be queued counts as completed, so a mid-batch
    /// disconnect can never leave the completion hanging.
    ///
    /// @p onEach, when given, sees every reply as it lands, keyed by the string
    /// that built it — so a caller can report which entries failed rather than
    /// only that some did. A request that could not be queued calls it with a
    /// default-constructed message, which reads as a failure, keeping one code
    /// path for both.
    void sendBatchRequest(const QStringList& keys,
                          const std::function<Ipc::IpcMessage(const QString& key)>& build,
                          QObject* context, std::function<void()> onAllDone,
                          std::function<void(const QString& key,
                                             const Ipc::IpcMessage& reply)> onEach = {});

    /// Send a Shutdown request to the daemon, then disconnect.
    /// Use this when the GUI launched the daemon and is about to close.
    void sendShutdown();

    /// The daemon's engine-wide Usenet pause, as last reported: seeded from the
    /// connect-time GetPreferences, then kept by PushUsenetEngineState.
    [[nodiscard]] bool usenetEnginePaused() const { return m_usenetEnginePaused; }
    void setUsenetEnginePaused(bool paused)
    {
        if (paused == m_usenetEnginePaused)
            return;
        m_usenetEnginePaused = paused;
        emit usenetEnginePausedChanged(paused);
    }

signals:
    /// Emitted when connection + handshake succeeds.
    void connected();

    /// Emitted when connection is lost.
    void disconnected();

    /// Emitted on connection failure.
    void connectionFailed(const QString& error);

    // -- Push event signals ---------------------------------------------------

    void statsUpdated(const Ipc::IpcMessage& msg);
    void downloadUpdated(const Ipc::IpcMessage& msg);
    void downloadAdded(const Ipc::IpcMessage& msg);
    void downloadRemoved(const Ipc::IpcMessage& msg);
    void serverStateChanged(const Ipc::IpcMessage& msg);
    /// One Server Info line: [type: ServerMsgType, text: string].
    void serverMessageReceived(const Ipc::IpcMessage& msg);
    void searchResultReceived(const Ipc::IpcMessage& msg);
    /// ED2K global (UDP) sweep progress: [searchID, asked, total, running].
    void globalSearchProgress(const Ipc::IpcMessage& msg);
    void logMessageReceived(const Ipc::IpcMessage& msg);
    void sharedFileUpdated(const Ipc::IpcMessage& msg);
    void uploadUpdated(const Ipc::IpcMessage& msg);
    void kadUpdated(const Ipc::IpcMessage& msg);
    void kadSearchesChanged(const Ipc::IpcMessage& msg);
    void knownClientsChanged(const Ipc::IpcMessage& msg);
    /// The daemon's category list changed — refetch it with GetCategories.
    /// Carries no payload: every consumer wants the whole list.
    void categoriesChanged(const Ipc::IpcMessage& msg);
    void chatMessageReceived(const Ipc::IpcMessage& msg);
    /// [friendHash, ChatConnectProgress] — how a chat dial to a friend is going.
    void chatStateReceived(const Ipc::IpcMessage& msg);
    void friendListChanged(const Ipc::IpcMessage& msg);
    void clientSharedFilesReceived(const Ipc::IpcMessage& msg);
    /// Port-mapping status changed (protocol chosen, mapping gained or lost).
    void portMapStatusChanged(const Ipc::IpcMessage& msg);

    /// One Usenet queue item changed or arrived; the payload is the whole row.
    void usenetItemUpdated(const Ipc::IpcMessage& msg);
    /// [id] — the item is gone.
    void usenetItemRemoved(const Ipc::IpcMessage& msg);
    /// [id, success, message] — terminal outcome, never coalesced.
    void usenetItemFinished(const Ipc::IpcMessage& msg);
    /// The engine-wide pause changed; see usenetEnginePaused().
    void usenetEnginePausedChanged(bool paused);

    /// [searchId, rows] — a batch of indexer results, as each indexer answers.
    void indexerResultsReceived(const Ipc::IpcMessage& msg);
    /// [searchId, done, total] — how many indexers have answered.
    void indexerSearchProgress(const Ipc::IpcMessage& msg);
    /// [searchId, error] — the fan-out is over. `error` names the indexers that
    /// failed and is often set alongside perfectly good rows.
    void indexerSearchFinished(const Ipc::IpcMessage& msg);

    /// One feed's last-poll report. The Options page is the only listener —
    /// a feed acts unattended, so this is the only visibility there is.
    void indexerFeedStatus(const Ipc::IpcMessage& msg);

    /// Emitted for every outgoing request and incoming message when enableIpcLog is on.
    void ipcLogMessage(const QString& text, bool outgoing);

private slots:
    void onSocketConnected();
    void onMessageReceived(const Ipc::IpcMessage& msg);
    void onConnectionLost();
    void onSocketError();
    void attemptReconnect();
    void sendKeepalive();

private:
    void performHandshake();
    void dispatchPushEvent(const Ipc::IpcMessage& msg);
    void requestLogSync();

    /// Fetch the daemon's Server Info backlog and replay it through
    /// serverMessageReceived. The daemon outlives the GUI, so without this a GUI
    /// restart shows an empty pane even though the greeting already arrived.
    void requestServerMessages();
    void scheduleReconnect();
    void resetConnection();

    /// Answer every pending request with a failure, one event-loop turn later.
    void failPendingRequests();

    std::unique_ptr<Ipc::IpcConnection> m_connection;
    QTcpSocket* m_socket = nullptr;  // Owned by IpcConnection after handoff
    QTimer m_reconnectTimer;
    QTimer m_handshakeTimer;         // Aborts a stalled handshake
    QTimer m_keepaliveTimer;         // Periodic ping when connected
    QTimer m_keepaliveTimeoutTimer;  // Window to receive the pong
    QHostAddress m_address;
    QString m_hostname;  // Non-empty when connected by hostname (DNS)
    uint16_t m_port = 0;
    int m_nextSeqId = 1;
    int m_reconnectDelayMs = 1000;
    bool m_handshaked = false;
    bool m_usenetEnginePaused = false;
    bool m_autoReconnect = false;
    int64_t m_lastKadId     = 0;
    int64_t m_lastServerId  = 0;
    int64_t m_lastLogId     = 0;  // Log tab
    int64_t m_lastVerboseId = 0;
    /// Highest Server Info line id displayed, so a reconnect replays only the gap.
    int64_t m_lastServerMsgId = 0;
    QString m_daemonToken;
    QString m_authToken;
    std::unordered_map<int, ResponseCallback> m_pendingCallbacks;
    int m_remotePollingMs = 1500;

    static constexpr int MaxReconnectDelay      = 10'000;
    static constexpr int HandshakeTimeoutMs     = 10'000;
    static constexpr int KeepaliveIntervalMs    = 30'000;
    static constexpr int KeepaliveTimeoutMs     = 10'000;
};

} // namespace eMule
