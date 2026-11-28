#include "pch.h"
/// @file IrcPanelEd3k.cpp
/// @brief The IRC panel's ed3k rooms (room.md): the "Rooms (ed3k)" tab, a tab per room, the
/// nick list of its members and the moderator's actions. IPC 1120-1129 / 1160-1164.
///
/// A room link with `s` is a credential (§1.3): never logged, never in an error line.

#include "utils/Clipboard.h"
#include "panels/IrcPanel.h"

#include "app/IpcClient.h"
#include "controls/LogTextView.h"
#include "panels/Ed3kRoomList.h"
#include "panels/Ed3kRoomTab.h"
#include "utils/MediaSniff.h"
#include "IpcMessage.h"
#include "prefs/Preferences.h"
#include "app/SkinIcons.h"
#include "app/UiState.h"
#include "utils/Log.h"
#include "utils/ArgStr.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QRegularExpression>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QBuffer>
#include <QSpinBox>
#include <QFormLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QUrlQuery>
#include <QCborArray>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

namespace eMule {

using Ipc::IpcMessage;

namespace {

const QString kRoomListKey = QStringLiteral("__ed3krooms__");
constexpr qint64 kBanSeconds = 30ll * 24 * 3600;   // room.md §2.3: at most 30 days

// ED3K_ROOM_* of ed3k.h (the GUI does not include it)
constexpr int kRoomPrivate = 1;
constexpr int kRoomClosed = 2;
constexpr int kRoomModerated = 4;
// room.md §2.1, §2.6: media default 6.5 MiB, at most 16 MiB; history 1 to 30 days
constexpr double kMediaDefaultMiB = 6.5;
constexpr double kMediaMaxMiB = 16.0;
constexpr int kKeepDaysMax = 30;
constexpr int kKeepPostsPerDay = 1000;   // posts kept per day of history, up to 10,000

// Ed3kRooms::Notice and ::Moderation (daemon side)
enum class Notice { Joined = 0, Left, Neighbour, Banned, Generation, KeyMissing, WrapBad, Refused, Fault };
enum class Moderation { Admit = 0, Ban = 1, Lift = 2, NewGeneration = 3 };

/// The room form: a new room, or the owner's room info.
struct RoomForm {
    QString title;
    QString about;
    QString nick;
    int flags = 0;
    qint64 mediaMax = 0;   ///< bytes, 0 = no media
    bool files = false;    ///< text, NZB, DLC, torrent too (§2.6)
    int keepDays = 1;
};

/// Someone else's post, not muted: one that can be unread.
bool countsAsUnread(const Ed3kRoomTab* tab, const Ed3kRoomTab::Post& p)
{
    return p.author != tab->memberKey() && !tab->isMuted(p.authorId.toLower());
}

/// @p p answers us or names our nick.
bool mentionsMe(const Ed3kRoomTab* tab, const Ed3kRoomTab::Post& p)
{
    const QString me = tab->row().value(QStringLiteral("nick")).toString().trimmed();
    const Ed3kRoomTab::Post* answered = p.replyTo.isEmpty() ? nullptr : tab->post(p.replyTo);
    return (answered && answered->author == tab->memberKey()) || (!me.isEmpty() && p.text.contains(me, Qt::CaseInsensitive));
}

/// keep_posts for a history of @p days (§2.1: 1,000 to 10,000)
int keepPostsOf(int days)
{
    return std::clamp(days * kKeepPostsPerDay, 1000, 10000);
}

std::optional<RoomForm> askRoomForm(QWidget* parent, const QString& caption, const RoomForm& start, bool create)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(caption);
    auto* form = new QFormLayout(&dlg);
    auto* title = new QLineEdit(start.title, &dlg);
    title->setMaxLength(128);
    auto* about = new QPlainTextEdit(start.about, &dlg);
    about->setMaximumHeight(90);
    auto* nick = new QLineEdit(start.nick, &dlg);
    nick->setMaxLength(64);
    auto* kind = new QComboBox(&dlg);
    kind->addItem(IrcPanel::tr("Public: anyone with the link"), 0);
    kind->addItem(IrcPanel::tr("Private: posts sealed, anyone with the link"), kRoomPrivate);
    kind->addItem(IrcPanel::tr("Closed: only admitted members"), kRoomPrivate | kRoomClosed);
    auto* moderated = new QCheckBox(IrcPanel::tr("Moderated: only admitted members post"), &dlg);
    moderated->setChecked(start.flags & kRoomModerated);
    // §2.6: owner allows media up to a size; §4.3: how long members keep posts
    auto* media = new QCheckBox(IrcPanel::tr("Allow images and videos, up to"), &dlg);
    media->setObjectName(QStringLiteral("roomMedia"));
    media->setChecked(start.mediaMax > 0);
    auto* mediaMiB = new QDoubleSpinBox(&dlg);
    mediaMiB->setRange(0.1, kMediaMaxMiB);
    mediaMiB->setDecimals(1);
    mediaMiB->setSingleStep(0.5);
    mediaMiB->setSuffix(IrcPanel::tr(" MiB"));
    mediaMiB->setValue(start.mediaMax > 0 ? double(start.mediaMax) / (1 << 20) : kMediaDefaultMiB);
    mediaMiB->setEnabled(media->isChecked());
    QObject::connect(media, &QCheckBox::toggled, mediaMiB, &QWidget::setEnabled);
    auto* mediaRow = new QHBoxLayout;
    mediaRow->addWidget(media);
    mediaRow->addWidget(mediaMiB);
    mediaRow->addStretch(1);
    // §2.6 `files`: never fetched or opened by themselves; same size limit
    auto* files = new QCheckBox(IrcPanel::tr("Also allow small files: text, NZB, DLC and torrent"), &dlg);
    files->setObjectName(QStringLiteral("roomFiles"));
    files->setToolTip(IrcPanel::tr("Members download them only on a click and open them themselves. "
                                   "NZB, DLC and torrent files open in the add dialog, nothing starts by itself"));
    files->setChecked(start.files && start.mediaMax > 0);
    files->setEnabled(media->isChecked());
    QObject::connect(media, &QCheckBox::toggled, files, &QWidget::setEnabled);
    auto* keep = new QSpinBox(&dlg);
    keep->setRange(1, kKeepDaysMax);
    keep->setSuffix(IrcPanel::tr(" day(s)"));
    keep->setValue(std::clamp(start.keepDays, 1, kKeepDaysMax));
    keep->setToolTip(IrcPanel::tr("Members keep the posts that long and give them to members coming back. "
                                  "Older clients keep one day"));
    form->addRow(IrcPanel::tr("Title:"), title);
    form->addRow(IrcPanel::tr("About:"), about);
    if (create) {
        form->addRow(IrcPanel::tr("Your nick:"), nick);
        form->addRow(IrcPanel::tr("Kind:"), kind);
    } else {
        // the secret is fixed by the link: only closed or not, and only with a secret
        nick->hide();
        kind->clear();
        if (start.flags & kRoomPrivate) {
            kind->addItem(IrcPanel::tr("Private: posts sealed, anyone with the link"), kRoomPrivate);
            kind->addItem(IrcPanel::tr("Closed: only admitted members"), kRoomPrivate | kRoomClosed);
            kind->setCurrentIndex(start.flags & kRoomClosed ? 1 : 0);
            form->addRow(IrcPanel::tr("Kind:"), kind);
        } else {
            kind->addItem(IrcPanel::tr("Public: anyone with the link"), 0);
            kind->hide();
        }
    }
    form->addRow(QString(), moderated);
    form->addRow(IrcPanel::tr("Media:"), mediaRow);
    form->addRow(QString(), files);
    form->addRow(IrcPanel::tr("History kept:"), keep);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    dlg.resize(460, dlg.sizeHint().height());
    if (dlg.exec() != QDialog::Accepted || title->text().trimmed().isEmpty())
        return std::nullopt;
    RoomForm out;
    out.title = title->text().trimmed();
    out.about = about->toPlainText().trimmed();
    out.nick = nick->text().trimmed();
    out.flags = kind->currentData().toInt() | (moderated->isChecked() ? kRoomModerated : 0);
    out.mediaMax = media->isChecked() ? qint64(mediaMiB->value() * (1 << 20)) : 0;
    out.files = media->isChecked() && files->isChecked();
    out.keepDays = keep->value();
    return out;
}

/// What an ed3k::Error of a room request means to the user.
QString errorText(qint64 error)
{
    switch (error) {
    case 0:   return IrcPanel::tr("the ed3k node is not running");
    case -10: return IrcPanel::tr("not a valid room link or admit");
    case -15: return IrcPanel::tr("no key of the current generation yet");
    case -16: return IrcPanel::tr("too many rooms (at most 16)");
    case -17: return IrcPanel::tr("not allowed (not admitted, banned or no moderator)");
    case -18: return IrcPanel::tr("unknown room");
    case -20: return IrcPanel::tr("a key that cannot be used here");
    case -22: return IrcPanel::tr("the room keys could not be stored");
    default:  return IrcPanel::tr("error %1").arg(error);
    }
}

/// `dn` of an ed3k:room link, else a short form of its key (never `s`).
QString linkName(const QString& link)
{
    const QUrlQuery q(link.mid(link.indexOf(QLatin1Char('?')) + 1));
    const QString dn = q.queryItemValue(QStringLiteral("dn"), QUrl::FullyDecoded);
    return dn.isEmpty() ? IrcPanel::tr("room %1").arg(q.queryItemValue(QStringLiteral("key")).left(8)) : dn;
}

/// Same room: same `key` and `id` of two ed3k:room links, any case, any `dn` (core: Ed3kRooms::sameRoomLink).
bool sameRoomLink(const QString& a, const QString& b)
{
    const auto ident = [](const QString& link) {
        const QUrlQuery q(link.mid(link.indexOf(QLatin1Char('?')) + 1));
        return std::pair{q.queryItemValue(QStringLiteral("key")).toLower(), q.queryItemValue(QStringLiteral("id")).toLower()};
    };
    const auto x = ident(a);
    return !x.first.isEmpty() && !x.second.isEmpty() && x == ident(b);
}

} // namespace

