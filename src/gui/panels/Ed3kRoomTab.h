#pragma once

/// @file Ed3kRoomTab.h
/// @brief An ed3k room (room.md) as a tab of the IRC panel: the channel view plus what the
/// daemon says of the room (GetEd3kRooms row), its members and its posts.
///
/// - A member shows as "nick#abcd" (abcd: the start of its member key), "@" before a
///   moderator: two members with one nick stay apart (room.md §6).
/// - Posts are kept in the order of `sent` (§6) and shown as plain text; a post a ban
///   dropped is taken out (the log is drawn again).
/// - Reply (§2.5): a post's menu (right click on it) or the nick menu picks the post the next
///   one answers; a reply shows a quoted line of its post, or "a post not held" (§2.5).
/// - Mute (§6, local): the posts of a muted member ID are not drawn; the daemon keeps the list.
/// - #17: after ROOM_MEMBER (a closed room, not admitted) a bar shows the own member key with
///   Copy, to send to a moderator, and Import admit.

#include "panels/ChatChannelView.h"

#include <QCborMap>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <utility>

class QLineEdit;
class QPushButton;

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
        QString authorId;   ///< member ID hex (§1.1)
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

    /// A held post by id; null: none.
    [[nodiscard]] const Post* post(const QString& id) const;
    /// The newest post of a member key; null: none.
    [[nodiscard]] const Post* lastPostOf(const QString& key) const;
    /// The post drawn at @p pos of the log's viewport; empty: none there.
    [[nodiscard]] QString postIdAt(const QPoint& pos) const;
    /// The post the next one answers (§2.5); empty: none.
    void setReplyTo(const QString& id) { m_replyTo = id; }
    [[nodiscard]] const QString& replyTo() const { return m_replyTo; }
    /// The pending reply, cleared.
    QString takeReplyTo() { return std::exchange(m_replyTo, QString()); }
    /// §6: muted member IDs (hex) of the row.
    [[nodiscard]] bool isMuted(const QString& memberId) const { return m_muted.contains(memberId); }
    /// The member ID (hex) of a member key; empty: not known yet.
    [[nodiscard]] static QString memberIdOf(const QString& key);
    /// #17: the bar with the own member key shows (a ROOM_MEMBER refusal).
    [[nodiscard]] bool showsMemberKeyBar() const;
    void showMemberKeyBar(bool on);
    /// "Import admit" makes sense: the room is known closed or moderated, or refused us (#17).
    [[nodiscard]] bool admitImportable() const;

signals:
    /// The post menu: answer @p id with the next post.
    void replyRequested(const QString& id);
    /// The post menu: mute (or unmute) a member ID.
    void muteRequested(const QString& memberId, bool on);
    /// The bar: import an admit.
    void importAdmitRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void redraw();
    /// Appends @p p to the log, marked as its post (postIdAt); nothing when muted.
    void draw(const Post& p);
    [[nodiscard]] QString render(const Post& p) const;
    void buildMemberKeyBar();

    QString m_room;
    PostRenderer m_render;
    QCborMap m_row;
    QStringList m_mods;
    QMap<QString, QString> m_members;   ///< member key -> nick
    QVector<Post> m_posts;              ///< by sent
    QString m_replyTo;
    QSet<QString> m_muted;              ///< member IDs hex
    QWidget* m_keyBar = nullptr;
    QLineEdit* m_keyEdit = nullptr;
    QPushButton* m_importBtn = nullptr;
};

} // namespace eMule
