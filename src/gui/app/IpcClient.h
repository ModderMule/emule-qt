#pragma once

/// @file IpcClient.h
/// @brief GUI-side IPC client — connects to the daemon and provides
///        request/response + push event handling.
///
/// Connects to the daemon's TCP IPC server, performs the handshake,
/// and provides a request/callback interface for GUI panels.

#include "IpcConnection.h"
#include "IpcMessage.h"

#include <QDeadlineTimer>
#include <QHostAddress>
#include <QObject>
#include <QStringList>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>

#include <QByteArray>
#include <QHash>

#include <functional>
#include <unordered_map>

namespace eMule {

class IpcClient : public QObject {
    Q_OBJECT

public:
    explicit IpcClient(QObject* parent = nullptr);
    ~IpcClient() override;

    /// Where the link to the core stands — what the status bar shows.
    enum class CoreLink {
        Offline,        ///< not trying
        Starting,       ///< a core we launched has not opened its port yet
        Connecting,
        Handshaking,    ///< TCP is up, the core has not answered yet
        Loading,        ///< connected; the core still reads its files, lists are held
        Ready,
        NotResponding,  ///< the core accepts the connection but does not answer
        Refused,        ///< the core answered the handshake with an error
    };
    Q_ENUM(CoreLink)

    [[nodiscard]] CoreLink coreLink() const { return m_coreLink; }
    /// Connect attempts since the last completed handshake.
    [[nodiscard]] int connectAttempts() const { return m_connectAttempts; }
    /// The core is connected but still loading its file lists.
    [[nodiscard]] bool coreLoading() const { return m_coreLink == CoreLink::Loading; }
    /// Stage of that load as the core names it ("part-files", …).
    [[nodiscard]] QString loadStage() const { return m_loadStage; }
    /// The connected core's process id, from the handshake; 0 = not known.
    [[nodiscard]] qint64 corePid() const { return m_corePid; }
    /// Why the core refused the handshake (CoreLink::Refused).
    [[nodiscard]] QString refusalText() const { return m_refusalText; }

    /// Pin store for remote cores' TLS fingerprints. Default KnownCores::instance().
    void setKnownCores(class KnownCores* store) { m_knownCores = store; }
    /// The core's certificate changed (certificateChanged()): pin the new one and
    /// reconnect. No-op when nothing is pending.
    void trustNewCertificate();

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

    /// sendRequest() for a request repeated on a timer or on every push. At most
    /// one identical request is in flight: asked again before the reply is in, it
    /// is not sent — a slow core must not be handed a queue of the same question,
    /// nor the GUI a queue of full lists to apply. One follow-up goes out after the
    /// reply instead, with the newest @p callback; the callback it replaces is
    /// dropped without being run.
    ///
    /// @p owner tells apart two pollers that ask the same question for different
    /// reasons (the status bar and the Statistics page both poll GetStats): each
    /// keeps its own request in flight and its own callback.
    void sendPoll(const QObject* owner, Ipc::IpcMessage msg, ResponseCallback callback);

    /// Polls not sent because the same one was still unanswered.
    [[nodiscard]] quint64 pollsSkipped() const { return m_pollsSkipped; }

    /// Handshake, keepalive and core-start patience in ms. For tests and slow links.
    void setTimeouts(int handshakeMs, int keepaliveIntervalMs, int keepaliveTimeoutMs);
    void setStartGraceMs(int ms) { m_startGraceMs = ms; }

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

    // -- ed3k readable names and websites (name.md, IPC 1010-1019) ------------
    // Replies: [ok, data]; a failure is [false, {error}] (0: no node, else ed3k::Error).
    // Zones are 64 hex digits (base32 is taken too). The Names options page uses these.

    /// What the user opened (ed3k:name link, petname, alice, a domain, a handle) -> {id};
    /// ed3kNameResolved / ed3kNameFailed follow with that id.
    int resolveEd3kName(const QString& text, ResponseCallback callback = nullptr);
    /// The user opens resolution @p id (pins it) -> {links, url}: url the website to browse.
    int openEd3kName(qint64 id, ResponseCallback callback);
    /// -> {running, sitePort, pins: [{name, zone, since, how, howA, howB, vouched, link}],
    ///     petnames: [{text, zone, shareable}], lists: [{id, link, label, entries, refused}]}
    int requestEd3kNames(ResponseCallback callback);
    /// A petname for a zone; shareable (a global label only): friends get it.
    int setEd3kPetname(const QString& text, const QString& zone, bool shareable, ResponseCallback callback = nullptr);
    int removeEd3kPetname(const QString& text, ResponseCallback callback = nullptr);
    /// Only the user changes a pin (§4.4).
    int unpinEd3kName(const QString& name, ResponseCallback callback = nullptr);
    /// Vouching publishes a claim and answers friends with it (§4.2, §4.4).
    int vouchEd3kName(const QString& name, bool on, ResponseCallback callback = nullptr);
    /// Follows the name list of an ed3k:set link, last in the order -> {id}.
    int followEd3kNameList(const QString& link, const QString& label, ResponseCallback callback = nullptr);
    int unfollowEd3kNameList(qint64 id, ResponseCallback callback = nullptr);
    /// Moves a list to @p place (0 = first: the first list wins).
    int moveEd3kNameList(qint64 id, qint64 place, ResponseCallback callback = nullptr);

