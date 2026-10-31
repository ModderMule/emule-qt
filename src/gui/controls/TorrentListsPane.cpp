#include "pch.h"
/// @file TorrentListsPane.cpp
/// @brief Peers, Trackers and Files of the selected torrent — implementation.

#include "controls/TorrentListsPane.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/AbstractListView.h"
#include "controls/SortableItems.h"
#include "controls/TorrentFileTree.h"
#include "utils/MenuUtils.h"
#include "utils/PanelPoller.h"
#include "utils/StatusBarNotifier.h"
#include "utils/StringUtils.h"

#include <QCborArray>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QSet>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <array>
#include <functional>
#include <utility>

namespace eMule {

namespace {

constexpr int kPollMs = 2000;
/// File index on a Files row; -1 on a folder row
constexpr int kFileIndexRole = TorrentFileTree::kFileIndexRole;
/// Peer address / tracker URL on a row; folder path on a folder row
constexpr int kKeyRole = TorrentFileTree::kPathRole;

/// The priority levels of the Files menu, low to high.
constexpr std::array kPriorityLevels{0, 1, 4, 6, 7};

void setCell(QTreeWidgetItem* item, int column, const QString& text, const QVariant& sortKey = {},
             bool right = false)
{
    if (item->text(column) != text)
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

QIcon lockIcon()
{
    static const QIcon icon = [] {
        QIcon i = QIcon::fromTheme(QIcon::ThemeIcon::SecurityHigh);
        return i.isNull() ? menuIcon("ClientSecureHashOvl.ico") : i;
    }();
    return icon;
}

QString sourceName(const QString& source)
{
    if (source == QLatin1String("tracker"))
        return TorrentListsPane::tr("Tracker");
    if (source == QLatin1String("dht"))
        return TorrentListsPane::tr("DHT");
    if (source == QLatin1String("pex"))
        return TorrentListsPane::tr("PEX");
    if (source == QLatin1String("lsd"))
        return TorrentListsPane::tr("Local discovery");
    if (source == QLatin1String("resume"))
        return TorrentListsPane::tr("Resume data");
    if (source == QLatin1String("incoming"))
        return TorrentListsPane::tr("Incoming");
    return source;
}

/// Top-level items by their kKeyRole.
QHash<QString, QTreeWidgetItem*> itemsByKey(QTreeWidget* tree)
{
    QHash<QString, QTreeWidgetItem*> out;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto* item = tree->topLevelItem(i);
        out.insert(item->data(0, kKeyRole).toString(), item);
    }
    return out;
}

void forEachFile(QTreeWidgetItem* item, const std::function<void(QTreeWidgetItem*)>& fn)
{
    if (item->data(TorrentListsPane::FileName, kFileIndexRole).toInt() >= 0) {
        fn(item);
        return;
    }
    for (int i = 0; i < item->childCount(); ++i)
        forEachFile(item->child(i), fn);
}

} // namespace

QString torrentPeerFlagsTooltip(const QString& flags)
{
    QStringList lines;
    for (const QChar c : flags) {
        QString text;
        switch (c.unicode()) {
        case 'T': text = TorrentListsPane::tr("Peer is connected over µTP"); break;
        case 'O': text = TorrentListsPane::tr("Optimistic unchoke"); break;
        case 'D': text = TorrentListsPane::tr("Downloading from this peer"); break;
        case 'd': text = TorrentListsPane::tr("We would download from this peer if they would let us"); break;
        case 'U': text = TorrentListsPane::tr("Uploading to peer"); break;
        case 'u': text = TorrentListsPane::tr("We would upload to this peer if we unchoked it"); break;
        case 'K': text = TorrentListsPane::tr("Peer has unchoked us, but we're not interested"); break;
        case '?': text = TorrentListsPane::tr("We unchoked this peer, but they're not interested"); break;
        case 'E': text = TorrentListsPane::tr("Encrypted connection"); break;
        case 'h': text = TorrentListsPane::tr("Connected through a holepunch"); break;
        case 'H': text = TorrentListsPane::tr("Peer was found through DHT"); break;
        case 'X': text = TorrentListsPane::tr("Peer was found through Peer Exchange (PEX)"); break;
        case 'I': text = TorrentListsPane::tr("Peer is an incoming connection"); break;
        default: continue;
        }
        lines << QStringLiteral("%1: %2").arg(c, text);
    }
    return lines.join(QLatin1Char('\n'));
}

TorrentListsPane::TorrentListsPane(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    m_poller = new PanelPoller(this, [this] { poll(); });
    m_poller->setInterval(kPollMs);
}

void TorrentListsPane::setIpcClient(IpcClient* ipc)
{
    m_ipc = ipc;
    if (!m_ipc)
        return;
    m_poller->setEnabled(m_ipc->isConnected());
    connect(m_ipc, &IpcClient::connected, this, [this] { m_poller->setEnabled(true); });
    connect(m_ipc, &IpcClient::disconnected, this, [this] { m_poller->setEnabled(false); });
}

void TorrentListsPane::setTorrentId(const QString& id)
{
    if (id == m_id)
        return;
    m_id = id;
    m_updating = true;
    m_peers->clear();
    m_trackers->clear();
    m_files->clear();
    m_updating = false;
    m_priorities.clear();
    for (int tab = TabPeers; tab <= TabFiles; ++tab)
        m_tabs->setTabText(tab, m_tabs->tabText(tab).section(QStringLiteral(" ("), 0, 0));
    if (m_id.isEmpty()) {
        showNote(tr("Select a torrent to see its peers, trackers and files."));
        return;
    }
    m_stack->setCurrentWidget(m_tabs);
    m_poller->refreshNow();
}

void TorrentListsPane::applyDetails(const QCborMap& details)
{
    m_stack->setCurrentWidget(m_tabs);
    applyPeers(details.value(QStringLiteral("peers")).toArray());
    applyTrackers(details.value(QStringLiteral("trackers")).toArray());
    applyFiles(details.value(QStringLiteral("files")).toArray());
}

QList<int> TorrentListsPane::prioritiesFromChecks() const
{
    QList<int> prio = m_priorities;
    for (int i = 0; i < m_files->topLevelItemCount(); ++i) {
        forEachFile(m_files->topLevelItem(i), [&prio](QTreeWidgetItem* item) {
            const int index = item->data(FileName, kFileIndexRole).toInt();
            if (index < 0 || index >= prio.size())
                return;
            const bool wanted = item->checkState(FileDownload) != Qt::Unchecked;
            if (!wanted)
                prio[index] = 0;
            else if (prio[index] == 0)
                prio[index] = 4;
        });
    }
    return prio;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void TorrentListsPane::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget(this);
    m_note = new QLabel(m_stack);
    m_note->setAlignment(Qt::AlignCenter);
    m_note->setWordWrap(true);
    m_note->setEnabled(false);
    m_stack->addWidget(m_note);

    m_tabs = new QTabWidget(m_stack);
    m_tabs->setDocumentMode(true);

    auto* peers = new ListTreeWidget(m_tabs);
    m_peers = peers;
    peers->setColumnCount(PeerColCount);
    peers->setHeaderLabels({QString(), tr("Up"), tr("Down"), tr("%"), tr("Status"), tr("Address"),
                            tr("Client"), tr("Downloaded"), tr("Uploaded"), tr("Source")});
    peers->headerItem()->setIcon(PeerLock, lockIcon());
    peers->headerItem()->setToolTip(PeerLock, tr("Encrypted connection"));
    peers->setRootIsDecorated(false);
    peers->setAlternatingRowColors(true);
    peers->setUniformRowHeights(true);
    peers->setSelectionMode(QAbstractItemView::ExtendedSelection);
    peers->setSortingEnabled(true);
    peers->setDescendingFirst({PeerUp, PeerDown, PeerProgress, PeerDownloaded, PeerUploaded});
    peers->bindColumns(QStringLiteral("torrentPanePeers"),
                       {24, 75, 75, 55, 80, 170, 150, 85, 85, 80});
    if (!theUiState.hasHeaderState(QStringLiteral("torrentPanePeers")))
        peers->sortByColumn(PeerAddress, Qt::AscendingOrder);
    m_tabs->insertTab(TabPeers, peers, menuIcon("ClientsKnown.ico"), tr("Peers"));

    m_tabs->insertTab(TabTrackers, buildTrackersPage(), menuIcon("Server.ico"), tr("Trackers"));

    auto* files = new ListTreeWidget(m_tabs);
    m_files = files;
    files->setColumnCount(FileColCount);
    files->setHeaderLabels({tr("Name"), tr("Size"), tr("Progress"), tr("Download"), tr("Priority")});
    files->setAlternatingRowColors(true);
    files->setUniformRowHeights(true);
    files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    files->setSortingEnabled(true);
    files->setContextMenuPolicy(Qt::CustomContextMenu);
    files->setDescendingFirst({FileSize, FileProgress});
    files->bindColumns(QStringLiteral("torrentPaneFiles"), {360, 90, 80, 70, 80});
    connect(files, &QWidget::customContextMenuRequested, this,
            &TorrentListsPane::onFilesContextMenu);
    connect(files, &QTreeWidget::itemChanged, this, &TorrentListsPane::onFileItemChanged);
    m_tabs->insertTab(TabFiles, files, menuIcon("FileInfo.ico"), tr("Files"));

    m_tabs->setCurrentIndex(std::clamp(theUiState.torrentPaneTab(), 0, int(TabFiles)));
    connect(m_tabs, &QTabWidget::currentChanged, this,
            [](int tab) { theUiState.setTorrentPaneTab(tab); });

    m_stack->addWidget(m_tabs);
    layout->addWidget(m_stack);
    showNote(tr("Select a torrent to see its peers, trackers and files."));
}

QWidget* TorrentListsPane::buildTrackersPage()
{
    auto* page = new QWidget(m_tabs);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto* trackers = new ListTreeWidget(page);
    m_trackers = trackers;
    trackers->setColumnCount(TrackerColCount);
    trackers->setHeaderLabels({tr("URL"), tr("Tier"), tr("Status"), tr("Next Announce"),
                               tr("Seeds"), tr("Peers"), tr("Downloaded")});
    trackers->setRootIsDecorated(false);
    trackers->setAlternatingRowColors(true);
    trackers->setUniformRowHeights(true);
    trackers->setSelectionMode(QAbstractItemView::ExtendedSelection);
    trackers->setSortingEnabled(true);
    trackers->setDescendingFirst({TrackerSeeds, TrackerPeers, TrackerDownloaded});
    trackers->bindColumns(QStringLiteral("torrentPaneTrackers"), {330, 45, 260, 100, 60, 60, 80});
    if (!theUiState.hasHeaderState(QStringLiteral("torrentPaneTrackers")))
        trackers->sortByColumn(TrackerTier, Qt::AscendingOrder);
    connect(trackers, &QTreeWidget::itemSelectionChanged, this, [this] {
        m_removeTracker->setEnabled(!m_trackers->selectedItems().isEmpty());
    });
    layout->addWidget(trackers, 1);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(4, 0, 4, 2);
    auto addButton = [page, row](const char* icon, const QString& text) {
        auto* b = new QToolButton(page);
        b->setIcon(menuIcon(icon));
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setAutoRaise(true);
        row->addWidget(b);
        return b;
    };
    connect(addButton("ListAdd.ico", tr("Add…")), &QToolButton::clicked, this,
            &TorrentListsPane::onAddTrackers);
    m_removeTracker = addButton("ListRemove.ico", tr("Remove"));
    m_removeTracker->setEnabled(false);
    connect(m_removeTracker, &QToolButton::clicked, this, &TorrentListsPane::onRemoveTrackers);
    connect(addButton("ServersUpdate.ico", tr("Reannounce")), &QToolButton::clicked, this,
            &TorrentListsPane::onReannounce);
    row->addStretch(1);
    m_showBackup = new QCheckBox(tr("Show backup trackers"), page);
    m_showBackup->setChecked(true);
    connect(m_showBackup, &QCheckBox::toggled, this, &TorrentListsPane::updateTrackerVisibility);
    row->addWidget(m_showBackup);
    layout->addLayout(row);
    return page;
}

void TorrentListsPane::poll()
{
    if (!m_ipc || !m_ipc->isConnected() || m_id.isEmpty())
        return;
    Ipc::IpcMessage msg(Ipc::IpcMsgType::GetTorrentDetails);
    msg.append(m_id);
    m_ipc->sendPoll(this, std::move(msg), [this, id = m_id](const Ipc::IpcMessage& resp) {
        if (!resp.isValid() || id != m_id)
            return;   // the selection moved on meanwhile
        if (resp.type() == Ipc::IpcMsgType::Error) {
            showNote(resp.fieldInt(0) == 404 ? tr("This torrent is no longer in the list.")
                                             : tr("BitTorrent is not running."));
            return;
        }
        if (resp.fieldBool(0))
            applyDetails(resp.fieldMap(1));
    });
}

void TorrentListsPane::showNote(const QString& text)
{
    m_note->setText(text);
    m_stack->setCurrentWidget(m_note);
}

void TorrentListsPane::applyPeers(const QCborArray& peers)
{
    // In place by address, so the selection survives the poll
    QHash<QString, QTreeWidgetItem*> existing = itemsByKey(m_peers);
    m_peers->setSortingEnabled(false);
    for (const auto& v : peers) {
        const QCborMap p = v.toMap();
        const QString address = p.value(QStringLiteral("address")).toString();
        QTreeWidgetItem* item = existing.take(address);
        if (!item) {
            item = new SortableTreeItem(m_peers);
            item->setData(0, kKeyRole, address);
        }
        const double progress = p.value(QStringLiteral("progress")).toDouble();
        const qint64 down = p.value(QStringLiteral("rateDown")).toInteger();
        const qint64 up = p.value(QStringLiteral("rateUp")).toInteger();
        const qint64 downloaded = p.value(QStringLiteral("downloaded")).toInteger();
        const qint64 uploaded = p.value(QStringLiteral("uploaded")).toInteger();
        const bool encrypted = p.value(QStringLiteral("encrypted")).toBool();
        const QString flags = p.value(QStringLiteral("flags")).toString();

        item->setIcon(PeerLock, encrypted ? lockIcon() : QIcon());
        item->setData(PeerLock, SortRole, encrypted ? 1 : 0);
        item->setToolTip(PeerLock, encrypted ? tr("Encrypted connection") : QString());
        setCell(item, PeerUp, rateText(up), QVariant::fromValue(up), true);
        setCell(item, PeerDown, rateText(down), QVariant::fromValue(down), true);
        setCell(item, PeerProgress, percentText(progress), progress, true);
        setCell(item, PeerStatus, flags);
        item->setToolTip(PeerStatus, torrentPeerFlagsTooltip(flags));
        setCell(item, PeerAddress, address);
        setCell(item, PeerClient, p.value(QStringLiteral("client")).toString());
        setCell(item, PeerDownloaded, formatByteSize(downloaded), QVariant::fromValue(downloaded), true);
        setCell(item, PeerUploaded, formatByteSize(uploaded), QVariant::fromValue(uploaded), true);
        setCell(item, PeerSource, sourceName(p.value(QStringLiteral("source")).toString()));
    }
    qDeleteAll(existing);
    m_peers->setSortingEnabled(true);
    m_tabs->setTabText(TabPeers, tr("Peers (%1)").arg(m_peers->topLevelItemCount()));
}

void TorrentListsPane::applyTrackers(const QCborArray& trackers)
{
    QHash<QString, QTreeWidgetItem*> existing = itemsByKey(m_trackers);
    m_trackers->setSortingEnabled(false);
    for (const auto& v : trackers) {
        const QCborMap t = v.toMap();
        const QString url = t.value(QStringLiteral("url")).toString();
        QTreeWidgetItem* item = existing.take(url);
        if (!item) {
            item = new SortableTreeItem(m_trackers);
            item->setData(0, kKeyRole, url);
        }
        const int tier = int(t.value(QStringLiteral("tier")).toInteger());
        const qint64 seeds = t.value(QStringLiteral("seeds")).toInteger(-1);
        const qint64 peers = t.value(QStringLiteral("peers")).toInteger(-1);
        const qint64 downloaded = t.value(QStringLiteral("downloaded")).toInteger(-1);
        const qint64 next = t.value(QStringLiteral("nextAnnounce")).toInteger(-1);
        const QString message = t.value(QStringLiteral("message")).toString();
        const bool working = t.value(QStringLiteral("working")).toBool();
        const bool updating = t.value(QStringLiteral("updating")).toBool();
        const bool hasError = t.value(QStringLiteral("hasError")).toBool();

        QString status;
        QColor color;
        if (updating) {
            status = tr("Announcing…");
        } else if (hasError) {
            status = tr("Error: %1").arg(message);
            color = QColor(0xc0, 0x20, 0x20);
        } else if (working) {
            status = message.isEmpty() ? tr("Working") : tr("Working — %1").arg(message);
            color = QColor(0x20, 0x90, 0x20);
        } else {
            status = message.isEmpty() ? tr("Not contacted yet") : message;
        }
        setCell(item, TrackerUrl, url);
        item->setToolTip(TrackerUrl, url);
        setCell(item, TrackerTier, QString::number(tier + 1), tier, true);
        setCell(item, TrackerStatus, status);
        item->setToolTip(TrackerStatus, status);
        item->setForeground(TrackerStatus, color.isValid() ? QBrush(color) : QBrush());
        setCell(item, TrackerNext, next >= 0 ? formatSecondsHM(next) : QString(),
                QVariant::fromValue(next), true);
        setCell(item, TrackerSeeds, countText(seeds), QVariant::fromValue(seeds), true);
        setCell(item, TrackerPeers, countText(peers), QVariant::fromValue(peers), true);
        setCell(item, TrackerDownloaded, countText(downloaded), QVariant::fromValue(downloaded), true);
    }
    qDeleteAll(existing);
    m_trackers->setSortingEnabled(true);
    updateTrackerVisibility();
    m_tabs->setTabText(TabTrackers, tr("Trackers (%1)").arg(m_trackers->topLevelItemCount()));
}

void TorrentListsPane::updateTrackerVisibility()
{
    const bool showBackup = m_showBackup->isChecked();
    for (int i = 0; i < m_trackers->topLevelItemCount(); ++i) {
        auto* item = m_trackers->topLevelItem(i);
        item->setHidden(!showBackup && item->data(TrackerTier, SortRole).toInt() > 0);
    }
}

void TorrentListsPane::applyFiles(const QCborArray& files)
{
    // In place by file index / folder path, so selection and expansion survive
    QHash<int, QTreeWidgetItem*> existing;
    QHash<QString, QTreeWidgetItem*> folders;
    std::function<void(QTreeWidgetItem*)> collect = [&](QTreeWidgetItem* item) {
        const int index = item->data(FileName, kFileIndexRole).toInt();
        if (index >= 0)
            existing.insert(index, item);
        else
            folders.insert(item->data(0, kKeyRole).toString(), item);
        for (int i = 0; i < item->childCount(); ++i)
            collect(item->child(i));
    };
    for (int i = 0; i < m_files->topLevelItemCount(); ++i)
        collect(m_files->topLevelItem(i));
    const QSet<QTreeWidgetItem*> oldFolders(folders.cbegin(), folders.cend());

    int maxIndex = -1;
    for (const auto& v : files)
        maxIndex = std::max(maxIndex, int(v.toMap().value(QStringLiteral("index")).toInteger()));
    m_priorities = QList<int>(maxIndex + 1, 4);

    m_updating = true;
    m_files->setSortingEnabled(false);
    QSet<QTreeWidgetItem*> usedFolders;
    QSet<int> seen;
    for (const auto& v : files) {
        const QCborMap f = v.toMap();
        const int index = int(f.value(QStringLiteral("index")).toInteger());
        const int priority = int(f.value(QStringLiteral("priority")).toInteger(4));
        if (index >= 0 && index < m_priorities.size())
            m_priorities[index] = priority;
        if (f.value(QStringLiteral("pad")).toBool())
            continue;   // alignment padding, not a real file

        const QString path = f.value(QStringLiteral("path")).toString();
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        QTreeWidgetItem* parent = slash > 0 ? folderItem(path.left(slash), folders) : nullptr;
        for (auto* p = parent; p; p = p->parent())
            usedFolders.insert(p);

        seen.insert(index);
        QTreeWidgetItem* item = existing.value(index);
        if (item && item->parent() != parent) {
            delete item;   // moved: rebuilt below
            item = nullptr;
        }
        if (!item) {
            item = parent ? new SortableTreeItem(parent) : new SortableTreeItem(m_files);
            item->setData(FileName, kFileIndexRole, index);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        }
        const qint64 size = f.value(QStringLiteral("size")).toInteger();
        const qint64 done = f.value(QStringLiteral("done")).toInteger();
        const double share = size > 0 ? double(done) / double(size) : 1.0;
        setCell(item, FileName, slash >= 0 ? path.mid(slash + 1) : path);
        item->setToolTip(FileName, path);
        item->setData(FileSize, kKeyRole, size);
        item->setData(FileProgress, kKeyRole, done);
        setCell(item, FileSize, formatByteSize(size), QVariant::fromValue(size), true);
        setCell(item, FileProgress, percentText(share), share, true);
        item->setCheckState(FileDownload, priority > 0 ? Qt::Checked : Qt::Unchecked);
        item->setData(FileDownload, SortRole, priority > 0 ? 1 : 0);
        setCell(item, FilePriority, priority > 0 ? torrentFilePriorityName(priority) : QString(),
                priority);
        item->setForeground(FileName, priority == 0 ? QBrush(QColor(0x88, 0x88, 0x88)) : QBrush());
    }
    for (auto it = existing.cbegin(); it != existing.cend(); ++it)
        if (!seen.contains(it.key()))
            delete it.value();
    // only the topmost unused folder: deleting it takes its subfolders along
    for (auto* folder : oldFolders)
        if (!usedFolders.contains(folder) && (!folder->parent() || usedFolders.contains(folder->parent())))
            delete folder;
    sumFolders();
    m_files->setSortingEnabled(true);
    m_updating = false;   // folders start collapsed

    int count = 0;
    for (const int i : std::as_const(seen))
        count += i >= 0 ? 1 : 0;
    m_tabs->setTabText(TabFiles, tr("Files (%1)").arg(count));
}

QTreeWidgetItem* TorrentListsPane::folderItem(const QString& path,
                                              QHash<QString, QTreeWidgetItem*>& folders)
{
    return TorrentFileTree::folderItem(m_files, path, folders,
                                       Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
}

void TorrentListsPane::sumFolders()
{
    // post-order: sizes, progress and the check state of every folder from its files
    std::function<void(QTreeWidgetItem*, qint64&, qint64&)> sum =
        [&](QTreeWidgetItem* item, qint64& size, qint64& done) {
            if (item->data(FileName, kFileIndexRole).toInt() >= 0) {
                size += item->data(FileSize, kKeyRole).toLongLong();
                done += item->data(FileProgress, kKeyRole).toLongLong();
                return;
            }
            qint64 s = 0;
            qint64 d = 0;
            for (int i = 0; i < item->childCount(); ++i)
                sum(item->child(i), s, d);
            const double share = s > 0 ? double(d) / double(s) : 1.0;
            setCell(item, FileSize, formatByteSize(s), QVariant::fromValue(s), true);
            setCell(item, FileProgress, percentText(share), share, true);
            size += s;
            done += d;
        };
    for (int i = 0; i < m_files->topLevelItemCount(); ++i) {
        qint64 s = 0;
        qint64 d = 0;
        sum(m_files->topLevelItem(i), s, d);
    }
}

void TorrentListsPane::onFilesContextMenu(const QPoint& pos)
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
    menu.addSeparator();
    menu.addAction(tr("Expand All"), m_files, &QTreeWidget::expandAll);
    menu.addAction(tr("Collapse All"), m_files, &QTreeWidget::collapseAll);
    menu.exec(m_files->viewport()->mapToGlobal(pos));
}

void TorrentListsPane::onFileItemChanged(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(item);
    if (m_updating || column != FileDownload || m_checkSendQueued)
        return;
    // A folder click changes every child: send once, after the burst
    m_checkSendQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_checkSendQueued = false;
        sendPriorities(prioritiesFromChecks());
    });
}

