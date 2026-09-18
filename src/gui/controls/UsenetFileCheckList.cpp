#include "pch.h"
/// @file UsenetFileCheckList.cpp
/// @brief Which files of a release download: checkboxes on a file table.

#include "controls/UsenetFileCheckList.h"

#include "app/IpcClient.h"
#include "utils/StatusBarNotifier.h"

#include <QCborArray>
#include <QHBoxLayout>
#include <QPointer>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QTreeWidget>

namespace eMule {

namespace {

/// Clear of Qt::UserRole + 1 and SortRole, which the details dialog spends.
constexpr int kIndexRole = Qt::UserRole + 0x200;
constexpr int kLockedRole = Qt::UserRole + 0x201;
constexpr int kSetKeyRole = Qt::UserRole + 0x202;

/// The state this class last wrote or saw. itemChanged() fires for any data
/// change on the row, and only a real change of this one is a click.
constexpr int kCheckedRole = Qt::UserRole + 0x203;

} // namespace

UsenetFileCheckList::UsenetFileCheckList(QTreeWidget* tree, int column)
    : QObject(tree)
    , m_tree(tree)
    , m_column(column)
{
    connect(m_tree, &QTreeWidget::itemChanged, this, &UsenetFileCheckList::onItemChanged);
}

QWidget* UsenetFileCheckList::createButtonRow(QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    const auto add = [this, row, layout](const QString& text, Bulk mode) {
        auto* button = new QPushButton(text, row);
        connect(button, &QPushButton::clicked, this, [this, mode] { applyBulk(mode); });
        button->setEnabled(m_editable);
        layout->addWidget(button);
        m_buttons.append(button);
    };
    add(tr("Select All"), Bulk::All);
    add(tr("Select None"), Bulk::None);
    add(tr("Invert Selection"), Bulk::Invert);
    layout->addStretch(1);
    return row;
}

void UsenetFileCheckList::bindRow(QTreeWidgetItem* item, int fileIndex, bool checked,
                                  bool locked, const QString& setKey)
{
    const QScopedValueRollback guard(m_updating, true);
    item->setData(m_column, kIndexRole, fileIndex);
    item->setData(m_column, kLockedRole, locked);
    item->setData(m_column, kSetKeyRole, setKey);
    item->setData(m_column, kCheckedRole, checked);
    applyFlags(item);
    item->setCheckState(m_column, checked ? Qt::Checked : Qt::Unchecked);
}

void UsenetFileCheckList::setEditable(bool editable)
{
    if (editable == m_editable)
        return;
    m_editable = editable;

    const QScopedValueRollback guard(m_updating, true);
    for (QTreeWidgetItem* parent : groups()) {
        for (QTreeWidgetItem* item : fileRows(parent))
            applyFlags(item);
    }
    for (QPushButton* button : std::as_const(m_buttons))
        button->setEnabled(editable);
}

QList<int> UsenetFileCheckList::uncheckedIndices(QTreeWidgetItem* parent) const
{
    QList<int> out;
    for (QTreeWidgetItem* item : fileRows(parent)) {
        if (item->checkState(m_column) != Qt::Checked)
            out.append(item->data(m_column, kIndexRole).toInt());
    }
    return out;
}

bool UsenetFileCheckList::anyUnlockedChecked(QTreeWidgetItem* parent) const
{
    const QList<QTreeWidgetItem*> rows = fileRows(parent);
    return std::ranges::any_of(rows, [this](QTreeWidgetItem* item) {
        return !item->data(m_column, kLockedRole).toBool()
               && item->checkState(m_column) == Qt::Checked;
    });
}

void UsenetFileCheckList::sendSkip(IpcClient* ipc, QObject* context, const QString& itemId,
                                   const QList<int>& fileIndices, bool skipped)
{
    if (!ipc || !ipc->isConnected() || fileIndices.isEmpty())
        return;

    QCborArray indices;
    for (const int f : fileIndices)
        indices.append(f);

    Ipc::IpcMessage msg(Ipc::IpcMsgType::SetUsenetFilesSkipped);
    msg.append(itemId);
    msg.append(QCborValue(indices));
    msg.append(skipped);

    const QPointer<QObject> guard(context);
    ipc->sendRequest(std::move(msg), [guard](const Ipc::IpcMessage& reply) {
        // A dropped connection is no refusal; the next poll shows what stands.
        if (!guard || !reply.isValid() || reply.fieldBool(0))
            return;
        const QString why = reply.fieldString(1);
        StatusBarNotifier::post(why.isEmpty()
                                    ? UsenetFileCheckList::tr("Could not change which files download.")
                                    : why);
    });
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void UsenetFileCheckList::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_updating || column != m_column || !item->data(m_column, kIndexRole).isValid())
        return;

