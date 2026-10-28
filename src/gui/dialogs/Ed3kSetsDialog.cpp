/// @file Ed3kSetsDialog.cpp
/// @brief "ed3k Sets" — implementation.

#include "pch.h"
#include "dialogs/Ed3kSetsDialog.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "utils/Log.h"
#include "utils/StringUtils.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QStyle>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

#include <map>
#include <utility>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

QPointer<Ed3kSetsDialog> s_dialog;

enum SetCol { SetName = 0, SetAuthor, SetVersion, SetFiles, SetSize, SetPolicy, SetState };
enum MemberCol { MemberName = 0, MemberSize, MemberState };
enum LiveCol { LiveName = 0, LiveState, LivePeers, LiveSegments, LiveReceived, LiveViewers };

constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kIdRole = Qt::UserRole + 2;
constexpr int kLinkRole = Qt::UserRole + 3;

QTreeWidget* makeList(const QStringList& headers, QWidget* parent)
{
    auto* t = new QTreeWidget(parent);
    t->setHeaderLabels(headers);
    t->setRootIsDecorated(false);
    t->setUniformRowHeights(true);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setAlternatingRowColors(false);
    t->header()->setStretchLastSection(true);
    return t;
}

QPushButton* button(const QString& text, QWidget* parent)
{
    auto* b = new QPushButton(text, parent);
    b->setAutoDefault(false);
    return b;
}

} // namespace