void IrcPanel::setIpcClient(IpcClient* ipc)
{
    m_ipc = ipc;
    connectEd3kRooms();
    if (qEnvironmentVariableIntValue("EMULE_DUMP_ROOM_MENUS") == 1) {
        auto* dump = new QTimer(this);
        connect(dump, &QTimer::timeout, this, &IrcPanel::dumpEd3kNickMenus);
        dump->start(30'000);
    }
    if (const QString dir = qEnvironmentVariable("EMULE_ROOM_FILE_SELFTEST"); !dir.isEmpty())
        selfTestRoomFiles(dir);
}

bool IrcPanel::isEd3kRoomMember(const QString& link) const
{
    return m_roomList && std::ranges::any_of(m_roomList->rooms(), [&](const QCborValue& v) {
        return sameRoomLink(v.toMap().value(QStringLiteral("link")).toString(), link);
    });
}

bool IrcPanel::openEd3kRoomLink(const QString& link)
{
    // no room list yet (GUI start, node still starting): membership unknown, so wait for it
    if (!m_roomList) {
        if (!std::ranges::any_of(m_pendingRoomLinks, [&](const QString& l) { return sameRoomLink(l, link); }))
            m_pendingRoomLinks << link;
        return false;
    }
    // already a member (as known eD2K/ed3k files): no question, its tab shows
    for (const QCborValue& v : m_roomList->rooms()) {
        const QCborMap row = v.toMap();
        if (!sameRoomLink(row.value(QStringLiteral("link")).toString(), link))
            continue;
        const QString room = row.value(QStringLiteral("room")).toString();
        m_roomTabsClosed.removeAll(room);
        ensureRoomTab(room, true);
        const QString title = row.value(QStringLiteral("title")).toString();
        appendInfo(Ed3kRoomTab::keyOf(room), tr("* You are already a member of the room \"%1\"")
                                                 .arg(title.isEmpty() ? linkName(link) : title));
        return true;
    }
    // the link may carry s: the dialog names the room, never shows the secret
    const auto answer = QMessageBox::question(this, tr("ed3k room"),
        tr("Join the ed3k room \"%1\"?\n\nOther members see your nick and posts; your node takes "
           "part in passing the room's posts on.").arg(linkName(link)));
    if (answer != QMessageBox::Yes)
        return false;
    joinEd3kRoom(link);
    return true;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void IrcPanel::connectEd3kRooms()
{
    if (!m_ipc)
        return;
    constexpr auto unique = Qt::UniqueConnection;
    connect(m_ipc, &IpcClient::connected, this, &IrcPanel::refreshEd3kRooms, unique);
    connect(m_ipc, &IpcClient::ed3kStatusChanged, this, &IrcPanel::refreshEd3kRooms, unique);
    connect(m_ipc, &IpcClient::ed3kRoomsChanged, this, &IrcPanel::refreshEd3kRooms, unique);
    connect(m_ipc, &IpcClient::ed3kRoomPost, this, &IrcPanel::onEd3kRoomPost, unique);
    connect(m_ipc, &IpcClient::ed3kRoomPostsRemoved, this, &IrcPanel::onEd3kRoomPostsRemoved, unique);
    connect(m_ipc, &IpcClient::ed3kRoomPresence, this, &IrcPanel::onEd3kRoomPresence, unique);
    connect(m_ipc, &IpcClient::ed3kRoomNotice, this, &IrcPanel::onEd3kRoomNotice, unique);
    connect(m_ipc, &IpcClient::ed3kRoomMedia, this, &IrcPanel::onEd3kRoomMedia, unique);
    refreshEd3kRooms();
}

void IrcPanel::refreshEd3kRooms()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    m_ipc->requestEd3kRooms(this, [self = QPointer<IrcPanel>(this)](const IpcMessage& reply) {
        if (self)
            self->onEd3kRooms(reply);
    });
}

void IrcPanel::onEd3kRooms(const IpcMessage& reply)
{
    if (!reply.isValid() || !reply.fieldBool(0))
        return;
    const QCborMap data = reply.fieldMap(1);
    const QCborArray rooms = data.value(QStringLiteral("rooms")).toArray();
    if (!data.value(QStringLiteral("running")).toBool()) {
        // no node: neither the list nor a room
        QStringList keys;
        for (auto it = m_channels.cbegin(); it != m_channels.cend(); ++it)
            if (it->isEd3k())
                keys << it.key();
        for (const QString& k : keys)
            removeChannelTab(k);
        updateNickList();
        return;
    }
    if (!m_roomList) {
        // "Rooms (ed3k)" next to "Channels", not closable, as the channel list
        m_roomList = new Ed3kRoomList(this);
        connect(m_roomList, &Ed3kRoomList::actionRequested, this,
                [this](Ed3kRoomList::Action a, const QString& room) { onEd3kRoomAction(int(a), room); });
        IrcChannel ch;
        ch.name = tr("Rooms (ed3k)");
        ch.type = IrcChannel::Ed3kRoomList;
        ch.widget = m_roomList;
        const int channels = findTab(QStringLiteral("__channels__"));
        const int idx = m_tabWidget->insertTab(channels >= 0 ? channels + 1 : 1, m_roomList,
                                               skinIcon(QStringLiteral(":/icons/IRC.ico")), ch.name);
        m_tabWidget->tabBar()->setTabButton(idx, QTabBar::RightSide, nullptr);
        m_tabWidget->tabBar()->setTabButton(idx, QTabBar::LeftSide, nullptr);
        m_channels[kRoomListKey] = ch;
        // default tab on GUI start, unless the user already picked another one
        if (!m_roomListShownOnce) {
            m_roomListShownOnce = true;
            if (m_tabWidget->currentWidget() == m_statusBrowser)
                m_tabWidget->setCurrentIndex(idx);
        }
    }
    m_roomList->setRooms(rooms);

    QStringList present;
    for (const QCborValue& v : rooms) {
        const QCborMap row = v.toMap();
        const QString room = row.value(QStringLiteral("room")).toString();
        present << room;
        Ed3kRoomTab* tab = roomTab(room);
        if (!tab && !m_roomTabsClosed.contains(room))
            tab = ensureRoomTab(room);
        if (!tab)
            continue;
        tab->setRow(row);
        const QString key = Ed3kRoomTab::keyOf(room);
        updateAttachButton();
        auto it = m_channels.find(key);
        if (it == m_channels.end())
            continue;
        it->name = tab->tabText();
        it->topic = tab->topicText();
        tab->fillNicks(it->nicks);
        if (const int idx = findTab(key); idx >= 0)
            m_tabWidget->setTabText(idx, it->name);
    }
    // rooms left elsewhere (another GUI, a restart): their tabs go
    QStringList gone;
    for (auto it = m_channels.cbegin(); it != m_channels.cend(); ++it)
        if (it->type == IrcChannel::Ed3kRoom && it->room && !present.contains(it->room->room())) {
            gone << it.key();
            theUiState.forgetRoomRead(it->room->room());   // a later join starts unread-free
        }
    for (const QString& k : gone)
        removeChannelTab(k);
    m_roomTabsClosed.removeIf([&](const QString& r) { return !present.contains(r); });
    updateNickList();

    // links that came before the list: a member's tab just shows, the rest are asked now
    for (const QString& link : std::exchange(m_pendingRoomLinks, {})) {
        const bool member = std::ranges::any_of(rooms, [&](const QCborValue& v) {
            return sameRoomLink(v.toMap().value(QStringLiteral("link")).toString(), link);
        });
        if (!member && openEd3kRoomLink(link))
            emit ed3kRoomJoined();
    }
}

void IrcPanel::onEd3kRoomPost(const IpcMessage& msg)
{
    const QString room = msg.fieldString(0);
    Ed3kRoomTab* tab = roomTab(room);
    if (!tab)
        return;   // a closed tab: opening it fetches the posts
    const Ed3kRoomTab::Post p = Ed3kRoomTab::postFrom(msg.fieldMap(1));
    if (!tab->addPost(p))
        return;
    const QString key = Ed3kRoomTab::keyOf(room);
    markActivity(key);
    // someone's new post: read ones (before the read mark), muted members and, with no
    // mark yet, history a join brings (older than 10 min) stay quiet
    const qint64 readUpTo = theUiState.roomReadUpTo(room);
    const qint64 recent = QDateTime::currentSecsSinceEpoch() - 600;
    if (!countsAsUnread(tab, p) || (readUpTo > 0 ? p.sent <= readUpTo : p.sent < recent)) {
        if (!m_unread.contains(key))
            rememberRoomRead(key);
        return;
    }
    // one missed while away (synced late): unread, no sound
    if (p.sent < recent)
        markUnread(key);
    else
        noteMessage(key, mentionsMe(tab, p));
    if (!m_unread.contains(key))
        rememberRoomRead(key);   // on screen: read now
}

void IrcPanel::onEd3kRoomPostsRemoved(const IpcMessage& msg)
{
    Ed3kRoomTab* tab = roomTab(msg.fieldString(0));
    if (!tab)
        return;
    QStringList ids;
    for (const QCborValue& v : msg.fieldArray(1))
        ids << v.toString();
    tab->removePosts(ids);
}

void IrcPanel::onEd3kRoomPresence(const IpcMessage& msg)
{
    const QString room = msg.fieldString(0);
    Ed3kRoomTab* tab = roomTab(room);
    if (!tab)
        return;
    const QString member = msg.fieldString(1);
    const QString nick = msg.fieldString(2);
    const bool left = msg.fieldBool(3);
    Ed3kRoomTab::rememberMemberId(member, msg.fieldString(4));
    const QString key = Ed3kRoomTab::keyOf(room);
    auto it = m_channels.find(key);
    if (it == m_channels.end())
        return;
    // a presence comes every 5 min (§2.4): a line only for a change
    const bool known = tab->hasMember(member);
    const QString oldNick = tab->memberNick(member);
    tab->setMember(member, nick, left);
    tab->fillNicks(it->nicks);
    const QString shown = Ed3kRoomTab::shownName(nick, member);
    if (left) {
        if (known && !thePrefs.ircIgnorePartMessages())
            appendInfo(key, tr("* %1 has left the room").arg(shown));
    } else if (!known) {
        if (member != tab->memberKey() && !thePrefs.ircIgnoreJoinMessages())
            appendInfo(key, tr("* %1 has joined the room").arg(shown));
    } else if (oldNick != nick && !thePrefs.ircIgnoreMiscInfoMessages()) {
        appendInfo(key, tr("* %1 is now known as %2").arg(Ed3kRoomTab::shownName(oldNick, member), shown));
    }
    if (activeChannelName() == key)
        updateNickList();
}

void IrcPanel::onEd3kRoomNotice(const IpcMessage& msg)
{
    const QString room = msg.fieldString(0);
    if (!roomTab(room))
        return;
    const QString key = Ed3kRoomTab::keyOf(room);
    const auto kind = Notice(msg.fieldInt(1));
    const qint64 value = msg.fieldInt(2);
    const bool flag = msg.fieldBool(3);
    const QString who = msg.fieldString(4);
    switch (kind) {
    case Notice::Joined:
        // as IRC's "Now talking in": who we are here; flag: joined before (restart, reconnect)
        if (const Ed3kRoomTab* tab = roomTab(room)) {
            const QString me = Ed3kRoomTab::shownName(tab->row().value(QStringLiteral("nick")).toString(), tab->memberKey());
            appendInfo(key, flag ? tr("* You re-joined the room as %1").arg(me) : tr("* You joined the room as %1").arg(me));
        }
        break;
    case Notice::Banned:
        appendInfo(key, tr("* %1 was banned").arg(who.left(8)), IrcRouting::LineColor::Quit);
        break;
    case Notice::Generation:
        if (flag)
            appendInfo(key, tr("* Room key of generation %1 taken").arg(value));
        break;
    case Notice::KeyMissing:
        appendInfo(key, tr("* The room key of generation %1 is missing: posting waits for a moderator").arg(value),
                   IrcRouting::LineColor::Notice);
        break;
    case Notice::WrapBad:
        appendInfo(key, tr("* A moderator's room key (generation %1) does not open").arg(value),
                   IrcRouting::LineColor::Notice);
        break;
    case Notice::Refused:
        // ED3K_ROOM_FULL 1, _LEFT 2, _BLOCKED 3, _MEMBER 4
        if (value == 4) {
            // #17: a closed room; the key goes to a moderator, whose admit reaches the room
            // was: "... then import the admit (Rooms (ed3k) tab)" (no import needed, §4.2)
            if (Ed3kRoomTab* tab = roomTab(room); tab && !tab->showsMemberKeyBar()) {
                tab->showMemberKeyBar(true);
                appendInfo(key, tr("* A member refused us: this room is closed and you are not admitted yet. Send your "
                                   "member key (shown above) to a moderator"), IrcRouting::LineColor::Notice);
            }
        }
        else if (value == 3)
            appendInfo(key, tr("* A member refused us: banned in this room"), IrcRouting::LineColor::Notice);
        break;
    case Notice::Fault:
        appendInfo(key, tr("* A member sent a bad item"), IrcRouting::LineColor::Notice);
        break;
    case Notice::Left:
    case Notice::Neighbour:
        break;   // the state is in the list
    }
}

void IrcPanel::onEd3kRoomAction(int action, const QString& room)
{
    using A = Ed3kRoomList::Action;
    Ed3kRoomTab* tab = roomTab(room);
    QCborMap row;
    if (m_roomList)
        for (const QCborValue& v : m_roomList->rooms())
            if (v.toMap().value(QStringLiteral("room")).toString() == room)
                row = v.toMap();
    switch (A(action)) {
    case A::Open:
        ensureRoomTab(room, true);
        break;
    case A::Join:
        joinEd3kRoom({});
        break;
    case A::Paste:
        // pasting is the consent (as Paste eD2K Links): joins and shows the tab
        joinEd3kRoom(room);
        break;
    case A::Create:
        createEd3kRoom();
        break;
    case A::Leave: {
        // the member key always goes; an owner may also delete the room key
        const bool owner = row.value(QStringLiteral("owner")).toBool();
        QMessageBox box(QMessageBox::Question, tr("ed3k room"),
                        tr("Leave this room? The room goes on for its other members. To come back you need "
                           "its link and you join as a new member."),
                        QMessageBox::Yes | QMessageBox::No, this);
        box.setDefaultButton(QMessageBox::No);
        const QString keptText = tr("You own this room: its room key stays on this computer, so joining by "
                                    "its link makes you the owner again.");
        const QString dropText = tr("Warning: this deletes the owner key of the room for good. Nobody can "
                                    "change its title, settings or moderators ever again. Moderators can still "
                                    "admit and ban. There is no undo.");
        if (owner) {
            box.setInformativeText(keptText);
            auto* drop = new QCheckBox(tr("Also delete the room key (owner key)"), &box);
            box.setCheckBox(drop);
            connect(drop, &QCheckBox::toggled, &box, [&box, keptText, dropText](bool on) {
                box.setIcon(on ? QMessageBox::Warning : QMessageBox::Question);
                box.setInformativeText(on ? dropText : keptText);
            });
        }
        if (box.exec() != QMessageBox::Yes || !m_ipc)
            return;
        const bool dropRoomKey = owner && box.checkBox() && box.checkBox()->isChecked();
        m_ipc->leaveEd3kRoom(room, dropRoomKey, [self = QPointer<IrcPanel>(this)](const IpcMessage& r) {
            if (self && (!r.isValid() || !r.fieldBool(0)))
                self->ed3kRoomFailed(tr("Leaving the room failed"), r);
        });
        break;
    }
    case A::CopyLink:
        Clipboard::setText(row.value(QStringLiteral("link")).toString());
        appendInfo(m_statusKey, row.value(QStringLiteral("private")).toBool()
                                    ? tr("* Room link copied. It holds the room's secret: give it to members only")
                                    : tr("* Room link copied"));
        break;
    case A::CopyMemberKey:
        Clipboard::setText(row.value(QStringLiteral("memberKey")).toString());
        appendInfo(m_statusKey, tr("* Your member key of this room was copied: send it to a moderator to be admitted"));
        break;
    case A::ImportAdmit: {
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(this, tr("Import admit"),
            tr("Paste the admit text a moderator gave you:"), QString(), &ok);
        if (!ok || text.trimmed().isEmpty() || !m_ipc)
            return;
        m_ipc->importEd3kRoomAdmit(text, [self = QPointer<IrcPanel>(this)](const IpcMessage& r) {
            if (!self)
                return;
            if (!r.isValid() || !r.fieldBool(0))
                self->ed3kRoomFailed(tr("The admit was not taken"), r);
            else
                self->appendInfo(Ed3kRoomTab::keyOf(r.fieldMap(1).value(QStringLiteral("room")).toString()),
                                 tr("* Admit taken in: you can join the room's members now"));
        });
        break;
    }
    case A::AdmitKey:
        admitEd3kMember(room, {});
        break;
    case A::NewGeneration:
        moderateEd3kMember(room, int(Moderation::NewGeneration), {});
        break;
    case A::EditInfo:
        if (Ed3kRoomTab* t = tab ? tab : ensureRoomTab(room))
            editEd3kRoomInfo(t);
        break;
    case A::ChangeNick: {
        bool ok = false;
        const QString nick = QInputDialog::getText(this, tr("ed3k room"), tr("Your nick in this room:"), QLineEdit::Normal,
                                                   row.value(QStringLiteral("nick")).toString(), &ok).trimmed();
        if (!ok || nick.isEmpty() || !m_ipc)
            return;
        m_ipc->setEd3kRoomNick(room, nick.left(64), [self = QPointer<IrcPanel>(this)](const IpcMessage& r) {
            if (self && (!r.isValid() || !r.fieldBool(0)))
                self->ed3kRoomFailed(tr("The nick was not changed"), r);
        });
        break;
    }
    }
}

Ed3kRoomTab* IrcPanel::ensureRoomTab(const QString& room, bool show)
{
    Ed3kRoomTab* tab = roomTab(room);
    if (!tab) {
        QCborMap row;
        if (m_roomList)
            for (const QCborValue& v : m_roomList->rooms())
                if (v.toMap().value(QStringLiteral("room")).toString() == room)
                    row = v.toMap();
        if (row.isEmpty())
            return nullptr;
        // a post: "[sent] <@nick#abcd> text", a reply names its post first
        auto render = [this](const Ed3kRoomTab::Post& p, const QString& shown, bool own, const QString& reply) {
            const QString stamp = thePrefs.ircAddTimestamp()
                ? QStringLiteral("[%1] ").arg(QDateTime::fromSecsSinceEpoch(p.sent).toString(QStringLiteral("HH:mm:ss")))
                : QString();
            // §6: moderators' posts marked: a star by option, else the IRC "@"
            const bool star = p.moderator && thePrefs.chatModStar();
            QString line = messageLine(star ? stamp + Ed3kRoomTab::modMarkHtml() : stamp,
                                       p.moderator && !star ? QStringLiteral("@") + shown : shown, p.text, own);
            if (!reply.isEmpty())
                line = QStringLiteral("<span style=\"color:%1;\">&#8618; %2</span><br>")
                           .arg(IrcRouting::colorName(IrcRouting::LineColor::Notice), reply.toHtmlEscaped())
                     + line;
            return line;
        };
        // a media link is the tab's own (Ed3kRoomMediaView), never one to open
        tab = new Ed3kRoomTab(room, [this](const QString& link) {
            if (Ed3kRoomMediaView::rootOfLink(link).isEmpty())
                emit linkActivated(link);
        }, render, this);
        tab->setMediaLoader([this, tab = QPointer<Ed3kRoomTab>(tab)](const QString& root) {
            if (!m_ipc)
                return;
            m_ipc->getEd3kRoomMedia(root, [tab, root](bool ok, const QString&, const QByteArray& data) {
                if (tab)
                    tab->setMediaBytes(root, ok ? data : QByteArray());
            });
        });
        connect(tab, &Ed3kRoomTab::mediaFetchRequested, this, [this, room](const QString& root) {
            if (m_ipc)
                m_ipc->fetchEd3kRoomMedia(room, root, [self = QPointer<IrcPanel>(this)](const IpcMessage& r) {
                    if (self && (!r.isValid() || !r.fieldBool(0)))
                        self->ed3kRoomFailed(tr("The file was not fetched"), r);
                });
        });
        tab->applyFont(m_customFont);
        connect(tab, &Ed3kRoomTab::fileOpenRequested, this, &IrcPanel::roomFileOpened);
        connect(tab, &Ed3kRoomTab::replyRequested, this, [this, tab = QPointer<Ed3kRoomTab>(tab)](const QString& id) {
            if (tab)
                replyInEd3kRoom(tab, id);
        });
        connect(tab, &Ed3kRoomTab::muteRequested, this, [this, room](const QString& memberId, bool on) {
            muteEd3kMember(room, memberId, on);
        });
        connect(tab, &Ed3kRoomTab::importAdmitRequested, this,
                [this, room] { onEd3kRoomAction(int(Ed3kRoomList::Action::ImportAdmit), room); });
        tab->setRow(row);
        IrcChannel ch;
        ch.name = tab->tabText();
        ch.topic = tab->topicText();
        ch.type = IrcChannel::Ed3kRoom;
        ch.widget = tab;
        ch.log = tab->log();
        ch.topicView = tab->topicView();
        ch.room = tab;
        tab->fillNicks(ch.nicks);
        m_tabWidget->addTab(tab, skinIcon(QStringLiteral(":/icons/IRC.ico")), ch.name);
        m_channels[Ed3kRoomTab::keyOf(room)] = ch;
        m_roomTabsClosed.removeAll(room);
        if (m_ipc)
            m_ipc->requestEd3kRoomPosts(room, [self = QPointer<IrcPanel>(this),
                                               tab = QPointer<Ed3kRoomTab>(tab)](const IpcMessage& r) {
                if (!self || !tab || !r.isValid() || !r.fieldBool(0))
                    return;
                QVector<Ed3kRoomTab::Post> posts;
                for (const QCborValue& v : r.fieldArray(1))
                    posts << Ed3kRoomTab::postFrom(v.toMap());
                tab->setPosts(posts);
                self->noteUnreadRoomPosts(tab);   // posts while the GUI was off: toolbar "!"
            });
    }
    if (show) {
        if (const int idx = findTab(Ed3kRoomTab::keyOf(room)); idx >= 0)
            m_tabWidget->setCurrentIndex(idx);
        m_input->setFocus();
    }
    return tab;
}

Ed3kRoomTab* IrcPanel::roomTab(const QString& room) const
{
    const auto it = m_channels.constFind(Ed3kRoomTab::keyOf(room));
    return it != m_channels.constEnd() ? it->room : nullptr;
}

void IrcPanel::postToRoom(Ed3kRoomTab* tab, const QString& text)
{
    if (!tab || !m_ipc || !m_ipc->isConnected() || text.trimmed().isEmpty())
        return;
    // shown when the daemon's post comes back (PushEd3kRoomPost)
    // was: ToDo: reply to a post (PostEd3kRoom takes replyTo) and a local mute of a member (§6)
    // (done: the post and nick menus, replyInEd3kRoom / muteEd3kMember)
    const QString key = Ed3kRoomTab::keyOf(tab->room());
    m_ipc->postEd3kRoom(tab->room(), text, tab->takeReplyTo(), [self = QPointer<IrcPanel>(this), key](const IpcMessage& r) {
        if (self && (!r.isValid() || !r.fieldBool(0)))
            self->appendInfo(key, tr("* Not posted: %1").arg(errorText(r.fieldMap(1).value(QStringLiteral("error")).toInteger())),
                             IrcRouting::LineColor::Notice);
    });
}

void IrcPanel::onEd3kRoomMedia(const IpcMessage& msg)
{
    if (Ed3kRoomTab* tab = roomTab(msg.fieldString(0)))
        tab->setMediaState(msg.fieldString(1).toLower(), int(msg.fieldInt(2)));
}

void IrcPanel::updateAttachButton()
{
    if (!m_attachBtn)   // setupUi's first addTab fires currentChanged before the button exists
        return;
    const IrcChannel* ch = activeChannel();
    m_attachBtn->setVisible(ch && ch->room && ch->room->mediaMax() > 0);
}

void IrcPanel::attachToRoom()
{
    IrcChannel* ch = activeChannel();
    if (!ch || !ch->room)
        return;
    const bool files = ch->room->takesFiles();
    QString filter = tr("Images and videos (*.png *.jpg *.jpeg *.gif *.webp *.mp4 *.webm)");
    if (files)
        filter = tr("Images, videos and small files (*.png *.jpg *.jpeg *.gif *.webp *.mp4 *.webm *.txt *.nzb *.dlc *.torrent)") + QStringLiteral(";;")
               + filter + QStringLiteral(";;") + tr("Text, NZB, DLC and torrent files (*.txt *.nzb *.dlc *.torrent)");
    const QString path = QFileDialog::getOpenFileName(this, files ? tr("Send a file") : tr("Send an image or video"), QString(), filter);
    if (path.isEmpty())
        return;
    QFile f(path);
    if (f.size() > ch->room->mediaMax()) {
        QMessageBox::information(this, tr("ed3k room"), tr("This room takes files up to %1.")
                                     .arg(QLocale().formattedDataSize(ch->room->mediaMax(), 1, QLocale::DataSizeTraditionalFormat)));
        return;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("ed3k room"), tr("The file could not be read."));
        return;
    }
    postMediaToRoom(ch->room, f.readAll(), QFileInfo(path).fileName());
}

