#include "pch.h"
/// @file IrcPanelEd3k.cpp
/// @brief The IRC panel's ed3k rooms (room.md): the "Rooms (ed3k)" tab, a tab per room, the
/// nick list of its members and the moderator's actions. IPC 1120-1129 / 1160-1164.
///
/// A room link with `s` is a credential (§1.3): never logged, never in an error line.

#include "panels/IrcPanel.h"

#include "app/IpcClient.h"
#include "controls/LogTextView.h"
#include "panels/Ed3kRoomList.h"
#include "panels/Ed3kRoomTab.h"
#include "IpcMessage.h"
#include "prefs/Preferences.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
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

namespace eMule {

using Ipc::IpcMessage;

namespace {

const QString kRoomListKey = QStringLiteral("__ed3krooms__");
constexpr qint64 kBanSeconds = 30ll * 24 * 3600;   // room.md §2.3: at most 30 days

// ED3K_ROOM_* of ed3k.h (the GUI does not include it)
constexpr int kRoomPrivate = 1;
constexpr int kRoomClosed = 2;
constexpr int kRoomModerated = 4;

// Ed3kRooms::Notice and ::Moderation (daemon side)
enum class Notice { Joined = 0, Left, Neighbour, Banned, Generation, KeyMissing, WrapBad, Refused, Fault };
enum class Moderation { Admit = 0, Ban = 1, Lift = 2, NewGeneration = 3 };

/// The room form: a new room, or the owner's room info.
struct RoomForm {
    QString title;
    QString about;
    QString nick;
    int flags = 0;
};

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

} // namespace

void IrcPanel::setIpcClient(IpcClient* ipc)
{
    m_ipc = ipc;
    connectEd3kRooms();
}

void IrcPanel::openEd3kRoomLink(const QString& link)
{
    // the link may carry s: the dialog names the room, never shows the secret
    const auto answer = QMessageBox::question(this, tr("ed3k room"),
        tr("Join the ed3k room \"%1\"?\n\nOther members see your nick and posts; your node takes "
           "part in passing the room's posts on.").arg(linkName(link)));
    if (answer == QMessageBox::Yes)
        joinEd3kRoom(link);
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
                                               QIcon(QStringLiteral(":/icons/IRC.ico")), ch.name);
        m_tabWidget->tabBar()->setTabButton(idx, QTabBar::RightSide, nullptr);
        m_tabWidget->tabBar()->setTabButton(idx, QTabBar::LeftSide, nullptr);
        m_channels[kRoomListKey] = ch;
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
        if (it->type == IrcChannel::Ed3kRoom && it->room && !present.contains(it->room->room()))
            gone << it.key();
    for (const QString& k : gone)
        removeChannelTab(k);
    m_roomTabsClosed.removeIf([&](const QString& r) { return !present.contains(r); });
    updateNickList();
}