Ed3kSetsDialog* Ed3kSetsDialog::showDialog(QWidget* parent, IpcClient* ipc, Tab tab)
{
    if (!s_dialog) {
        s_dialog = new Ed3kSetsDialog(ipc, parent);
        s_dialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    s_dialog->m_tabs->setCurrentIndex(tab);
    s_dialog->show();
    s_dialog->raise();
    s_dialog->activateWindow();
    return s_dialog;
}

Ed3kSetsDialog::Ed3kSetsDialog(IpcClient* ipc, QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
{
    setWindowTitle(tr("ed3k Sets"));
    resize(880, 560);
    auto* layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs);

    // -- Sets ------------------------------------------------------------------
    auto* setsPage = new QWidget(m_tabs);
    auto* setsLayout = new QHBoxLayout(setsPage);
    auto* split = new QSplitter(Qt::Vertical, setsPage);
    m_sets = makeList({tr("Name"), tr("Author"), tr("Version"), tr("Files"), tr("Size"), tr("New Versions"), tr("Status")}, split);
    m_sets->setObjectName(QStringLiteral("ed3kSets"));
    m_sets->header()->resizeSection(SetName, 180);
    for (const auto& [col, width] : {std::pair{SetAuthor, 80}, {SetVersion, 60}, {SetFiles, 50}, {SetSize, 80}, {SetPolicy, 100}})
        m_sets->header()->resizeSection(col, width);
    m_members = new QTreeWidget(split);
    m_members->setHeaderLabels({tr("File"), tr("Size"), tr("Status")});
    m_members->setObjectName(QStringLiteral("ed3kMembers"));
    m_members->setUniformRowHeights(true);
    m_members->header()->resizeSection(MemberName, 380);
    m_members->header()->resizeSection(MemberSize, 90);
    split->addWidget(m_sets);
    split->addWidget(m_members);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    setsLayout->addWidget(split, 1);

    auto* side = new QVBoxLayout;
    m_follow = button(tr("Follow Link..."), setsPage);
    m_unfollow = button(tr("Unfollow"), setsPage);
    auto* policyLabel = new QLabel(tr("New versions:"), setsPage);
    m_policy = new QComboBox(setsPage);
    m_policy->addItem(policyText(0));
    m_policy->addItem(policyText(1));
    m_download = button(tr("Download Selected"), setsPage);
    m_parts = button(tr("Download Parts"), setsPage);
    m_buy = button(tr("Buy Pass..."), setsPage);
    m_buy->setObjectName(QStringLiteral("ed3kBuyPass"));
    m_reports = new QCheckBox(tr("Share reports"), setsPage);
    m_reports->setToolTip(tr("Report a pass key seen at more computers than the seller allows (subscription.md §5.3). "
                             "The report shows the seller the node IDs that used the key."));
    m_reports->setChecked(true);
    m_info = new QLabel(setsPage);
    m_info->setObjectName(QStringLiteral("ed3kSetInfo"));
    m_info->setWordWrap(true);
    m_info->setMaximumWidth(170);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    side->addWidget(m_follow);
    side->addWidget(m_unfollow);
    side->addSpacing(12);
    side->addWidget(policyLabel);
    side->addWidget(m_policy);
    side->addSpacing(12);
    side->addWidget(m_download);
    side->addWidget(m_parts);
    side->addSpacing(12);
    side->addWidget(m_buy);
    side->addWidget(m_reports);
    side->addSpacing(12);
    side->addWidget(m_info);
    side->addStretch(1);
    setsLayout->addLayout(side);
    m_tabs->addTab(setsPage, tr("Sets"));

    // -- Live ------------------------------------------------------------------
    auto* livePage = new QWidget(m_tabs);
    auto* liveLayout = new QHBoxLayout(livePage);
    m_live = makeList({tr("Channel"), tr("Status"), tr("Peers"), tr("Segments"), tr("Received"), tr("Players")}, livePage);
    m_live->setObjectName(QStringLiteral("ed3kLive"));
    m_live->header()->resizeSection(LiveName, 220);
    liveLayout->addWidget(m_live, 1);
    auto* liveSide = new QVBoxLayout;
    m_watch = button(tr("Watch Link..."), livePage);
    m_open = button(tr("Open"), livePage);
    m_leave = button(tr("Stop"), livePage);
    m_broadcast = button(tr("Broadcast..."), livePage);
    m_copyLink = button(tr("Copy Link"), livePage);
    liveSide->addWidget(m_watch);
    liveSide->addWidget(m_open);
    liveSide->addWidget(m_leave);
    liveSide->addSpacing(12);
    liveSide->addWidget(m_broadcast);
    liveSide->addWidget(m_copyLink);
    liveSide->addStretch(1);
    liveLayout->addLayout(liveSide);
    m_tabs->addTab(livePage, tr("Live Streams"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    connect(m_sets, &QTreeWidget::currentItemChanged, this, &Ed3kSetsDialog::onSetChanged);
    connect(m_follow, &QPushButton::clicked, this, &Ed3kSetsDialog::followLink);
    connect(m_unfollow, &QPushButton::clicked, this, &Ed3kSetsDialog::unfollow);
    connect(m_policy, &QComboBox::activated, this, &Ed3kSetsDialog::setPolicy);
    connect(m_download, &QPushButton::clicked, this, &Ed3kSetsDialog::downloadSelected);
    connect(m_parts, &QPushButton::clicked, this, &Ed3kSetsDialog::downloadParts);
    connect(m_watch, &QPushButton::clicked, this, &Ed3kSetsDialog::watchLink);
    connect(m_open, &QPushButton::clicked, this, &Ed3kSetsDialog::openStream);
    connect(m_leave, &QPushButton::clicked, this, &Ed3kSetsDialog::leaveStream);
    connect(m_live, &QTreeWidget::itemDoubleClicked, this, &Ed3kSetsDialog::openStream);
    connect(m_live, &QTreeWidget::currentItemChanged, this, &Ed3kSetsDialog::updateButtons);
    connect(m_broadcast, &QPushButton::clicked, this, &Ed3kSetsDialog::broadcast);
    connect(m_copyLink, &QPushButton::clicked, this, &Ed3kSetsDialog::copyLink);
    connect(m_buy, &QPushButton::clicked, this, &Ed3kSetsDialog::buyPass);
    connect(m_reports, &QCheckBox::clicked, this, [this](bool on) {
        if (m_ipc)
            m_ipc->setEd3kPassReports(on);
    });
    if (m_ipc) {
        connect(m_ipc, &IpcClient::ed3kSetsChanged, this, [this](const IpcMessage& msg) { fillSets(msg.fieldMap(0)); });
        connect(m_ipc, &IpcClient::ed3kLiveChanged, this, [this](const IpcMessage& msg) { fillLive(msg.fieldMap(0)); });
        connect(m_ipc, &IpcClient::ed3kPaidChanged, this, [this](const IpcMessage& msg) { fillPaid(msg.fieldMap(0)); });
        connect(m_ipc, &IpcClient::ed3kPaidAnonWarn, this,
                [this](const IpcMessage& msg) { onAnonWarn(msg.fieldString(0), msg.fieldString(1)); });
        connect(m_ipc, &IpcClient::connected, this, &Ed3kSetsDialog::refresh);
    }
    refresh();
    updateButtons();
}

Ed3kSetsDialog::~Ed3kSetsDialog() = default;

void Ed3kSetsDialog::fillSets(const QCborMap& sets)
{
    m_filling = true;
    const qint64 keep = currentId();
    m_setRows = sets.value(QStringLiteral("sets")).toArray();
    m_sets->clear();
    QTreeWidgetItem* current = nullptr;
    for (const auto& v : m_setRows) {
        const QCborMap s = v.toMap();
        auto* item = new QTreeWidgetItem(m_sets);
        const qint64 id = s.value(QStringLiteral("id")).toInteger();
        item->setData(SetName, kIdRole, id);
        item->setText(SetName, s.value(QStringLiteral("title")).toString());
        const QString author = s.value(QStringLiteral("author")).toString();
        item->setText(SetAuthor, s.value(QStringLiteral("hidden")).toBool() ? tr("(hidden)") : author.left(8));
        item->setToolTip(SetAuthor, author);
        const qint64 seq = s.value(QStringLiteral("seq")).toInteger(-1);
        const qint64 pending = s.value(QStringLiteral("pendingSeq")).toInteger(-1);
        QString version = seq < 0 ? QStringLiteral("-") : QString::number(seq);
        if (pending >= 0)
            version += QStringLiteral(" → %1").arg(pending);
        item->setText(SetVersion, version);
        const QCborArray members = s.value(QStringLiteral("members")).toArray();
        // no version yet: nothing to count
        const bool known = seq >= 0 || pending >= 0;
        item->setText(SetFiles, known ? QString::number(members.size()) : QStringLiteral("-"));
        item->setText(SetSize, known ? formatByteSize(s.value(QStringLiteral("totalSize")).toInteger()) : QStringLiteral("-"));
        item->setText(SetPolicy, policyText(int(s.value(QStringLiteral("policy")).toInteger())));
        QString state;
        const QCborMap paid = paidOf(s);
        const int paidState = paid.isEmpty() ? -1 : int(paid.value(QStringLiteral("state")).toInteger());
        if (!s.value(QStringLiteral("error")).toString().isEmpty())
            state = tr("Error");
        else if (pending >= 0 || !s.value(QStringLiteral("pendingMounts")).toArray().isEmpty())
            state = tr("New version");
        else if (paidState >= 0 && paidState != 2 && seq < 0)
            state = paidStateText(paidState);
        else if (seq < 0)
            state = s.value(QStringLiteral("followed")).toBool() ? tr("Looking for it") : tr("Waiting for ed3k");
        else {
            int complete = 0;
            int wanted = 0;
            for (const auto& m : members) {
                const int st = int(m.toMap().value(QStringLiteral("state")).toInteger());
                if (st != 0)
                    ++wanted;
                if (st == 3)
                    ++complete;
            }
            state = complete == wanted ? tr("Complete") : tr("Downloading (%1 of %2)").arg(complete).arg(wanted);
        }
        item->setText(SetState, state);
        if (id == keep)
            current = item;
    }
    if (!current && m_sets->topLevelItemCount() > 0)
        current = m_sets->topLevelItem(0);
    m_sets->setCurrentItem(current);
    m_filling = false;
    onSetChanged();
}

void Ed3kSetsDialog::fillLive(const QCborMap& live)
{
    const QString keep = currentStream();
    m_live->clear();
    for (const auto& v : live.value(QStringLiteral("streams")).toArray()) {
        const QCborMap s = v.toMap();
        auto* item = new QTreeWidgetItem(m_live);
        const QString id = s.value(QStringLiteral("id")).toString();
        item->setData(LiveName, kIdRole, id);
        item->setData(LiveName, kPathRole, s.value(QStringLiteral("url")).toString());
        const QString title = s.value(QStringLiteral("title")).toString();
        item->setText(LiveName, title.isEmpty() ? id.left(12) : title);
        item->setToolTip(LiveName, s.value(QStringLiteral("media")).toString() + QLatin1Char(' ') + s.value(QStringLiteral("codecs")).toString());
        item->setText(LiveState, liveStateText(int(s.value(QStringLiteral("state")).toInteger())));
        item->setText(LivePeers, QString::number(s.value(QStringLiteral("neighbours")).toInteger()));
        item->setText(LiveSegments, QString::number(s.value(QStringLiteral("segments")).toInteger()));
        item->setText(LiveReceived, formatByteSize(s.value(QStringLiteral("bytes")).toInteger()));
        item->setText(LiveViewers, QString::number(s.value(QStringLiteral("viewers")).toInteger()));
        if (s.value(QStringLiteral("broadcast")).toBool()) {
            // our own broadcast: on air, its link to copy (a private one's is a secret: not shown)
            item->setData(LiveName, kLinkRole, s.value(QStringLiteral("link")).toString());
            const QString err = s.value(QStringLiteral("error")).toString();
            item->setText(LiveState, err.isEmpty() ? tr("On air") : tr("On air (%1)").arg(err));
            item->setToolTip(LiveName, s.value(QStringLiteral("folder")).toString());
            item->setText(LiveReceived, formatByteSize(s.value(QStringLiteral("bytes")).toInteger()));
            item->setText(LiveViewers, QStringLiteral("-"));
        }
        if (id == keep)
            m_live->setCurrentItem(item);
    }
    if (!m_live->currentItem() && m_live->topLevelItemCount() > 0)
        m_live->setCurrentItem(m_live->topLevelItem(0));
    updateButtons();
}

void Ed3kSetsDialog::fillPaid(const QCborMap& paid)
{
    m_paidRows.clear();
    for (const auto& v : paid.value(QStringLiteral("subs")).toArray())
        m_paidRows.insert(v.toMap().value(QStringLiteral("scope")).toString(), v.toMap());
    m_reports->setChecked(paid.value(QStringLiteral("reports")).toBool(true));
    // the set rows show the pass state
    QCborMap sets;
    sets.insert(QStringLiteral("sets"), m_setRows);
    fillSets(sets);
}

void Ed3kSetsDialog::selectSet(qint64 id)
{
    for (int i = 0; i < m_sets->topLevelItemCount(); ++i)
        if (m_sets->topLevelItem(i)->data(SetName, kIdRole).toLongLong() == id)
            m_sets->setCurrentItem(m_sets->topLevelItem(i));
}

QString Ed3kSetsDialog::policyText(int policy)
{
    return policy == 1 ? tr("Download") : tr("Ask");
}

QString Ed3kSetsDialog::memberStateText(int state)
{
    switch (state) {
    case 0: return tr("Not selected");
    case 1: return tr("Waiting");
    case 2: return tr("Downloading");
    case 3: return tr("Complete");
    case 4: return tr("Failed");
    case 5: return tr("Paused");
    default: return {};
    }
}

QString Ed3kSetsDialog::liveStateText(int state)
{
    switch (state) {
    case 0: return tr("Joining");
    case 1: return tr("Playing");
    case 2: return tr("Interrupted");
    case 3: return tr("No peers");
    case 4: return tr("Ended");
    default: return {};
    }
}

QString Ed3kSetsDialog::paidStateText(int state)
{
    switch (state) {
    case 0: return tr("Pass needed");
    case 1: return tr("Buying a pass");
    case 2: return tr("Pass held");
    case 3: return tr("Pass refused");
    case 4: return tr("No pass came");
    case 5: return tr("Waiting for your answer");
    default: return {};
    }
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kSetsDialog::refresh()
{
    if (!m_ipc)
        return;
    m_ipc->requestEd3kSets(this, [self = QPointer(this)](const IpcMessage& r) {
        if (self && r.fieldBool(0))
            self->fillSets(r.fieldMap(1));
    });
    m_ipc->requestEd3kLive(this, [self = QPointer(this)](const IpcMessage& r) {
        if (self && r.fieldBool(0))
            self->fillLive(r.fieldMap(1));
    });
    m_ipc->requestEd3kPaid(this, [self = QPointer(this)](const IpcMessage& r) {
        if (self && r.fieldBool(0))
            self->fillPaid(r.fieldMap(1));
    });
}

void Ed3kSetsDialog::onSetChanged()
{
    if (m_filling)
        return;
    const QCborMap s = currentSet();
    m_policy->setCurrentIndex(int(s.value(QStringLiteral("policy")).toInteger()));
    const qint64 id = s.isEmpty() ? -1 : s.value(QStringLiteral("id")).toInteger();
    const qint64 seq = s.value(QStringLiteral("seq")).toInteger(-1);
    const qint64 pending = s.value(QStringLiteral("pendingSeq")).toInteger(-1);
    QStringList mounts;
    for (const auto& v : s.value(QStringLiteral("pendingMounts")).toArray())
        mounts << v.toString();
    const QString mountsKey = mounts.join(QLatin1Char('\n'));
    // the ticks the user set stay while the same version is shown
    if (id != m_shownId || seq != m_shownSeq || pending != m_shownPending || mountsKey != m_shownMounts) {
        fillMembers(s);
        m_shownId = id;
        m_shownSeq = seq;
        m_shownPending = pending;
        m_shownMounts = mountsKey;
    } else {
        std::map<QString, QCborMap> byPath;
        for (const auto& v : s.value(QStringLiteral("members")).toArray())
            byPath[v.toMap().value(QStringLiteral("path")).toString()] = v.toMap();
        for (QTreeWidgetItemIterator it(m_members); *it; ++it) {
            const auto m = byPath.find((*it)->data(MemberName, kPathRole).toString());
            if (m != byPath.end())
                (*it)->setText(MemberState, memberStateText(int(m->second.value(QStringLiteral("state")).toInteger())));
        }
    }
    QStringList lines;
    if (!s.isEmpty()) {
        lines << tr("Folder: %1").arg(s.value(QStringLiteral("dir")).toString());
        const auto count = [&](const char* key) { return s.value(QLatin1String(key)).toArray().size(); };
        if (pending >= 0)
            lines << tr("Version %1 is waiting: %2 added, %3 changed, %4 removed.")
                         .arg(pending)
                         .arg(count("added"))
                         .arg(count("changed"))
                         .arg(count("removed"));
        if (!mounts.isEmpty())
            lines << tr("A new version of the mounted folder(s) %1 is waiting: %2 added, %3 changed, %4 removed.")
                         .arg(mounts.join(QStringLiteral(", ")))
                         .arg(count("added"))
                         .arg(count("changed"))
                         .arg(count("removed"));
        lines << paidLines(paidOf(s));
        const qsizetype parts = s.value(QStringLiteral("parts")).toArray().size();
        if (parts > 0)
            lines << tr("%n open part(s) by other authors.", nullptr, int(parts));
        if (const QString e = s.value(QStringLiteral("error")).toString(); !e.isEmpty())
            lines << e;
    }
    m_info->setText(lines.join(QStringLiteral("\n\n")));
    updateButtons();
}

void Ed3kSetsDialog::fillMembers(const QCborMap& set)
{
    m_members->clear();
    std::map<QString, QTreeWidgetItem*> dirs;
    // folders first for each path, as eMule's directory trees
    const auto folder = [&](const QString& path) {
        QTreeWidgetItem* parent = nullptr;
        QString sofar;
        for (const QString& part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            sofar = sofar.isEmpty() ? part : sofar + QLatin1Char('/') + part;
            auto& slot = dirs[sofar];
            if (!slot) {
                slot = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_members);
                slot->setText(MemberName, part);
                slot->setIcon(MemberName, style()->standardIcon(QStyle::SP_DirIcon));
                slot->setFlags(slot->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
                slot->setData(MemberName, kPathRole, sofar);
            }
            parent = slot;
        }
        return parent;
    };
    for (const auto& v : set.value(QStringLiteral("members")).toArray()) {
        const QCborMap m = v.toMap();
        const QString path = m.value(QStringLiteral("path")).toString();
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem* parent = slash > 0 ? folder(path.left(slash)) : nullptr;
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_members);
        item->setText(MemberName, slash >= 0 ? path.mid(slash + 1) : path);
        item->setToolTip(MemberName, path);
        item->setIcon(MemberName, style()->standardIcon(QStyle::SP_FileIcon));
        item->setData(MemberName, kPathRole, path);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(MemberName, m.value(QStringLiteral("skipped")).toBool() ? Qt::Unchecked : Qt::Checked);
        item->setText(MemberSize, formatByteSize(m.value(QStringLiteral("size")).toInteger()));
        item->setText(MemberState, memberStateText(int(m.value(QStringLiteral("state")).toInteger())));
    }
    m_members->expandAll();
}

void Ed3kSetsDialog::updateButtons()
{
    const bool connected = m_ipc && m_ipc->isConnected();
    const QCborMap s = currentSet();
    m_follow->setEnabled(connected);
    m_unfollow->setEnabled(connected && !s.isEmpty());
    m_policy->setEnabled(connected && !s.isEmpty());
    m_download->setEnabled(connected && !s.isEmpty()
                           && (s.value(QStringLiteral("seq")).toInteger(-1) >= 0 || s.value(QStringLiteral("pendingSeq")).toInteger(-1) >= 0));
    const QCborMap paid = paidOf(s);
    const int paidState = paid.isEmpty() ? -1 : int(paid.value(QStringLiteral("state")).toInteger());
    // a pass is bought with a known offer, and again when it ended or none came
    m_buy->setVisible(!paid.isEmpty());
    m_buy->setEnabled(connected && paid.value(QStringLiteral("hasOffer")).toBool() && paidState != 1 && paidState != 5
                      && !(paidState == 2 && paid.value(QStringLiteral("until")).toInteger() > QDateTime::currentSecsSinceEpoch()));
    m_reports->setVisible(!m_paidRows.isEmpty());
    m_reports->setEnabled(connected);
    m_parts->setEnabled(connected && !s.value(QStringLiteral("parts")).toArray().isEmpty());
    const bool stream = !currentStream().isEmpty();
    m_watch->setEnabled(connected);
    m_leave->setEnabled(connected && stream);
    // the stream's URL is the core's loopback: only a GUI on the same machine plays it
    m_open->setEnabled(connected && stream && m_ipc->isLocalConnection() && m_live->currentItem()
                       && !m_live->currentItem()->data(LiveName, kPathRole).toString().isEmpty());
    m_broadcast->setEnabled(connected);
    m_copyLink->setEnabled(stream && m_live->currentItem() && !m_live->currentItem()->data(LiveName, kLinkRole).toString().isEmpty());
}

QCborMap Ed3kSetsDialog::currentSet() const
{
    const QTreeWidgetItem* item = m_sets->currentItem();
    if (!item)
        return {};
    const qint64 id = item->data(SetName, kIdRole).toLongLong();
    for (const auto& v : m_setRows)
        if (v.toMap().value(QStringLiteral("id")).toInteger() == id)
            return v.toMap();
    return {};
}

qint64 Ed3kSetsDialog::currentId() const
{
    const QTreeWidgetItem* item = m_sets->currentItem();
    return item ? item->data(SetName, kIdRole).toLongLong() : -1;
}

QString Ed3kSetsDialog::currentStream() const
{
    const QTreeWidgetItem* item = m_live->currentItem();
    return item ? item->data(LiveName, kIdRole).toString() : QString();
}

void Ed3kSetsDialog::followLink()
{
    if (!m_ipc)
        return;
    const QString clip = QApplication::clipboard()->text().trimmed();
    bool ok = false;
    const QString link = QInputDialog::getText(this, tr("Follow ed3k Set"), tr("ed3k:set link:"), QLineEdit::Normal,
                                               clip.startsWith(QStringLiteral("ed3k:set?"), Qt::CaseInsensitive) ? clip : QString(), &ok)
                             .trimmed();
    if (!ok || link.isEmpty())
        return;
    // file-set.md §2.3: the members are shown before they download; "ask" waits for the user
    m_ipc->followEd3kSet(link, m_policy->currentIndex(), [self = QPointer(this)](const IpcMessage& r) {
        if (!self)
            return;
        if (!r.fieldBool(0)) {
            self->showError(tr("The set could not be followed."), r.fieldMap(1));
            return;
        }
        self->refresh();
    });
}

void Ed3kSetsDialog::unfollow()
{
    const QCborMap s = currentSet();
    if (s.isEmpty() || !m_ipc)
        return;
    if (QMessageBox::question(this, tr("Unfollow ed3k Set"),
                              tr("Stop following \"%1\"? The files downloaded stay where they are.")
                                  .arg(s.value(QStringLiteral("title")).toString()))
        != QMessageBox::Yes)
        return;
    m_ipc->unfollowEd3kSet(currentId(), [self = QPointer(this)](const IpcMessage&) {
        if (self)
            self->refresh();
    });
}

void Ed3kSetsDialog::setPolicy(int index)
{
    if (!m_ipc || currentId() < 0)
        return;
    m_ipc->setEd3kSetPolicy(currentId(), index);
}

void Ed3kSetsDialog::downloadSelected()
{
    if (!m_ipc || currentId() < 0)
        return;
    const qint64 id = currentId();
    m_ipc->setEd3kSetSelection(id, skippedPaths(), [self = QPointer(this), id](const IpcMessage& r) {
        if (!self || !self->m_ipc)
            return;
        if (!r.fieldBool(0)) {
            self->showError(tr("The selection could not be set."), r.fieldMap(1));
            return;
        }
        self->m_ipc->applyEd3kSetVersion(id, [self](const IpcMessage&) {
            if (self)
                self->refresh();
        });
    });
}

void Ed3kSetsDialog::downloadParts()
{
    if (!m_ipc || currentId() < 0)
        return;
    m_ipc->downloadEd3kSetParts(currentId(), {}, [self = QPointer(this)](const IpcMessage& r) {
        if (self && !r.fieldBool(0))
            self->showError(tr("The parts could not be downloaded."), r.fieldMap(1));
    });
}

void Ed3kSetsDialog::watchLink()
{
    if (!m_ipc)
        return;
    const QString clip = QApplication::clipboard()->text().trimmed();
    bool ok = false;
    const QString link = QInputDialog::getText(this, tr("Watch ed3k Live Stream"), tr("ed3k:live link:"), QLineEdit::Normal,
                                               clip.startsWith(QStringLiteral("ed3k:live?"), Qt::CaseInsensitive) ? clip : QString(), &ok)
                             .trimmed();
    if (!ok || link.isEmpty())
        return;
    m_ipc->watchEd3kLive(link, [self = QPointer(this)](const IpcMessage& r) {
        if (!self)
            return;
        if (!r.fieldBool(0)) {
            self->showError(tr("The stream could not be watched."), r.fieldMap(1));
            return;
        }
        const QString url = r.fieldMap(1).value(QStringLiteral("url")).toString();
        if (!url.isEmpty() && self->m_ipc && self->m_ipc->isLocalConnection())
            QDesktopServices::openUrl(QUrl(url));
        self->refresh();
    });
}

void Ed3kSetsDialog::openStream()
{
    const QTreeWidgetItem* item = m_live->currentItem();
    if (!item || !m_ipc || !m_ipc->isLocalConnection())
        return;
    const QString url = item->data(LiveName, kPathRole).toString();
    if (!url.isEmpty())
        QDesktopServices::openUrl(QUrl(url));
}

void Ed3kSetsDialog::leaveStream()
{
    const QString id = currentStream();
    if (id.isEmpty() || !m_ipc)
        return;
    m_ipc->leaveEd3kLive(id, [self = QPointer(this)](const IpcMessage&) {
        if (self)
            self->refresh();
    });
}

void Ed3kSetsDialog::broadcast()
{
    if (!m_ipc)
        return;
    // live.md §4: the encoder writes HLS with fMP4 segments into a folder of the core's machine
    QDialog d(this);
    d.setWindowTitle(tr("Broadcast ed3k Live Stream"));
    auto* form = new QFormLayout(&d);
    auto* title = new QLineEdit(&d);
    auto* folder = new QLineEdit(&d);
    auto* browse = button(tr("Browse..."), &d);
    browse->setEnabled(m_ipc->isLocalConnection());
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(folder, 1);
    folderRow->addWidget(browse);
    auto* codecs = new QLineEdit(&d);
    codecs->setPlaceholderText(QStringLiteral("avc1.64001f,mp4a.40.2"));
    auto* priv = new QCheckBox(tr("Private (only who has the link can watch)"), &d);
    auto* hint = new QLabel(tr("The folder of an HLS output with fMP4 segments, e.g. OBS, or\n"
                               "ffmpeg … -f hls -hls_segment_type fmp4 -hls_time 2 -hls_list_size 6 live.m3u8"),
                            &d);
    hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Title:"), title);
    form->addRow(tr("Folder:"), folderRow);
    form->addRow(tr("Codecs:"), codecs);
    form->addRow(priv);
    form->addRow(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Broadcast"));
    form->addRow(buttons);
    connect(browse, &QPushButton::clicked, &d, [&] {
        const QString dir = QFileDialog::getExistingDirectory(&d, tr("HLS Output Folder"), folder->text());
        if (!dir.isEmpty())
            folder->setText(dir);
    });
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return;
    m_ipc->broadcastEd3kLive(folder->text().trimmed(), title->text().trimmed(), priv->isChecked(), {}, codecs->text().trimmed(),
                             [self = QPointer(this)](const IpcMessage& r) {
                                 if (!self)
                                     return;
                                 if (!r.fieldBool(0)) {
                                     self->showError(tr("The broadcast could not start."), r.fieldMap(1));
                                     return;
                                 }
                                 const QString link = r.fieldMap(1).value(QStringLiteral("link")).toString();
                                 QApplication::clipboard()->setText(link);
                                 QMessageBox::information(self, tr("ed3k Live Stream"),
                                                          tr("On air. The link was copied to the clipboard:\n%1").arg(link));
                                 self->refresh();
                             });
}

void Ed3kSetsDialog::copyLink()
{
    const QTreeWidgetItem* item = m_live->currentItem();
    if (item && !item->data(LiveName, kLinkRole).toString().isEmpty())
        QApplication::clipboard()->setText(item->data(LiveName, kLinkRole).toString());
}

void Ed3kSetsDialog::buyPass()
{
    const QCborMap p = paidOf(currentSet());
    if (p.isEmpty() || !m_ipc)
        return;
    const QString scope = p.value(QStringLiteral("scope")).toString();
    // subscription.md §3 (price, terms for display), §9.1 (the pay page of the seller)
    QStringList text;
    text << tr("Buy a pass for \"%1\"?").arg(p.value(QStringLiteral("title")).toString());
    text << tr("Price: %1").arg(p.value(QStringLiteral("price")).toString());
    if (const QString terms = p.value(QStringLiteral("terms")).toString(); !terms.isEmpty())
        text << tr("Terms: %1").arg(terms);
    text << tr("The seller's page at %1 opens in your browser; payment is up to the seller, not ed3k. "
               "Your pass key may be used on %n computer(s) at once.",
               nullptr, int(p.value(QStringLiteral("devices")).toInteger(3)))
                .arg(p.value(QStringLiteral("payHost")).toString());
    QMessageBox box(QMessageBox::Question, tr("Buy ed3k Pass"), text.join(QStringLiteral("\n\n")), QMessageBox::Cancel, this);
    auto* buy = box.addButton(tr("Buy"), QMessageBox::AcceptRole);
    auto* anon = box.addButton(tr("Buy for Anonymous Key"), QMessageBox::AcceptRole);
    anon->setToolTip(tr("The pass is shown under the anonymous key of the hops (hops.md) only."));
    box.exec();
    if (box.clickedButton() != buy && box.clickedButton() != anon)
        return;
    m_ipc->buyEd3kPass(scope, box.clickedButton() == anon, [self = QPointer(this)](const IpcMessage& r) {
        if (!self)
            return;
        if (!r.fieldBool(0)) {
            self->showError(tr("The pass could not be bought."), r.fieldMap(1));
            return;
        }
        // empty: the anonymous key's warning comes first (PushEd3kPaidAnonWarn)
        self->openPayLink(r.fieldMap(1).value(QStringLiteral("payLink")).toString());
        self->refresh();
    });
}

void Ed3kSetsDialog::onAnonWarn(const QString& scope, const QString& title)
{
    if (!m_ipc)
        return;
    // subscription.md §5.1: MUST tell the user before buying for the anonymous key
    const auto answer = QMessageBox::warning(
        this, tr("Pass for the Anonymous Key"),
        tr("Buying a pass for \"%1\" and asking the seller for it are not ed3k: the seller sees an address of this "
           "computer and can tie it to the pass key, which is shown under your anonymous key.\n\nGo on?")
            .arg(title),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    m_ipc->confirmEd3kPaidAnon(scope, [self = QPointer(this)](const IpcMessage& r) {
        if (!self)
            return;
        if (!r.fieldBool(0)) {
            self->showError(tr("The pass could not be bought."), r.fieldMap(1));
            return;
        }
        self->openPayLink(r.fieldMap(1).value(QStringLiteral("payLink")).toString());
        self->refresh();
    });
}

void Ed3kSetsDialog::openPayLink(const QString& link)
{
    if (link.isEmpty())
        return;
    // §9.1 step 2: the seller's page in the browser, with scope and holder key
    if (!QDesktopServices::openUrl(QUrl(link)))
        QApplication::clipboard()->setText(link);
}

QCborMap Ed3kSetsDialog::paidOf(const QCborMap& s) const
{
    const QString scope = s.value(QStringLiteral("paidScope")).toString();
    if (scope.isEmpty())
        return {};
    // a paid set before its subscription row came: a pass is needed
    return m_paidRows.value(scope, QCborMap{{QStringLiteral("scope"), scope}, {QStringLiteral("state"), 0}});
}

QStringList Ed3kSetsDialog::paidLines(const QCborMap& p) const
{
    if (p.isEmpty())
        return {};
    QStringList lines;
    if (p.value(QStringLiteral("hasOffer")).toBool())
        lines << tr("Paid: %1").arg(p.value(QStringLiteral("price")).toString());
    else
        lines << tr("Paid; the price is not known yet.");
    const int state = int(p.value(QStringLiteral("state")).toInteger());
    const qint64 until = p.value(QStringLiteral("until")).toInteger();
    const QString end = QLocale().toString(QDateTime::fromSecsSinceEpoch(until), QLocale::ShortFormat);
    if (state == 2 && until > QDateTime::currentSecsSinceEpoch()) {
        lines << tr("Pass valid until %1.").arg(end);
        if (p.value(QStringLiteral("renewalEnded")).toBool())
            lines << tr("The seller does not renew it.");
        else if (p.value(QStringLiteral("renewing")).toBool())
            lines << tr("Renewed from the seller in its last 48 hours.");
    } else if (state == 2) {
        lines << tr("The pass ended on %1; what it opened stays open.").arg(end);
    } else {
        lines << paidStateText(state) + QLatin1Char('.');
    }
    if (p.value(QStringLiteral("anon")).toBool())
        lines << tr("Under the anonymous key.");
    return lines;
}

QStringList Ed3kSetsDialog::skippedPaths() const
{
    QStringList out;
    for (QTreeWidgetItemIterator it(m_members); *it; ++it)
        if ((*it)->childCount() == 0 && (*it)->checkState(MemberName) == Qt::Unchecked)
            out << (*it)->data(MemberName, kPathRole).toString();
    return out;
}

void Ed3kSetsDialog::showError(const QString& what, const QCborMap& reply)
{
    const QString why = reply.value(QStringLiteral("error")).toString();
    QMessageBox::warning(this, tr("ed3k Sets"), why.isEmpty() ? what : what + QLatin1Char('\n') + why);
}

} // namespace eMule