bool IrcPanel::postMediaToRoom(Ed3kRoomTab* tab, const QByteArray& data, const QString& name)
{
    if (!tab || !m_ipc || !m_ipc->isConnected())
        return false;
    // by the bytes (room.md §6): only what members show
    const QString type = MediaSniff::type(data);
    if (type.isEmpty() && !(tab->takesFiles() && MediaSniff::fileKind(data, name) != MediaSniff::FileKind::None)) {
        QMessageBox::information(this, tr("ed3k room"),
                                 tab->takesFiles() ? tr("Only PNG, JPEG, GIF and WebP images, MP4 and WebM videos and text, NZB, DLC and torrent files can be sent.")
                                                   : tr("Only PNG, JPEG, GIF and WebP images and MP4 and WebM videos can be sent."));
        return false;
    }
    if (data.size() > tab->mediaMax()) {
        QMessageBox::information(this, tr("ed3k room"), tr("This room takes files up to %1.")
                                     .arg(QLocale().formattedDataSize(tab->mediaMax(), 1, QLocale::DataSizeTraditionalFormat)));
        return false;
    }
    QSize size;
    if (MediaSniff::isImage(type)) {
        QBuffer buf;
        buf.setData(data);
        buf.open(QIODevice::ReadOnly);
        size = QImageReader(&buf).size();
    }
    // the line is the caption (§2.6: text stays required; empty: the daemon takes the name)
    const QString caption = m_input->text().trimmed();
    m_input->clear();
    const QString key = Ed3kRoomTab::keyOf(tab->room());
    appendInfo(key, tr("* Sending %1...").arg(name), IrcRouting::LineColor::Notice);
    m_ipc->postEd3kRoomMedia(tab->room(), caption, tab->takeReplyTo(), data, name, size.isValid() ? size.width() : 0,
                             size.isValid() ? size.height() : 0, [self = QPointer<IrcPanel>(this), key](const IpcMessage& r) {
                                 if (self && (!r.isValid() || !r.fieldBool(0)))
                                     self->appendInfo(key, tr("* Not posted: %1").arg(errorText(r.fieldMap(1).value(QStringLiteral("error")).toInteger())),
                                                      IrcRouting::LineColor::Notice);
                             });
    return true;
}

