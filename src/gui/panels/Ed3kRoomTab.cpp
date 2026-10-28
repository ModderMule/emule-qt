#include "pch.h"
/// @file Ed3kRoomTab.cpp
/// @brief An ed3k room tab — implementation.

#include "panels/Ed3kRoomTab.h"

#include "chat/IrcNickList.h"
#include "controls/LogTextView.h"

#include <QApplication>
#include <QCborArray>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>
#include <memory>

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
/// a quoted parent: its first characters
constexpr qsizetype kQuoteChars = 60;

/// marks a log block as a post's (postIdAt)
class PostBlock : public QTextBlockUserData {
public:
    explicit PostBlock(QString id) : id(std::move(id)) {}
    QString id;
};

} // namespace

Ed3kRoomTab::Ed3kRoomTab(const QString& room, std::function<void(const QString&)> onLink, PostRenderer render, QWidget* parent)
    : ChatChannelView(true, std::move(onLink), parent)
    , m_room(room)
    , m_render(std::move(render))
{
    // a post's menu: reply, mute (the log's own entries stay)
    log()->viewport()->installEventFilter(this);
    buildMemberKeyBar();
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
    // §6: a mute changed: the log without (or with again) that member's posts
    QSet<QString> muted;
    for (const QCborValue& v : row.value(QStringLiteral("muted")).toArray())
        muted.insert(v.toString().toLower());
    if (muted != m_muted) {
        m_muted = muted;
        redraw();
    }
    // #17: refused as not admitted, until a member takes us
    m_keyEdit->setText(memberKey());
    if (row.value(QStringLiteral("notAdmitted")).toBool())
        showMemberKeyBar(true);
    else if (row.value(QStringLiteral("joined")).toBool())
        showMemberKeyBar(false);
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
        draw(post);
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
    p.authorId = map.value(QStringLiteral("authorId")).toString().toLower();
    rememberMemberId(p.author, p.authorId);
    p.nick = map.value(QStringLiteral("nick")).toString();
    p.moderator = map.value(QStringLiteral("moderator")).toBool();
    p.sent = map.value(QStringLiteral("sent")).toInteger();
    p.text = map.value(QStringLiteral("text")).toString();
    p.replyTo = map.value(QStringLiteral("replyTo")).toString();
    return p;
}

const Ed3kRoomTab::Post* Ed3kRoomTab::post(const QString& id) const
{
    const auto it = std::ranges::find_if(m_posts, [&](const Post& q) { return q.id == id; });
    return it != m_posts.cend() ? &*it : nullptr;
}

const Ed3kRoomTab::Post* Ed3kRoomTab::lastPostOf(const QString& key) const
{
    const auto it = std::ranges::find_if(m_posts.crbegin(), m_posts.crend(), [&](const Post& q) { return q.author == key; });
    return it != m_posts.crend() ? &*it : nullptr;
}

QString Ed3kRoomTab::postIdAt(const QPoint& pos) const
{
    const QTextBlock block = log()->cursorForPosition(pos).block();
    const auto* data = dynamic_cast<const PostBlock*>(block.userData());
    return data ? data->id : QString();
}

QString Ed3kRoomTab::memberIdOf(const QString& key)
{
    return memberIds().value(key).toLower();
}

bool Ed3kRoomTab::showsMemberKeyBar() const
{
    return m_keyBar && !m_keyBar->isHidden();
}

void Ed3kRoomTab::showMemberKeyBar(bool on)
{
    if (m_keyBar)
        m_keyBar->setVisible(on);
}

bool Ed3kRoomTab::admitImportable() const
{
    // batch 12 ruling #17: not for every room with `s`
    return m_row.value(QStringLiteral("closed")).toBool() || m_row.value(QStringLiteral("moderated")).toBool()
        || m_row.value(QStringLiteral("notAdmitted")).toBool();
}

