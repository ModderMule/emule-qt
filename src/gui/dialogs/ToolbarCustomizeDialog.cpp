#include "pch.h"
#include "dialogs/ToolbarCustomizeDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace eMule {

// Must match kAllButtons in MainWindow.cpp — provides labels/icons for the dialog.
struct ButtonMeta {
    ToolbarButtonId id;
    const char* label;
    const char* iconResource;
};

static constexpr ButtonMeta kButtonMeta[] = {
    {ToolbarButtonId::Connect,     "Connect",      "ConnectDrop.ico"},
    {ToolbarButtonId::Kad,         "Kad",           "Kad.ico"},
    {ToolbarButtonId::Servers,     "Servers",       "Server.ico"},
    {ToolbarButtonId::Transfers,   "Transfers",     "Transfer.ico"},
    {ToolbarButtonId::Search,      "Search",        "Search.ico"},
    {ToolbarButtonId::SharedFiles, "Shared Files",  "SharedFiles.ico"},
    {ToolbarButtonId::Messages,    "Messages",      "Messages.ico"},
    {ToolbarButtonId::IRC,         "IRC",           "IRC.ico"},
    {ToolbarButtonId::Statistics,  "Statistics",     "Statistics.ico"},
    {ToolbarButtonId::Options,     "Options",       "Preferences.ico"},
    {ToolbarButtonId::Tools,       "Tools",         "Tools.ico"},
    {ToolbarButtonId::Help,        "Help",          "Help.ico"},
};

static const ButtonMeta* findMeta(ToolbarButtonId id)
{
    for (const auto& m : kButtonMeta) {
        if (m.id == id)
            return &m;
    }
    return nullptr;
}

static QIcon iconForButton(ToolbarButtonId id)
{
    if (id == ToolbarButtonId::Separator)
        return {};
    if (const auto* m = findMeta(id))
        return QIcon(QStringLiteral(":/icons/") + QLatin1String(m->iconResource));
    return {};
}

static QString labelForButton(ToolbarButtonId id)
{
    if (id == ToolbarButtonId::Separator)
        return QStringLiteral("── Separator ──");
    if (const auto* m = findMeta(id))
        return QString::fromLatin1(m->label);
    return QStringLiteral("Unknown");
}

static const QList<int> kDefaultToolbarOrder = {
    0, 1, 10, 11, 12, 13, 14, 15, 16, 17, 1, 20, 21, 22
};

ToolbarCustomizeDialog::ToolbarCustomizeDialog(
    const QList<ToolbarButtonId>& currentOrder, QWidget* parent)
    : QDialog(parent)
    , m_order(currentOrder)
    , m_initialOrder(currentOrder)
{
    setWindowTitle(tr("Customize Toolbar"));
    setMinimumSize(600, 400);

    auto* mainLayout = new QHBoxLayout(this);

    // Left: Available buttons
    auto* leftLayout = new QVBoxLayout;
    leftLayout->addWidget(new QLabel(tr("Available toolbar buttons:")));
    m_availableList = new QListWidget;
    m_availableList->setIconSize(QSize(32, 32));
    leftLayout->addWidget(m_availableList);
    mainLayout->addLayout(leftLayout);

    // Center: Add/Remove buttons
    auto* centerLayout = new QVBoxLayout;
    centerLayout->addStretch();
    m_addBtn = new QPushButton(tr("Add ->"));
    m_removeBtn = new QPushButton(tr("<- Remove"));
    centerLayout->addWidget(m_addBtn);
    centerLayout->addWidget(m_removeBtn);
    centerLayout->addStretch();
    mainLayout->addLayout(centerLayout);

    // Right: Current buttons
    auto* rightLayout = new QVBoxLayout;
    rightLayout->addWidget(new QLabel(tr("Current toolbar buttons:")));
    m_currentList = new QListWidget;
    m_currentList->setIconSize(QSize(32, 32));
    rightLayout->addWidget(m_currentList);
    mainLayout->addLayout(rightLayout);

    // Far right: Close/Reset/Move buttons
    auto* btnLayout = new QVBoxLayout;
    auto* closeBtn = new QPushButton(tr("Close"));
    auto* resetBtn = new QPushButton(tr("Reset"));
    m_moveUpBtn = new QPushButton(tr("Move Up"));
    m_moveDownBtn = new QPushButton(tr("Move Down"));
    btnLayout->addWidget(closeBtn);
    btnLayout->addWidget(resetBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_moveUpBtn);
    btnLayout->addWidget(m_moveDownBtn);
    mainLayout->addLayout(btnLayout);

    populateLists();

    connect(m_addBtn, &QPushButton::clicked, this, &ToolbarCustomizeDialog::onAdd);
    connect(m_removeBtn, &QPushButton::clicked, this, &ToolbarCustomizeDialog::onRemove);
    connect(m_moveUpBtn, &QPushButton::clicked, this, &ToolbarCustomizeDialog::onMoveUp);
    connect(m_moveDownBtn, &QPushButton::clicked, this, &ToolbarCustomizeDialog::onMoveDown);
    connect(resetBtn, &QPushButton::clicked, this, &ToolbarCustomizeDialog::onReset);
    connect(closeBtn, &QPushButton::clicked, this, [this]() {
        if (m_order != m_initialOrder)
            emit orderChanged(m_order);
        accept();
    });
}