void IrcPanel::ed3kNickMenu(Ed3kRoomTab* tab, const QString& shown, const QPoint& globalPos)
{
    if (!tab)
        return;
    QMenu menu(this);
    fillEd3kNickMenu(menu, tab, shown);
    menu.exec(globalPos);
}

void IrcPanel::dumpEd3kNickMenus()
{
    // dev aid (EMULE_DUMP_ROOM_MENUS=1): every listed member's menu in the log, no clicking
    for (const IrcChannel& ch : std::as_const(m_channels)) {
        if (!ch.room)
            continue;
        for (const QString& shown : ch.nicks.display()) {
            QMenu menu;
            fillEd3kNickMenu(menu, ch.room, shown);
            QStringList items;
            for (const QAction* a : menu.actions())
                if (!a->isSeparator())
                    items << (a->isEnabled() ? a->text() : QStringLiteral("(%1)").arg(a->text()));
            logInfo(QStringLiteral("room menu %1 [%2%3%4] %5: %6")
                        .arg(ch.room->row().value(QStringLiteral("title")).toString(),
                             ch.room->row().value(QStringLiteral("closed")).toBool() ? QStringLiteral("closed ") : QString(),
                             ch.room->row().value(QStringLiteral("moderated")).toBool() ? QStringLiteral("moderated ") : QString(),
                             ch.room->isOwner() ? QStringLiteral("owner") : ch.room->canModerate() ? QStringLiteral("mod") : QStringLiteral("member"),
                             shown, items.join(QStringLiteral(" | "))));
        }
    }
}

