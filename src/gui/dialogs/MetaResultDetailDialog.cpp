#include "pch.h"

/// @file MetaResultDetailDialog.cpp
/// @brief Details of a torrent or Usenet search result — see MetaResultDetailDialog.h.

#include "dialogs/MetaResultDetailDialog.h"

#include "controls/SortableItems.h"
#include "controls/TorrentFileTree.h"
#include "utils/StringUtils.h"

#include <QCborArray>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eMule {

namespace {

QTreeWidget* makeTree(QWidget* parent, const QStringList& headers)
{
    auto* tree = new QTreeWidget(parent);
    tree->setColumnCount(int(headers.size()));
    tree->setHeaderLabels(headers);
    tree->setAlternatingRowColors(true);
    tree->setUniformRowHeights(true);
    tree->setSortingEnabled(true);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    return tree;
}

QString dateText(qint64 secs)
{
    return secs > 0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch(secs), QLocale::ShortFormat)
                    : QString();
}

QString sizeText(qint64 bytes)
{
    return QStringLiteral("%1 (%2 bytes)").arg(formatByteSize(bytes), QLocale().toString(bytes));
}

} // namespace

MetaResultDetailDialog::MetaResultDetailDialog(const RowInfo& row, QWidget* parent)
    : DetailDialog(parent)
    , m_row(row)
{
    setDesignedSize(QSize(520, 380), QSize(680, 480));
    setWindowTitle(tr("Details: %1").arg(row.name));
    setSubjectKey(row.name);

    m_stack = new QStackedWidget(this);
    m_note = new QLabel(row.torrent ? tr("Fetching the torrent...") : tr("Fetching the NZB..."), m_stack);
    m_note->setAlignment(Qt::AlignCenter);
    m_note->setWordWrap(true);
    m_note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_stack->addWidget(m_note);

    m_tabs = new QTabWidget(m_stack);
    m_general = new QWidget(m_tabs);
    m_tabs->addTab(m_general, tr("General"));
    m_files = row.torrent ? makeTree(m_tabs, {tr("Name"), tr("Size")})
                          : makeTree(m_tabs, {tr("Name"), tr("Size"), tr("Articles"), tr("Type")});
    m_tabs->addTab(m_files, tr("Files"));
    m_trackers = makeTree(m_tabs, {tr("URL"), tr("Tier")});
    m_trackers->setRootIsDecorated(false);
    m_tabs->addTab(m_trackers, tr("Trackers"));
    m_tabs->setTabVisible(TabTrackers, row.torrent);
    m_stack->addWidget(m_tabs);
    contentLayout()->addWidget(m_stack);

    m_download = buttonBox()->addButton(tr("Download"), QDialogButtonBox::AcceptRole);
    m_download->setEnabled(false);   // once the file is there
    connect(m_download, &QPushButton::clicked, this, [this] {
        emit downloadRequested();
        close();
    });

    fitToContent();
}

void MetaResultDetailDialog::setDetails(const QCborMap& details)
{
    const QString error = details.value(QStringLiteral("error")).toString();
    if (!error.isEmpty()) {
        setError(error);
        return;
    }
    const bool torrent = details.value(QStringLiteral("kind")).toString() == QLatin1String("torrent");
    const QCborMap inspection = details.value(QStringLiteral("inspection")).toMap();
    if (inspection.isEmpty()) {
        setError(details.value(QStringLiteral("inspectError")).toString());
        return;
    }

    buildGeneral(inspection, torrent);
    if (torrent)
        buildTorrentFiles(inspection);
    else
        buildNzbFiles(inspection);
    m_tabs->setTabVisible(TabTrackers, torrent);
    if (torrent)
        buildTrackers(inspection);

    m_stack->setCurrentWidget(m_tabs);
    m_download->setEnabled(true);
    fitToContent();
}

