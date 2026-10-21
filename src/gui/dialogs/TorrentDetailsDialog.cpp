#include "pch.h"
/// @file TorrentDetailsDialog.cpp
/// @brief One torrent's files, peers and trackers — implementation.

#include "dialogs/TorrentDetailsDialog.h"

#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "controls/SortableItems.h"
#include "utils/DialogSizing.h"
#include "utils/MenuUtils.h"
#include "utils/StatusBarNotifier.h"
#include "utils/StringUtils.h"

#include <QCborArray>
#include <QDialogButtonBox>
#include <QHash>
#include <QSet>
#include <QLabel>
#include <QMenu>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace eMule {

namespace {

constexpr int kPollMs = 2000;
/// File index on a Files row.
constexpr int kFileIndexRole = Qt::UserRole + 1;

SortableTreeItem* makeItem(QTreeWidget* tree)
{
    return new SortableTreeItem(tree);
}

void setCell(QTreeWidgetItem* item, int column, const QString& text, const QVariant& sortKey = {},
             bool right = false)
{
    item->setText(column, text);
    if (sortKey.isValid())
        item->setData(column, SortRole, sortKey);
    if (right)
        item->setTextAlignment(column, Qt::AlignRight | Qt::AlignVCenter);
}

QString percentText(double share)
{
    return QStringLiteral("%1%").arg(share * 100.0, 0, 'f', 1);
}

QString rateText(qint64 rate)
{
    return rate > 0 ? formatByteRate(rate) : QString();
}

QString countText(qint64 n)
{
    return n < 0 ? QStringLiteral("?") : QString::number(n);
}

} // namespace

QString torrentFilePriorityName(int priority)
{
    if (priority <= 0)
        return TorrentDetailsDialog::tr("Skip");
    if (priority <= 2)
        return TorrentDetailsDialog::tr("Low");
    if (priority <= 4)
        return TorrentDetailsDialog::tr("Normal");
    if (priority <= 6)
        return TorrentDetailsDialog::tr("High");
    return TorrentDetailsDialog::tr("Maximum");
}

TorrentDetailsDialog::TorrentDetailsDialog(IpcClient* ipc, QString torrentId, QString name,
                                           QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
    , m_id(std::move(torrentId))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Torrent Details"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/Torrent.ico")));
    buildUi(name);
    DialogSizing::applySize(this, {}, QSize(760, 480), DialogSizing::Fit::Layout);

    m_timer = new QTimer(this);
    m_timer->setInterval(kPollMs);
    connect(m_timer, &QTimer::timeout, this, &TorrentDetailsDialog::poll);
    m_timer->start();
    QTimer::singleShot(0, this, &TorrentDetailsDialog::poll);
}