void IrcPanel::selfTestRoomFiles(const QString& dir)
{
    // dev aid (EMULE_ROOM_FILE_SELFTEST=<dir>): clicks every room file through the real path,
    // grabs each window that opens to <dir>, says Yes to questions, closes the rest unchanged;
    // an owner opens Room info once and turns `files` on with the checkbox
    QDir().mkpath(dir);
    struct State {
        QSet<QString> clicked;            // root + state
        int shot = 0;
        bool formDone = false;
    };
    auto st = std::make_shared<State>();
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this, dir, st] {
        const auto grab = [&](QWidget* w, const QString& what) {
            const QString file = QDir(dir).filePath(QStringLiteral("%1-%2.png").arg(++st->shot, 2, 10, QLatin1Char('0')).arg(what));
            w->grab().save(file);
            logInfo(QStringLiteral("selftest shot %1").arg(file));
        };
        const auto safe = [](QString s) { return s.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9.]+")), QStringLiteral("_")); };
        for (QWidget* w : QApplication::topLevelWidgets()) {
            auto* dlg = qobject_cast<QDialog*>(w);
            // a property, not the address: a new box may take a closed one's
            if (!dlg || !dlg->isVisible() || dlg->property("selftestSeen").toBool())
                continue;
            dlg->setProperty("selftestSeen", true);
            if (auto* box = qobject_cast<QMessageBox*>(dlg)) {
                logInfo(QStringLiteral("selftest box \"%1\": %2").arg(box->windowTitle(), box->text()));
                grab(box, QStringLiteral("box"));
                QAbstractButton* yes = box->button(QMessageBox::Yes);
                (yes ? yes : box->defaultButton() ? box->defaultButton() : box->buttons().value(0))->click();
                continue;
            }
            QStringList texts;
            for (auto* e : dlg->findChildren<QPlainTextEdit*>())
                texts << e->toPlainText().left(60).simplified();
            for (auto* c : dlg->findChildren<QCheckBox*>())
                texts << QStringLiteral("[%1%2] %3").arg(c->isChecked() ? u"x" : u" ", c->isEnabled() ? u"" : u" disabled", c->text());
            logInfo(QStringLiteral("selftest dialog \"%1\" (%2): %3").arg(dlg->windowTitle(), QString::fromLatin1(dlg->metaObject()->className()), texts.join(QStringLiteral(" | "))));
            grab(dlg, safe(dlg->windowTitle()));
            if (dlg->findChild<QCheckBox*>(QStringLiteral("roomFiles"))) {
                auto* media = dlg->findChild<QCheckBox*>(QStringLiteral("roomMedia"));
                auto* files = dlg->findChild<QCheckBox*>(QStringLiteral("roomFiles"));
                if (!media || !files) {
                    dlg->reject();
                    continue;
                }
                media->setChecked(false);
                logInfo(QStringLiteral("selftest form media off: files enabled %1").arg(files->isEnabled()));
                grab(dlg, QStringLiteral("form_media_off"));
                media->setChecked(true);
                files->setChecked(true);
                logInfo(QStringLiteral("selftest form media on: files enabled %1 checked %2").arg(argStr(files->isEnabled()), argStr(files->isChecked())));
                grab(dlg, QStringLiteral("form_files_on"));
                dlg->accept();
                continue;
            }
            dlg->reject();
        }
        // one window at a time
        if (std::ranges::any_of(QApplication::topLevelWidgets(), [](const QWidget* w) { return qobject_cast<const QDialog*>(w) && w->isVisible(); }))
            return;
        for (const IrcChannel& ch : std::as_const(m_channels)) {
            Ed3kRoomTab* tab = ch.room;
            if (!tab)
                continue;
            if (tab->isOwner() && !st->formDone) {
                st->formDone = true;
                logInfo(QStringLiteral("selftest room info, files now %1").arg(tab->takesFiles()));
                QTimer::singleShot(0, this, [this, tab = QPointer<Ed3kRoomTab>(tab)] {
                    if (tab)
                        editEd3kRoomInfo(tab);
                });
                QTimer::singleShot(15'000, this, [tab = QPointer<Ed3kRoomTab>(tab)] {
                    if (tab)
                        logInfo(QStringLiteral("selftest room info after, files %1").arg(tab->takesFiles()));
                });
                return;
            }
            for (const Ed3kRoomTab::Post& p : tab->posts()) {
                const QString& root = p.media.root;
                if (root.isEmpty() || MediaSniff::isMediaType(p.media.type))
                    continue;
                const int state = tab->mediaState(root);
                if (state == Ed3kRoomMediaView::Fetching || st->clicked.contains(root + QString::number(state)))
                    continue;
                st->clicked.insert(root + QString::number(state));
                logInfo(QStringLiteral("selftest click %1 state %2").arg(argStr(p.media.name), argStr(state)));
                // queued: the question box runs its own loop
                QTimer::singleShot(0, this, [tab = QPointer<Ed3kRoomTab>(tab), root] {
                    if (tab)
                        tab->clickMedia(root);
                });
                return;
            }
        }
    });
    timer->start(1500);
}

