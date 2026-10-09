#include "pch.h"
/// @file CollectionViewDialog.cpp
/// @brief Dialog for viewing and downloading .emulecollection contents.

#include "dialogs/CollectionViewDialog.h"
#include "dialogs/CollectionCategory.h"

#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "controls/SortableItems.h"
#include "files/Collection.h"
#include "files/CollectionFile.h"
#include "utils/StringUtils.h"
#include "utils/DialogSizing.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
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
        auto* item = new SortableTreeItem(m_tree);
        item->setText(0, cf->fileName());
        item->setText(1, formatByteSize(cf->fileSize()));
        item->setData(1, SortRole, static_cast<qint64>(cf->fileSize()));
        // UserRole is the payload downloadSelected() reads back, not a sort key;
        // the two are separate roles precisely so neither can shadow the other.
        item->setData(1, Qt::UserRole, static_cast<qint64>(cf->fileSize()));
        item->setText(2, md4str(cf->fileHash()));
        // The full link: the only thing that carries the entry's AICH hash to the daemon
        item->setData(0, Qt::UserRole, cf->getED2kLink());
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
    // MFC IDS_COLL_ADDINCAT. A category already named like the collection is used
    // either way; ticked, one is created when there is none.
    m_addCategoryCheck = new QCheckBox(tr("Add new downloads into the collection category"));
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

    DialogSizing::applySize(this, {}, QSize(600, 450), DialogSizing::Fit::Layout);
}

// ---------------------------------------------------------------------------
// Download helpers
// ---------------------------------------------------------------------------

void CollectionViewDialog::downloadSelected()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    // Collected first: the dialog closes on Download and the answers come later.
    struct Entry { QString hash; QString name; qint64 size; QString link; };
    QList<Entry> entries;
    const auto selected = m_tree->selectedItems();
    for (const auto* item : selected)
        entries.append({item->text(2), item->text(0), item->data(1, Qt::UserRole).toLongLong(),
                        item->data(0, Qt::UserRole).toString()});
    if (entries.isEmpty())
        return;

    const QPointer<IpcClient> ipc(m_ipc);
    const QString collectionName = m_collection.m_name;
    const bool createCategory = m_addCategoryCheck->isChecked();

    const auto queue = [ipc, entries](int category) {
        if (!ipc || !ipc->isConnected())
            return;
        for (const Entry& e : entries) {
            Ipc::IpcMessage msg(Ipc::IpcMsgType::DownloadSearchFile);
            msg.append(e.hash);
            msg.append(e.name);
            msg.append(e.size);
            msg.append(e.link);
            msg.append(static_cast<qint64>(category));
            ipc->sendRequest(std::move(msg), [](const Ipc::IpcMessage&) {});
        }
    };

    // MFC CCollectionViewDialog::DownloadSelected (CollectionViewDialog.cpp:156-180)
    m_ipc->sendRequest(Ipc::IpcMessage(Ipc::IpcMsgType::GetCategories),
        [ipc, queue, collectionName, createCategory](const Ipc::IpcMessage& resp) {
            if (!ipc || !ipc->isConnected())
                return;
            const QCborArray categories = resp.fieldBool(0) ? resp.fieldArray(1) : QCborArray();
            const int existing = CollectionCategory::indexFor(categories, collectionName);
            if (existing > 0 || !createCategory || categories.isEmpty() || collectionName.isEmpty()) {
                queue(existing);
                return;
            }
            Ipc::IpcMessage create(Ipc::IpcMsgType::SetCategories);
            create.append(CollectionCategory::withNewCategory(categories, collectionName));
            const int newIndex = static_cast<int>(categories.size());
            ipc->sendRequest(std::move(create), [queue, newIndex](const Ipc::IpcMessage& r) {
                // without the category the files still download, uncategorised
                queue(r.fieldBool(0) ? newIndex : 0);
            });
        });
}

} // namespace eMule