void TorrentListsPane::setSelectedPriority(int priority)
{
    QList<int> prio = m_priorities;
    for (QTreeWidgetItem* selected : m_files->selectedItems()) {
        forEachFile(selected, [&prio, priority](QTreeWidgetItem* item) {
            const int index = item->data(FileName, kFileIndexRole).toInt();
            if (index >= 0 && index < prio.size())
                prio[index] = priority;
        });
    }
    sendPriorities(prio);
}

void TorrentListsPane::sendPriorities(const QList<int>& priorities)
{
    if (!m_ipc || !m_ipc->isConnected() || priorities.isEmpty() || priorities == m_priorities)
        return;
    QCborArray arr;
    for (const int p : priorities)
        arr.append(p);
    Ipc::IpcMessage msg(Ipc::IpcMsgType::SetTorrentFilePriorities);
    msg.append(m_id);
    msg.append(arr);
    m_ipc->sendRequest(std::move(msg), [this](const Ipc::IpcMessage& resp) {
        if (resp.isValid() && !resp.fieldBool(0))
            StatusBarNotifier::post(tr("Could not change the file priorities."));
        m_poller->refreshNow();
        emit changed();
    });
}

void TorrentListsPane::onAddTrackers()
{
    if (m_id.isEmpty())
        return;
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        this, tr("Add Trackers"), tr("Tracker URLs, one per line:"), QString(), &ok);
    if (!ok)
        return;
    QStringList urls;
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        if (const QString url = line.trimmed(); !url.isEmpty())
            urls << url;
    if (!urls.isEmpty())
        sendTrackerRequest(int(Ipc::IpcMsgType::AddTorrentTrackers), urls);
}