void IrcPanel::fillEd3kNickMenu(QMenu& menu, Ed3kRoomTab* tab, const QString& shown)
{
    const QString member = tab->keyOfShown(shown);
    const QString room = tab->room();
    const bool self = member == tab->memberKey();
    QAction* title = menu.addAction(skinIcon(QStringLiteral(":/icons/User.ico")), tr("Nick : %1").arg(IrcNickList::bare(shown, IrcServerModes{})));
    title->setEnabled(false);
    menu.addSeparator();
    menu.addAction(Ed3kRoomList::actionIcon(Ed3kRoomList::Action::CopyMemberKey), tr("Copy member key"), this, [member] { Clipboard::setText(member); })->setEnabled(!member.isEmpty());
    // §2.5: answer the member's newest post; §6: mute it here alone
    const Ed3kRoomTab::Post* last = member.isEmpty() ? nullptr : tab->lastPostOf(member);
    menu.addAction(skinIcon(QStringLiteral(":/icons/UserMessage.ico")), tr("Reply to last post"), this, [this, tab = QPointer<Ed3kRoomTab>(tab), id = last ? last->id : QString()] {
        if (tab)
            replyInEd3kRoom(tab, id);
    })->setEnabled(last != nullptr);
    if (const QString memberId = Ed3kRoomTab::memberIdOf(member); !self && !memberId.isEmpty()) {
        const bool muted = tab->isMuted(memberId);
        menu.addAction(skinIcon(QStringLiteral(":/icons/Spam.ico")), muted ? tr("Unmute") : tr("Mute"), this,
                       [this, room, memberId, muted] { muteEd3kMember(room, memberId, !muted); });
    }
    // #17: only for a room known closed or moderated, or one that refused us
    // was: if (self && !tab->canModerate())
    if (self && !tab->canModerate() && tab->admitImportable()) {
        menu.addAction(Ed3kRoomList::actionIcon(Ed3kRoomList::Action::ImportAdmit), tr("Import admit..."), this, [this, room] { onEd3kRoomAction(int(Ed3kRoomList::Action::ImportAdmit), room); });
    }
    if (tab->canModerate() && !member.isEmpty() && !self) {
        // only what changes something: an admit in a closed or moderated room for a key not
        // admitted yet (§2.2); a moderator banned only with the room key (§2.3). A banned
        // member is never listed (§2.4): lifting is in "Banned members..."
        const bool memberIsMod = tab->isModerator(member);
        const bool canAdmit = tab->admitsMatter() && !memberIsMod && !tab->isAdmitted(member);
        const bool canBan = !memberIsMod || tab->isOwner();
        if (canAdmit || canBan)
            menu.addSeparator();
        if (canAdmit)
            menu.addAction(skinIcon(QStringLiteral(":/icons/UserAdd.ico")), tr("Admit"), this, [this, room, member] { admitEd3kMember(room, member); });
        if (canBan)
            menu.addAction(skinIcon(QStringLiteral(":/icons/UserDelete.ico")), tr("Ban (30 days)"), this, [this, room, member] { moderateEd3kMember(room, int(Moderation::Ban), member); });
        // was: menu.addAction(.. tr("Lift ban") ..) for every listed member
    }
    if (tab->isOwner() && !member.isEmpty()) {
        const bool mod = tab->isModerator(member);
        menu.addAction(skinIcon(QStringLiteral(":/icons/FriendSlot.ico")), mod ? tr("Remove moderator") : tr("Make moderator"), this,
                       [this, tab = QPointer<Ed3kRoomTab>(tab), member, mod] {
                           if (tab)
                               setEd3kModerator(tab, member, !mod);
                       });
    }
    if (tab->canModerate()) {
        menu.addSeparator();
        if (tab->admitsMatter())
            menu.addAction(Ed3kRoomList::actionIcon(Ed3kRoomList::Action::AdmitKey), tr("Admit a member key..."), this, [this, room] { admitEd3kMember(room, {}); });
        if (tab->row().value(QStringLiteral("closed")).toBool())
            menu.addAction(Ed3kRoomList::actionIcon(Ed3kRoomList::Action::NewGeneration), tr("New generation"), this, [this, room] { moderateEd3kMember(room, int(Moderation::NewGeneration), {}); });
        menu.addAction(skinIcon(QStringLiteral(":/icons/Restore.ico")), tr("Banned members..."), this,
                       [this, tab = QPointer<Ed3kRoomTab>(tab)] { showEd3kBans(tab); })
            ->setEnabled(!tab->row().value(QStringLiteral("bans")).toArray().isEmpty());
    }
}