void TorrentDetailsDialog::applyDetails(const QCborMap& details)
{
    m_note->clear();
    m_note->hide();
    applyFiles(details.value(QStringLiteral("files")).toArray());
    applyPeers(details.value(QStringLiteral("peers")).toArray());
    applyTrackers(details.value(QStringLiteral("trackers")).toArray());
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void TorrentDetailsDialog::buildUi(const QString& name)
{
    auto* layout = new QVBoxLayout(this);

    m_title = new QLabel(name, this);
    QFont f = m_title->font();
    f.setBold(true);
    m_title->setFont(f);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_title);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    m_note->hide();
    layout->addWidget(m_note);

    m_tabs = new QTabWidget(this);

    auto* files = new ListTreeWidget(m_tabs);
    m_files = files;
    files->setColumnCount(FileColCount);
    files->setHeaderLabels({tr("File"), tr("Size"), tr("Completed"), tr("Progress"), tr("Priority")});
    files->setRootIsDecorated(false);
    files->setAlternatingRowColors(true);
    files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    files->setSortingEnabled(true);
    files->setContextMenuPolicy(Qt::CustomContextMenu);
    files->bindColumns(QStringLiteral("torrentDetailsFiles"), {320, 90, 90, 80, 80});
    connect(files, &QWidget::customContextMenuRequested, this,
            &TorrentDetailsDialog::onFilesContextMenu);
    m_tabs->insertTab(TabFiles, files, menuIcon("FileInfo.ico"), tr("Files"));

    auto* peers = new ListTreeWidget(m_tabs);
    m_peers = peers;
    peers->setHeaderLabels({tr("Address"), tr("Client"), tr("Progress"), tr("Down Speed"),
                            tr("Up Speed"), tr("Downloaded"), tr("Uploaded"), tr("Flags"),
                            tr("Source")});
    peers->setRootIsDecorated(false);
    peers->setAlternatingRowColors(true);
    peers->setSortingEnabled(true);
    peers->bindColumns(QStringLiteral("torrentDetailsPeers"),
                       {150, 130, 70, 80, 80, 80, 80, 70, 70});
    m_tabs->insertTab(TabPeers, peers, menuIcon("ClientsKnown.ico"), tr("Peers"));

    auto* trackers = new ListTreeWidget(m_tabs);
    m_trackers = trackers;
    trackers->setHeaderLabels({tr("URL"), tr("Tier"), tr("Status"), tr("Seeds"), tr("Peers")});
    trackers->setRootIsDecorated(false);
    trackers->setAlternatingRowColors(true);
    trackers->setSortingEnabled(true);
    trackers->bindColumns(QStringLiteral("torrentDetailsTrackers"), {320, 50, 200, 60, 60});
    m_tabs->insertTab(TabTrackers, trackers, menuIcon("Server.ico"), tr("Trackers"));

    layout->addWidget(m_tabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
}

void TorrentDetailsDialog::poll()
{
    if (!m_ipc || !m_ipc->isConnected() || !isVisible())
        return;
    Ipc::IpcMessage msg(Ipc::IpcMsgType::GetTorrentDetails);
    msg.append(m_id);
    m_ipc->sendPoll(this, std::move(msg), [this](const Ipc::IpcMessage& resp) {
        if (!resp.isValid())
            return;
        if (resp.type() == Ipc::IpcMsgType::Error) {
            m_note->setText(resp.fieldInt(0) == 404
                                ? tr("This torrent is no longer in the list.")
                                : tr("BitTorrent is not running."));
            m_note->show();
            return;
        }
        if (resp.fieldBool(0))
            applyDetails(resp.fieldMap(1));
    });
}

void TorrentDetailsDialog::onFilesContextMenu(const QPoint& pos)
{
    if (m_files->selectedItems().isEmpty())
        return;
    QMenu menu(this);
    auto* prio = menu.addMenu(menuIcon("FilePriority.ico"), tr("Priority"));
    for (const int level : kPriorityLevels) {
        const char* icon = level == 0  ? "Cancel.ico"
                           : level < 4 ? "PriorityLow.ico"
                           : level > 4 ? "PriorityHigh.ico"
                                       : "PriorityNormal.ico";
        prio->addAction(menuIcon(icon), torrentFilePriorityName(level), this,
                        [this, level] { setSelectedPriority(level); });
    }
    menu.addAction(menuIcon("Download.ico"), tr("Download"), this,
                   [this] { setSelectedPriority(4); });
    menu.addAction(menuIcon("Cancel.ico"), tr("Do Not Download"), this,
                   [this] { setSelectedPriority(0); });
    menu.exec(m_files->viewport()->mapToGlobal(pos));
}

void TorrentDetailsDialog::setSelectedPriority(int priority)
{
    if (!m_ipc || !m_ipc->isConnected() || m_priorities.isEmpty())
        return;
    QList<int> prio = m_priorities;
    for (const QTreeWidgetItem* item : m_files->selectedItems()) {
        const int index = item->data(FileName, kFileIndexRole).toInt();
        if (index >= 0 && index < prio.size())
            prio[index] = priority;
    }
    if (prio == m_priorities)
        return;

    QCborArray arr;
    for (const int p : prio)
        arr.append(p);
    Ipc::IpcMessage msg(Ipc::IpcMsgType::SetTorrentFilePriorities);
    msg.append(m_id);
    msg.append(arr);
    m_ipc->sendRequest(std::move(msg), [this](const Ipc::IpcMessage& resp) {
        if (resp.isValid() && !resp.fieldBool(0))
            StatusBarNotifier::post(tr("Could not change the file priorities."));
        poll();
    });
}

void TorrentDetailsDialog::applyFiles(const QCborArray& files)
{
    // In place by file index, so the selection survives the poll
    QHash<int, QTreeWidgetItem*> existing;
    for (int i = 0; i < m_files->topLevelItemCount(); ++i) {
        auto* item = m_files->topLevelItem(i);
        existing.insert(item->data(FileName, kFileIndexRole).toInt(), item);
    }

    int maxIndex = -1;
    for (const auto& v : files)
        maxIndex = std::max(maxIndex, int(v.toMap().value(QStringLiteral("index")).toInteger()));
    m_priorities = QList<int>(maxIndex + 1, 4);

    m_files->setSortingEnabled(false);
    QSet<int> seen;
    for (const auto& v : files) {
        const QCborMap f = v.toMap();
        const int index = int(f.value(QStringLiteral("index")).toInteger());
        const int priority = int(f.value(QStringLiteral("priority")).toInteger(4));
        if (index >= 0 && index < m_priorities.size())
            m_priorities[index] = priority;
        if (f.value(QStringLiteral("pad")).toBool())
            continue;   // alignment padding, not a real file

        seen.insert(index);
        QTreeWidgetItem* item = existing.value(index);
        if (!item) {
            item = makeItem(m_files);
            item->setData(FileName, kFileIndexRole, index);
        }
        const qint64 size = f.value(QStringLiteral("size")).toInteger();
        const qint64 done = f.value(QStringLiteral("done")).toInteger();
        const double share = size > 0 ? double(done) / double(size) : 1.0;
        setCell(item, FileName, f.value(QStringLiteral("path")).toString());
        setCell(item, FileSize, formatByteSize(size), QVariant::fromValue(size), true);
        setCell(item, FileDone, formatByteSize(done), QVariant::fromValue(done), true);
        setCell(item, FileProgress, percentText(share), share, true);
        setCell(item, FilePriority, torrentFilePriorityName(priority), priority);
        item->setForeground(FileName, priority == 0 ? QBrush(QColor(0x88, 0x88, 0x88)) : QBrush());
    }
    for (auto it = existing.cbegin(); it != existing.cend(); ++it) {
        if (!seen.contains(it.key()))
            delete it.value();
    }
    m_files->setSortingEnabled(true);
    m_tabs->setTabText(TabFiles, tr("Files (%1)").arg(m_files->topLevelItemCount()));
}

void TorrentDetailsDialog::applyPeers(const QCborArray& peers)
{
    m_peers->setSortingEnabled(false);
    m_peers->clear();
    for (const auto& v : peers) {
        const QCborMap p = v.toMap();
        auto* item = makeItem(m_peers);
        const double progress = p.value(QStringLiteral("progress")).toDouble();
        const qint64 down = p.value(QStringLiteral("rateDown")).toInteger();
        const qint64 up = p.value(QStringLiteral("rateUp")).toInteger();
        const qint64 downloaded = p.value(QStringLiteral("downloaded")).toInteger();
        const qint64 uploaded = p.value(QStringLiteral("uploaded")).toInteger();
        QStringList flags;
        if (p.value(QStringLiteral("seed")).toBool())
            flags << tr("Seed");
        if (p.value(QStringLiteral("encrypted")).toBool())
            flags << tr("Enc");
        if (p.value(QStringLiteral("utp")).toBool())
            flags << QStringLiteral("µTP");
        setCell(item, 0, p.value(QStringLiteral("address")).toString());
        setCell(item, 1, p.value(QStringLiteral("client")).toString());
        setCell(item, 2, percentText(progress), progress, true);
        setCell(item, 3, rateText(down), QVariant::fromValue(down), true);
        setCell(item, 4, rateText(up), QVariant::fromValue(up), true);
        setCell(item, 5, formatByteSize(downloaded), QVariant::fromValue(downloaded), true);
        setCell(item, 6, formatByteSize(uploaded), QVariant::fromValue(uploaded), true);
        setCell(item, 7, flags.join(QStringLiteral(", ")));
        setCell(item, 8, p.value(QStringLiteral("source")).toString());
    }
    m_peers->setSortingEnabled(true);
    m_tabs->setTabText(TabPeers, tr("Peers (%1)").arg(peers.size()));
}

void TorrentDetailsDialog::applyTrackers(const QCborArray& trackers)
{
    m_trackers->setSortingEnabled(false);
    m_trackers->clear();
    for (const auto& v : trackers) {
        const QCborMap t = v.toMap();
        auto* item = makeItem(m_trackers);
        const int tier = int(t.value(QStringLiteral("tier")).toInteger());
        const qint64 seeds = t.value(QStringLiteral("seeds")).toInteger(-1);
        const qint64 peers = t.value(QStringLiteral("peers")).toInteger(-1);
        const QString message = t.value(QStringLiteral("message")).toString();
        QString status = t.value(QStringLiteral("working")).toBool() ? tr("Working")
                                                                     : tr("Not working");
        if (!message.isEmpty())
            status += QStringLiteral(" — ") + message;
        setCell(item, 0, t.value(QStringLiteral("url")).toString());
        setCell(item, 1, QString::number(tier), tier, true);
        setCell(item, 2, status);
        setCell(item, 3, countText(seeds), QVariant::fromValue(seeds), true);
        setCell(item, 4, countText(peers), QVariant::fromValue(peers), true);
    }
    m_trackers->setSortingEnabled(true);
    m_tabs->setTabText(TabTrackers, tr("Trackers (%1)").arg(trackers.size()));
}

} // namespace eMule