void TorrentListsPane::onRemoveTrackers()
{
    QStringList urls;
    for (const QTreeWidgetItem* item : m_trackers->selectedItems())
        urls << item->data(0, kKeyRole).toString();
    if (!urls.isEmpty())
        sendTrackerRequest(int(Ipc::IpcMsgType::RemoveTorrentTrackers), urls);
}

void TorrentListsPane::onReannounce()
{
    if (!m_ipc || !m_ipc->isConnected() || m_id.isEmpty())
        return;
    Ipc::IpcMessage msg(Ipc::IpcMsgType::ReannounceTorrent);
    msg.append(m_id);
    m_ipc->sendRequest(std::move(msg), [this](const Ipc::IpcMessage&) { m_poller->refreshNow(); });
}

void TorrentListsPane::sendTrackerRequest(int type, const QStringList& urls)
{
    if (!m_ipc || !m_ipc->isConnected() || m_id.isEmpty())
        return;
    QCborArray arr;
    for (const QString& url : urls)
        arr.append(url);
    Ipc::IpcMessage msg(static_cast<Ipc::IpcMsgType>(type));
    msg.append(m_id);
    msg.append(arr);
    m_ipc->sendRequest(std::move(msg), [this](const Ipc::IpcMessage& resp) {
        if (resp.isValid() && !resp.fieldBool(0))
            StatusBarNotifier::post(tr("Could not change the trackers."));
        m_poller->refreshNow();
        emit changed();
    });
}

} // namespace eMule