void IrcPanel::createEd3kRoom()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    RoomForm start;
    start.nick = ed3kNick();
    start.mediaMax = qint64(kMediaDefaultMiB * (1 << 20));
    const auto form = askRoomForm(this, tr("Create an ed3k room"), start, true);
    if (!form)
        return;
    QString nick = form->nick.isEmpty() ? ed3kNick() : form->nick;
    if (nick.isEmpty() && (nick = askIrcNick({})).isEmpty())
        return;
    m_ipc->createEd3kRoom(form->title, form->about, form->flags, nick,
                          [self = QPointer<IrcPanel>(this), mediaMax = form->mediaMax, files = form->files,
                           days = form->keepDays](const IpcMessage& r) {
                              if (!self)
                                  return;
                              if (!r.isValid() || !r.fieldBool(0)) {
                                  self->ed3kRoomFailed(tr("The room was not created"), r);
                                  return;
                              }
                              const QString room = r.fieldMap(1).value(QStringLiteral("room")).toString();
                              // a second room info with the limits (§2.1)
                              if (mediaMax > 0 || days > 1)
                                  self->m_ipc->setEd3kRoomLimits(room, mediaMax, days * 24, keepPostsOf(days), files, [self](const IpcMessage& l) {
                                      if (self && (!l.isValid() || !l.fieldBool(0)))
                                          self->ed3kRoomFailed(tr("The media and history limits were not set"), l);
                                  });
                              // the list comes with PushEd3kRoomsChanged; then the tab opens
                              self->m_roomTabsClosed.removeAll(room);
                              self->appendInfo(self->m_statusKey,
                                               tr("* ed3k room created. Copy its link from the Rooms (ed3k) tab to invite members"));
                          });
}

void IrcPanel::joinEd3kRoom(const QString& link)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    QString l = link;
    if (l.isEmpty()) {
        bool ok = false;
        l = QInputDialog::getText(this, tr("Join an ed3k room"), tr("Room link (ed3k:room?...):"), QLineEdit::Normal,
                                  Clipboard::firstLink(QLatin1StringView("ed3k:room?")), &ok).trimmed();
        if (!ok || l.isEmpty())
            return;
    }
    if (!l.startsWith(QLatin1String("ed3k:room?"), Qt::CaseInsensitive)) {
        appendInfo(m_statusKey, tr("* Not an ed3k room link"), IrcRouting::LineColor::Notice);
        return;
    }
    QString nick = ed3kNick();
    if (nick.isEmpty() && (nick = askIrcNick({})).isEmpty())
        return;
    m_ipc->joinEd3kRoom(l, nick, [self = QPointer<IrcPanel>(this)](const IpcMessage& r) {
        if (!self)
            return;
        if (!r.isValid() || !r.fieldBool(0)) {
            self->ed3kRoomFailed(tr("The room was not joined"), r);
            return;
        }
        const QString room = r.fieldMap(1).value(QStringLiteral("room")).toString();
        self->m_roomTabsClosed.removeAll(room);
        // the row comes with the list; then the tab shows
        self->m_ipc->requestEd3kRooms(self, [self, room](const IpcMessage& list) {
            if (!self)
                return;
            self->onEd3kRooms(list);
            self->ensureRoomTab(room, true);
        });
    });
}

void IrcPanel::editEd3kRoomInfo(Ed3kRoomTab* tab)
{
    if (!m_ipc || !tab)
        return;
    const QCborMap row = tab->row();
    RoomForm start;
    start.title = row.value(QStringLiteral("title")).toString();
    start.about = row.value(QStringLiteral("about")).toString();
    start.flags = (row.value(QStringLiteral("private")).toBool() ? kRoomPrivate : 0)
                | (row.value(QStringLiteral("closed")).toBool() ? kRoomClosed : 0)
                | (row.value(QStringLiteral("moderated")).toBool() ? kRoomModerated : 0);
    start.mediaMax = row.value(QStringLiteral("mediaMax")).toInteger();
    start.files = row.value(QStringLiteral("files")).toBool();
    start.keepDays = int(std::max<qint64>(1, row.value(QStringLiteral("keepHours")).toInteger() / 24));
    const auto form = askRoomForm(this, tr("Room info"), start, false);
    if (!form)
        return;
    if (form->mediaMax != start.mediaMax || form->files != start.files || form->keepDays != start.keepDays)
        m_ipc->setEd3kRoomLimits(tab->room(), form->mediaMax, form->keepDays * 24, keepPostsOf(form->keepDays), form->files,
                                 [self = QPointer<IrcPanel>(this)](const IpcMessage& r) {
                                     if (self && (!r.isValid() || !r.fieldBool(0)))
                                         self->ed3kRoomFailed(tr("The media and history limits were not changed"), r);
                                 });
    m_ipc->setEd3kRoomInfo(tab->room(), form->title, form->about, form->flags & (kRoomClosed | kRoomModerated), tab->mods(),
                           [self = QPointer<IrcPanel>(this)](const IpcMessage& r) {
                               if (self && (!r.isValid() || !r.fieldBool(0)))
                                   self->ed3kRoomFailed(tr("The room info was not changed"), r);
                           });
}

void IrcPanel::admitEd3kMember(const QString& room, const QString& memberKey)
{
    if (!m_ipc)
        return;
    QString key = memberKey;
    if (key.isEmpty()) {
        bool ok = false;
        key = QInputDialog::getText(this, tr("Admit a member"),
                                    tr("The newcomer's member key (52 characters):"), QLineEdit::Normal, QString(), &ok)
                  .trimmed();
        if (!ok || key.isEmpty())
            return;
    }
    // reading 1 of batch 10: the admit goes back out of band, as text
    m_ipc->moderateEd3kRoom(room, int(Moderation::Admit), key, 0, [self = QPointer<IrcPanel>(this)](const IpcMessage& r) {
        if (!self)
            return;
        if (!r.isValid() || !r.fieldBool(0)) {
            self->ed3kRoomFailed(tr("Not admitted"), r);
            return;
        }
        const QString text = r.fieldMap(1).value(QStringLiteral("admit")).toString();
        Clipboard::setText(text);
        QMessageBox box(QMessageBox::Information, tr("Admit"),
                        tr("The admit is in the clipboard. Hand it to the newcomer, who imports it "
                           "(Rooms (ed3k) tab, Import admit)."),
                        QMessageBox::Ok, self);
        box.setDetailedText(text);
        box.exec();
    });
}

