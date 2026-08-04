#include "pch.h"
/// @file CollectionViewDialog.cpp
/// @brief Dialog for viewing and downloading .emulecollection contents.

#include "dialogs/CollectionViewDialog.h"

#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "files/Collection.h"
#include "files/CollectionFile.h"
#include "utils/StringUtils.h"

#include <QLocale>

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CollectionViewDialog::CollectionViewDialog(const Collection& collection,
                                            IpcClient* ipc,
                                            QWidget* parent)
    : QDialog(parent)
    , m_collection(collection)
    , m_ipc(ipc)
{
    setWindowTitle(tr("Collection: %1").arg(collection.m_name));
    resize(600, 450);

    auto* layout = new QVBoxLayout(this);

    // Collection list label + file count
    layout->addWidget(new QLabel(tr("Collection List (%1)").arg(collection.fileCount())));

    // File tree
    auto* tree = new ListTreeWidget(this);
    m_tree = tree;
    m_tree->setHeaderLabels({tr("File Name"), tr("Size"), tr("Hash")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setSortingEnabled(true);
    // File Name is Interactive, not Stretch: a Qt-owned width can't be resized
    // by the user, so there would be nothing to remember.
    m_tree->header()->setStretchLastSection(true);
    tree->bindColumns(QStringLiteral("collectionView"), {280, 90, 230});

    for (const auto& [key, cf] : collection.files()) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, cf->fileName());
        item->setText(1, QLocale::system().formattedDataSize(cf->fileSize()));
        item->setData(1, Qt::UserRole, static_cast<qint64>(cf->fileSize())); // for sorting
        item->setText(2, md4str(cf->fileHash()));
    }

    // Select all items by default (matching MFC behavior)
    m_tree->selectAll();

    layout->addWidget(m_tree, 1);

    // Details group: author info
    auto* detailsGroup = new QGroupBox(tr("Details"));
    auto* detailsLayout = new QVBoxLayout(detailsGroup);

    auto* authorRow = new QHBoxLayout;
    authorRow->addWidget(new QLabel(tr("Author:")));
    m_authorNameEdit = new QLineEdit;
    m_authorNameEdit->setReadOnly(true);
    m_authorNameEdit->setText(collection.m_authorName);
    authorRow->addWidget(m_authorNameEdit, 1);
    detailsLayout->addLayout(authorRow);

    auto* keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel(tr("Author Key:")));
    m_authorKeyHashEdit = new QLineEdit;
    m_authorKeyHashEdit->setReadOnly(true);
    m_authorKeyHashEdit->setText(collection.authorKeyHashString());
    keyRow->addWidget(m_authorKeyHashEdit, 1);
    detailsLayout->addLayout(keyRow);

    layout->addWidget(detailsGroup);

    // Options group: category checkbox
    auto* optionsGroup = new QGroupBox(tr("Options"));
    auto* optionsLayout = new QVBoxLayout(optionsGroup);
    m_addCategoryCheck = new QCheckBox(tr("Add to new category"));
    optionsLayout->addWidget(m_addCategoryCheck);
    layout->addWidget(optionsGroup);

    // Buttons
    auto* btnLayout = new QHBoxLayout;

    auto* downloadBtn = new QPushButton(tr("Download"));
    downloadBtn->setDefault(true);
    connect(downloadBtn, &QPushButton::clicked, this, [this]() {
        downloadSelected();
        accept();
    });
    btnLayout->addWidget(downloadBtn);

    btnLayout->addStretch();

    auto* closeBtn = new QPushButton(tr("Close"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);

    layout->addLayout(btnLayout);

    // Double-click to download
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &CollectionViewDialog::downloadSelected);
}

// ---------------------------------------------------------------------------
// Download helpers
// ---------------------------------------------------------------------------

void CollectionViewDialog::downloadSelected()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    const auto selected = m_tree->selectedItems();
    for (const auto* item : selected) {
        const QString hash = item->text(2);
        const QString name = item->text(0);
        const qint64 size = item->data(1, Qt::UserRole).toLongLong();

        Ipc::IpcMessage msg(Ipc::IpcMsgType::DownloadSearchFile);
        msg.append(hash);
        msg.append(name);
        msg.append(size);
        m_ipc->sendRequest(std::move(msg), [](const Ipc::IpcMessage&) {});
    }
}

void CollectionViewDialog::downloadAll()
{
    // TODO: category creation when m_addCategoryCheck is checked
    m_tree->selectAll();
    downloadSelected();
}

} // namespace eMule
