#include "pch.h"
/// @file SimpleCleanupDialog.cpp
/// @brief Mass Rename "Simple cleanup" options — implementation.

#include "dialogs/SimpleCleanupDialog.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "prefs/Preferences.h"
#include "utils/DialogSizing.h"

#include <QCborArray>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eMule {

using namespace massrename;

namespace {

// '"' separated MorphXT's stored list entries; keep it out of the table as Morph did
[[nodiscard]] QString cleanEntry(const QString& s)
{
    return QString(s).remove(u'"');
}

[[nodiscard]] QCborArray toCbor(const QStringList& list)
{
    QCborArray arr;
    for (const QString& s : list)
        arr.append(s);
    return arr;
}

} // namespace

SimpleCleanupDialog::SimpleCleanupDialog(const SimpleCleanupOptions& opt, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Simple cleanup"));
    auto* root = new QVBoxLayout(this);

    m_dotCheck = new QCheckBox(tr("Convert \".\" to \" \""), this);
    m_underlineCheck = new QCheckBox(tr("Convert \"_\" to \" \""), this);
    m_apostropheCheck = new QCheckBox(
        tr("Convert wrong apostrophes \"´\", \"`\" to standard apostrophes \" ' \""), this);
    m_hexCheck = new QCheckBox(tr("Convert hexadecimal numbers \"%xx\" back to characters"), this);
    for (QCheckBox* c : {m_dotCheck, m_underlineCheck, m_apostropheCheck, m_hexCheck})
        root->addWidget(c);

    // Search and replace
    m_searchCheck = new QCheckBox(tr("Search and Replace:"), this);
    root->addWidget(m_searchCheck);
    auto* searchGrid = new QGridLayout;
    searchGrid->setContentsMargins(20, 0, 0, 0);
    m_searchEdit = new QLineEdit(opt.search, this);
    m_replaceEdit = new QLineEdit(opt.replace, this);
    searchGrid->addWidget(new QLabel(tr("Search for:"), this), 0, 0);
    searchGrid->addWidget(m_searchEdit, 0, 1);
    searchGrid->addWidget(new QLabel(tr("Replace by:"), this), 1, 0);
    searchGrid->addWidget(m_replaceEdit, 1, 1);
    m_caseCheck = new QCheckBox(tr("Case sensitive search"), this);
    searchGrid->addWidget(m_caseCheck, 2, 0, 1, 2);
    root->addLayout(searchGrid);

    // Character table
    m_charsCheck = new QCheckBox(tr("Replace characters:"), this);
    root->addWidget(m_charsCheck);
    auto* charsBox = new QGridLayout;
    charsBox->setContentsMargins(20, 0, 0, 0);
    auto* pairRow = new QHBoxLayout;
    m_charSource = new QLineEdit(this);
    m_charDest = new QLineEdit(this);
    pairRow->addWidget(new QLabel(tr("Replace"), this));
    pairRow->addWidget(m_charSource);
    pairRow->addWidget(new QLabel(tr("by"), this));
    pairRow->addWidget(m_charDest);
    charsBox->addLayout(pairRow, 0, 0);
    m_charList = new QTreeWidget(this);
    m_charList->setColumnCount(2);
    m_charList->setHeaderLabels({tr("Search for"), tr("Replace by")});
    m_charList->setRootIsDecorated(false);
    m_charList->setSortingEnabled(true);
    m_charList->sortByColumn(0, Qt::AscendingOrder);
    m_charList->header()->setSectionResizeMode(QHeaderView::Stretch);
    for (qsizetype i = 0; i < opt.searchChars.size() && i < opt.replaceChars.size(); ++i)
        m_charList->addTopLevelItem(new QTreeWidgetItem({opt.searchChars[i], opt.replaceChars[i]}));
    charsBox->addWidget(m_charList, 1, 0);
    auto* btnCol = new QVBoxLayout;
    m_addBtn = new QPushButton(tr("&Add"), this);
    m_editBtn = new QPushButton(tr("&Edit"), this);
    m_deleteBtn = new QPushButton(tr("&Delete"), this);
    btnCol->addWidget(m_addBtn);
    btnCol->addWidget(m_editBtn);
    btnCol->addWidget(m_deleteBtn);
    btnCol->addStretch();
    charsBox->addLayout(btnCol, 0, 1, 2, 1);
    root->addLayout(charsBox, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);

    m_dotCheck->setChecked(opt.flags & DotToSpace);
    m_underlineCheck->setChecked(opt.flags & UnderlineToSpace);
    m_apostropheCheck->setChecked(opt.flags & ReplaceApostrophe);
    m_hexCheck->setChecked(opt.flags & ReplaceHex);
    m_searchCheck->setChecked(opt.flags & SearchAndReplace);
    m_caseCheck->setChecked(opt.flags & SearchCaseSensitive);
    m_charsCheck->setChecked(opt.flags & ReplaceChars);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_addBtn, &QPushButton::clicked, this, &SimpleCleanupDialog::addPair);
    connect(m_editBtn, &QPushButton::clicked, this, &SimpleCleanupDialog::editPair);
    connect(m_deleteBtn, &QPushButton::clicked, this, &SimpleCleanupDialog::deletePair);
    connect(m_charList, &QTreeWidget::itemSelectionChanged, this, &SimpleCleanupDialog::onPairSelected);
    connect(m_searchCheck, &QCheckBox::toggled, this, &SimpleCleanupDialog::updateEnabled);
    connect(m_charsCheck, &QCheckBox::toggled, this, &SimpleCleanupDialog::updateEnabled);
    connect(m_charSource, &QLineEdit::textChanged, this, &SimpleCleanupDialog::updateEnabled);
    updateEnabled();

    DialogSizing::applySize(this, {}, {460, 440}, DialogSizing::Fit::Layout);
}