void IrcPanel::moderateEd3kMember(const QString& room, int action, const QString& memberKey)
{
    if (!m_ipc)
        return;
    const qint64 until = action == int(Moderation::Ban) ? QDateTime::currentSecsSinceEpoch() + kBanSeconds : 0;
    m_ipc->moderateEd3kRoom(room, action, memberKey, until,
                            [self = QPointer<IrcPanel>(this), room, action](const IpcMessage& r) {
                                if (!self)
                                    return;
                                if (!r.isValid() || !r.fieldBool(0)) {
                                    self->ed3kRoomFailed(tr("The moderation failed"), r);
                                    return;
                                }
                                if (action == int(Moderation::NewGeneration))
                                    self->appendInfo(Ed3kRoomTab::keyOf(room), tr("* New generation %1 started")
                                        .arg(r.fieldMap(1).value(QStringLiteral("generation")).toInteger()));
                                else if (action == int(Moderation::Lift))
                                    self->appendInfo(Ed3kRoomTab::keyOf(room), tr("* Ban lifted"));
                            });
}

void IrcPanel::setEd3kModerator(Ed3kRoomTab* tab, const QString& memberKey, bool on)
{
    if (!m_ipc || !tab)
        return;
    QStringList mods = tab->mods();
    if (on && !mods.contains(memberKey))
        mods << memberKey;
    if (!on)
        mods.removeAll(memberKey);
    const QCborMap row = tab->row();
    const int flags = (row.value(QStringLiteral("closed")).toBool() ? kRoomClosed : 0)
                    | (row.value(QStringLiteral("moderated")).toBool() ? kRoomModerated : 0);
    m_ipc->setEd3kRoomInfo(tab->room(), row.value(QStringLiteral("title")).toString(),
                           row.value(QStringLiteral("about")).toString(), flags, mods,
                           [self = QPointer<IrcPanel>(this), room = tab->room(), memberKey,
                            keep = !on && memberKey != tab->memberKey() && tab->admitsMatter()](const IpcMessage& r) {
                               if (!self)
                                   return;
                               if (!r.isValid() || !r.fieldBool(0)) {
                                   self->ed3kRoomFailed(tr("The moderators were not changed"), r);
                                   return;
                               }
                               // §2.1: a dropped moderator of a closed or moderated room needs an
                               // admit to stay a member; it is kept (our own key: the core admits it)
                               if (keep)
                                   self->m_ipc->moderateEd3kRoom(room, int(Moderation::Admit), memberKey, 0, [](const IpcMessage&) {});
                           });
}

void IrcPanel::showEd3kBans(Ed3kRoomTab* tab)
{
    if (!tab || !m_ipc)
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Banned members - %1").arg(tab->title()));
    auto* layout = new QVBoxLayout(&dlg);
    auto* list = new QTreeWidget(&dlg);
    list->setHeaderLabels({tr("Member"), tr("Banned until")});
    list->setRootIsDecorated(false);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // a banned member is gone from the list (§2.4): its nick from its posts, if any were held
    for (const QCborValue& v : tab->row().value(QStringLiteral("bans")).toArray()) {
        const QCborMap b = v.toMap();
        const QString key = b.value(QStringLiteral("key")).toString();
        const Ed3kRoomTab::Post* last = tab->lastPostOf(key);
        auto* item = new QTreeWidgetItem(list);
        item->setText(0, QStringLiteral("%1#%2").arg(last ? last->nick : QStringLiteral("?"),
                                                    b.value(QStringLiteral("id")).toString().left(4)));
        item->setToolTip(0, key);
        item->setText(1, QLocale().toString(QDateTime::fromSecsSinceEpoch(b.value(QStringLiteral("until")).toInteger()),
                                            QLocale::ShortFormat));
        item->setData(0, Qt::UserRole, key);
    }
    list->resizeColumnToContents(0);
    layout->addWidget(list);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    auto* lift = buttons->addButton(tr("Lift ban"), QDialogButtonBox::ActionRole);
    lift->setEnabled(false);
    connect(list, &QTreeWidget::itemSelectionChanged, lift, [list, lift] { lift->setEnabled(!list->selectedItems().isEmpty()); });
    connect(lift, &QPushButton::clicked, &dlg, [this, list, room = tab->room()] {
        for (QTreeWidgetItem* item : list->selectedItems()) {
            moderateEd3kMember(room, int(Moderation::Lift), item->data(0, Qt::UserRole).toString());
            delete item;
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    dlg.resize(420, 300);
    dlg.exec();
}

void IrcPanel::replyInEd3kRoom(Ed3kRoomTab* tab, const QString& postId)
{
    const Ed3kRoomTab::Post* p = tab ? tab->post(postId) : nullptr;
    if (!p)
        return;
    tab->setReplyTo(postId);
    QString quote = p->text.simplified();
    if (quote.size() > 60)
        quote = quote.left(60) + QStringLiteral("…");
    appendInfo(Ed3kRoomTab::keyOf(tab->room()),
               tr("* Your next post answers %1: %2").arg(Ed3kRoomTab::shownName(p->nick, p->author), quote));
    m_input->setFocus();
}

void IrcPanel::muteEd3kMember(const QString& room, const QString& memberId, bool on)
{
    if (!m_ipc)
        return;
    m_ipc->muteEd3kRoomMember(room, memberId, on, [self = QPointer<IrcPanel>(this), room, on](const IpcMessage& r) {
        if (!self)
            return;
        if (!r.isValid() || !r.fieldBool(0)) {
            self->ed3kRoomFailed(on ? tr("Not muted") : tr("Not unmuted"), r);
            return;
        }
        self->appendInfo(Ed3kRoomTab::keyOf(room), on ? tr("* Member muted: its posts are hidden here")
                                                      : tr("* Member unmuted"));
    });
}

void IrcPanel::ed3kRoomFailed(const QString& what, const IpcMessage& reply)
{
    const QString why = reply.isValid() ? errorText(reply.fieldMap(1).value(QStringLiteral("error")).toInteger())
                                        : tr("no core connection");
    appendInfo(m_statusKey, QStringLiteral("* %1: %2").arg(what, why), IrcRouting::LineColor::Notice);
    QMessageBox::warning(this, tr("ed3k room"), QStringLiteral("%1: %2").arg(what, why));
}

QString IrcPanel::ed3kNick() const
{
    const QString irc = thePrefs.ircNick();
    if (!irc.isEmpty())
        return irc;
    // the default eMule nick is the website URL: no name in a room
    const QString nick = thePrefs.nick().trimmed();
    if (nick == Preferences::defaultNick() || Preferences::isMfcDefaultNick(nick) || nick.contains(QLatin1String("://")))
        return {};
    return nick;
}

void IrcPanel::noteUnreadRoomPosts(Ed3kRoomTab* tab)
{
    const QString key = Ed3kRoomTab::keyOf(tab->room());
    const qint64 readUpTo = theUiState.roomReadUpTo(tab->room());
    if (readUpTo <= 0) {
        // first sight of the room (joined, or no mark from older builds): all held is read
        const qint64 newest = tab->posts().isEmpty() ? 0 : tab->posts().last().sent;
        theUiState.setRoomReadUpTo(tab->room(), std::max(newest, QDateTime::currentSecsSinceEpoch()));
        return;
    }
    const bool unread = std::ranges::any_of(tab->posts(), [&](const Ed3kRoomTab::Post& p) {
        return p.sent > readUpTo && countsAsUnread(tab, p);
    });
    // no sound: these came while the GUI was off
    if (!unread || markUnread(key))
        rememberRoomRead(key);
}

void IrcPanel::rememberRoomRead(const QString& key)
{
    const auto it = m_channels.constFind(key);
    if (it == m_channels.constEnd() || !it->room || it->room->posts().isEmpty())
        return;
    const qint64 newest = it->room->posts().last().sent;
    if (newest > theUiState.roomReadUpTo(it->room->room()))
        theUiState.setRoomReadUpTo(it->room->room(), newest);
}

} // namespace eMule
