#include "pch.h"
/// @file ImportDownloadsDialog.cpp
/// @brief Dialog for importing legacy download files — implementation.

#include "dialogs/ImportDownloadsDialog.h"
#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "IpcMessage.h"
#include "prefs/Preferences.h"
#include "utils/DialogSizing.h"
#include "utils/StringUtils.h"
#include "app/SkinIcons.h"

#include <QCborArray>
#include <QPointer>
#include <QCborMap>
#include <QFileDialog>
#include <QHash>
#include <QSet>
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

#include <algorithm>
#include <functional>

namespace eMule {

using namespace Ipc;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ImportDownloadsDialog::ImportDownloadsDialog(IpcClient* ipc, QWidget* parent)
    : RememberedSizeDialog(parent)
    , m_ipc(ipc)
{
    setWindowTitle(tr("Convert Part Files"));
    setWindowIcon(skinIcon(QStringLiteral(":/icons/Convert.ico")));

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

    auto* jobList = new ListTreeWidget(queueGroup);
    m_jobList = jobList;
    m_jobList->setHeaderLabels({tr("Filename"), tr("Status"), tr("Size"), tr("File Hash")});
    m_jobList->setRootIsDecorated(false);
    m_jobList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_jobList->header()->setStretchLastSection(true);
    jobList->bindColumns(QStringLiteral("importDownloads"), {220, 100, 80, 260});
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

    connect(m_jobList, &QTreeWidget::itemSelectionChanged, this, &ImportDownloadsDialog::updateButtons);

    // --- Refresh timer ---
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(1500);
    connect(m_refreshTimer, &QTimer::timeout, this, &ImportDownloadsDialog::onRefresh);
    m_refreshTimer->start();

    // Initial fetch
    requestJobs();

    DialogSizing::applySize(this, {}, QSize(620, 400), DialogSizing::Fit::Layout);
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

    // MFC asks, No by default, and only in advanced mode (PartFileConvert.cpp:632-648)
    const bool removeSource = thePrefs.showExtControls()
        && QMessageBox::question(
               this, tr("Import Downloads"),
               tr("Do you want to delete the source files after a successful import?"),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;

    IpcMessage req(IpcMsgType::ScanImportFolder);
    req.append(folder);
    req.append(removeSource);
    m_ipc->sendRequest(std::move(req), [this, guard = QPointer(this)](const IpcMessage& resp) {
        if (!guard || !resp.fieldBool(0))
            return;
        const QCborArray jobs = resp.fieldArray(1);
        updateJobList(jobs);
    });
}

void ImportDownloadsDialog::onRetrySelected()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    // retry keeps indices stable, order does not matter
    const QList<int> indices = selectedJobIndices([](int state) { return isRetryable(state); });
    for (qsizetype i = 0; i < indices.size(); ++i) {
        IpcMessage req(IpcMsgType::RetryConvertJob);
        req.append(static_cast<qint64>(indices[i]));
        const bool last = i + 1 == indices.size();
        m_ipc->sendRequest(std::move(req), [this, last, guard = QPointer(this)](const IpcMessage&) {
            if (guard && last)
                requestJobs();
        });
    }
}

void ImportDownloadsDialog::onRemoveSelected()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    // highest index first so earlier removals don't shift later ones
    QList<int> indices = selectedJobIndices([](int state) { return isRemovable(state); });
    std::ranges::sort(indices, std::greater{});
    for (qsizetype i = 0; i < indices.size(); ++i) {
        IpcMessage req(IpcMsgType::RemoveConvertJob);
        req.append(static_cast<qint64>(indices[i]));
        const bool last = i + 1 == indices.size();
        m_ipc->sendRequest(std::move(req), [this, last, guard = QPointer(this)](const IpcMessage&) {
            if (guard && last)
                requestJobs();
        });
    }
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
    m_ipc->sendRequest(std::move(req), [this, guard = QPointer(this)](const IpcMessage& resp) {
        if (!guard || !resp.fieldBool(0))
            return;
        const QCborArray jobs = resp.fieldArray(1);
        updateJobList(jobs);
    });
}

void ImportDownloadsDialog::updateJobList(const QCborArray& jobs)
{
    // refresh in place when the job set is unchanged so selection + scroll survive
    QHash<QString, QTreeWidgetItem*> existing;
    for (int i = 0; i < m_jobList->topLevelItemCount(); ++i) {
        auto* item = m_jobList->topLevelItem(i);
        existing.insert(item->data(0, kKeyRole).toString(), item);
    }

    QStringList keys;
    keys.reserve(jobs.size());
    for (const QCborValue& v : jobs) {
        const QCborMap m = v.toMap();
        keys.append(m.value(QStringLiteral("folder")).toString() + u'/'
                    + m.value(QStringLiteral("filename")).toString());
    }

    bool sameSet = keys.size() == existing.size();
    for (const QString& key : std::as_const(keys))
        sameSet = sameSet && existing.contains(key);

    QSet<QString> selectedKeys;
    QString currentKey;
    if (!sameSet) {
        for (const auto* item : m_jobList->selectedItems())
            selectedKeys.insert(item->data(0, kKeyRole).toString());
        if (const auto* cur = m_jobList->currentItem())
            currentKey = cur->data(0, kKeyRole).toString();
        m_jobList->clear();
    }

    bool hasInProgress = false;

    for (qsizetype i = 0; i < jobs.size(); ++i) {
        const QCborMap m = jobs.at(i).toMap();
        const QString filename = m.value(QStringLiteral("filename")).toString();
        const int state = static_cast<int>(m.value(QStringLiteral("state")).toInteger());
        const int64_t size = m.value(QStringLiteral("size")).toInteger();
        const QString fileHash = m.value(QStringLiteral("fileHash")).toString();

        QTreeWidgetItem* item = sameSet ? existing.value(keys[i]) : new QTreeWidgetItem(m_jobList);
        item->setText(0, filename);
        item->setText(1, statusString(state));
        item->setText(2, size > 0 ? formatByteSize(size) : QString());
        item->setText(3, fileHash);
        item->setData(0, Qt::UserRole, static_cast<int>(i));      // job index
        item->setData(0, kStateRole, state);                      // state for button logic
        item->setData(0, kKeyRole, keys[i]);

        if (!sameSet) {
            if (keys[i] == currentKey)
                m_jobList->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
            if (selectedKeys.contains(keys[i]))
                item->setSelected(true);
        }

        if (state == 2) // InProgress
            hasInProgress = true;
    }

    // states may have changed under an unchanged selection
    updateButtons();

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

void ImportDownloadsDialog::updateButtons()
{
    bool canRetry = false;
    bool canRemove = false;
    for (const auto* item : m_jobList->selectedItems()) {
        const int state = item->data(0, kStateRole).toInt();
        canRetry = canRetry || isRetryable(state);
        canRemove = canRemove || isRemovable(state);
    }
    m_retryBtn->setEnabled(canRetry);
    m_removeBtn->setEnabled(canRemove);
}

QList<int> ImportDownloadsDialog::selectedJobIndices(bool (*accept)(int state)) const
{
    QList<int> indices;
    for (const auto* item : m_jobList->selectedItems()) {
        if (accept(item->data(0, kStateRole).toInt()))
            indices.append(item->data(0, Qt::UserRole).toInt());
    }
    return indices;
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

} // namespace eMule