    // -- ed3k rooms (room.md, IPC 1120-1129) -----------------------------------
    // Rooms and post ids as hex, member keys as base32. A link with `s` is a credential.
    // Replies: [ok, data]; a failure is [false, {error}] (0: no node, else ed3k::Error).

    /// -> {running, rooms: [{room, link, title, about, nick, owner, moderator, ...}]}
    void requestEd3kRooms(const QObject* owner, ResponseCallback callback);
    /// -> [ok, posts: [{id, author, authorId, nick, moderator, sent, text, replyTo}]]
    int requestEd3kRoomPosts(const QString& room, ResponseCallback callback);
    /// flags ED3K_ROOM_PRIVATE 1, _CLOSED 2, _MODERATED 4 -> {room, link}
    int createEd3kRoom(const QString& title, const QString& about, int flags, const QString& nick,
                       ResponseCallback callback = nullptr);
    /// An ed3k:room link -> {room}
    int joinEd3kRoom(const QString& link, const QString& nick, ResponseCallback callback = nullptr);
    int leaveEd3kRoom(const QString& room, ResponseCallback callback = nullptr);
    /// replyTo: a post id (hex), empty none -> ids
    int postEd3kRoom(const QString& room, const QString& text, const QString& replyTo = {},
                     ResponseCallback callback = nullptr);
    int setEd3kRoomNick(const QString& room, const QString& nick, ResponseCallback callback = nullptr);
    /// Owner: flags _CLOSED 2, _MODERATED 4; mods: member keys
    int setEd3kRoomInfo(const QString& room, const QString& title, const QString& about, int flags,
                        const QStringList& mods, ResponseCallback callback = nullptr);
    /// Moderator: action 0 admit (-> {admit}), 1 ban, 2 lift, 3 new generation (-> {generation})
    int moderateEd3kRoom(const QString& room, int action, const QString& member, qint64 until,
                         ResponseCallback callback = nullptr);
    /// Newcomer of a closed room: the admit text -> {room}
    int importEd3kRoomAdmit(const QString& text, ResponseCallback callback = nullptr);

    // -- ed3k downloads (plain files, IPC 1140-1143) ---------------------------
    // Roots as 64 hex digits. Failure: [false, {error: localized text}].

    /// -> {running, downloads: [{root, name, size, done, rate, sources, transferring, state,
    ///     priority, category, owner, path, addedOn, completedOn, error}]}
    void requestEd3kDownloads(const QObject* owner, ResponseCallback callback);
    /// A search row's file -> {root}
    int addEd3kDownload(const QString& root, qint64 size, const QString& name, int category, bool paused,
                        ResponseCallback callback = nullptr);
    /// An ed3k:file link without bt= -> {root}
    int addEd3kDownloadLink(const QString& link, int category, bool paused, ResponseCallback callback = nullptr);
    /// action 0 pause, 1 resume, 2 cancel, 3 priority (value kPr*), 4 category (value), 5 clear completed
    int controlEd3kDownload(const QString& root, int action, qint64 value = 0, ResponseCallback callback = nullptr);

    // -- ed3k sets and live streams (IPC 1110-1119, 1144) ------------------------
    // Set ids are numbers; policy 0 ask, 1 download. Failure: [false, {error: localized text}].

