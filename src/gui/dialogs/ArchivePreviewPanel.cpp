#include "pch.h"
/// @file ArchivePreviewPanel.cpp
/// @brief Archive content viewer panel implementation.

#include "dialogs/ArchivePreviewPanel.h"
#include "archive/ArchiveReader.h"
#include "utils/Log.h"

#include <QHeaderView>
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
    clear();
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
    m_statusLabel->setText(tr("Scanning..."));
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0); // indeterminate
    m_updateBtn->setEnabled(false);

    const QString path = m_filePath;

    // Run archive reading on a background thread
    auto* watcher = new QFutureWatcher<QList<QList<QStandardItem*>>>(this);

    connect(watcher, &QFutureWatcher<QList<QList<QStandardItem*>>>::finished, this,
            [this, watcher]() {
                auto rows = watcher->result();
                m_model->removeRows(0, m_model->rowCount());
                for (auto& row : rows)
                    m_model->appendRow(row);

                onScanComplete(m_model->rowCount());
                watcher->deleteLater();
            });

    watcher->setFuture(QtConcurrent::run([path]() -> QList<QList<QStandardItem*>> {
        QList<QList<QStandardItem*>> rows;

        ArchiveReader reader;
        if (!reader.open(path))
            return rows;

        const int count = reader.entryCount();
        rows.reserve(count);

        QLocale locale;
        for (int i = 0; i < count; ++i) {
            auto* nameItem = new QStandardItem(reader.entryName(i));
            nameItem->setEditable(false);

            auto* sizeItem = new QStandardItem;
            sizeItem->setEditable(false);
            const uint64_t sz = reader.entrySize(i);
            if (reader.entryIsDir(i)) {
                sizeItem->setText(QStringLiteral("--"));
            } else {
                sizeItem->setText(locale.formattedDataSize(static_cast<qint64>(sz)));
                sizeItem->setData(static_cast<qint64>(sz), Qt::UserRole);
            }
            sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

            // CRC — not available from libarchive read API
            auto* crcItem = new QStandardItem(QStringLiteral("--"));
            crcItem->setEditable(false);

            // Attributes — show 'D' for directories, octal mode otherwise
            auto* attrItem = new QStandardItem;
            attrItem->setEditable(false);
            if (reader.entryIsDir(i)) {
                attrItem->setText(QStringLiteral("D"));
            } else {
                uint16_t mode = reader.entryMode(i);
                if (mode > 0)
                    attrItem->setText(QString::number(mode, 8));
            }

            // Last Modified
            auto* mtimeItem = new QStandardItem;
            mtimeItem->setEditable(false);
            QDateTime mtime = reader.entryMtime(i);
            if (mtime.isValid()) {
                mtimeItem->setText(locale.toString(mtime, QLocale::ShortFormat));
                mtimeItem->setData(mtime, Qt::UserRole);
            }

            // Comment — not available from libarchive
            auto* commentItem = new QStandardItem;
            commentItem->setEditable(false);

            rows.append({nameItem, sizeItem, crcItem, attrItem, mtimeItem, commentItem});
        }

        return rows;
    }));
}

void ArchivePreviewPanel::clear()
{
    m_model->removeRows(0, m_model->rowCount());
    m_archiveTypeLabel->setText(tr("Archive type: --"));
    m_statusLabel->setText(tr("Ready"));
    m_progressBar->setVisible(false);
    m_fileCountLabel->setText({});
    m_updateBtn->setEnabled(!m_filePath.isEmpty());
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void ArchivePreviewPanel::onScanComplete(int count)
{
    m_scanning = false;
    m_progressBar->setVisible(false);
    m_updateBtn->setEnabled(true);

    if (count > 0) {
        m_statusLabel->setText(tr("Ready"));

        // Detect archive type from extension
        QString ext = m_filePath.section(u'.', -1).toLower();
        static const QHash<QString, QString> typeNames = {
            {QStringLiteral("zip"), QStringLiteral("ZIP")},
            {QStringLiteral("rar"), QStringLiteral("RAR")},
            {QStringLiteral("7z"),  QStringLiteral("7z")},
            {QStringLiteral("iso"), QStringLiteral("ISO")},
            {QStringLiteral("ace"), QStringLiteral("ACE")},
            {QStringLiteral("tar"), QStringLiteral("TAR")},
            {QStringLiteral("gz"),  QStringLiteral("GZip")},
            {QStringLiteral("bz2"), QStringLiteral("BZip2")},
            {QStringLiteral("cab"), QStringLiteral("CAB")},
        };
        QString typeName = typeNames.value(ext, ext.toUpper());
        m_archiveTypeLabel->setText(tr("Archive type: %1").arg(typeName));
    } else {
        m_statusLabel->setText(tr("No entries found or unsupported format"));
        m_archiveTypeLabel->setText(tr("Archive type: --"));
    }

    m_fileCountLabel->setText(tr("Files: %1").arg(count));
    emit scanFinished(count);
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

    m_previewCopyBtn = new QPushButton(tr("Create Preview Copy"), this);
    m_previewCopyBtn->setVisible(false); // shown only for partial files
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

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_model);
    m_treeView->setRootIsDecorated(false);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setSortingEnabled(true);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->header()->setStretchLastSection(true);
    m_treeView->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    mainLayout->addWidget(m_treeView, 1);

    // Progress bar (hidden by default)
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    m_progressBar->setTextVisible(false);
    m_progressBar->setMaximumHeight(16);
    mainLayout->addWidget(m_progressBar);

    // Bottom row: file count
    m_fileCountLabel = new QLabel(this);
    mainLayout->addWidget(m_fileCountLabel);
}

} // namespace eMule
