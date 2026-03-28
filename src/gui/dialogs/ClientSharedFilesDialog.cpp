#include "pch.h"
/// @file ClientSharedFilesDialog.cpp
/// @brief Dialog showing shared files received from a remote client — implementation.

#include "dialogs/ClientSharedFilesDialog.h"

#include "app/IpcClient.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCborMap>
#include <QHeaderView>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace eMule {

using namespace Ipc;

namespace {

/// Format a byte count for display (B / KiB / MiB / GiB).
QString formatBytes(int64_t bytes)
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

} // anonymous namespace

ClientSharedFilesDialog::ClientSharedFilesDialog(const QString& clientName,
                                                   const QCborArray& files,
                                                   IpcClient* ipc,
                                                   QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Shared Files \u2014 %1").arg(clientName));
    resize(640, 400);

    auto* layout = new QVBoxLayout(this);

    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({tr("File Name"), tr("Size"), tr("Hash")});

    for (int i = 0; i < files.size(); ++i) {
        const auto map = files[i].toMap();
        const QString fileName = map.value(QStringLiteral("fileName")).toString();
        const int64_t fileSize = map.value(QStringLiteral("fileSize")).toInteger();
        const QString hash = map.value(QStringLiteral("hash")).toString();

        auto* nameItem = new QStandardItem(fileName);
        nameItem->setEditable(false);
        nameItem->setData(hash, Qt::UserRole);
        nameItem->setData(fileName, Qt::UserRole + 1);
        nameItem->setData(static_cast<qlonglong>(fileSize), Qt::UserRole + 2);

        auto* sizeItem = new QStandardItem(formatBytes(fileSize));
        sizeItem->setEditable(false);
        sizeItem->setData(static_cast<qlonglong>(fileSize), Qt::UserRole);

        auto* hashItem = new QStandardItem(hash);
        hashItem->setEditable(false);

        m_model->appendRow({nameItem, sizeItem, hashItem});
    }

    m_view = new QTreeView(this);
    m_view->setModel(m_model);
    m_view->setRootIsDecorated(false);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSortingEnabled(true);
    m_view->setAllColumnsShowFocus(true);
    m_view->header()->setStretchLastSection(true);
    m_view->setColumnWidth(0, 300);
    m_view->setColumnWidth(1, 100);
    layout->addWidget(m_view);

    auto* btnLayout = new QHBoxLayout;
    auto* downloadBtn = new QPushButton(tr("Download Selected"), this);
    connect(downloadBtn, &QPushButton::clicked, this, &ClientSharedFilesDialog::downloadSelected);
    btnLayout->addWidget(downloadBtn);
    btnLayout->addStretch();
    auto* closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);
}

void ClientSharedFilesDialog::downloadSelected()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    const auto selected = m_view->selectionModel()->selectedRows();
    for (const auto& idx : selected) {
        const auto* item = m_model->item(idx.row(), 0);
        if (!item) continue;

        const QString hash = item->data(Qt::UserRole).toString();
        const QString fileName = item->data(Qt::UserRole + 1).toString();
        const int64_t fileSize = item->data(Qt::UserRole + 2).toLongLong();

        IpcMessage msg(IpcMsgType::DownloadSearchFile);
        msg.append(hash);
        msg.append(fileName);
        msg.append(static_cast<qint64>(fileSize));
        m_ipc->sendRequest(std::move(msg));
    }
}

} // namespace eMule