    /// -> {running, sets: [{id, link, title, author, hidden, pinned, followed, policy, seq,
    ///     pendingSeq, added, changed, removed, dir, error, totalSize, members, parts}]}
    void requestEd3kSets(const QObject* owner, ResponseCallback callback);
    /// An ed3k:set link -> {id}
    int followEd3kSet(const QString& link, int policy, ResponseCallback callback = nullptr);
    int unfollowEd3kSet(qint64 id, ResponseCallback callback = nullptr);
    int setEd3kSetPolicy(qint64 id, int policy, ResponseCallback callback = nullptr);
    /// The members (paths) left out
    int setEd3kSetSelection(qint64 id, const QStringList& skipped, ResponseCallback callback = nullptr);
    /// Takes the pending version, downloads what the selection wants
    int applyEd3kSetVersion(qint64 id, ResponseCallback callback = nullptr);
    /// author: key hex, empty = every author
    int downloadEd3kSetParts(qint64 id, const QString& author, ResponseCallback callback = nullptr);
    /// An ed3k:set link -> {url} of its website ("" while the website server does not listen)
    int requestEd3kSetSite(const QString& link, ResponseCallback callback);
    /// An ed3k:live link -> {id, url}
    int watchEd3kLive(const QString& link, ResponseCallback callback = nullptr);
    int leaveEd3kLive(const QString& id, ResponseCallback callback = nullptr);
    /// -> {running, streams: [{id, title, media, codecs, state, neighbours, segments, bytes, viewers, url}]}
    void requestEd3kLive(const QObject* owner, ResponseCallback callback);

    /// Send a Shutdown request to the daemon, then disconnect.
    /// Use this when the GUI launched the daemon and is about to close.
    void sendShutdown();

    /// Ask the daemon to restart itself. The connection drops and auto-reconnect
    /// picks the new one up.
    void sendRestart();
    /// A requested restart is under way; gives up after kRestartGraceMs.
    [[nodiscard]] bool daemonRestarting() const;

    /// The local core was just launched: retry at a short fixed interval until it
    /// answers, instead of backing off from a port that opens within moments.
    void expectDaemonStart();
    /// expectDaemonStart() was called and the core has not answered yet.
    [[nodiscard]] bool daemonStarting() const;

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

    /// coreLink(), connectAttempts() or loadStage() changed.
    void coreLinkChanged();

    /// The remote core at @p hostPort shows a different TLS certificate than the
    /// one pinned. Nothing was sent; reconnecting stops until trustNewCertificate().
    void certificateChanged(const QString& hostPort, const QString& pinned,
                            const QString& presented);

    /// Replies are waiting behind a dialog a reply handler opened. The GUI brings
    /// that dialog to the front: hidden, it looks like a hang.
    void dispatchBlocked();

    /// Bracket a run of logMessageReceived that arrived as one batch.
    void logBatchStarted();
    void logBatchFinished();

    // -- Push event signals ---------------------------------------------------

    void statsUpdated(const Ipc::IpcMessage& msg);
    void downloadUpdated(const Ipc::IpcMessage& msg);
    void downloadAdded(const Ipc::IpcMessage& msg);
    void downloadRemoved(const Ipc::IpcMessage& msg);
    void notifierEvent(const Ipc::IpcMessage& msg);   ///< [kind: NotifierEvent, text]
    void serverStateChanged(const Ipc::IpcMessage& msg);
    /// One Server Info line: [type: ServerMsgType, text: string].
    void serverMessageReceived(const Ipc::IpcMessage& msg);
    void searchResultReceived(const Ipc::IpcMessage& msg);
    /// ED2K global (UDP) sweep progress: [searchID, asked, total, running].
    void globalSearchProgress(const Ipc::IpcMessage& msg);
    /// A search was queued, sent, finished or failed — see PushSearchState.
    void searchStateChanged(const Ipc::IpcMessage& msg);
    void logMessageReceived(const Ipc::IpcMessage& msg);
    void sharedFileUpdated(const Ipc::IpcMessage& msg);
    void sharedFileRemoved(const Ipc::IpcMessage& msg);
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
    /// [senderHash, senderName, pngBase64] — the peer wants a captcha solved.
    void chatCaptchaReceived(const Ipc::IpcMessage& msg);
    /// [senderHash, solved] — whether the answer we sent was right.
    void chatCaptchaResultReceived(const Ipc::IpcMessage& msg);
    void friendListChanged(const Ipc::IpcMessage& msg);
    void clientSharedFilesReceived(const Ipc::IpcMessage& msg);
    void searchPreviewReceived(const Ipc::IpcMessage& msg);
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

