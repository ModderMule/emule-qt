#include "pch.h"

/// @file UsenetArchiveEntryDialog.cpp
/// @brief Pick which file inside a Usenet archive set to preview.

#include "dialogs/UsenetArchiveEntryDialog.h"

#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "controls/SortableItems.h"
#include "utils/DialogSizing.h"
#include "utils/FileTypeIcons.h"
#include "utils/StringUtils.h"

#include <QCborArray>
#include <QCborMap>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eMule {

namespace {

/// Matches UsenetQueue::ArchiveListing::Status over the wire.
enum class ListStatus { Unknown = 0, Scanning = 1, Complete = 2, NotSeekable = 3, NotAnArchive = 4 };

/// Between the preview route's 250 ms and the queue's own tick: responsive
/// without pretending to be the download scheduler.
constexpr int kPollMs = 500;

/// Four times the route's stream wait. A player times out on its own; here a
/// person is watching a Cancel button, so patience is cheaper.
constexpr int kScanDeadlineMs = 60000;

/// Long enough that a set already parsed answers first, short enough that a
/// window never flashes on its way to being dismissed.
constexpr int kRevealDelayMs = 200;

enum Column { ColName = 0, ColSize, ColStatus, ColCount };

} // namespace

UsenetArchiveEntryDialog::UsenetArchiveEntryDialog(IpcClient* ipc, QString itemId, int fileIndex,
                                                   QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
    , m_itemId(std::move(itemId))
    , m_fileIndex(fileIndex)
    , m_deadline(kScanDeadlineMs)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Preview File"));

    buildUi();

    m_poll = new QTimer(this);
    m_poll->setInterval(kPollMs);
    connect(m_poll, &QTimer::timeout, this, &UsenetArchiveEntryDialog::poll);
    m_poll->start();
    poll();

    // Stay hidden until the scan proves there is a choice worth making. A set
    // already on disk answers in one loopback round trip, so the common case
    // never puts a window on screen at all.
    QTimer::singleShot(kRevealDelayMs, this, &UsenetArchiveEntryDialog::revealIfStillScanning);

    DialogSizing::applySize(this, {}, QSize(520, 340), DialogSizing::Fit::Layout);
}

void UsenetArchiveEntryDialog::applyListing(const QCborMap& listing)
{
    const auto status = ListStatus(listing.value(QStringLiteral("status")).toInteger(0));
    const QCborArray entries = listing.value(QStringLiteral("entries")).toArray();

    m_tree->clear();
    int playable = 0;
    int firstPlayable = -1;

    for (const auto& value : entries) {
        const QCborMap row = value.toMap();
        const int entry = int(row.value(QStringLiteral("entry")).toInteger(-1));
        const QString name = row.value(QStringLiteral("name")).toString();
        const qint64 size = row.value(QStringLiteral("size")).toInteger(0);
        const bool canPlay = row.value(QStringLiteral("playable")).toBool();
        const QString note = row.value(QStringLiteral("note")).toString();

        auto* item = new SortableTreeItem(m_tree);
        item->setText(ColName, name);
        item->setIcon(ColName, fileTypeIconForName(name));
        item->setText(ColSize, size > 0 ? formatByteSize(size) : QString());
        // SortRole, not Qt::UserRole: ColName already spends UserRole on the
        // entry ordinal below, and nothing reads UserRole for sorting anyway.
        item->setData(ColSize, SortRole, size);
        item->setText(ColStatus, canPlay ? tr("Playable") : note);
        item->setData(ColName, Qt::UserRole, entry);  // never the row index: this list sorts

        if (canPlay) {
            ++playable;
            if (firstPlayable < 0)
                firstPlayable = entry;
            for (int c = 0; c < ColCount; ++c)
                item->setData(c, Qt::ForegroundRole, QVariant());
        } else {
            // Both flags, not just ItemIsSelectable: without clearing
            // ItemIsEnabled the row still takes arrow-key focus in most styles,
            // and the user lands on it wondering why Preview stays greyed.
            item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
            // Clear the role rather than painting a "normal" brush, so the grey
            // follows the palette through a theme change.
            for (int c = 0; c < ColCount; ++c)
                item->setForeground(c, palette().brush(QPalette::Disabled, QPalette::Text));
        }
    }

    m_scanning = (status == ListStatus::Scanning);

    switch (status) {
    case ListStatus::NotSeekable:
        stopPolling();
        m_status->setText(listing.value(QStringLiteral("note")).toString());
        break;
    case ListStatus::Unknown:
        stopPolling();
        m_status->setText(tr("Nothing has arrived for this download yet."));
        break;
    case ListStatus::Scanning:
        m_status->setText(tr("Reading the archive… %n file(s) found so far", nullptr,
                             int(entries.size())));
        break;
    case ListStatus::Complete:
    case ListStatus::NotAnArchive:
        stopPolling();
        m_status->setText(tr("%n file(s) in the archive.", nullptr, int(entries.size())));
        break;
    }

    // Nothing to choose between: answer and go, whether or not we were ever
    // visible. A set with one playable file behaves exactly as it did before
    // this dialog existed.
    if (!m_scanning && !m_revealed && playable <= 1) {
        finishWith(playable == 1 ? firstPlayable : -1);
        return;
    }

    if (m_tree->currentItem() == nullptr) {
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = m_tree->topLevelItem(i);
            if (item->flags() & Qt::ItemIsSelectable) {
                m_tree->setCurrentItem(item);
                break;
            }
        }
    }

    updateButtons();
}

