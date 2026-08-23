#include "pch.h"
/// @file CollectionCreateDialog.cpp
/// @brief Dialog for creating and modifying .emulecollection files.

#include "dialogs/CollectionCreateDialog.h"
#include "app/IpcClient.h"
#include "controls/AbstractListView.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "utils/DialogSizing.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CollectionCreateDialog::CollectionCreateDialog(IpcClient* ipc,
                                               const QStringList& selectedHashes,
                                               QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
    , m_preselectedHashes(selectedHashes)
{
    setupUi();
    populateSharedFiles();
}

// ---------------------------------------------------------------------------
// loadExistingCollection — modify mode
// ---------------------------------------------------------------------------

void CollectionCreateDialog::loadExistingCollection(const QString& collectionHash,
                                                     const QString& name,
                                                     const QList<QVariantMap>& files,
                                                     bool textFormat)
{
    m_existingCollectionHash = collectionHash;
    setWindowTitle(tr("Modify Collection..."));
    m_nameEdit->setText(name);
    m_textFormatCheck->setChecked(textFormat);
    onFormatChanged();

    // Populate right pane with existing collection files
    for (const auto& f : files) {
        auto* item = new QTreeWidgetItem(m_collectionTree);
        item->setText(0, f.value(QStringLiteral("fileName")).toString());
        item->setData(0, Qt::UserRole, f.value(QStringLiteral("hash")).toString());
        item->setData(0, Qt::UserRole + 1, f.value(QStringLiteral("fileSize")).toLongLong());
    }
    updateLabels();
}

// ---------------------------------------------------------------------------
// setupUi
// ---------------------------------------------------------------------------

void CollectionCreateDialog::setupUi()
{
    setWindowTitle(tr("Create Collection..."));

    auto* mainLayout = new QVBoxLayout(this);

    // Top: dual pane with arrows in between
    auto* paneLayout = new QHBoxLayout;

    // Left pane: shared files
    auto* leftLayout = new QVBoxLayout;
    m_sharedLabel = new QLabel(tr("Shared (0)"));
    leftLayout->addWidget(m_sharedLabel);

    auto* sharedTree = new ListTreeWidget;
    m_sharedTree = sharedTree;
    m_sharedTree->setHeaderLabels({tr("File Name")});
    m_sharedTree->setRootIsDecorated(false);
    m_sharedTree->setAlternatingRowColors(true);
    m_sharedTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_sharedTree->setSortingEnabled(true);
    m_sharedTree->header()->setStretchLastSection(true);
    sharedTree->bindColumns(QStringLiteral("collectionCreateShared"), {280});
    leftLayout->addWidget(m_sharedTree, 1);
    paneLayout->addLayout(leftLayout, 1);

    // Center arrows
    auto* arrowLayout = new QVBoxLayout;
    arrowLayout->addStretch();

    auto* addBtn = new QPushButton(QStringLiteral("\u25B6")); // ▶
    addBtn->setFixedSize(32, 32);
    addBtn->setToolTip(tr("Add to collection"));
    connect(addBtn, &QPushButton::clicked, this, &CollectionCreateDialog::addToCollection);
    arrowLayout->addWidget(addBtn);

    auto* removeBtn = new QPushButton(QStringLiteral("\u25C0")); // ◀
    removeBtn->setFixedSize(32, 32);
    removeBtn->setToolTip(tr("Remove from collection"));
    connect(removeBtn, &QPushButton::clicked, this, &CollectionCreateDialog::removeFromCollection);
    arrowLayout->addWidget(removeBtn);

    arrowLayout->addStretch();
    paneLayout->addLayout(arrowLayout);

    // Right pane: collection files
    auto* rightLayout = new QVBoxLayout;
    m_collectionLabel = new QLabel(tr("Collection List (0)"));
    rightLayout->addWidget(m_collectionLabel);

    auto* collectionTree = new ListTreeWidget;
    m_collectionTree = collectionTree;
    m_collectionTree->setHeaderLabels({tr("File Name")});
    m_collectionTree->setRootIsDecorated(false);
    m_collectionTree->setAlternatingRowColors(true);
    m_collectionTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_collectionTree->setSortingEnabled(true);
    m_collectionTree->header()->setStretchLastSection(true);
    collectionTree->bindColumns(QStringLiteral("collectionCreateFiles"), {280});
    rightLayout->addWidget(m_collectionTree, 1);
    paneLayout->addLayout(rightLayout, 1);

    mainLayout->addLayout(paneLayout, 1);

    // Double-click to add/remove
    connect(m_sharedTree, &QTreeWidget::itemDoubleClicked,
            this, &CollectionCreateDialog::addToCollection);
    connect(m_collectionTree, &QTreeWidget::itemDoubleClicked,
            this, &CollectionCreateDialog::removeFromCollection);

    // Basic Options
    auto* basicGroup = new QGroupBox(tr("Basic Options"));
    auto* basicLayout = new QHBoxLayout(basicGroup);
    basicLayout->addWidget(new QLabel(tr("Name:")));
    m_nameEdit = new QLineEdit;
    m_nameEdit->setText(QStringLiteral("New Collection-%1")
                            .arg(QDateTime::currentSecsSinceEpoch()));
    basicLayout->addWidget(m_nameEdit, 1);
    mainLayout->addWidget(basicGroup);

    // Advanced Options
    auto* advGroup = new QGroupBox(tr("Advanced Options"));
    auto* advLayout = new QVBoxLayout(advGroup);
    m_textFormatCheck = new QCheckBox(tr("Save collection in plain text format"));
    connect(m_textFormatCheck, &QCheckBox::toggled,
            this, &CollectionCreateDialog::onFormatChanged);
    advLayout->addWidget(m_textFormatCheck);
    m_signCheck = new QCheckBox(tr("Sign collection with name and key"));
    advLayout->addWidget(m_signCheck);
    mainLayout->addWidget(advGroup);

    // Buttons
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();

    auto* saveBtn = new QPushButton(tr("Save"));
    saveBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &CollectionCreateDialog::onSave);
    btnLayout->addWidget(saveBtn);

    auto* cancelBtn = new QPushButton(tr("Cancel"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    mainLayout->addLayout(btnLayout);

    DialogSizing::applySize(this, {}, QSize(640, 420), DialogSizing::Fit::Layout);
}

// ---------------------------------------------------------------------------
// populateSharedFiles
// ---------------------------------------------------------------------------

void CollectionCreateDialog::populateSharedFiles()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    Ipc::IpcMessage msg(Ipc::IpcMsgType::GetSharedFiles);
    m_ipc->sendRequest(std::move(msg), [this](const Ipc::IpcMessage& resp) {
        if (resp.type() != Ipc::IpcMsgType::Result)
            return;

        const QCborArray arr = resp.fieldArray(1);

        // Collect hashes already in the right pane
        QSet<QString> rightHashes;
        for (int i = 0; i < m_collectionTree->topLevelItemCount(); ++i)
            rightHashes.insert(m_collectionTree->topLevelItem(i)->text(2));

        for (const auto& val : arr) {
            const QCborMap m = val.toMap();
            const QString hash = m.value(QStringLiteral("hash")).toString();
            const QString name = m.value(QStringLiteral("fileName")).toString();
            const qint64 size = m.value(QStringLiteral("fileSize")).toInteger();

            // Skip files already in collection
            if (rightHashes.contains(hash))
                continue;

            auto* item = new QTreeWidgetItem(m_sharedTree);
            item->setText(0, name);
            item->setData(0, Qt::UserRole, hash);          // store hash
            item->setData(0, Qt::UserRole + 1, size);      // store size for sorting

            // If this was pre-selected, move it to collection
            if (m_preselectedHashes.contains(hash)) {
                m_sharedTree->takeTopLevelItem(m_sharedTree->indexOfTopLevelItem(item));
                m_collectionTree->addTopLevelItem(item);
            }
        }

        updateLabels();
    });
}