void MetaResultDetailDialog::setError(const QString& error)
{
    m_note->setText(error.isEmpty() ? tr("The file could not be read.") : error);
    m_note->setEnabled(true);
    m_stack->setCurrentWidget(m_note);
    m_download->setEnabled(false);
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void MetaResultDetailDialog::buildGeneral(const QCborMap& inspection, bool torrent)
{
    delete m_general->layout();
    qDeleteAll(m_general->findChildren<QWidget*>(Qt::FindDirectChildrenOnly));
    auto* form = new QFormLayout(m_general);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    const auto str = [&inspection](const char* key) {
        return inspection.value(QLatin1StringView(key)).toString();
    };
    const auto num = [&inspection](const char* key) {
        return inspection.value(QLatin1StringView(key)).toInteger();
    };

    const QString name = str("name");
    addDetailRow(form, tr("Name"), name.isEmpty() ? m_row.name : name);
    addDetailRow(form, tr("Type"), torrent ? tr("Torrent") : tr("Usenet (NZB)"));
    const qint64 total = num("totalSize");
    if (total > 0)
        addDetailRow(form, tr("Size"), sizeText(total));
    int fileCount = 0;
    for (const auto& f : inspection.value(QStringLiteral("files")).toArray())
        fileCount += f.toMap().value(QStringLiteral("pad")).toBool() ? 0 : 1;
    addDetailRow(form, tr("Files"), QLocale().toString(fileCount));
    addRowFacts(form);

    if (torrent) {
        if (const QString v1 = str("infoHashV1"); !v1.isEmpty())
            addDetailRow(form, tr("Info hash v1"), v1);
        if (const QString v2 = str("infoHashV2"); !v2.isEmpty())
            addDetailRow(form, tr("Info hash v2"), v2);
        if (num("pieceCount") > 0)
            addDetailRow(form, tr("Pieces"), tr("%1 × %2").arg(QLocale().toString(num("pieceCount")),
                                                                formatByteSize(num("pieceLength"))));
        addDetailRow(form, tr("Private"), inspection.value(QStringLiteral("private")).toBool() ? tr("Yes") : tr("No"));
        if (const QString creator = str("creator"); !creator.isEmpty())
            addDetailRow(form, tr("Created by"), creator);
        if (const QString created = dateText(num("creationDate")); !created.isEmpty())
            addDetailRow(form, tr("Created on"), created);
        if (const QString comment = str("comment"); !comment.isEmpty())
            addDetailRow(form, tr("Comment"), comment);
    } else {
        if (const QString poster = str("poster"); !poster.isEmpty())
            addDetailRow(form, tr("Poster"), poster);
        QStringList groups;
        for (const auto& g : inspection.value(QStringLiteral("groups")).toArray())
            groups.append(g.toString());
        if (!groups.isEmpty())
            addDetailRow(form, tr("Groups"), groups.join(QStringLiteral(", ")));
        if (const QString posted = dateText(num("date")); !posted.isEmpty())
            addDetailRow(form, tr("Posted"), posted);
        if (num("articles") > 0)
            addDetailRow(form, tr("Articles"), QLocale().toString(num("articles")));
        int par2 = 0;
        for (const auto& f : inspection.value(QStringLiteral("files")).toArray())
            par2 += f.toMap().value(QStringLiteral("isPar2")).toBool() ? 1 : 0;
        if (par2 > 0)
            addDetailRow(form, tr("PAR2 files"), QLocale().toString(par2));
        if (inspection.value(QStringLiteral("password")).toBool())
            addDetailRow(form, tr("Password"), tr("Included in the NZB"));
    }
}

void MetaResultDetailDialog::addRowFacts(QFormLayout* form) const
{
    if (!m_row.source.isEmpty())
        addDetailRow(form, tr("Source"), m_row.source);
    if (!m_row.category.isEmpty())
        addDetailRow(form, tr("Category"), m_row.category);
    if (m_row.seeders >= 0 || m_row.peers >= 0)
        addDetailRow(form, tr("Seeders / peers"),
                     QStringLiteral("%1 / %2")
                         .arg(m_row.seeders >= 0 ? QString::number(m_row.seeders) : QStringLiteral("?"),
                              m_row.peers >= 0 ? QString::number(m_row.peers) : QStringLiteral("?")));
    if (m_row.ageDays >= 0)
        addDetailRow(form, tr("Age"), tr("%n day(s)", nullptr, m_row.ageDays));
}

void MetaResultDetailDialog::buildTorrentFiles(const QCborMap& inspection)
{
    const int count = TorrentFileTree::fill(m_files, inspection.value(QStringLiteral("files")).toArray(),
                                            TorrentName, TorrentSize);
    m_files->header()->setSectionResizeMode(TorrentSize, QHeaderView::ResizeToContents);
    m_tabs->setTabText(TabFiles, tr("Files (%1)").arg(count));
}

void MetaResultDetailDialog::buildNzbFiles(const QCborMap& inspection)
{
    m_files->setSortingEnabled(false);
    m_files->clear();
    m_files->setRootIsDecorated(false);
    int count = 0;
    for (const auto& v : inspection.value(QStringLiteral("files")).toArray()) {
        const QCborMap f = v.toMap();
        auto* item = new SortableTreeItem(m_files);
        const qint64 size = f.value(QStringLiteral("size")).toInteger();
        const qint64 segments = f.value(QStringLiteral("segments")).toInteger();
        const qint64 missing = f.value(QStringLiteral("missingSegments")).toInteger();
        item->setText(NzbName, f.value(QStringLiteral("name")).toString());
        item->setToolTip(NzbName, item->text(NzbName));
        item->setText(NzbSize, formatByteSize(size));
        item->setData(NzbSize, SortRole, QVariant::fromValue(size));
        item->setTextAlignment(NzbSize, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(NzbArticles, missing > 0 ? tr("%1 (%2 missing)").arg(segments).arg(missing)
                                               : QString::number(segments));
        item->setData(NzbArticles, SortRole, QVariant::fromValue(segments));
        item->setTextAlignment(NzbArticles, Qt::AlignRight | Qt::AlignVCenter);
        item->setText(NzbType, f.value(QStringLiteral("isPar2")).toBool() ? tr("PAR2") : QString());
        ++count;
    }
    m_files->setSortingEnabled(true);
    m_files->sortByColumn(NzbName, Qt::AscendingOrder);
    for (int c = NzbSize; c < NzbColCount; ++c)
        m_files->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_tabs->setTabText(TabFiles, tr("Files (%1)").arg(count));
}

void MetaResultDetailDialog::buildTrackers(const QCborMap& inspection)
{
    m_trackers->setSortingEnabled(false);
    m_trackers->clear();
    int count = 0;
    for (const auto& v : inspection.value(QStringLiteral("trackers")).toArray()) {
        const QCborMap t = v.toMap();
        auto* item = new SortableTreeItem(m_trackers);
        const int tier = int(t.value(QStringLiteral("tier")).toInteger());
        item->setText(0, t.value(QStringLiteral("url")).toString());
        item->setText(1, QString::number(tier));
        item->setData(1, SortRole, tier);
        ++count;
    }
    for (const auto& v : inspection.value(QStringLiteral("webSeeds")).toArray()) {
        auto* item = new SortableTreeItem(m_trackers);
        item->setText(0, v.toString());
        item->setText(1, tr("Web seed"));
        item->setData(1, SortRole, 1000);
        ++count;
    }
    m_trackers->setSortingEnabled(true);
    m_trackers->sortByColumn(1, Qt::AscendingOrder);
    m_trackers->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tabs->setTabText(TabTrackers, tr("Trackers (%1)").arg(count));
}

} // namespace eMule