void IrcPanel::onEd3kRoomPost(const IpcMessage& msg)
{
    const QString room = msg.fieldString(0);
    Ed3kRoomTab* tab = roomTab(room);
    if (!tab)
        return;   // a closed tab: opening it fetches the posts
    if (tab->addPost(Ed3kRoomTab::postFrom(msg.fieldMap(1))))
        markActivity(Ed3kRoomTab::keyOf(room));
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
        appendInfo(key, tr("* Now in the room"));
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
    case A::Create:
        createEd3kRoom();
        break;
    case A::Leave: {
        const QString text = row.value(QStringLiteral("owner")).toBool()
            ? tr("Leave this room? You own it: its room key stays on this computer, so joining by its link "
                 "makes you the owner again.")
            : tr("Leave this room? To come back you need its link.");
        if (QMessageBox::question(this, tr("ed3k room"), text) != QMessageBox::Yes || !m_ipc)
            return;
        m_ipc->leaveEd3kRoom(room, [self = QPointer<IrcPanel>(this)](const IpcMessage& r) {
            if (self && (!r.isValid() || !r.fieldBool(0)))
                self->ed3kRoomFailed(tr("Leaving the room failed"), r);
        });
        break;
    }
    case A::CopyLink:
        QApplication::clipboard()->setText(row.value(QStringLiteral("link")).toString());
        appendInfo(m_statusKey, row.value(QStringLiteral("private")).toBool()
                                    ? tr("* Room link copied. It holds the room's secret: give it to members only")
                                    : tr("* Room link copied"));
        break;
    case A::CopyMemberKey:
        QApplication::clipboard()->setText(row.value(QStringLiteral("memberKey")).toString());
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
            QString line = messageLine(stamp, shown, p.text, own);
            if (!reply.isEmpty())
                line = QStringLiteral("<span style=\"color:%1;\">&#8618; %2</span><br>")
                           .arg(IrcRouting::colorName(IrcRouting::LineColor::Notice), reply.toHtmlEscaped())
                     + line;
            return line;
        };
        tab = new Ed3kRoomTab(room, [this](const QString& link) { emit linkActivated(link); }, render, this);
        tab->applyFont(m_customFont);
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
        m_tabWidget->addTab(tab, QIcon(QStringLiteral(":/icons/IRC.ico")), ch.name);
        m_channels[Ed3kRoomTab::keyOf(room)] = ch;
        m_roomTabsClosed.removeAll(room);
        if (m_ipc)
            m_ipc->requestEd3kRoomPosts(room, [tab = QPointer<Ed3kRoomTab>(tab)](const IpcMessage& r) {
                if (!tab || !r.isValid() || !r.fieldBool(0))
                    return;
                QVector<Ed3kRoomTab::Post> posts;
                for (const QCborValue& v : r.fieldArray(1))
                    posts << Ed3kRoomTab::postFrom(v.toMap());
                tab->setPosts(posts);
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

void IrcPanel::ed3kNickMenu(Ed3kRoomTab* tab, const QString& shown, const QPoint& globalPos)
{
    if (!tab)
        return;
    const QString member = tab->keyOfShown(shown);
    const QString room = tab->room();
    const bool self = member == tab->memberKey();
    QMenu menu(this);
    QAction* title = menu.addAction(tr("Nick : %1").arg(IrcNickList::bare(shown, IrcServerModes{})));
    title->setEnabled(false);
    menu.addSeparator();
    menu.addAction(tr("Copy member key"), this, [member] { QApplication::clipboard()->setText(member); })->setEnabled(!member.isEmpty());
    // §2.5: answer the member's newest post; §6: mute it here alone
    const Ed3kRoomTab::Post* last = member.isEmpty() ? nullptr : tab->lastPostOf(member);
    menu.addAction(tr("Reply to last post"), this, [this, tab = QPointer<Ed3kRoomTab>(tab), id = last ? last->id : QString()] {
        if (tab)
            replyInEd3kRoom(tab, id);
    })->setEnabled(last != nullptr);
    if (const QString memberId = Ed3kRoomTab::memberIdOf(member); !self && !memberId.isEmpty()) {
        const bool muted = tab->isMuted(memberId);
        menu.addAction(muted ? tr("Unmute") : tr("Mute"), this,
                       [this, room, memberId, muted] { muteEd3kMember(room, memberId, !muted); });
    }
    // #17: only for a room known closed or moderated, or one that refused us
    // was: if (self && !tab->canModerate())
    if (self && !tab->canModerate() && tab->admitImportable()) {
        menu.addAction(tr("Import admit..."), this, [this, room] { onEd3kRoomAction(int(Ed3kRoomList::Action::ImportAdmit), room); });
    }
    if (tab->canModerate() && !member.isEmpty() && !self) {
        menu.addSeparator();
        menu.addAction(tr("Admit"), this, [this, room, member] { admitEd3kMember(room, member); });
        menu.addAction(tr("Ban (30 days)"), this, [this, room, member] { moderateEd3kMember(room, int(Moderation::Ban), member); });
        menu.addAction(tr("Lift ban"), this, [this, room, member] { moderateEd3kMember(room, int(Moderation::Lift), member); });
    }
    if (tab->isOwner() && !member.isEmpty()) {
        const bool mod = tab->isModerator(member);
        menu.addAction(mod ? tr("Remove moderator") : tr("Make moderator"), this,
                       [this, tab = QPointer<Ed3kRoomTab>(tab), member, mod] {
                           if (tab)
                               setEd3kModerator(tab, member, !mod);
                       });
    }
    if (tab->canModerate()) {
        menu.addSeparator();
        menu.addAction(tr("Admit a member key..."), this, [this, room] { admitEd3kMember(room, {}); });
        menu.addAction(tr("New generation"), this, [this, room] { moderateEd3kMember(room, int(Moderation::NewGeneration), {}); })
            ->setEnabled(tab->row().value(QStringLiteral("closed")).toBool());
    }
    menu.exec(globalPos);
}

void IrcPanel::createEd3kRoom()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    RoomForm start;
    start.nick = ed3kNick();
    const auto form = askRoomForm(this, tr("Create an ed3k room"), start, true);
    if (!form)
        return;
    m_ipc->createEd3kRoom(form->title, form->about, form->flags, form->nick.isEmpty() ? ed3kNick() : form->nick,
                          [self = QPointer<IrcPanel>(this)](const IpcMessage& r) {
                              if (!self)
                                  return;
                              if (!r.isValid() || !r.fieldBool(0)) {
                                  self->ed3kRoomFailed(tr("The room was not created"), r);
                                  return;
                              }
                              const QString room = r.fieldMap(1).value(QStringLiteral("room")).toString();
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
                                  QString(), &ok).trimmed();
        if (!ok || l.isEmpty())
            return;
    }
    if (!l.startsWith(QLatin1String("ed3k:room?"), Qt::CaseInsensitive)) {
        appendInfo(m_statusKey, tr("* Not an ed3k room link"), IrcRouting::LineColor::Notice);
        return;
    }
    m_ipc->joinEd3kRoom(l, ed3kNick(), [self = QPointer<IrcPanel>(this)](const IpcMessage& r) {
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
    const auto form = askRoomForm(this, tr("Room info"), start, false);
    if (!form)
        return;
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
        QApplication::clipboard()->setText(text);
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
                           [self = QPointer<IrcPanel>(this)](const IpcMessage& r) {
                               if (self && (!r.isValid() || !r.fieldBool(0)))
                                   self->ed3kRoomFailed(tr("The moderators were not changed"), r);
                           });
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
    return irc.isEmpty() ? thePrefs.nick() : irc;
}

} // namespace eMule