    const bool checked = item->checkState(m_column) == Qt::Checked;
    if (checked == item->data(m_column, kCheckedRole).toBool())
        return;   // some other data on the row changed

    QList<int> changed;
    {
        const QScopedValueRollback guard(m_updating, true);
        if (item->data(m_column, kLockedRole).toBool()) {
            item->setCheckState(m_column, checked ? Qt::Unchecked : Qt::Checked);
            return;
        }
        item->setData(m_column, kCheckedRole, checked);
        changed.append(item->data(m_column, kIndexRole).toInt());

        // One volume short fails the whole unpack, so a set moves as one. The
        // daemon widens the request anyway; this only keeps the table honest.
        const QString setKey = item->data(m_column, kSetKeyRole).toString();
        if (!setKey.isEmpty()) {
            for (QTreeWidgetItem* other : fileRows(item->parent())) {
                if (other == item || other->data(m_column, kSetKeyRole).toString() != setKey
                    || (other->checkState(m_column) == Qt::Checked) == checked) {
                    continue;
                }
                setChecked(other, checked);
                changed.append(other->data(m_column, kIndexRole).toInt());
            }
        }
    }
    emit skipChanged(changed, !checked);
}

void UsenetFileCheckList::applyBulk(Bulk mode)
{
    if (!m_editable)
        return;

    for (QTreeWidgetItem* parent : groups()) {
        QList<int> toFetch;
        QList<int> toSkip;
        {
            const QScopedValueRollback guard(m_updating, true);
            for (QTreeWidgetItem* item : fileRows(parent)) {
                if (item->data(m_column, kLockedRole).toBool())
                    continue;
                const bool checked = item->checkState(m_column) == Qt::Checked;
                const bool want = mode == Bulk::All    ? true
                                  : mode == Bulk::None ? false
                                                       : !checked;
                if (want == checked)
                    continue;
                setChecked(item, want);
                (want ? toFetch : toSkip).append(item->data(m_column, kIndexRole).toInt());
            }
        }
        if (!toFetch.isEmpty())
            emit skipChanged(toFetch, false);
        if (!toSkip.isEmpty())
            emit skipChanged(toSkip, true);
    }
}

QList<QTreeWidgetItem*> UsenetFileCheckList::fileRows(QTreeWidgetItem* parent) const
{
    QList<QTreeWidgetItem*> rows;
    const int count = parent ? parent->childCount() : m_tree->topLevelItemCount();
    for (int i = 0; i < count; ++i) {
        QTreeWidgetItem* item = parent ? parent->child(i) : m_tree->topLevelItem(i);
        if (item->data(m_column, kIndexRole).isValid())
            rows.append(item);
    }
    return rows;
}

QList<QTreeWidgetItem*> UsenetFileCheckList::groups() const
{
    QList<QTreeWidgetItem*> out{nullptr};
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (m_tree->topLevelItem(i)->childCount() > 0)
            out.append(m_tree->topLevelItem(i));
    }
    return out;
}

void UsenetFileCheckList::setChecked(QTreeWidgetItem* item, bool checked)
{
    item->setData(m_column, kCheckedRole, checked);
    item->setCheckState(m_column, checked ? Qt::Checked : Qt::Unchecked);
}

void UsenetFileCheckList::applyFlags(QTreeWidgetItem* item)
{
    Qt::ItemFlags flags = item->flags();
    if (m_editable && !item->data(m_column, kLockedRole).toBool())
        flags |= Qt::ItemIsUserCheckable;
    else
        flags &= ~Qt::ItemIsUserCheckable;
    item->setFlags(flags);
}

} // namespace eMule
