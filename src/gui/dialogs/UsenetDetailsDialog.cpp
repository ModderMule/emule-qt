#include "pch.h"

/// @file UsenetDetailsDialog.cpp
/// @brief Everything about one queued Usenet release.

#include "dialogs/UsenetDetailsDialog.h"

#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "controls/SortableItems.h"
#include "controls/UsenetQueueModel.h"
#include "utils/DialogSizing.h"
#include "utils/FileTypeIcons.h"
#include "utils/PreviewLauncher.h"
#include "utils/StatusBarNotifier.h"

#include "prefs/Preferences.h"
#include "utils/OtherFunctions.h"
#include "utils/StringUtils.h"

#include <QCborArray>
#include <QCborMap>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace eMule {

namespace {

/// Slower than the archive chooser's 500 ms: nothing here is being waited on, and
/// a release's article tallies move at the speed of a person reading them.
constexpr int kPollMs = 1000;

/// Which release-wide fact a per-file field carries. An NZB records poster, date
/// and newsgroup per <file>, but a real release posts them identically across
/// every file, so the header shows the one value — and says so when it does not.
QString aggregate(const QStringList& values)
{
    QStringList distinct;
    for (const QString& v : values) {
        if (!v.isEmpty() && !distinct.contains(v))
            distinct.append(v);
    }
    if (distinct.isEmpty())
        return {};
    if (distinct.size() == 1)
        return distinct.first();

    // Naming one of several without saying so would be a quiet lie about the
    // other files; the first plus a count is honest and still short.
    return QCoreApplication::translate("UsenetDetailsDialog", "%1 (and %n other(s))",
                                       nullptr, int(distinct.size()) - 1)
        .arg(distinct.first());
}

/// The per-file status cell. Deliberately not the item's status: a file is done,
/// short, or still coming, and only the first of those is worth a word.
QString fileStatusText(int percent, int missing, bool finalized)
{
    if (missing > 0) {
        return QCoreApplication::translate("UsenetDetailsDialog", "%n article(s) missing",
                                           nullptr, missing);
    }
    if (finalized || percent >= 100)
        return QCoreApplication::translate("UsenetDetailsDialog", "Complete");
    if (percent > 0)
        return QCoreApplication::translate("UsenetDetailsDialog", "Downloading");
    return QCoreApplication::translate("UsenetDetailsDialog", "Queued");
}

} // namespace

UsenetDetailsDialog::UsenetDetailsDialog(IpcClient* ipc, QString itemId, QString streamToken,
                                         QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
    , m_itemId(std::move(itemId))
    , m_streamToken(std::move(streamToken))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Release Details"));

    buildUi();

    m_poll = new QTimer(this);
    m_poll->setInterval(kPollMs);
    connect(m_poll, &QTimer::timeout, this, &UsenetDetailsDialog::poll);
    m_poll->start();
    poll();

    DialogSizing::applySize(this, {}, QSize(760, 480), DialogSizing::Fit::Layout);
}

