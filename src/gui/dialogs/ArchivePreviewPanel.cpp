#include "pch.h"
/// @file ArchivePreviewPanel.cpp
/// @brief Archive content viewer panel implementation.

#include "dialogs/ArchivePreviewPanel.h"
#include "archive/ArchiveReader.h"
#include "archive/ArchiveScan.h"
#include "controls/AbstractListView.h"
#include "utils/ListActivation.h"
#include "controls/SortableItems.h"
#include "utils/StringUtils.h"

#include <QFile>
#include <QHeaderView>
#include <QMessageBox>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

#include <QtConcurrent/QtConcurrent>

namespace eMule {

// Column indices
enum Column {
    ColName,
    ColSize,
    ColCRC,
    ColAttributes,
    ColModified,
    ColComment,
    ColCount
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ArchivePreviewPanel::ArchivePreviewPanel(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ArchivePreviewPanel::setFile(const QString& filePath, uint64_t fileSize)
{
    m_filePath = filePath;
    m_fileSize = fileSize;
    m_gapPairs.clear();
    m_previewCopyBtn->setVisible(false);
    clear();
}

void ArchivePreviewPanel::setPartFile(const QList<qint64>& gapPairs, bool canCreatePreviewFile)
{
    m_gapPairs = gapPairs;
    m_previewCopyBtn->setVisible(canCreatePreviewFile);
}

void ArchivePreviewPanel::setAutoScan(bool autoScan)
{
    if (autoScan && !m_filePath.isEmpty() && !m_scanning)
        startScan();
}

void ArchivePreviewPanel::startScan()
{
    if (m_filePath.isEmpty() || m_scanning)
        return;

    m_scanning = true;
    m_statusLabel->setText(tr("Scanning archive..."));
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0); // indeterminate
    m_updateBtn->setEnabled(false);

    // Run archive reading on a background thread
    auto* watcher = new QFutureWatcher<ScanOutput>(this);
    connect(watcher, &QFutureWatcher<ScanOutput>::finished, this, [this, watcher]() {
        applyScan(watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&ArchivePreviewPanel::scanFile, m_filePath, m_gapPairs));
}

ArchivePreviewPanel::ScanOutput ArchivePreviewPanel::scanFile(const QString& filePath,
                                                              const QList<qint64>& gapPairs)
{
    ScanOutput out;
    ArchiveGaps gaps;
    for (qsizetype i = 0; i + 1 < gapPairs.size(); i += 2)
        gaps.append({gapPairs.at(i), gapPairs.at(i + 1)});

    // ZIP and RAR from their own headers: that is where the CRC, the comments and
    // the position of each entry's data are — and it works on a file with holes.
    QFile file(filePath);
    ArchiveScanResult scan;
    if (file.open(QIODevice::ReadOnly))
        scan = scanArchive(file, file.size(), gaps);
    file.close();

    using Status = ArchiveScanResult::Status;
    if (scan.type != ArchiveScanType::Unknown) {
        out.typeName = scan.type == ArchiveScanType::Zip ? QStringLiteral("ZIP") : QStringLiteral("RAR");
        out.hasCrcAndComment = true;
        out.fileCount = scan.fileCount();
        out.info = scan.infoLine(tr("Password protection"), tr("Comment"));
        for (const ArchiveScanEntry& e : std::as_const(scan.entries)) {
            Row row;
            row.name = e.name;
            row.directory = e.directory;
            row.size = e.size;
            if (e.hasCrc && !e.directory)
                row.crc = QStringLiteral("%1").arg(e.crc, 8, 16, QLatin1Char('0')).toUpper();
            row.attributes = e.attributes();
            row.modified = e.modified;
            row.comment = e.comment;
            row.complete = e.complete;
            out.rows.append(row);
        }
        switch (scan.status) {
        case Status::Ok:                out.status = tr("Archive scanned."); break;
        case Status::ListIncomplete:    out.status = tr("File list may be incomplete."); break;
        case Status::HeadersEncrypted:  out.status = tr("Headers encrypted - unable to read archive."); break;
        case Status::InsufficientData:  out.status = tr("Insufficient data available."); break;
        case Status::NoTableOfContents: out.status = tr("Table of contents not found."); break;
        }
        return out;
    }
    if (scan.status == Status::InsufficientData) {
        out.status = tr("Insufficient data available.");
        return out;
    }

    // Everything else (7z, tar, ISO, ...) through libarchive, which needs the whole
    // file and knows neither CRC nor comments.
    ArchiveReader reader;
    if (!reader.open(filePath)) {
        out.status = reader.encryptionBlocked() ? tr("Headers encrypted - unable to read archive.")
                                                : tr("Table of contents not found.");
        if (reader.encryptionBlocked())
            out.info = tr("Password protection");
        return out;
    }
    out.typeName = reader.formatName();
    for (int i = 0; i < reader.entryCount(); ++i) {
        Row row;
        row.name = reader.entryName(i);
        row.directory = reader.entryIsDir(i);
        row.size = reader.entrySize(i);
        row.attributes = row.directory ? QStringLiteral("D") : QString();
        row.modified = reader.entryMtime(i);
        out.rows.append(row);
        out.fileCount += !row.directory;
    }
    if (reader.hasEncryptedEntries())
        out.info = tr("Password protection");
    out.status = gaps.isEmpty() ? tr("Archive scanned.") : tr("File list may be incomplete.");
    return out;
}

void ArchivePreviewPanel::clear()
{
    m_model->removeRows(0, m_model->rowCount());
    m_archiveTypeLabel->setText(tr("Archive type: --"));
    m_statusLabel->setText(tr("Ready"));
    m_progressBar->setVisible(false);
    m_fileCountLabel->setText({});
    m_infoLabel->setText({});
    m_updateBtn->setEnabled(!m_filePath.isEmpty());
}

void ArchivePreviewPanel::applyScan(const ScanOutput& output)
{
    m_scanning = false;
    m_progressBar->setVisible(false);
    m_updateBtn->setEnabled(true);

    m_model->removeRows(0, m_model->rowCount());
    const QLocale locale;
    // An entry whose packed data has not arrived yet is greyed (MFC lParam 1,
    // ArchivePreviewDlg.cpp:297-311).
    const QBrush missing = palette().brush(QPalette::Disabled, QPalette::Text);
    for (const Row& row : output.rows) {
        // SortableStandardItem throughout: the model is bound straight to the view,
        // so its sortRole -- Qt::DisplayRole -- would otherwise order "9.90 MB" above
        // "10.00 GB" and a date by its leading digit.
        auto* nameItem = new SortableStandardItem(row.name);
        auto* sizeItem = new SortableStandardItem;
        if (row.directory) {
            // -1 rather than no key at all: folders gather at one end instead of
            // scattering through the list as text
            sizeItem->setData(qint64{-1}, SortRole);
        } else {
            sizeItem->setText(formatByteSize(row.size));
            sizeItem->setData(static_cast<qint64>(row.size), SortRole);
        }
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* crcItem = new SortableStandardItem(row.crc);
        auto* attrItem = new SortableStandardItem(row.attributes);
        auto* mtimeItem = new SortableStandardItem;
        if (row.modified.isValid()) {
            mtimeItem->setText(locale.toString(row.modified, QLocale::ShortFormat));
            mtimeItem->setData(row.modified, SortRole);
        }
        auto* commentItem = new SortableStandardItem(row.comment);

        const QList<QStandardItem*> items{nameItem, sizeItem, crcItem, attrItem, mtimeItem, commentItem};
        for (QStandardItem* item : items) {
            item->setEditable(false);
            if (!row.complete)
                item->setForeground(missing);
        }
        m_model->appendRow(items);
    }

    // MFC hides CRC and Comment where the format has none (ISO, ArchivePreviewDlg.cpp:1138-1153)
    m_treeView->setColumnHidden(ColCRC, !output.hasCrcAndComment && !output.rows.isEmpty());
    m_treeView->setColumnHidden(ColComment, !output.hasCrcAndComment && !output.rows.isEmpty());

    m_statusLabel->setText(output.status);
    m_archiveTypeLabel->setText(tr("Archive type: %1").arg(
        output.typeName.isEmpty() ? tr("(unknown/unsupported)") : output.typeName));
    m_infoLabel->setText(output.info);
    // Files only: a folder is not a file (MFC uArchiveFileEntries)
    m_fileCountLabel->setText(tr("Files: %1").arg(output.fileCount));
    emit scanFinished(output.fileCount);
}

// ---------------------------------------------------------------------------
// UI construction (private)
// ---------------------------------------------------------------------------

void ArchivePreviewPanel::buildUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Top row: archive type + buttons
    auto* topRow = new QHBoxLayout;
    m_archiveTypeLabel = new QLabel(tr("Archive type: --"), this);
    topRow->addWidget(m_archiveTypeLabel);
    topRow->addStretch();

    m_statusLabel = new QLabel(tr("Ready"), this);
    topRow->addWidget(m_statusLabel);
    topRow->addStretch();

    m_previewCopyBtn = new QPushButton(tr("Create preview file"), this);
    m_previewCopyBtn->setObjectName(QStringLiteral("createPreviewFile"));
    m_previewCopyBtn->setVisible(false); // shown only for partial files
    connect(m_previewCopyBtn, &QPushButton::clicked, this, &ArchivePreviewPanel::previewFileRequested);
    topRow->addWidget(m_previewCopyBtn);

    m_updateBtn = new QPushButton(tr("Update"), this);
    m_updateBtn->setEnabled(false);
    connect(m_updateBtn, &QPushButton::clicked, this, &ArchivePreviewPanel::startScan);
    topRow->addWidget(m_updateBtn);

    mainLayout->addLayout(topRow);

    // Tree view
    m_model = new QStandardItemModel(0, ColCount, this);
    m_model->setHorizontalHeaderLabels({
        tr("Name"), tr("Size"), tr("CRC"),
        tr("Attributes"), tr("Last Modified"), tr("Comment")
    });

    auto* treeView = new ListTreeView(this);
    m_treeView = treeView;
    m_treeView->setModel(m_model);
    m_treeView->setRootIsDecorated(false);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setSortingEnabled(true);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // CListCtrlX always has Ctrl+F / F3 find (ListCtrlX.cpp:460)
    bindListKeys(m_treeView, {.find = true});
    // Name is Interactive, not Stretch: a Qt-owned width can't be resized by the
    // user, so there would be nothing to remember.
    m_treeView->header()->setStretchLastSection(true);
    // Name, Size, CRC, Attributes, Last Modified, Comment.
    treeView->bindColumns(QStringLiteral("archivePreview"),
        {280, 90, 90, 90, 130, 160});
    mainLayout->addWidget(m_treeView, 1);

    // Progress bar (hidden by default)
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    m_progressBar->setTextVisible(false);
    m_progressBar->setMaximumHeight(16);
    mainLayout->addWidget(m_progressBar);

    // Bottom row: file count, the archive's own marks, and what the letters mean
    // (MFC IDC_INFO_FILECOUNT, IDC_INFO_ATTR, IDC_AP_EXPLAIN)
    auto* bottomRow = new QHBoxLayout;
    m_fileCountLabel = new QLabel(this);
    bottomRow->addWidget(m_fileCountLabel);
    bottomRow->addSpacing(16);
    bottomRow->addWidget(new QLabel(tr("Info:"), this));
    m_infoLabel = new QLabel(this);
    m_infoLabel->setObjectName(QStringLiteral("archiveInfo"));
    bottomRow->addWidget(m_infoLabel, 1);
    auto* explainBtn = new QPushButton(QStringLiteral("?"), this);
    explainBtn->setFixedWidth(28);
    connect(explainBtn, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, tr("Archive Preview"),
            tr("File attributes can contain:\n\n"
               "P\tfile is password protected\n"
               "D\tis a directory\n"
               "C\tComment\n"
               "Lx\tCompression level x\n"
               "M\tcompressed data not completely downloaded yet\n"
               "<\tfile continued from previous volume\n"
               ">\tfile continues in next volume\n"));
    });
    bottomRow->addWidget(explainBtn);
    mainLayout->addLayout(bottomRow);
}

} // namespace eMule