bool Ed3kRoomTab::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != log()->viewport() || event->type() != QEvent::ContextMenu)
        return ChatChannelView::eventFilter(watched, event);
    const auto* ce = static_cast<QContextMenuEvent*>(event);
    const Post* p = post(postIdAt(ce->pos()));
    if (!p && m_replyTo.isEmpty())
        return false;   // the log's own menu
    const std::unique_ptr<QMenu> menu(log()->createStandardContextMenu(ce->pos()));
    QAction* first = menu->actions().value(0);
    QList<QAction*> ours;
    if (p) {
        const QString id = p->id;
        const QString memberId = p->authorId;
        ours << new QAction(tr("Reply"), menu.get());
        connect(ours.last(), &QAction::triggered, this, [this, id] { emit replyRequested(id); });
        if (p->author != memberKey() && !memberId.isEmpty()) {
            const bool muted = isMuted(memberId);
            ours << new QAction(muted ? tr("Unmute %1").arg(shownName(p->nick, p->author)) : tr("Mute %1").arg(shownName(p->nick, p->author)),
                                menu.get());
            connect(ours.last(), &QAction::triggered, this, [this, memberId, muted] { emit muteRequested(memberId, !muted); });
        }
        const QString author = p->author;
        ours << new QAction(tr("Copy member key"), menu.get());
        connect(ours.last(), &QAction::triggered, this, [author] { QApplication::clipboard()->setText(author); });
    }
    if (!m_replyTo.isEmpty()) {
        ours << new QAction(tr("Cancel reply"), menu.get());
        connect(ours.last(), &QAction::triggered, this, [this] { m_replyTo.clear(); });
    }
    auto* sep = new QAction(menu.get());
    sep->setSeparator(true);
    ours << sep;
    menu->insertActions(first, ours);
    menu->exec(ce->globalPos());
    return true;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kRoomTab::redraw()
{
    log()->clear();
    for (const Post& p : std::as_const(m_posts))
        draw(p);
}

void Ed3kRoomTab::draw(const Post& p)
{
    // §6: a muted member's posts are kept, not drawn
    if (!p.authorId.isEmpty() && isMuted(p.authorId))
        return;
    QTextDocument* doc = log()->document();
    const int before = doc->blockCount();
    const bool empty = doc->isEmpty();
    append(render(p));
    // every block of the post (the quoted line too) names it
    for (QTextBlock b = empty ? doc->firstBlock() : doc->findBlockByNumber(before); b.isValid(); b = b.next())
        b.setUserData(new PostBlock(p.id));
}

QString Ed3kRoomTab::render(const Post& p) const
{
    // a reply names its post; one not held is still a reply (§2.5): an answer to a post the
    // node lacks
    // was: .arg(shownName(it->nick, it->author), it->text.left(60)) : tr("a post not held")
    QString reply;
    if (!p.replyTo.isEmpty()) {
        const auto it = std::ranges::find_if(m_posts, [&](const Post& q) { return q.id == p.replyTo; });
        if (it == m_posts.cend()) {
            reply = tr("answer to a post not held");
        } else {
            QString quote = it->text.simplified();
            if (quote.size() > kQuoteChars)
                quote = quote.left(kQuoteChars) + QStringLiteral("…");
            reply = QStringLiteral("%1: %2").arg(shownName(it->nick, it->author), quote);
        }
    }
    const QString shown = (p.moderator ? QStringLiteral("@") : QString()) + shownName(p.nick, p.author);
    return m_render ? m_render(p, shown, p.author == memberKey(), reply) : p.text.toHtmlEscaped();
}

void Ed3kRoomTab::buildMemberKeyBar()
{
    // #17 (batch 12 ruling): a closed room refused us; the key goes to a moderator
    m_keyBar = new QWidget(this);
    m_keyBar->setObjectName(QStringLiteral("ed3kMemberKeyBar"));
    auto* row = new QHBoxLayout(m_keyBar);
    row->setContentsMargins(4, 2, 4, 2);
    auto* text = new QLabel(tr("This room is closed and you are not admitted yet. Send this member key to a moderator:"), m_keyBar);
    text->setWordWrap(true);
    m_keyEdit = new QLineEdit(m_keyBar);
    m_keyEdit->setReadOnly(true);
    m_keyEdit->setMinimumWidth(m_keyEdit->fontMetrics().horizontalAdvance(QLatin1Char('M')) * 30);
    auto* copy = new QPushButton(tr("Copy"), m_keyBar);
    m_importBtn = new QPushButton(tr("Import admit..."), m_keyBar);
    m_importBtn->setToolTip(tr("Only needed when a moderator gave you an admit text"));
    auto* close = new QPushButton(tr("Hide"), m_keyBar);
    row->addWidget(text, 1);
    row->addWidget(m_keyEdit, 1);
    row->addWidget(copy);
    row->addWidget(m_importBtn);
    row->addWidget(close);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_keyEdit->text()); });
    connect(m_importBtn, &QPushButton::clicked, this, &Ed3kRoomTab::importAdmitRequested);
    connect(close, &QPushButton::clicked, m_keyBar, &QWidget::hide);
    insertWidget(0, m_keyBar);
    m_keyBar->setMaximumHeight(m_keyBar->sizeHint().height() * 2);
    m_keyBar->hide();
}

} // namespace eMule