void UsenetDetailsDialog::applyDetails(const QCborMap& details)
{
    const UsenetItemRow row = usenetRowFromCbor(details);

    m_title->setText(row.name);
    setWindowTitle(row.name.isEmpty() ? tr("Release Details") : row.name);

    m_totalSize->setText(row.totalBytes > 0
                             ? formatByteSize(row.totalBytes)
                             : QString());

    QString status = row.statusText;
    if (!row.error.isEmpty())
        status += QStringLiteral(" — ") + row.error;
    else if (!row.stalledReason.isEmpty())
        status += QStringLiteral(" — ") + row.stalledReason;
    else if (!row.postDetail.isEmpty())
        status += QStringLiteral(" — ") + row.postDetail;
    m_status->setText(status);

    // -1 is "not assessed", and it must never render as 100 — the whole point of
    // the sentinel. Same wording as the queue list's Health tooltip.
    if (row.healthPercent < 0) {
        m_health->setText(QStringLiteral("—"));
        m_health->setToolTip(tr("Not checked."));
    } else {
        m_health->setText(QStringLiteral("%1%").arg(row.healthPercent));
        m_health->setToolTip(row.healthProbed
            ? tr("%1% of this release looks obtainable.").arg(row.healthPercent)
            : tr("%1% by the NZB's own article counts. No server was asked.")
                  .arg(row.healthPercent));
    }

    m_parts->setText(tr("%1 of %2")
                         .arg(QLocale::system().toString(row.doneSegments),
                              QLocale::system().toString(row.segmentCount)));
    m_fileCount->setText(QLocale::system().toString(int(row.files.size())));

    // Per-file header facts, aggregated across the release. PAR2 members carry
    // the same poster and date, so nothing is gained by excluding them here.
    const QCborArray files = details.value(QStringLiteral("files")).toArray();
    QStringList posters;
    QStringList groups;
    qint64 newest = 0;
    for (const auto& fv : files) {
        const QCborMap fm = fv.toMap();
        posters.append(fm.value(QStringLiteral("poster")).toString());
        for (const auto& g : fm.value(QStringLiteral("groups")).toArray())
            groups.append(g.toString());
        newest = qMax(newest, fm.value(QStringLiteral("date")).toInteger());
    }
    m_poster->setText(aggregate(posters));
    m_groups->setText(aggregate(groups));
    m_date->setText(newest > 0
                        ? QLocale::system().toString(
                              QDateTime::fromSecsSinceEpoch(newest), QLocale::ShortFormat)
                        : QString());

    // Rebuild rather than patch: a release's file list never changes shape once
    // the NZB is parsed, so the only cost is the current selection, which is
    // restored below by name.
    const QString selected = m_tree->currentItem()
                                 ? m_tree->currentItem()->text(ColName) : QString();

    const bool wasSorting = m_tree->isSortingEnabled();
    m_tree->setSortingEnabled(false);
    m_tree->clear();

    for (qsizetype i = 0; i < files.size(); ++i) {
        const QCborMap fm = files.at(i).toMap();

        const QString name = fm.value(QStringLiteral("name")).toString();
        const qint64 size = fm.value(QStringLiteral("size")).toInteger();
        const int percent = int(fm.value(QStringLiteral("percent")).toInteger());
        const int done = int(fm.value(QStringLiteral("doneSegments")).toInteger());
        const int total = int(fm.value(QStringLiteral("segmentCount")).toInteger());
        const int missing = int(fm.value(QStringLiteral("missingSegments")).toInteger());
        const bool isPar2 = fm.value(QStringLiteral("isPar2")).toBool();
        const bool finalized = fm.value(QStringLiteral("finalized")).toBool();
        const QString finalPath = fm.value(QStringLiteral("finalPath")).toString();

        auto* item = new SortableTreeItem(m_tree);
        item->setIcon(ColName, fileTypeIconForName(name));
        item->setText(ColName, name);
        item->setText(ColSize, size > 0 ? formatByteSize(size) : QString());
        item->setText(ColProgress, QStringLiteral("%1%").arg(percent));
        item->setText(ColArticles, QStringLiteral("%1/%2").arg(done).arg(total));
        item->setText(ColMissing, missing > 0 ? QString::number(missing) : QString());
        item->setText(ColStatus, fileStatusText(percent, missing, finalized));

        // Raw values behind the formatted ones, so these columns sort by
        // magnitude rather than by the leading digit of "9.90 MB". SortRole and
        // not Qt::UserRole: ColName spends UserRole on the path below, and a
        // comparator reading UserRole would compare paths as numbers -- 0 on both
        // sides, which is why the Name column used to not sort at all.
        item->setData(ColSize, SortRole, size);
        item->setData(ColProgress, SortRole, percent);
        item->setData(ColArticles, SortRole, done);
        item->setData(ColMissing, SortRole, missing);

        // What a double-click on this row opens. Empty for a file that was
        // consumed into an archive and never published on its own.
        item->setData(ColName, Qt::UserRole, finalPath);
        item->setData(ColName, Qt::UserRole + 1,
                      fm.value(QStringLiteral("relPath")).toString());

        item->setToolTip(ColName, [&] {
            QStringList lines;
            const QString subject = fm.value(QStringLiteral("subject")).toString();
            if (!subject.isEmpty())
                lines << subject;
            if (!finalPath.isEmpty())
                lines << finalPath;
            const int nzbShort = int(fm.value(QStringLiteral("nzbMissingSegments")).toInteger());
            if (nzbShort > 0) {
                // Distinct from `missing`: this is what the indexer never listed,
                // not what a server would not serve.
                lines << tr("%n article(s) were never listed in the NZB", nullptr, nzbShort);
            }
            return lines.join(QLatin1Char('\n'));
        }());

        // A recovery set is not payload, and greying it says so without hiding
        // it. The palette brush follows a theme change; a literal colour does not.
        if (isPar2) {
            const QBrush grey = palette().brush(QPalette::Disabled, QPalette::Text);
            for (int c = 0; c < ColCount; ++c)
                item->setForeground(c, grey);
        }
    }

    m_tree->setSortingEnabled(wasSorting);

    if (!selected.isEmpty()) {
        const auto matches = m_tree->findItems(selected, Qt::MatchExactly, ColName);
        if (!matches.isEmpty())
            m_tree->setCurrentItem(matches.first());
    }
}