    /// [row] — a torrent was added; same row shape as GetTorrents.
    void torrentAdded(const Ipc::IpcMessage& msg);
    /// [row] — a torrent changed (coalesced per id by the daemon).
    void torrentUpdated(const Ipc::IpcMessage& msg);
    /// [id] — the torrent is gone.
    void torrentRemoved(const Ipc::IpcMessage& msg);
    /// [id] — every wanted byte is there. Never coalesced.
    void torrentFinished(const Ipc::IpcMessage& msg);
    /// [status map] — the BitTorrent engine started, stopped or changed its port.
    void btStatusChanged(const Ipc::IpcMessage& msg);
    /// [status map] — the ed3k node started, stopped, or its contacts/class changed.
    void ed3kStatusChanged(const Ipc::IpcMessage& msg);
    /// [ask, host, url] — an ed3k:nodes link wants a node list source: ask the user.
    void ed3kBootstrapAsk(const Ipc::IpcMessage& msg);
    /// [] — ed3k friends, a name, an address or a request changed: GetEd3kFriends.
    void ed3kFriendsChanged(const Ipc::IpcMessage& msg);
    /// [node, name, text, id] — an ed3k chat message.
    void ed3kChatMessageReceived(const Ipc::IpcMessage& msg);
    /// [node, id] — the peer's node took the message.
    void ed3kChatSent(const Ipc::IpcMessage& msg);
    /// [node, id, why, retryAfter] — not delivered.
    void ed3kChatFailed(const Ipc::IpcMessage& msg);
    /// [key, node, text, issued] — someone asks to be our ed3k friend.
    void ed3kFriendRequestReceived(const Ipc::IpcMessage& msg);
    /// [node, ChatConnectProgress] — a send waits for a look of the friend.
    void ed3kChatStateReceived(const Ipc::IpcMessage& msg);
    /// [{id, name, zone, links, how, warnings, ties, confirm, viaSite, …}] — a name resolved.
    void ed3kNameResolved(const Ipc::IpcMessage& msg);
    /// [{id, name, why, ties}] — a name not resolved.
    void ed3kNameFailed(const Ipc::IpcMessage& msg);
    /// [] — pins, petnames or followed name lists changed: requestEd3kNames.
    void ed3kNamesChanged(const Ipc::IpcMessage& msg);
    /// [node, link] — a friend publishes a name list: offer to follow it.
    void ed3kNameFriendListOffered(const Ipc::IpcMessage& msg);
    /// [] — ed3k rooms, an info, a nick or a room's state changed: requestEd3kRooms.
    void ed3kRoomsChanged(const Ipc::IpcMessage& msg);
    /// [{...GetEd3kDownloads...}] — the ed3k download list changed.
    void ed3kDownloadsChanged(const Ipc::IpcMessage& msg);
    /// [{...GetEd3kSets...}] — followed ed3k sets changed.
    void ed3kSetsChanged(const Ipc::IpcMessage& msg);
    /// [id, title, seq, added, changed, removed] — a new version of a set waits for the user.
    void ed3kSetVersion(const Ipc::IpcMessage& msg);
    /// [{...GetEd3kLive...}] — an ed3k live stream changed.
    void ed3kLiveChanged(const Ipc::IpcMessage& msg);
    /// [link] — a set opened as a website is no website: offer it for download.
    void ed3kSetOffered(const Ipc::IpcMessage& msg);
    /// [room, {id, author, authorId, nick, moderator, sent, text, replyTo}] — a post.
    void ed3kRoomPost(const Ipc::IpcMessage& msg);
    /// [room, ids] — posts dropped by a ban.
    void ed3kRoomPostsRemoved(const Ipc::IpcMessage& msg);
    /// [room, member key, nick, left]
    void ed3kRoomPresence(const Ipc::IpcMessage& msg);
    /// [room, kind, value, flag, who] — Ed3kRooms::Notice.
    void ed3kRoomNotice(const Ipc::IpcMessage& msg);
    /// [{claims, rate, delay, address}] — the stake claims of own burns changed (credit.md §6.7).
    void ed3kStakeClaimsChanged(const Ipc::IpcMessage& msg);
    /// [{what, state, revert, tx}] — a stake transaction moved.
    void ed3kStakeTx(const Ipc::IpcMessage& msg);
    /// [] — filter lists or own verdicts changed: GetEd3kFilters.
    void ed3kFiltersChanged(const Ipc::IpcMessage& msg);
    /// [item] — a hoster download was added or changed.
    void hosterItemChanged(const Ipc::IpcMessage& msg);
    /// [id] — a hoster download was removed.
    void hosterItemRemoved(const Ipc::IpcMessage& msg);
    /// [item, puzzle, image, host, explain, timeout] — show an image puzzle.
    void hosterPuzzleAsked(const Ipc::IpcMessage& msg);
    /// [item, links] — a folder link: ask which links to add.
    void hosterLinksExpanded(const Ipc::IpcMessage& msg);
    /// [host, user, {valid, premium, expires, trafficLeft, error}] — a login was checked.
    void hosterAccountChecked(const Ipc::IpcMessage& msg);

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
    void onSocketEncrypted();
    void onMessageReceived(const Ipc::IpcMessage& msg);
    void onConnectionLost();
    void onSocketError();
    void attemptReconnect();
    void sendKeepalive();

private:
    void performHandshake();
    void dispatchPushEvent(const Ipc::IpcMessage& msg);
    void requestLogSync();
    /// The last line id shown of the tab a log line of @p cat / @p severity goes to.
    int64_t& lastLogId(const QString& cat, QtMsgType severity);

