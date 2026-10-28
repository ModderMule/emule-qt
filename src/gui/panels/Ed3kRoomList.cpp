#include "pch.h"
/// @file Ed3kRoomList.cpp
/// @brief The "Rooms (ed3k)" tab — implementation.

#include "panels/Ed3kRoomList.h"

#include "controls/AbstractListView.h"
#include "controls/SortableItems.h"

#include <QCborMap>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

namespace eMule {

Ed3kRoomList::Ed3kRoomList(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 2, 0, 0);
    layout->setSpacing(2);

    auto* buttons = new QHBoxLayout;
    auto* join = new QPushButton(tr("Join by link..."), this);
    auto* create = new QPushButton(tr("Create..."), this);
    buttons->addWidget(join);
    buttons->addWidget(create);
    buttons->addStretch();
    layout->addLayout(buttons);
    connect(join, &QPushButton::clicked, this, [this] { emit actionRequested(Action::Join, {}); });
    connect(create, &QPushButton::clicked, this, [this] { emit actionRequested(Action::Create, {}); });

    // as the channel list (MFC CIrcChannelListCtrl): a double click opens
    auto* tree = new ListTreeWidget(this);
    tree->setHeaderLabels({tr("Room"), tr("Nick"), tr("Members"), tr("Kind"), tr("State")});
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setSortingEnabled(true);
    tree->header()->setStretchLastSection(true);
    tree->bindColumns(QStringLiteral("ircEd3kRoomList"), {220, 110, 70, 140, 200});
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (const QString room = selectedRoom(); !room.isEmpty())
            emit actionRequested(Action::Open, room);
    });
    connect(tree, &QWidget::customContextMenuRequested, this, &Ed3kRoomList::showMenu);
    layout->addWidget(tree, 1);
    m_tree = tree;
}

void Ed3kRoomList::setRooms(const QCborArray& rooms)
{
    m_rooms = rooms;
    const QString selected = selectedRoom();
    m_tree->setSortingEnabled(false);
    m_tree->clear();
    for (const QCborValue& v : rooms) {
        const QCborMap r = v.toMap();
        auto* item = new SortableTreeItem(m_tree);
        const QString room = r.value(QStringLiteral("room")).toString();
        const QString title = r.value(QStringLiteral("title")).toString();
        item->setText(0, title.isEmpty() ? room.left(16) : title);
        item->setIcon(0, QIcon(QStringLiteral(":/icons/IRC.ico")));
        item->setData(0, Qt::UserRole, room);
        item->setText(1, r.value(QStringLiteral("nick")).toString());
        const int members = int(r.value(QStringLiteral("members")).toArray().size());
        item->setText(2, QString::number(members));
        item->setData(2, SortRole, members);
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        QStringList kind;
        if (r.value(QStringLiteral("closed")).toBool())
            kind << tr("Closed");
        else if (r.value(QStringLiteral("private")).toBool())
            kind << tr("Private");
        else
            kind << tr("Public");
        if (r.value(QStringLiteral("moderated")).toBool())
            kind << tr("moderated");
        if (r.value(QStringLiteral("owner")).toBool())
            kind << tr("owner");
        else if (r.value(QStringLiteral("moderator")).toBool())
            kind << tr("moderator");
        item->setText(3, kind.join(QStringLiteral(", ")));
        QString state;
        const int neighbours = int(r.value(QStringLiteral("neighbours")).toInteger());
        if (r.value(QStringLiteral("notAdmitted")).toBool())
            state = tr("Not admitted: send your member key to a moderator");
        else if (r.value(QStringLiteral("keyMissing")).toBool())
            state = tr("Waiting for the room key");
        else if (neighbours > 0)
            state = tr("Joined (%n neighbour(s))", nullptr, neighbours);
        else
            state = tr("Looking for members");
        item->setText(4, state);
        if (room == selected)
            item->setSelected(true);
    }
    m_tree->setSortingEnabled(true);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kRoomList::showMenu(const QPoint& pos)
{
    const QString room = selectedRoom();
    QCborMap row;
    for (const QCborValue& v : std::as_const(m_rooms))
        if (v.toMap().value(QStringLiteral("room")).toString() == room)
            row = v.toMap();
    const bool has = !room.isEmpty();
    const bool mod = row.value(QStringLiteral("moderator")).toBool();
    const bool closed = row.value(QStringLiteral("closed")).toBool();

    QMenu menu(this);
    const auto add = [&](const QString& text, Action a, bool enabled) {
        QAction* act = menu.addAction(text, this, [this, a, room] { emit actionRequested(a, room); });
        act->setEnabled(enabled);
        return act;
    };
    menu.setDefaultAction(add(tr("Open"), Action::Open, has));
    menu.addSeparator();
    add(tr("Join by link..."), Action::Join, true);
    add(tr("Create..."), Action::Create, true);
    add(tr("Leave"), Action::Leave, has);
    menu.addSeparator();
    add(tr("Copy link"), Action::CopyLink, has);
    add(tr("Change nick..."), Action::ChangeNick, has);
    add(tr("Copy my member key"), Action::CopyMemberKey, has);
    // #17 (batch 12 ruling): the room info says closed/moderated, or a member refused us
    // as not admitted; not every room with a secret
    // was: has && row.value(QStringLiteral("private")).toBool() && !mod
    const bool importable = closed || row.value(QStringLiteral("moderated")).toBool() || row.value(QStringLiteral("notAdmitted")).toBool();
    add(tr("Import admit..."), Action::ImportAdmit, has && importable && !mod);
    if (mod) {
        menu.addSeparator();
        add(tr("Admit a member key..."), Action::AdmitKey, true);
        add(tr("New generation"), Action::NewGeneration, closed);
    }
    if (row.value(QStringLiteral("owner")).toBool())
        add(tr("Room info..."), Action::EditInfo, true);
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

QString Ed3kRoomList::selectedRoom() const
{
    const auto items = m_tree->selectedItems();
    return items.isEmpty() ? QString() : items.first()->data(0, Qt::UserRole).toString();
}

} // namespace eMule