void UsenetDetailsDialog::poll()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    Ipc::IpcMessage msg(Ipc::IpcMsgType::GetUsenetItemDetails);
    msg.append(m_itemId);

    QPointer<UsenetDetailsDialog> self(this);
    m_ipc->sendRequest(msg, [self](const Ipc::IpcMessage& resp) {
        if (!self || !resp.isValid())   // a dropped connection is not a removed release
            return;
        if (!resp.fieldBool(0)) {
            // 404: the release was removed while this was open. There is nothing
            // left to show and nothing to poll.
            self->close();
            return;
        }
        self->applyDetails(resp.fieldMap(1));
    });
}

void UsenetDetailsDialog::onRowActivated(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (!item)
        return;

    const QString path = item->data(ColName, Qt::UserRole).toString();
    if (path.isEmpty()) {
        StatusBarNotifier::post(tr("\"%1\" has not been published on its own.")
                                    .arg(item->text(ColName)));
        return;
    }

    // Same local/remote rule as the panel: the recorded path is a path on the
    // *core's* filesystem, and only means anything here when the core is here.
    if (m_ipc && m_ipc->isLocalConnection()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return;
    }

    const QString rel = item->data(ColName, Qt::UserRole + 1).toString();
    if (rel.isEmpty()) {
        StatusBarNotifier::post(tr("\"%1\" is outside the core's Incoming folder and "
                                   "cannot be opened from here.").arg(item->text(ColName)));
        return;
    }

    const ED2KFileType type = getED2KFileTypeID(item->text(ColName));
    const bool play = type == ED2KFileType::Video || type == ED2KFileType::Audio;
    openIncomingFileInBrowser(m_ipc, m_streamToken, rel, play);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void UsenetDetailsDialog::buildUi()
{
    auto* layout = new QVBoxLayout(this);

    m_title = new QLabel(this);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 3);
    m_title->setFont(titleFont);
    m_title->setWordWrap(true);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_title);

    // Two form columns rather than one long one, following the reference layout:
    // the release-wide facts read as a block, not as a list.
    auto* header = new QHBoxLayout;
    auto* left = new QFormLayout;
    auto* right = new QFormLayout;

    // Named, so a test (and Qt's own object inspector) can find a value by what
    // it is rather than by where it happens to sit among the form's own labels.
    auto value = [this](QFormLayout* form, const QString& label, const char* name) {
        auto* l = new QLabel(this);
        l->setObjectName(QString::fromLatin1(name));
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(label, l);
        return l;
    };

    m_totalSize = value(left, tr("Total Size:"), "valueTotalSize");
    m_date      = value(left, tr("Date:"),       "valueDate");
    m_status    = value(left, tr("Status:"),     "valueStatus");
    m_health    = value(left, tr("Health:"),     "valueHealth");

    m_poster    = value(right, tr("Poster:"),     "valuePoster");
    m_parts     = value(right, tr("Articles:"),   "valueArticles");
    m_groups    = value(right, tr("Newsgroups:"), "valueGroups");
    m_fileCount = value(right, tr("Files:"),      "valueFiles");

    header->addLayout(left, 1);
    header->addLayout(right, 1);
    layout->addLayout(header);

    auto* tree = new ListTreeWidget(this);
    m_tree = tree;
    tree->setColumnCount(ColCount);
    tree->setHeaderLabels({tr("Name"), tr("Size"), tr("Progress"),
                           tr("Articles"), tr("Missing"), tr("Status")});
    tree->setRootIsDecorated(false);
    tree->setAlternatingRowColors(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setSortingEnabled(true);
    tree->bindColumns(QStringLiteral("usenetDetailsFiles"),
                      {260, 90, 75, 90, 70, 130});
    layout->addWidget(tree, 1);

    connect(tree, &QTreeWidget::itemDoubleClicked,
            this, &UsenetDetailsDialog::onRowActivated);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* folderBtn = buttons->addButton(tr("Open Folder"), QDialogButtonBox::ActionRole);
    connect(folderBtn, &QPushButton::clicked, this, &UsenetDetailsDialog::openFolder);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void UsenetDetailsDialog::openFolder()
{
    // openIncomingFolder() owns the local/remote split, so the dialog does not
    // repeat it. The release's own folder would be better than the root, but a
    // Usenet release publishes flat into Incoming — there is no subfolder.
    openIncomingFolder(m_ipc, m_streamToken);
}

} // namespace eMule
