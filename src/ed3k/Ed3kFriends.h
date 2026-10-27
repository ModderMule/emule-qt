#pragma once

/// @file Ed3kFriends.h
/// @brief ed3k friends (friend.md) and chat (chat.md) in the daemon: the Friend* and Chat*
/// events of the session, the friend view for IPC, names, addresses and chat sends.
///
/// - Chats are keyed by node ID (a ChatMessage carries only the node).
/// - A send goes to the newest address known (FriendFound/FriendSeen), else to the node
///   alone (an open connection is used at once). A friend's send that fails Unreachable or
///   Lost is retried once after a look (friendFind): chatProgress tells the GUI.
/// - Names come from `dn` of the added link and are kept in Config/ed3k/friend-names.dat
///   (not a credential). Open links, their text and the state blob are credentials: never
///   logged (ffi.md "Friends").
/// - Presence looks on while the node runs (friendSetPresence).

#include "Ed3kFfi.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace eMule::ed3k {

class Ed3kSession;

class Ed3kFriends : public QObject {
    Q_OBJECT

public:
    /// One friend as the GUI shows it.
    struct Friend {
        ::ed3k::Key32 key{};
        ::ed3k::NodeId node;
        QString name;           ///< `dn` of the link; empty: unknown
        bool mutual = false;    ///< it added us too
        bool asking = false;    ///< our request to it is pending
        bool heldLink = false;  ///< an open link of it is held
        std::optional<::ed3k::Address> addr;   ///< newest known address
        qint64 lastSeen = 0;    ///< Unix s of that address; 0 never
    };
    /// A request shown to the user, each key once (friend.md §3).
    struct Request {
        ::ed3k::Key32 key{};
        ::ed3k::NodeId node;
        QString text;
        std::uint64_t issued = 0;
    };
    /// From the eD2K "Messages and Comments" options (DaemonApp; tests set them).
    struct ChatOptions {
        bool friendsOnly = false;   ///< msgOnlyFriends: ChatPolicy::FriendsOnly
        bool rateLimit = true;      ///< enableSpamFilter: the engine's 20 a minute, else none
        QStringList filter;         ///< messageFilter split at '|': such messages dropped
    };
    /// What chatProgress says (values of ChatConnectProgress the GUI knows).
    enum class Progress : int { Searching = 2, Found = 3, Failed = 5 };

    explicit Ed3kFriends(Ed3kSession& session, QObject* parent = nullptr);
    ~Ed3kFriends() override;

    void setChatOptions(const ChatOptions& options);
    [[nodiscard]] const ChatOptions& chatOptions() const { return m_options; }

    [[nodiscard]] bool isRunning() const;
    /// Every friend; empty while not running.
    [[nodiscard]] std::vector<Friend> friends();
    /// Requests waiting for the user's answer.
    [[nodiscard]] std::vector<Request> requests() const;
    [[nodiscard]] bool isFriend(const ::ed3k::NodeId& node);
    /// Friend name of @p node, empty when unknown.
    [[nodiscard]] QString nameOf(const ::ed3k::NodeId& node);

    /// Adds an `ed3k:friend` link (with `r`: a request carrying @p text). The new key.
    ::ed3k::Result<::ed3k::Key32> add(const QString& link, const QString& text);
    ::ed3k::Result<> remove(const ::ed3k::Key32& key);
    /// The user's answer to a request (friend.md §3).
    ::ed3k::Result<> answerRequest(const ::ed3k::Key32& key, bool accept);
    /// This node's friend link, with `r` when @p withRequest; a name as `dn`.
    ::ed3k::Result<QString> myLink(bool withRequest, const QString& name);
    /// Sends @p text to @p node; the ids of its messages (each ends as chatSent/chatFailed).
    ::ed3k::Result<std::vector<std::uint64_t>> sendChat(const ::ed3k::NodeId& node, const QString& text);
    /// The chat window closed: sends waiting for a look are dropped.
    void endChat(const ::ed3k::NodeId& node);

    [[nodiscard]] static QByteArray toBytes(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static std::optional<::ed3k::Key32> keyFrom(const QByteArray& bytes);
    [[nodiscard]] static std::optional<::ed3k::NodeId> nodeFrom(const QByteArray& bytes);

signals:
    /// The list, a name, an address or a request changed.
    void friendsChanged();
    /// A message to show once; @p node 16 bytes.
    void chatMessage(const QByteArray& node, const QString& name, const QString& text, quint64 id);
    void chatSent(const QByteArray& node, quint64 id);
    /// @p why AskFailure (ED3K_ASK_*), @p retryAfter s.
    void chatFailed(const QByteArray& node, quint64 id, int why, quint32 retryAfter);
    /// A friend's send waits for a look: Progress.
    void chatProgress(const QByteArray& node, int progress);
    /// A request to answer with answerRequest.
    void friendRequest(const QByteArray& key, const QByteArray& node, const QString& text, quint64 issued);

private:
    /// A send that may be retried after a look.
    struct Outgoing {
        ::ed3k::NodeId node;
        QString text;
        std::uint64_t group = 0;   ///< first id of the send
        bool retried = false;
    };

    void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onSessionStatus();
    void attach();
    void applyChatOptions();
    void onChatFailed(const ::ed3k::event::ChatFailed& f);
    void onFound(const ::ed3k::Key32& key, const ::ed3k::NodeId& node, const ::ed3k::Address& addr, qint64 when);
    void onNotFound(const ::ed3k::NodeId& node);
    [[nodiscard]] bool filtered(const QString& text) const;
    [[nodiscard]] ::ed3k::Source sourceOf(const ::ed3k::NodeId& node) const;
    [[nodiscard]] std::optional<::ed3k::Key32> keyOfNode(const ::ed3k::NodeId& node);
    void loadNames();
    void saveNames() const;
    [[nodiscard]] QString namesPath() const;

    Ed3kSession& m_session;
    ChatOptions m_options;
    bool m_attached = false;
    std::map<::ed3k::Key32, QString> m_names;
    std::map<::ed3k::Key32, Request> m_requests;
    std::map<::ed3k::NodeId, ::ed3k::Address> m_addr;
    std::map<::ed3k::NodeId, qint64> m_seen;
    std::map<std::uint64_t, Outgoing> m_out;               ///< by message id
    std::set<std::uint64_t> m_suppressed;                  ///< ids whose failure is not shown (retried)
    std::map<::ed3k::NodeId, std::vector<Outgoing>> m_waiting;   ///< sends waiting for a look
};

} // namespace eMule::ed3k