SimpleCleanupOptions SimpleCleanupDialog::options() const
{
    SimpleCleanupOptions opt;
    opt.flags = (m_dotCheck->isChecked() ? DotToSpace : 0)
              | (m_underlineCheck->isChecked() ? UnderlineToSpace : 0)
              | (m_apostropheCheck->isChecked() ? ReplaceApostrophe : 0)
              | (m_hexCheck->isChecked() ? ReplaceHex : 0)
              | (m_searchCheck->isChecked() ? SearchAndReplace : 0)
              | (m_caseCheck->isChecked() ? SearchCaseSensitive : 0)
              | (m_charsCheck->isChecked() ? ReplaceChars : 0);
    opt.search = m_searchEdit->text();
    opt.replace = m_replaceEdit->text();
    for (int i = 0; i < m_charList->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* item = m_charList->topLevelItem(i);
        opt.searchChars << item->text(0);
        opt.replaceChars << item->text(1);
    }
    return opt;
}

SimpleCleanupOptions SimpleCleanupDialog::storedOptions()
{
    SimpleCleanupOptions opt;
    opt.flags = thePrefs.simpleCleanupOptions();
    opt.search = thePrefs.simpleCleanupSearch();
    opt.replace = thePrefs.simpleCleanupReplace();
    opt.searchChars = thePrefs.simpleCleanupSearchChars();
    opt.replaceChars = thePrefs.simpleCleanupReplaceChars();
    return opt;
}

void SimpleCleanupDialog::storeOptions(const SimpleCleanupOptions& opt, IpcClient* ipc)
{
    thePrefs.setSimpleCleanupOptions(opt.flags);
    thePrefs.setSimpleCleanupSearch(opt.search);
    thePrefs.setSimpleCleanupReplace(opt.replace);
    thePrefs.setSimpleCleanupSearchChars(opt.searchChars);
    thePrefs.setSimpleCleanupReplaceChars(opt.replaceChars);
    if (!ipc || !ipc->isConnected())
        return;
    Ipc::IpcMessage req(Ipc::IpcMsgType::SetPreferences);
    req.append(QStringLiteral("simpleCleanupOptions"));
    req.append(static_cast<qint64>(opt.flags));
    req.append(QStringLiteral("simpleCleanupSearch"));
    req.append(opt.search);
    req.append(QStringLiteral("simpleCleanupReplace"));
    req.append(opt.replace);
    req.append(QStringLiteral("simpleCleanupSearchChars"));
    req.append(toCbor(opt.searchChars));
    req.append(QStringLiteral("simpleCleanupReplaceChars"));
    req.append(toCbor(opt.replaceChars));
    ipc->sendRequest(std::move(req), [](const Ipc::IpcMessage&) {});
}

// ---------------------------------------------------------------------------

void SimpleCleanupDialog::addPair()
{
    const QString src = cleanEntry(m_charSource->text());
    if (src.isEmpty())
        return;
    m_charList->addTopLevelItem(new QTreeWidgetItem({src, cleanEntry(m_charDest->text())}));
}

void SimpleCleanupDialog::editPair()
{
    // Morph's button was wired to the wrong id and never fired
    QTreeWidgetItem* item = m_charList->currentItem();
    const QString src = cleanEntry(m_charSource->text());
    if (!item || src.isEmpty())
        return;
    item->setText(0, src);
    item->setText(1, cleanEntry(m_charDest->text()));
}

void SimpleCleanupDialog::deletePair()
{
    delete m_charList->currentItem();
    updateEnabled();
}

void SimpleCleanupDialog::onPairSelected()
{
    if (const QTreeWidgetItem* item = m_charList->currentItem()) {
        m_charSource->setText(item->text(0));
        m_charDest->setText(item->text(1));
    }
    updateEnabled();
}

void SimpleCleanupDialog::updateEnabled()
{
    const bool search = m_searchCheck->isChecked();
    m_searchEdit->setEnabled(search);
    m_replaceEdit->setEnabled(search);
    m_caseCheck->setEnabled(search);

    const bool chars = m_charsCheck->isChecked();
    const bool hasSel = m_charList->currentItem() != nullptr;
    for (QWidget* w : {static_cast<QWidget*>(m_charSource), static_cast<QWidget*>(m_charDest),
                       static_cast<QWidget*>(m_charList)})
        w->setEnabled(chars);
    m_addBtn->setEnabled(chars && !m_charSource->text().isEmpty());
    m_editBtn->setEnabled(chars && hasSel && !m_charSource->text().isEmpty());
    m_deleteBtn->setEnabled(chars && hasSel);
}

} // namespace eMule
