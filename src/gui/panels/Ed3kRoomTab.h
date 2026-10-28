#pragma once

/// @file Ed3kRoomTab.h
/// @brief An ed3k room (room.md) as a tab of the IRC panel: the channel view plus what the
/// daemon says of the room (GetEd3kRooms row), its members and its posts.
///
/// - A member shows as "nick#abcd" (abcd: the start of its member key), "@" before a
///   moderator: two members with one nick stay apart (room.md §6).
/// - Posts are kept in the order of `sent` (§6) and shown as plain text; a post a ban
///   dropped is taken out (the log is drawn again).

#include "panels/ChatChannelView.h"

#include <QCborMap>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace eMule {

class IrcNickList;

class Ed3kRoomTab : public ChatChannelView {
    Q_OBJECT

public:
    /// One post as GetEd3kRoomPosts / PushEd3kRoomPost carries it.
    struct Post {
        QString id;
        QString author;     ///< member key, base32
        QString nick;
        bool moderator = false;
        qint64 sent = 0;
        QString text;
        QString replyTo;    ///< post id, empty none
    };
    /// Draws one post: @p shown the member as listed, @p own our own post.
    using PostRenderer = std::function<QString(const Post& post, const QString& shown, bool own, const QString& replyText)>;

    Ed3kRoomTab(const QString& room, std::function<void(const QString&)> onLink, PostRenderer render, QWidget* parent = nullptr);

    [[nodiscard]] const QString& room() const { return m_room; }
    /// The panel's key of this tab ("ed3k:<room hex>").
    [[nodiscard]] static QString keyOf(const QString& room) { return QStringLiteral("ed3k:") + room; }

    /// A row of GetEd3kRooms: title, about, mods, members, own key and role.
    void setRow(const QCborMap& row);
    [[nodiscard]] const QCborMap& row() const { return m_row; }
    [[nodiscard]] QString title() const;
    /// "title (ed3k)", what the tab reads.
    [[nodiscard]] QString tabText() const;
    [[nodiscard]] QString topicText() const;
    [[nodiscard]] QString link() const { return m_row.value(QStringLiteral("link")).toString(); }
    [[nodiscard]] QString memberKey() const { return m_row.value(QStringLiteral("memberKey")).toString(); }
    [[nodiscard]] bool isOwner() const { return m_row.value(QStringLiteral("owner")).toBool(); }
    [[nodiscard]] bool canModerate() const { return m_row.value(QStringLiteral("moderator")).toBool(); }
    [[nodiscard]] bool isModerator(const QString& key) const { return m_mods.contains(key); }
    [[nodiscard]] QStringList mods() const { return m_mods; }

    /// A presence: @p left drops the member.
    void setMember(const QString& key, const QString& nick, bool left);
    [[nodiscard]] bool hasMember(const QString& key) const { return m_members.contains(key); }
    [[nodiscard]] QString memberNick(const QString& key) const { return m_members.value(key); }
    /// The members as IRC nicks ("@" for a moderator), ourselves included.
    void fillNicks(IrcNickList& nicks) const;
    /// The member key of a listed name ("@nick#abcd"); empty when unknown.
    [[nodiscard]] QString keyOfShown(const QString& shown) const;
    /// "nick#abcd" of a member: the start of its member ID (room §6) once known, else of its key.
    [[nodiscard]] static QString shownName(const QString& nick, const QString& key);
    /// Remembers the member ID (hex) of a member key for shownName().
    static void rememberMemberId(const QString& key, const QString& idHex);

    /// All posts at once (GetEd3kRoomPosts); drawn again.
    void setPosts(const QVector<Post>& posts);
    /// A new post; false when held already.
    bool addPost(const Post& post);
    void removePosts(const QStringList& ids);
    [[nodiscard]] static Post postFrom(const QCborMap& map);

private:
    void redraw();
    [[nodiscard]] QString render(const Post& p) const;

    QString m_room;
    PostRenderer m_render;
    QCborMap m_row;
    QStringList m_mods;
    QMap<QString, QString> m_members;   ///< member key -> nick
    QVector<Post> m_posts;              ///< by sent
};

} // namespace eMule