    /// Emit the lines of a SyncLogs reply or a PushLogBatch not yet shown.
    void replayLogEntries(const QCborArray& entries);
    void setCoreLink(CoreLink link);
    void beginConnect();
    /// TCP (and TLS, if used) is up: hand the socket to IpcConnection and say hello.
    void startSession();
    /// "host:port" this client pins the core's certificate under.
    [[nodiscard]] QString pinKey() const;

    /// The reply timer ran out: true when the core is in fact talking and it is
    /// this process that has not got round to reading it.
    [[nodiscard]] bool peerStillTalking() const;

    /// A reply timer ran out on a loopback link that is still open: the core is
    /// alive but its loop is held by a load stage. A new connection would queue
    /// behind the same stage, so keep this one. Issues #24, #40.
    [[nodiscard]] bool waitOnBusyCore(bool handshake);

    /// Fetch the daemon's Server Info backlog and replay it through
    /// serverMessageReceived. The daemon outlives the GUI, so without this a GUI
    /// restart shows an empty pane even though the greeting already arrived.
    void requestServerMessages();
    void scheduleReconnect();
    void resetConnection();

    /// A connect attempt that never reached the daemon: report it, drop the socket, retry.
    void failConnect(const QString& error);

    /// Unhook and close m_connection, deleting it one event-loop turn later.
    void dropConnection();

    /// Answer every pending request with a failure, one event-loop turn later.
    void failPendingRequests();
    /// The core restarts itself: a dropped link is expected, not a lost core.
    void expectRestart();

    std::unique_ptr<Ipc::IpcConnection> m_connection;
    QSslSocket* m_socket = nullptr;  // Owned by IpcConnection after handoff
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
    bool m_waitingOnBusyCore = false;   // this connection outlived a reply timer
    QDeadlineTimer m_restartDeadline;   // expired = no restart pending
    QDeadlineTimer m_startDeadline;     // expired = no launched core awaited
    int64_t m_lastKadId     = 0;
    int64_t m_lastUsenetId  = 0;
    int64_t m_lastServerId  = 0;
    int64_t m_lastLogId     = 0;  // Log tab
    int64_t m_lastVerboseId = 0;
    int64_t m_lastEd3kId = 0;         // ed3k tabs
    int64_t m_lastEd3kVerboseId = 0;
    int64_t m_lastEd3kKadId = 0;
    int64_t m_lastLeecherId = 0;      // Leecher tab
    /// Highest Server Info line id displayed, so a reconnect replays only the gap.
    int64_t m_lastServerMsgId = 0;
    QString m_daemonToken;
    QString m_authToken;
    class KnownCores* m_knownCores = nullptr;
    /// Fingerprint presented by a core whose pin did not match; reconnects wait.
    QString m_pendingFingerprint;
    std::unordered_map<int, ResponseCallback> m_pendingCallbacks;
    int m_remotePollingMs = 1500;

    /// A poll in flight, and what to ask once it is answered.
    struct Poll {
        int seqId = -1;
        bool again = false;
        Ipc::IpcMessage msg;
        ResponseCallback callback;
    };
    QHash<QByteArray, Poll> m_polls;   ///< by requestIdentity
    quint64 m_pollsSkipped = 0;

    CoreLink m_coreLink = CoreLink::Offline;
    int m_connectAttempts = 0;
    QString m_loadStage;
    qint64 m_corePid = 0;
    QString m_refusalText;

    int m_startGraceMs         = 30'000;
    int m_handshakeTimeoutMs   = 10'000;
    int m_keepaliveIntervalMs  = 30'000;
    int m_keepaliveTimeoutMs   = 10'000;

    static constexpr int MaxReconnectDelay      = 10'000;
    static constexpr int StartRetryDelayMs      = 200;     // while a launched core loads
};

} // namespace eMule
