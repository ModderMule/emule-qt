#include "pch.h"
/// @file ImportDownloadsDialog.cpp
/// @brief Dialog for importing legacy download files — implementation.

#include "dialogs/ImportDownloadsDialog.h"
#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCborArray>
#include <QCborMap>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eMule {

using namespace Ipc;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ImportDownloadsDialog::ImportDownloadsDialog(IpcClient* ipc, QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
{
    setWindowTitle(tr("Convert Part Files"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/Convert.ico")));
    resize(620, 400);

    auto* mainLayout = new QVBoxLayout(this);

    // --- Current Job group ---
    auto* currentGroup = new QGroupBox(tr("Current Job"), this);
    auto* currentLayout = new QVBoxLayout(currentGroup);

    m_statusLabel = new QLabel(tr("Idle"), currentGroup);
    currentLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(currentGroup);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    currentLayout->addWidget(m_progressBar);

    mainLayout->addWidget(currentGroup);

    // --- Job Queue group ---
    auto* queueGroup = new QGroupBox(tr("Job Queue"), this);
    auto* queueLayout = new QVBoxLayout(queueGroup);

    m_jobList = new QTreeWidget(queueGroup);
    m_jobList->setHeaderLabels({tr("Filename"), tr("Status"), tr("Size"), tr("File Hash")});
    m_jobList->setRootIsDecorated(false);
    m_jobList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_jobList->header()->setStretchLastSection(true);
    m_jobList->setColumnWidth(0, 220);
    m_jobList->setColumnWidth(1, 100);
    m_jobList->setColumnWidth(2, 80);
    queueLayout->addWidget(m_jobList);

    mainLayout->addWidget(queueGroup, 1);

    // --- Buttons ---
    auto* btnLayout = new QHBoxLayout;

    m_addBtn = new QPushButton(tr("Add Imports..."), this);
    m_retryBtn = new QPushButton(tr("Retry Selected"), this);
    m_removeBtn = new QPushButton(tr("Remove Selected"), this);
    m_closeBtn = new QPushButton(tr("Close"), this);

    m_retryBtn->setEnabled(false);
    m_removeBtn->setEnabled(false);

    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_retryBtn);
    btnLayout->addWidget(m_removeBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(btnLayout);

    // --- Connections ---
    connect(m_addBtn, &QPushButton::clicked, this, &ImportDownloadsDialog::onAddImports);
    connect(m_retryBtn, &QPushButton::clicked, this, &ImportDownloadsDialog::onRetrySelected);
    connect(m_removeBtn, &QPushButton::clicked, this, &ImportDownloadsDialog::onRemoveSelected);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_jobList, &QTreeWidget::itemSelectionChanged, this, [this] {
        const auto items = m_jobList->selectedItems();
        if (items.isEmpty()) {
            m_retryBtn->setEnabled(false);
            m_removeBtn->setEnabled(false);
            return;
        }
        const int state = items.first()->data(0, Qt::UserRole + 1).toInt();
        // InProgress = 2
        const bool isTerminal = (state != 1 && state != 2); // not Queued, not InProgress
        m_retryBtn->setEnabled(isTerminal);
        m_removeBtn->setEnabled(state != 2); // can remove anything except InProgress
    });

    // --- Refresh timer ---
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(1500);
    connect(m_refreshTimer, &QTimer::timeout, this, &ImportDownloadsDialog::onRefresh);
    m_refreshTimer->start();

    // Initial fetch
    requestJobs();
}

ImportDownloadsDialog::~ImportDownloadsDialog() = default;

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ImportDownloadsDialog::onAddImports()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    if (!m_ipc->isLocalConnection()) {
        QMessageBox::warning(this, tr("Import Downloads"),
            tr("Import Downloads is only available for local connections."));
        return;
    }

    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Select folder to scan for importable downloads"));
    if (folder.isEmpty())
        return;

    IpcMessage req(IpcMsgType::ScanImportFolder);
    req.append(folder);
    req.append(true); // removeSource
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;
        const QCborArray jobs = resp.fieldArray(1);
        updateJobList(jobs);
    });
}

void ImportDownloadsDialog::onRetrySelected()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    const auto items = m_jobList->selectedItems();
    if (items.isEmpty())
        return;

    const int index = items.first()->data(0, Qt::UserRole).toInt();

    IpcMessage req(IpcMsgType::RetryConvertJob);
    req.append(static_cast<qint64>(index));
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage&) {
        requestJobs();
    });
}

void ImportDownloadsDialog::onRemoveSelected()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    const auto items = m_jobList->selectedItems();
    if (items.isEmpty())
        return;

    const int index = items.first()->data(0, Qt::UserRole).toInt();

    IpcMessage req(IpcMsgType::RemoveConvertJob);
    req.append(static_cast<qint64>(index));
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage&) {
        requestJobs();
    });
}

void ImportDownloadsDialog::onRefresh()
{
    requestJobs();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ImportDownloadsDialog::requestJobs()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::GetConvertJobs);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;
        const QCborArray jobs = resp.fieldArray(1);
        updateJobList(jobs);
    });
}

void ImportDownloadsDialog::updateJobList(const QCborArray& jobs)
{
    // Save selection by filename
    QString selectedFilename;
    if (auto* sel = m_jobList->currentItem())
        selectedFilename = sel->text(0);

    m_jobList->clear();

    bool hasInProgress = false;

    for (qsizetype i = 0; i < jobs.size(); ++i) {
        const QCborMap m = jobs.at(i).toMap();
        const QString filename = m.value(QStringLiteral("filename")).toString();
        const int state = static_cast<int>(m.value(QStringLiteral("state")).toInteger());
        const int64_t size = m.value(QStringLiteral("size")).toInteger();
        const QString fileHash = m.value(QStringLiteral("fileHash")).toString();

        auto* item = new QTreeWidgetItem(m_jobList);
        item->setText(0, filename);
        item->setText(1, statusString(state));
        item->setText(2, size > 0 ? formatSize(size) : QString());
        item->setText(3, fileHash);
        item->setData(0, Qt::UserRole, static_cast<int>(i));      // job index
        item->setData(0, Qt::UserRole + 1, state);                 // state for button logic

        if (state == 2) // InProgress
            hasInProgress = true;
    }

    // Restore selection
    if (!selectedFilename.isEmpty()) {
        for (int i = 0; i < m_jobList->topLevelItemCount(); ++i) {
            if (m_jobList->topLevelItem(i)->text(0) == selectedFilename) {
                m_jobList->setCurrentItem(m_jobList->topLevelItem(i));
                break;
            }
        }
    }

    // Update progress bar and status label
    if (hasInProgress) {
        m_progressBar->setRange(0, 0); // indeterminate
        m_statusLabel->setText(tr("Converting..."));
    } else if (jobs.isEmpty()) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        m_statusLabel->setText(tr("Idle"));
    } else {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(100);
        m_statusLabel->setText(tr("Done"));
    }
}

QString ImportDownloadsDialog::statusString(int state)
{
    switch (state) {
    case 0: return tr("OK");
    case 1: return tr("Queued");
    case 2: return tr("In Progress");
    case 3: return tr("Out of Disk Space");
    case 4: return tr(".part.met Not Found");
    case 5: return tr("I/O Error");
    case 6: return tr("Failed");
    case 7: return tr("Bad Format");
    case 8: return tr("Already Exists");
    default: return tr("Unknown");
    }
}

QString ImportDownloadsDialog::formatSize(int64_t bytes)
{
    if (bytes < 0)
        return {};
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GiB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

} // namespace eMule
