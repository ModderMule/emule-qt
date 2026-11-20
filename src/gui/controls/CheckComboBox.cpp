#include "pch.h"

/// @file CheckComboBox.cpp
/// @brief Multi-select drop-down — see CheckComboBox.h.

#include "controls/CheckComboBox.h"

#include <QAbstractItemView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QStyleOptionComboBox>
#include <QStylePainter>

namespace eMule {

CheckComboBox::CheckComboBox(QWidget* parent)
    : QComboBox(parent)
    , m_model(new QStandardItemModel(this))
    , m_emptyText(tr("None"))
{
    setModel(m_model);
    // the default combo menu delegate draws no check boxes (macOS, Fusion)
    setItemDelegate(new QStyledItemDelegate(this));
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(16);
    // view() builds the popup container, which filters the viewport too: ours goes in
    // after it and so runs first, eating the release that would close the list
    view()->viewport()->installEventFilter(this);
    view()->installEventFilter(this);
}

void CheckComboBox::addCheckItem(const QString& text, const QString& value, const QString& toolTip)
{
    auto* item = new QStandardItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    item->setData(Qt::Unchecked, Qt::CheckStateRole);
    item->setData(value.isEmpty() ? text : value, Qt::UserRole);
    if (!toolTip.isEmpty())
        item->setToolTip(toolTip);
    m_model->insertRow(itemCount(), item);   // before the action row
    update();
}

void CheckComboBox::setActionItem(const QString& text)
{
    if (m_hasAction) {
        m_model->item(itemCount())->setText(text);
        return;
    }
    auto* item = new QStandardItem(text);
    item->setFlags(Qt::ItemIsEnabled);
    QFont f = item->font();
    f.setItalic(true);
    item->setFont(f);
    m_model->appendRow(item);
    m_hasAction = true;
}

void CheckComboBox::clearCheckItems()
{
    const bool hadChecked = !checkedValues().isEmpty();
    m_model->removeRows(0, itemCount());
    update();
    if (hadChecked)
        emit checkedChanged();
}

QStringList CheckComboBox::checkedValues() const
{
    QStringList out;
    for (int r = 0; r < itemCount(); ++r)
        if (const QStandardItem* it = m_model->item(r); it->checkState() == Qt::Checked)
            out.append(it->data(Qt::UserRole).toString());
    return out;
}

void CheckComboBox::setCheckedValues(const QStringList& values)
{
    bool changed = false;
    for (int r = 0; r < itemCount(); ++r) {
        QStandardItem* it = m_model->item(r);
        const Qt::CheckState want = values.contains(it->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked;
        if (it->checkState() != want) {
            it->setCheckState(want);
            changed = true;
        }
    }
    update();
    if (changed)
        emit checkedChanged();
}

void CheckComboBox::setEmptyText(const QString& text)
{
    m_emptyText = text;
    update();
}

QStringList CheckComboBox::values() const
{
    QStringList out;
    for (int r = 0; r < itemCount(); ++r)
        out.append(m_model->item(r)->data(Qt::UserRole).toString());
    return out;
}

void CheckComboBox::showPopup()
{
    // list as wide as its longest entry, not the closed box
    view()->setMinimumWidth(view()->sizeHintForColumn(0) + 2 * view()->frameWidth() + 24);
    QComboBox::showPopup();
}

bool CheckComboBox::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == view()->viewport() && event->type() == QEvent::MouseButtonRelease) {
        const auto* me = static_cast<QMouseEvent*>(event);
        if (const QModelIndex idx = view()->indexAt(me->position().toPoint()); idx.isValid())
            toggleRow(idx.row());
        return true;
    }
    if (watched == view() && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (const QModelIndex idx = view()->currentIndex(); idx.isValid())
                toggleRow(idx.row());
            return true;
        }
        if (m_removable && (key == Qt::Key_Delete || key == Qt::Key_Backspace)) {
            if (const QModelIndex idx = view()->currentIndex(); idx.isValid() && !isActionRow(idx.row()))
                removeRow(idx.row());
            return true;
        }
    }
    return QComboBox::eventFilter(watched, event);
}

void CheckComboBox::paintEvent(QPaintEvent* /*event*/)
{
    QStylePainter painter(this);
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    opt.currentText = summary();
    opt.currentIcon = {};
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);
    painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void CheckComboBox::toggleRow(int row)
{
    if (isActionRow(row)) {
        emit actionTriggered();
        return;
    }
    QStandardItem* it = m_model->item(row);
    if (!it)
        return;
    it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    update();
    emit checkedChanged();
}

void CheckComboBox::removeRow(int row)
{
    QStandardItem* it = m_model->item(row);
    if (!it)
        return;
    const QString value = it->data(Qt::UserRole).toString();
    const bool wasChecked = it->checkState() == Qt::Checked;
    m_model->removeRow(row);
    if (itemCount() > 0)
        view()->setCurrentIndex(m_model->index(std::min(row, itemCount() - 1), 0));
    else if (m_hasAction)
        view()->setCurrentIndex(m_model->index(0, 0));
    else
        hidePopup();
    update();
    emit itemRemoved(value);
    if (wasChecked)
        emit checkedChanged();
}

QString CheckComboBox::summary() const
{
    const qsizetype n = checkedValues().size();
    if (n == 0)
        return m_emptyText;
    return tr("%1 of %2 selected").arg(n).arg(itemCount());
}

int CheckComboBox::itemCount() const
{
    return m_model->rowCount() - (m_hasAction ? 1 : 0);
}

} // namespace eMule