// --- private ---------------------------------------------------------------

void UsenetArchiveEntryDialog::buildUi()
{
    auto* layout = new QVBoxLayout(this);

    auto* tree = new ListTreeWidget(this);
    m_tree = tree;
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({tr("Name"), tr("Size"), tr("Status")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSortingEnabled(true);
    // Ascending by name unless a saved layout says otherwise: for a season pack
    // that is episode order, and Qt's untouched default comes up descending.
    m_tree->sortByColumn(ColName, Qt::AscendingOrder);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setStretchLastSection(true);
    tree->bindColumns(QStringLiteral("usenetArchiveEntries"), {260, 90, 110});
    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &UsenetArchiveEntryDialog::updateButtons);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item && (item->flags() & Qt::ItemIsSelectable))
            finishWith(item->data(ColName, Qt::UserRole).toInt());
    });

    m_busy = new QProgressBar(this);
    m_busy->setRange(0, 0);            // indeterminate: there is no total to count to
    m_busy->setTextVisible(false);
    m_busy->setMaximumHeight(16);
    layout->addWidget(m_busy);

    m_status = new QLabel(tr("Reading the archive…"), this);
    layout->addWidget(m_status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_previewBtn = buttons->addButton(tr("Preview"), QDialogButtonBox::AcceptRole);
    m_previewBtn->setEnabled(false);
    m_keepScanningBtn = buttons->addButton(tr("Keep Scanning"), QDialogButtonBox::ActionRole);
    m_keepScanningBtn->setVisible(false);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(m_previewBtn, &QPushButton::clicked, this, [this] {
        const QTreeWidgetItem* item = m_tree->currentItem();
        if (item)
            finishWith(item->data(ColName, Qt::UserRole).toInt());
    });
    connect(m_keepScanningBtn, &QPushButton::clicked, this, [this] {
        m_deadline = QDeadlineTimer(kScanDeadlineMs);
        m_keepScanningBtn->setVisible(false);
        m_busy->setVisible(true);
        m_poll->start();
        poll();
    });
}

void UsenetArchiveEntryDialog::poll()
{
    if (m_done)
        return;

    if (!m_ipc || !m_ipc->isConnected()) {
        stopPolling();
        m_status->setText(tr("The connection to the core was lost."));
        return;
    }

    if (m_deadline.hasExpired()) {
        stopPolling();
        m_status->setText(tr("The archive is still being read — the files listed so far are "
                             "all that is known."));
        m_keepScanningBtn->setVisible(true);
        // Reveal even if we were still hidden: a scan this slow is worth
        // showing rather than silently abandoning.
        revealIfStillScanning();
        return;
    }

    Ipc::IpcMessage msg(Ipc::IpcMsgType::ListUsenetArchiveEntries);
    msg.append(m_itemId);
    msg.append(qint64(m_fileIndex));

    QPointer<UsenetArchiveEntryDialog> self(this);
    m_ipc->sendRequest(std::move(msg), [self](const Ipc::IpcMessage& resp) {
        if (!self || !resp.fieldBool(0))
            return;
        self->applyListing(resp.fieldMap(1));
    });
}

void UsenetArchiveEntryDialog::revealIfStillScanning()
{
    if (m_done || m_revealed)
        return;
    m_revealed = true;
    show();
}

void UsenetArchiveEntryDialog::updateButtons()
{
    const QTreeWidgetItem* item = m_tree->currentItem();
    m_previewBtn->setEnabled(item && (item->flags() & Qt::ItemIsSelectable));
}

void UsenetArchiveEntryDialog::stopPolling()
{
    m_scanning = false;
    if (m_poll)
        m_poll->stop();
    if (m_busy)
        m_busy->setVisible(false);
}

void UsenetArchiveEntryDialog::finishWith(int entry)
{
    if (m_done)
        return;
    m_done = true;
    m_chosen = entry;
    stopPolling();
    emit entryChosen(entry);
    close();
}

} // namespace eMule
