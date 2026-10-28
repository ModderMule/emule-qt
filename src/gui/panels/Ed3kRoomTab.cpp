#include "pch.h"
/// @file Ed3kRoomTab.cpp
/// @brief An ed3k room tab — implementation.

#include "panels/Ed3kRoomTab.h"

#include "chat/IrcNickList.h"
#include "controls/LogTextView.h"

#include <QCborArray>
#include <QHash>

#include <algorithm>

namespace eMule {

namespace {

constexpr int kShownKeyChars = 4;

/// member key text -> member ID hex, from the daemon's rows and pushes
QHash<QString, QString>& memberIds()
{
    static QHash<QString, QString> ids;
    return ids;
}
constexpr qsizetype kMaxPosts = 500;

} // namespace

Ed3kRoomTab::Ed3kRoomTab(const QString& room, std::function<void(const QString&)> onLink, PostRenderer render, QWidget* parent)
    : ChatChannelView(true, std::move(onLink), parent)
    , m_room(room)
    , m_render(std::move(render))
{
}

void Ed3kRoomTab::setRow(const QCborMap& row)
{
    m_row = row;
    m_mods.clear();
    for (const QCborValue& v : row.value(QStringLiteral("mods")).toArray())
        m_mods << v.toString();
    m_members.clear();
    for (const QCborValue& v : row.value(QStringLiteral("members")).toArray()) {
        const QCborMap m = v.toMap();
        m_members.insert(m.value(QStringLiteral("key")).toString(), m.value(QStringLiteral("nick")).toString());
        rememberMemberId(m.value(QStringLiteral("key")).toString(), m.value(QStringLiteral("id")).toString());
    }
    rememberMemberId(memberKey(), row.value(QStringLiteral("memberId")).toString());
    setTopicHtml(topicText().toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
}

QString Ed3kRoomTab::title() const
{
    const QString t = m_row.value(QStringLiteral("title")).toString();
    return t.isEmpty() ? tr("ed3k room %1").arg(m_room.left(8)) : t;
}

QString Ed3kRoomTab::tabText() const
{
    return tr("%1 (ed3k)").arg(title());
}

QString Ed3kRoomTab::topicText() const
{
    QStringList kind;
    if (m_row.value(QStringLiteral("closed")).toBool())
        kind << tr("closed");
    else if (m_row.value(QStringLiteral("private")).toBool())
        kind << tr("private");
    else
        kind << tr("public");
    if (m_row.value(QStringLiteral("moderated")).toBool())
        kind << tr("moderated");
    QString text = QStringLiteral("%1 [%2]").arg(title(), kind.join(QStringLiteral(", ")));
    const QString about = m_row.value(QStringLiteral("about")).toString();
    if (!about.isEmpty())
        text += QStringLiteral(" — ") + about;
    return text;
}

void Ed3kRoomTab::setMember(const QString& key, const QString& nick, bool left)
{
    if (left)
        m_members.remove(key);
    else
        m_members.insert(key, nick);
}

void Ed3kRoomTab::fillNicks(IrcNickList& nicks) const
{
    const IrcServerModes modes;   // "@" for a moderator
    nicks.clear();
    const auto add = [&](const QString& key, const QString& nick) {
        nicks.add((isModerator(key) ? QStringLiteral("@") : QString()) + shownName(nick, key), modes);
    };
    for (auto it = m_members.cbegin(); it != m_members.cend(); ++it)
        add(it.key(), it.value());
    if (!m_members.contains(memberKey()) && !memberKey().isEmpty())
        add(memberKey(), m_row.value(QStringLiteral("nick")).toString());
}

QString Ed3kRoomTab::keyOfShown(const QString& shown) const
{
    const QString bare = IrcNickList::bare(shown, IrcServerModes{});
    for (auto it = m_members.cbegin(); it != m_members.cend(); ++it)
        if (shownName(it.value(), it.key()) == bare)
            return it.key();
    if (shownName(m_row.value(QStringLiteral("nick")).toString(), memberKey()) == bare)
        return memberKey();
    return {};
}

QString Ed3kRoomTab::shownName(const QString& nick, const QString& key)
{
    // blanks would split the name in a nick completion
    QString n = nick.simplified();
    n.replace(QLatin1Char(' '), QLatin1Char('_'));
    if (n.isEmpty())
        n = QStringLiteral("?");
    // §6: something of the member ID; the key's start until the ID is known
    // was: return QStringLiteral("%1#%2").arg(n, key.left(kShownKeyChars));
    const QString id = memberIds().value(key);
    return QStringLiteral("%1#%2").arg(n, id.isEmpty() ? key.left(kShownKeyChars) : id.left(kShownKeyChars));
}

void Ed3kRoomTab::rememberMemberId(const QString& key, const QString& idHex)
{
    if (!key.isEmpty() && !idHex.isEmpty())
        memberIds().insert(key, idHex);
}

void Ed3kRoomTab::setPosts(const QVector<Post>& posts)
{
    m_posts = posts;
    std::ranges::stable_sort(m_posts, {}, &Post::sent);
    redraw();
}

bool Ed3kRoomTab::addPost(const Post& post)
{
    if (std::ranges::any_of(m_posts, [&](const Post& p) { return p.id == post.id; }))
        return false;
    // in the order of sent (§6): a late one sorts in and the log is drawn again
    if (m_posts.isEmpty() || m_posts.last().sent <= post.sent) {
        m_posts.append(post);
        append(render(post));
    } else {
        const auto at = std::ranges::upper_bound(m_posts, post.sent, {}, &Post::sent);
        m_posts.insert(at, post);
        redraw();
    }
    if (m_posts.size() > kMaxPosts)
        m_posts.remove(0, m_posts.size() - kMaxPosts);
    return true;
}

void Ed3kRoomTab::removePosts(const QStringList& ids)
{
    const auto n = m_posts.size();
    m_posts.removeIf([&](const Post& p) { return ids.contains(p.id); });
    if (m_posts.size() != n)
        redraw();
}

Ed3kRoomTab::Post Ed3kRoomTab::postFrom(const QCborMap& map)
{
    Post p;
    p.id = map.value(QStringLiteral("id")).toString();
    p.author = map.value(QStringLiteral("author")).toString();
    rememberMemberId(p.author, map.value(QStringLiteral("authorId")).toString());
    p.nick = map.value(QStringLiteral("nick")).toString();
    p.moderator = map.value(QStringLiteral("moderator")).toBool();
    p.sent = map.value(QStringLiteral("sent")).toInteger();
    p.text = map.value(QStringLiteral("text")).toString();
    p.replyTo = map.value(QStringLiteral("replyTo")).toString();
    return p;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kRoomTab::redraw()
{
    log()->clear();
    for (const Post& p : std::as_const(m_posts))
        append(render(p));
}

QString Ed3kRoomTab::render(const Post& p) const
{
    // a reply names its post; one not held is still a reply (§2.5)
    QString reply;
    if (!p.replyTo.isEmpty()) {
        const auto it = std::ranges::find_if(m_posts, [&](const Post& q) { return q.id == p.replyTo; });
        reply = it != m_posts.cend() ? QStringLiteral("%1: %2").arg(shownName(it->nick, it->author), it->text.left(60))
                                     : tr("a post not held");
    }
    const QString shown = (p.moderator ? QStringLiteral("@") : QString()) + shownName(p.nick, p.author);
    return m_render ? m_render(p, shown, p.author == memberKey(), reply) : p.text.toHtmlEscaped();
}

} // namespace eMule
