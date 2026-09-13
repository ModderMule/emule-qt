#include "pch.h"
/// @file ClientSharedFilesDialog.cpp
/// @brief Dialog showing shared files received from a remote client — implementation.

#include "dialogs/ClientSharedFilesDialog.h"

#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "controls/SortableItems.h"

#include "IpcMessage.h"
#include "utils/DialogSizing.h"
#include "utils/StringUtils.h"

#include <QHeaderView>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace eMule {

using namespace Ipc;

ClientSharedFilesDialog::ClientSharedFilesDialog(const QString& clientName,
                                                   const QCborArray& files,
                                                   IpcClient* ipc,
                                                   QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Shared Files \u2014 %1").arg(clientName));

    auto* layout = new QVBoxLayout(this);

    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({tr("File Name"), tr("Size"), tr("Hash")});

    for (int i = 0; i < files.size(); ++i) {
        const auto map = files[i].toMap();
        const QString fileName = map.value(QStringLiteral("fileName")).toString();
        const int64_t fileSize = map.value(QStringLiteral("fileSize")).toInteger();
        const QString hash = map.value(QStringLiteral("hash")).toString();

        // SortableStandardItem throughout: this model is bound straight to the
        // view with no proxy, so QStandardItemModel::sortRole() -- Qt::DisplayRole
        // -- is what orders it, and every Qt::UserRole here is a payload
        // downloadSelected() reads, not a sort key.
        auto* nameItem = new SortableStandardItem(fileName);
        nameItem->setEditable(false);
        nameItem->setData(hash, Qt::UserRole);
        nameItem->setData(fileName, Qt::UserRole + 1);
        nameItem->setData(static_cast<qlonglong>(fileSize), Qt::UserRole + 2);

        auto* sizeItem = new SortableStandardItem(formatByteSize(fileSize));
        sizeItem->setEditable(false);
        sizeItem->setData(static_cast<qlonglong>(fileSize), Qt::UserRole);
        sizeItem->setData(static_cast<qlonglong>(fileSize), SortRole);

        auto* hashItem = new SortableStandardItem(hash);
        hashItem->setEditable(false);

        m_model->appendRow({nameItem, sizeItem, hashItem});
    }

    auto* view = new ListTreeView(this);
    m_view = view;
    m_view->setModel(m_model);
    m_view->setRootIsDecorated(false);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSortingEnabled(true);
    m_view->setAllColumnsShowFocus(true);
    m_view->header()->setStretchLastSection(true);
    // File Name, Size, Hash.
    view->bindColumns(QStringLiteral("clientSharedFiles"), {300, 100, 260});
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

    DialogSizing::applySize(this, {}, QSize(640, 400), DialogSizing::Fit::Layout);
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
