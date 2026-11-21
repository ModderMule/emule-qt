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
#include "panels/Ed3kRoomMediaView.h"

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
        Ed3kRoomMediaView::Ref media;   ///< §2.6; root empty: none
    };
    /// Draws one post: @p shown the member as listed, @p own our own post.
    /// The moderator star as an inline image of the log (§6); the post renderer puts it first.
    [[nodiscard]] static QString modMarkHtml();
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
    /// The star of a post: its author was a moderator then or is one now.
    [[nodiscard]] bool marked(const Post& p) const { return p.moderator || isModerator(p.author); }
    [[nodiscard]] QStringList mods() const { return m_mods; }
    /// Admitted by an admit or as moderator (§2.2); known to a moderator of a closed or
    /// moderated room only.
    [[nodiscard]] bool isAdmitted(const QString& key) const { return m_admitted.contains(key); }
    /// Admits change something only here (§2.2).
    [[nodiscard]] bool admitsMatter() const
    {
        return m_row.value(QStringLiteral("closed")).toBool() || m_row.value(QStringLiteral("moderated")).toBool();
    }

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
    /// The room takes media (§2.6): its largest file, bytes; 0 none.
    [[nodiscard]] qint64 mediaMax() const { return m_row.value(QStringLiteral("mediaMax")).toInteger(); }
    /// The room takes other small files too: text, NZB, DLC (§2.6 `files`).
    [[nodiscard]] bool takesFiles() const { return mediaMax() > 0 && m_row.value(QStringLiteral("files")).toBool(); }
    /// How a ready file's bytes come (IPC); they go to setMediaBytes.
    void setMediaLoader(std::function<void(const QString& root)> load) { m_media->setLoader(std::move(load)); }
    /// PushEd3kRoomMedia: a post's media changed.
    void setMediaState(const QString& root, int state);
    void setMediaBytes(const QString& root, const QByteArray& bytes) { m_media->setBytes(root, bytes); }
    [[nodiscard]] const QVector<Post>& posts() const { return m_posts; }
    [[nodiscard]] int mediaState(const QString& root) const { return m_media->state(root); }
    /// As a click on the post's file or image
    void clickMedia(const QString& root) { m_media->clicked(root, this); }

signals:
    /// The post menu: answer @p id with the next post.
    void replyRequested(const QString& id);
    /// The post menu: mute (or unmute) a member ID.
    void muteRequested(const QString& memberId, bool on);
    /// The bar: import an admit.
    void importAdmitRequested();
    /// A click asks for a post's file (FetchEd3kRoomMedia).
    void mediaFetchRequested(const QString& root);
    /// The user opened a post's NZB, DLC or torrent: to its add dialog.
    void fileOpenRequested(const QString& name, const QByteArray& bytes);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

public:
    /// The log again from the posts held: after a mute or a changed chat option.
    void redraw();

private:
    /// Appends @p p to the log, marked as its post (postIdAt); nothing when muted.
    void draw(const Post& p);
    [[nodiscard]] QString render(const Post& p) const;
    void buildMemberKeyBar();

    QString m_room;
    PostRenderer m_render;
    QCborMap m_row;
    QStringList m_mods;
    QSet<QString> m_admitted;   ///< member keys admitted (row "admitted")
    QMap<QString, QString> m_members;   ///< member key -> nick
    QVector<Post> m_posts;              ///< by sent
    QString m_replyTo;
    QSet<QString> m_muted;              ///< member IDs hex
    QWidget* m_keyBar = nullptr;
    QLineEdit* m_keyEdit = nullptr;
    QPushButton* m_importBtn = nullptr;
    Ed3kRoomMediaView* m_media = nullptr;
};

} // namespace eMule
