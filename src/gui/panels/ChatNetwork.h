#pragma once

/// @file ChatNetwork.h
/// @brief What the Messages panel asks of a friend's network: eD2K (IPC 170-175) or ed3k
/// (IPC 1001-1009). The panel's chat tabs, history and cues are one code for both; only
/// these calls differ. A chat is keyed by FriendRow::hash (MD4 hex, or "ed3k:<node>").

#include "controls/FriendListModel.h"

#include <QCborArray>
#include <QCborMap>
#include <QObject>
#include <QString>

#include <functional>

namespace eMule {

class IpcClient;

class ChatNetwork {
public:
    explicit ChatNetwork(IpcClient* ipc) : m_ipc(ipc) {}
    virtual ~ChatNetwork() = default;

    [[nodiscard]] virtual FriendNetwork network() const = 0;
    /// Sends @p text in the chat @p key; @p done(ok) once the daemon took it.
    virtual void send(const QString& key, const QString& text, std::function<void(bool)> done) = 0;
    /// The chat window of @p key closed.
    virtual void endSession(const QString& key) = 0;
    /// Drops the friend @p row.
    virtual void removeFriend(const FriendRow& row, std::function<void()> done) = 0;
    /// Asks for this network's friends: @p done(rows, extra) with the GetFriends rows, or
    /// the ed3k "friends" with the whole map as extra. Not called on a failed reply.
    virtual void requestFriends(QObject* owner, std::function<void(const QCborArray&, const QCborMap&)> done) = 0;
    /// Whether the panel may ask the daemon for this client's details (eD2K only).
    [[nodiscard]] virtual bool hasClientDetails() const = 0;

    /// The network of a chat key.
    [[nodiscard]] static FriendNetwork networkOf(const QString& key);

protected:
    [[nodiscard]] bool online() const;

    IpcClient* m_ipc = nullptr;
};

/// eD2K: SendChatMessage, EndChatSession, RemoveFriend, GetFriends.
class Ed2kChatNetwork final : public ChatNetwork {
public:
    using ChatNetwork::ChatNetwork;
    [[nodiscard]] FriendNetwork network() const override { return FriendNetwork::Ed2k; }
    void send(const QString& key, const QString& text, std::function<void(bool)> done) override;
    void endSession(const QString& key) override;
    void removeFriend(const FriendRow& row, std::function<void()> done) override;
    void requestFriends(QObject* owner, std::function<void(const QCborArray&, const QCborMap&)> done) override;
    [[nodiscard]] bool hasClientDetails() const override { return true; }
};

/// ed3k: SendEd3kChat, EndEd3kChat, RemoveEd3kFriend, GetEd3kFriends.
class Ed3kChatNetwork final : public ChatNetwork {
public:
    using ChatNetwork::ChatNetwork;
    [[nodiscard]] FriendNetwork network() const override { return FriendNetwork::Ed3k; }
    void send(const QString& key, const QString& text, std::function<void(bool)> done) override;
    void endSession(const QString& key) override;
    void removeFriend(const FriendRow& row, std::function<void()> done) override;
    void requestFriends(QObject* owner, std::function<void(const QCborArray&, const QCborMap&)> done) override;
    [[nodiscard]] bool hasClientDetails() const override { return false; }
};

} // namespace eMule