// ---------------------------------------------------------------------------
// addToCollection / removeFromCollection
// ---------------------------------------------------------------------------

void CollectionCreateDialog::addToCollection()
{
    const auto selected = m_sharedTree->selectedItems();
    for (auto* item : selected) {
        m_sharedTree->takeTopLevelItem(m_sharedTree->indexOfTopLevelItem(item));
        m_collectionTree->addTopLevelItem(item);
    }
    updateLabels();
}

void CollectionCreateDialog::removeFromCollection()
{
    const auto selected = m_collectionTree->selectedItems();
    for (auto* item : selected) {
        m_collectionTree->takeTopLevelItem(m_collectionTree->indexOfTopLevelItem(item));
        m_sharedTree->addTopLevelItem(item);
    }
    updateLabels();
}

// ---------------------------------------------------------------------------
// updateLabels
// ---------------------------------------------------------------------------

void CollectionCreateDialog::updateLabels()
{
    m_sharedLabel->setText(tr("Shared (%1)").arg(m_sharedTree->topLevelItemCount()));
    m_collectionLabel->setText(tr("Collection List (%1)").arg(m_collectionTree->topLevelItemCount()));
}

// ---------------------------------------------------------------------------
// onFormatChanged
// ---------------------------------------------------------------------------

void CollectionCreateDialog::onFormatChanged()
{
    // Signing not supported for text format
    if (m_textFormatCheck->isChecked()) {
        m_signCheck->setChecked(false);
        m_signCheck->setEnabled(false);
    } else {
        m_signCheck->setEnabled(true);
    }
}

// ---------------------------------------------------------------------------
// onSave
// ---------------------------------------------------------------------------

void CollectionCreateDialog::onSave()
{
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Collection"), tr("Please enter a collection name."));
        return;
    }

    const int fileCount = m_collectionTree->topLevelItemCount();
    if (fileCount == 0) {
        QMessageBox::warning(this, tr("Collection"), tr("Collection is empty. Add files first."));
        return;
    }

    if (!m_ipc || !m_ipc->isConnected())
        return;

    // Collect hashes from right pane (stored in UserRole)
    QCborArray hashes;
    for (int i = 0; i < fileCount; ++i) {
        auto* item = m_collectionTree->topLevelItem(i);
        hashes.append(item->data(0, Qt::UserRole).toString());
    }

    Ipc::IpcMessage msg(Ipc::IpcMsgType::SaveCollection);
    msg.append(name);
    msg.append(QCborValue(hashes));
    msg.append(m_textFormatCheck->isChecked());
    msg.append(m_signCheck->isChecked());

    m_ipc->sendRequest(std::move(msg), [this](const Ipc::IpcMessage& resp) {
        if (resp.type() == Ipc::IpcMsgType::Result && resp.fieldBool(0)) {
            accept();
        } else {
            const QString err = resp.fieldString(1);
            QMessageBox::warning(this, tr("Collection"),
                tr("Failed to save collection: %1").arg(err));
        }
    });
}

} // namespace eMule