void ToolbarCustomizeDialog::populateLists()
{
    m_availableList->clear();
    m_currentList->clear();

    // Current list
    for (auto id : m_order) {
        auto* item = new QListWidgetItem(iconForButton(id), labelForButton(id));
        item->setData(Qt::UserRole, static_cast<int>(id));
        m_currentList->addItem(item);
    }

    // Available list: always has Separator first, plus any buttons not in current order
    auto* sepItem = new QListWidgetItem(QStringLiteral("Separator"));
    sepItem->setData(Qt::UserRole, static_cast<int>(ToolbarButtonId::Separator));
    m_availableList->addItem(sepItem);

    for (const auto& meta : kButtonMeta) {
        bool inOrder = false;
        for (auto id : m_order) {
            if (id == meta.id) {
                inOrder = true;
                break;
            }
        }
        if (!inOrder) {
            auto* item = new QListWidgetItem(
                iconForButton(meta.id), QString::fromLatin1(meta.label));
            item->setData(Qt::UserRole, static_cast<int>(meta.id));
            m_availableList->addItem(item);
        }
    }
}

void ToolbarCustomizeDialog::onAdd()
{
    auto* item = m_availableList->currentItem();
    if (!item)
        return;

    auto id = static_cast<ToolbarButtonId>(item->data(Qt::UserRole).toInt());

    // Insert after current selection in current list (or at end)
    int insertPos = m_currentList->currentRow();
    if (insertPos < 0)
        insertPos = static_cast<int>(m_order.size());
    else
        insertPos += 1;

    m_order.insert(insertPos, id);

    // If not Separator, remove from available list
    if (id != ToolbarButtonId::Separator)
        delete m_availableList->takeItem(m_availableList->row(item));

    populateLists();
    if (insertPos < m_currentList->count())
        m_currentList->setCurrentRow(insertPos);
}

void ToolbarCustomizeDialog::onRemove()
{
    int row = m_currentList->currentRow();
    if (row < 0 || row >= m_order.size())
        return;

    m_order.removeAt(row);
    populateLists();

    if (row < m_currentList->count())
        m_currentList->setCurrentRow(row);
    else if (m_currentList->count() > 0)
        m_currentList->setCurrentRow(m_currentList->count() - 1);
}

void ToolbarCustomizeDialog::onMoveUp()
{
    int row = m_currentList->currentRow();
    if (row <= 0)
        return;

    m_order.swapItemsAt(row, row - 1);
    populateLists();
    m_currentList->setCurrentRow(row - 1);
}

void ToolbarCustomizeDialog::onMoveDown()
{
    int row = m_currentList->currentRow();
    if (row < 0 || row >= m_order.size() - 1)
        return;

    m_order.swapItemsAt(row, row + 1);
    populateLists();
    m_currentList->setCurrentRow(row + 1);
}

void ToolbarCustomizeDialog::onReset()
{
    m_order.clear();
    for (int id : kDefaultToolbarOrder)
        m_order.append(static_cast<ToolbarButtonId>(id));
    populateLists();
}

} // namespace eMule
