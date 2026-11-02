#include "pch.h"
/// @file Ed3kOwnSetDialog.cpp
/// @brief "Create ed3k Set…" — implementation.

#include "dialogs/Ed3kOwnSetDialog.h"

#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "controls/SortableItems.h"
#include "dialogs/Ed3kSellDialogs.h"
#include "utils/AsyncMessageBox.h"
#include "utils/DialogSizing.h"
#include "utils/SharedFilesFetch.h"
#include "utils/StringUtils.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QApplication>
#include <QCborArray>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

constexpr int kPathRole = Qt::UserRole;

QString errorOf(const IpcMessage& r)
{
    return r.fieldMap(1).value(QStringLiteral("error")).toString();
}

} // namespace

Ed3kOwnSetDialog::Ed3kOwnSetDialog(IpcClient* ipc, const QStringList& files, const QString& folder, QWidget* parent)
    : RememberedSizeDialog(parent)
    , m_ipc(ipc)
    , m_folder(folder)
    , m_preselected(files)
{
    setupUi();
    if (m_folder.isEmpty())
        populateSharedFiles();
}

void Ed3kOwnSetDialog::run(IpcClient* ipc, QWidget* parent, const QStringList& files, const QString& folder)
{
    auto* dlg = new Ed3kOwnSetDialog(ipc, files, folder, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

QString Ed3kOwnSetDialog::title() const
{
    return m_nameEdit->text().trimmed();
}

int Ed3kOwnSetDialog::kind() const
{
    return m_kind->currentData().toInt();
}

QStringList Ed3kOwnSetDialog::files() const
{
    QStringList out;
    if (!m_folder.isEmpty())
        return out;
    for (int i = 0; i < m_setTree->topLevelItemCount(); ++i)
        out << m_setTree->topLevelItem(i)->data(0, kPathRole).toString();
    return out;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kOwnSetDialog::setupUi()
{
    setWindowTitle(m_folder.isEmpty() ? tr("Create ed3k Set...") : tr("Publish as ed3k Set..."));
    auto* mainLayout = new QVBoxLayout(this);

    if (m_folder.isEmpty()) {
        // MFC CCollectionCreateDialog: shared files | arrows | the set's files
        auto* paneLayout = new QHBoxLayout;
        auto* leftLayout = new QVBoxLayout;
        m_sharedLabel = new QLabel(tr("Shared (%1)").arg(0));
        leftLayout->addWidget(m_sharedLabel);
        auto* sharedTree = new ListTreeWidget;
        m_sharedTree = sharedTree;
        m_sharedTree->setHeaderLabels({tr("File Name"), tr("Size"), tr("Folder")});
        m_sharedTree->setRootIsDecorated(false);
        m_sharedTree->setAlternatingRowColors(true);
        m_sharedTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_sharedTree->setSortingEnabled(true);
        m_sharedTree->header()->setStretchLastSection(true);
        sharedTree->bindColumns(QStringLiteral("ed3kOwnSetShared"), {200, 70, 200});
        leftLayout->addWidget(m_sharedTree, 1);
        paneLayout->addLayout(leftLayout, 1);

        auto* arrowLayout = new QVBoxLayout;
        arrowLayout->addStretch();
        auto* addBtn = new QPushButton(QStringLiteral("▶"));   // ▶
        addBtn->setFixedSize(32, 32);
        addBtn->setToolTip(tr("Add to the set"));
        connect(addBtn, &QPushButton::clicked, this, &Ed3kOwnSetDialog::addToSet);
        arrowLayout->addWidget(addBtn);
        auto* removeBtn = new QPushButton(QStringLiteral("◀"));   // ◀
        removeBtn->setFixedSize(32, 32);
        removeBtn->setToolTip(tr("Remove from the set"));
        connect(removeBtn, &QPushButton::clicked, this, &Ed3kOwnSetDialog::removeFromSet);
        arrowLayout->addWidget(removeBtn);
        arrowLayout->addStretch();
        paneLayout->addLayout(arrowLayout);

        auto* rightLayout = new QVBoxLayout;
        m_setLabel = new QLabel(tr("ed3k Set (%1)").arg(0));
        rightLayout->addWidget(m_setLabel);
        auto* setTree = new ListTreeWidget;
        m_setTree = setTree;
        m_setTree->setHeaderLabels({tr("File Name"), tr("Size"), tr("Folder")});
        m_setTree->setRootIsDecorated(false);
        m_setTree->setAlternatingRowColors(true);
        m_setTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
        m_setTree->setSortingEnabled(true);
        m_setTree->header()->setStretchLastSection(true);
        setTree->bindColumns(QStringLiteral("ed3kOwnSetFiles"), {200, 70, 200});
        rightLayout->addWidget(m_setTree, 1);
        paneLayout->addLayout(rightLayout, 1);
        mainLayout->addLayout(paneLayout, 1);
        connect(m_sharedTree, &QTreeWidget::itemDoubleClicked, this, &Ed3kOwnSetDialog::addToSet);
        connect(m_setTree, &QTreeWidget::itemDoubleClicked, this, &Ed3kOwnSetDialog::removeFromSet);
    } else {
        auto* folderGroup = new QGroupBox(tr("Folder"));
        auto* folderLayout = new QVBoxLayout(folderGroup);
        auto* path = new QLabel(m_folder);
        path->setTextInteractionFlags(Qt::TextSelectableByMouse);
        folderLayout->addWidget(path);
        auto* note = new QLabel(tr("Every file below this folder is in the set; hidden files are left out. "
                                   "\"Publish New Version\" takes the folder as it is then."));
        note->setWordWrap(true);
        folderLayout->addWidget(note);
        mainLayout->addWidget(folderGroup);
    }

    // Basic Options (MFC: Name)
    auto* basicGroup = new QGroupBox(tr("Basic Options"));
    auto* basicLayout = new QHBoxLayout(basicGroup);
    basicLayout->addWidget(new QLabel(tr("Name:")));
    m_nameEdit = new QLineEdit;
    m_nameEdit->setText(m_folder.isEmpty() ? QStringLiteral("New ed3k Set-%1").arg(QDateTime::currentSecsSinceEpoch())
                                           : QFileInfo(m_folder).fileName());
    basicLayout->addWidget(m_nameEdit, 1);
    mainLayout->addWidget(basicGroup);

    // ed3k Options: the kind of set
    auto* kindGroup = new QGroupBox(tr("ed3k Options"));
    auto* kindLayout = new QVBoxLayout(kindGroup);
    auto* kindRow = new QHBoxLayout;
    kindRow->addWidget(new QLabel(tr("Kind:")));
    m_kind = new QComboBox;
    m_kind->addItem(tr("Open"), int(Open));
    m_kind->addItem(tr("Hidden"), int(Hidden));
    if constexpr (kEd3kSellingShown)
        m_kind->addItem(tr("Paid"), int(Paid));
    kindRow->addWidget(m_kind, 1);
    m_termsButton = new QPushButton(tr("Terms..."));
    connect(m_termsButton, &QPushButton::clicked, this, [this] { (void)askTerms(); });
    kindRow->addWidget(m_termsButton);
    m_termsButton->setVisible(kEd3kSellingShown);
    kindLayout->addLayout(kindRow);
    m_kindNote = new QLabel;
    m_kindNote->setWordWrap(true);
    kindLayout->addWidget(m_kindNote);
    mainLayout->addWidget(kindGroup);
    connect(m_kind, &QComboBox::currentIndexChanged, this, &Ed3kOwnSetDialog::onKindChanged);
    onKindChanged();

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    m_saveButton = new QPushButton(tr("Save"));
    m_saveButton->setDefault(true);
    connect(m_saveButton, &QPushButton::clicked, this, &Ed3kOwnSetDialog::onSave);
    btnLayout->addWidget(m_saveButton);
    auto* cancelBtn = new QPushButton(tr("Cancel"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

    DialogSizing::applySize(this, {}, m_folder.isEmpty() ? QSize(760, 480) : QSize(520, 300), DialogSizing::Fit::Layout);
}

QTreeWidgetItem* Ed3kOwnSetDialog::makeRow(const QString& name, qint64 size, const QString& path)
{
    auto* item = new SortableTreeItem;
    item->setText(0, name);
    item->setText(1, formatByteSize(size));
    item->setData(1, SortRole, size);
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    item->setText(2, QFileInfo(path).path());
    item->setData(0, kPathRole, path);
    return item;
}

void Ed3kOwnSetDialog::populateSharedFiles()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    fetchSharedFileRows(m_ipc, this, [this](bool ok, const QCborArray& rows) {
        if (!ok)
            return;
        const QSet<QString> pre(m_preselected.cbegin(), m_preselected.cend());
        for (const auto& v : rows) {
            const QCborMap m = v.toMap();
            // part files are no member (a set lists complete files)
            if (m.value(QStringLiteral("isPartFile")).toBool())
                continue;
            const QString path = m.value(QStringLiteral("filePath")).toString();
            if (path.isEmpty())
                continue;
            QTreeWidgetItem* item = makeRow(m.value(QStringLiteral("fileName")).toString(), m.value(QStringLiteral("fileSize")).toInteger(), path);
            if (pre.contains(path))
                m_setTree->addTopLevelItem(item);
            else
                m_sharedTree->addTopLevelItem(item);
        }
        updateLabels();
    });
}

void Ed3kOwnSetDialog::addToSet()
{
    for (auto* item : m_sharedTree->selectedItems()) {
        m_sharedTree->takeTopLevelItem(m_sharedTree->indexOfTopLevelItem(item));
        m_setTree->addTopLevelItem(item);
    }
    updateLabels();
}

void Ed3kOwnSetDialog::removeFromSet()
{
    for (auto* item : m_setTree->selectedItems()) {
        m_setTree->takeTopLevelItem(m_setTree->indexOfTopLevelItem(item));
        m_sharedTree->addTopLevelItem(item);
    }
    updateLabels();
}

void Ed3kOwnSetDialog::updateLabels()
{
    if (m_sharedLabel)
        m_sharedLabel->setText(tr("Shared (%1)").arg(m_sharedTree->topLevelItemCount()));
    if (m_setLabel)
        m_setLabel->setText(tr("ed3k Set (%1)").arg(m_setTree->topLevelItemCount()));
}

void Ed3kOwnSetDialog::onKindChanged()
{
    const int k = kind();
    m_termsButton->setEnabled(k == Paid);
    if (k == Hidden)
        m_kindNote->setText(tr("Only who has the link finds and reads the set; the files are shared sealed. The link is "
                               "a key: give it to the people who should have it."));
    else if (k == Paid)
        m_kindNote->setText(tr("Everyone sees the list and the price; the files are sealed, buyers with a pass open "
                               "them. Passes are issued in Options → ed3k → Selling."));
    else
        m_kindNote->setText(tr("Everyone with the link gets the set and its new versions."));
}

bool Ed3kOwnSetDialog::askTerms()
{
    // the defaults of a new offer are fetched once, then the terms dialog
    if (m_defaults.isEmpty() && m_ipc && m_ipc->isConnected() && !m_terms) {
        QPointer<Ed3kOwnSetDialog> self(this);
        m_ipc->sendRequest(IpcMessage(IpcMsgType::GetEd3kSelling), [self](const IpcMessage& r) {
            if (!self)
                return;
            self->m_defaults = r.fieldBool(0) ? r.fieldMap(1).value(QStringLiteral("defaults")).toMap() : QCborMap{};
            if (self->m_defaults.isEmpty())
                self->m_defaults.insert(QStringLiteral("passDays"), 30);
            (void)self->askTerms();
        });
        return false;
    }
    const auto t = Ed3kSellTermsDialog::ask(this, Ed3kSellTermsDialog::Mode::NewSet, m_terms.value_or(m_defaults),
                                            tr("Sell \"%1\"").arg(title()));
    if (!t)
        return false;
    m_terms = *t;
    return true;
}

void Ed3kOwnSetDialog::onSave()
{
    if (title().isEmpty()) {
        QMessageBox::warning(this, tr("ed3k Set"), tr("Please enter a name for the set."));
        return;
    }
    if (m_folder.isEmpty() && m_setTree->topLevelItemCount() == 0) {
        QMessageBox::warning(this, tr("ed3k Set"), tr("The set is empty. Add files first."));
        return;
    }
    if (kind() == Paid && !m_terms) {
        (void)askTerms();
        return;   // Save again once the terms are there
    }
    if (!m_ipc || !m_ipc->isConnected())
        return;
    m_saveButton->setEnabled(false);
    QPointer<Ed3kOwnSetDialog> self(this);
    m_ipc->publishEd3kOwnSet(m_folder, files(), title(), kind(), [self](const IpcMessage& r) {
        if (!self)
            return;
        self->m_saveButton->setEnabled(true);
        if (!r.fieldBool(0)) {
            AsyncMessageBox::warning(self, tr("ed3k Set"), tr("The set was not published: %1").arg(errorOf(r)));
            return;
        }
        const QString id = r.fieldMap(1).value(QStringLiteral("id")).toString();
        const QString link = r.fieldMap(1).value(QStringLiteral("link")).toString();
        if (self->kind() == Paid) {
            self->sellNew(id);
            return;
        }
        if (!link.isEmpty()) {
            QApplication::clipboard()->setText(link);
            AsyncMessageBox::information(self->parentWidget(), tr("ed3k Set"),
                                         tr("The set is being published. Its link was copied to the clipboard:\n%1").arg(link));
        } else {
            AsyncMessageBox::information(self->parentWidget(), tr("ed3k Set"),
                                         tr("The hidden set is being published. Copy its link from My ed3k Sets; it is a key."));
        }
        self->accept();
    });
}

void Ed3kOwnSetDialog::sellNew(const QString& id)
{
    IpcMessage req(IpcMsgType::SellEd3k);
    req.append(qint64(2));
    req.append(id);
    req.append(title());
    req.append(m_terms.value_or(QCborMap{}));
    QPointer<Ed3kOwnSetDialog> self(this);
    m_ipc->sendRequest(std::move(req), [self](const IpcMessage& r) {
        if (!self)
            return;
        if (!r.fieldBool(0)) {
            // the set stays listed, not sold: Sell… in My ed3k Sets
            AsyncMessageBox::warning(self->parentWidget(), tr("ed3k Set"),
                                     tr("The set was made but not sold: %1\nSell it from My ed3k Sets.").arg(errorOf(r)));
            self->accept();
            return;
        }
        const QString link = r.fieldMap(1).value(QStringLiteral("link")).toString();
        QApplication::clipboard()->setText(link);
        AsyncMessageBox::information(self->parentWidget(), tr("ed3k Set"),
                                     tr("The set is sold and being published; buyers need a pass. Issue passes in Options → "
                                        "ed3k → Selling. The link was copied to the clipboard:\n%1")
                                         .arg(link));
        self->accept();
    });
}

} // namespace eMule
