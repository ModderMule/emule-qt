#include "pch.h"
#include "controls/OptionsTree.h"

#include <QFontMetrics>
#include <QHeaderView>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QStyledItemDelegate>

#include <algorithm>

namespace eMule {

namespace {

/// A QSpinBox in place of a number row, right of its label.
class SpinDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const override
    {
        if (index.data(OptionsTree::KindRole).toInt() != OptionsTree::Spin)
            return nullptr;
        auto* spin = new QSpinBox(parent);
        spin->setRange(index.data(OptionsTree::MinRole).toInt(), index.data(OptionsTree::MaxRole).toInt());
        spin->setSpecialValueText(index.data(OptionsTree::SpecialRole).toString());
        const QString suffix = index.data(OptionsTree::SuffixRole).toString();
        if (!suffix.isEmpty())
            spin->setSuffix(QLatin1Char(' ') + suffix);
        return spin;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        if (auto* spin = qobject_cast<QSpinBox*>(editor))
            spin->setValue(index.data(OptionsTree::ValueRole).toInt());
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override
    {
        if (auto* spin = qobject_cast<QSpinBox*>(editor)) {
            spin->interpretText();
            model->setData(index, spin->value(), OptionsTree::ValueRole);
        }
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        // after "Label: ", at least wide enough for the range
        const QFontMetrics fm(option.font);
        const int iconW = option.decorationSize.width() + 6;
        const int labelW = fm.horizontalAdvance(index.data(OptionsTree::LabelRole).toString() + QStringLiteral(": "));
        QRect r = option.rect;
        r.setLeft(std::min(r.right() - 60, r.left() + iconW + labelW));
        r.setWidth(std::max(editor->sizeHint().width(), 90));
        editor->setGeometry(r);
    }
};

} // namespace

OptionsTree::OptionsTree(QWidget* parent)
    : QTreeWidget(parent)
{
    setHeaderHidden(true);
    setRootIsDecorated(true);
    setColumnCount(1);
    setIndentation(20);
    setUniformRowHeights(true);
    setItemDelegate(new SpinDelegate(this));
    setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int) {
        if (m_quiet)
            return;
        const int kind = item->data(0, KindRole).toInt();
        if (kind == Spin) {
            // the delegate wrote ValueRole: clamp, show it again
            const int v = std::clamp(item->data(0, ValueRole).toInt(), item->data(0, MinRole).toInt(), item->data(0, MaxRole).toInt());
            m_quiet = true;
            item->setData(0, ValueRole, v);
            showValue(item);
            m_quiet = false;
            emit changed();
        } else if (kind == Check) {
            emit changed();
        }
    });
}

QTreeWidgetItem* OptionsTree::addGroup(const QString& text, const QIcon& icon)
{
    m_quiet = true;
    auto* item = new QTreeWidgetItem(this, {text});
    item->setIcon(0, icon);
    item->setData(0, KindRole, Group);
    item->setFlags(Qt::ItemIsEnabled);
    item->setExpanded(true);
    m_quiet = false;
    return item;
}

QTreeWidgetItem* OptionsTree::addSpin(QTreeWidgetItem* group, const QString& label, int min, int max, const QString& special,
                                      const QString& suffix)
{
    m_quiet = true;
    auto* item = new QTreeWidgetItem(group);
    item->setIcon(0, editIcon());
    item->setData(0, KindRole, Spin);
    item->setData(0, LabelRole, label);
    item->setData(0, MinRole, min);
    item->setData(0, MaxRole, max);
    item->setData(0, SpecialRole, special);
    item->setData(0, SuffixRole, suffix);
    item->setData(0, ValueRole, min);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    showValue(item);
    m_quiet = false;
    return item;
}

QTreeWidgetItem* OptionsTree::addCheck(QTreeWidgetItem* group, const QString& label)
{
    m_quiet = true;
    auto* item = new QTreeWidgetItem(group, {label});
    item->setData(0, KindRole, Check);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    item->setCheckState(0, Qt::Unchecked);
    m_quiet = false;
    return item;
}

int OptionsTree::value(const QTreeWidgetItem* item) const
{
    return item ? item->data(0, ValueRole).toInt() : 0;
}

void OptionsTree::setValue(QTreeWidgetItem* item, int value)
{
    if (!item)
        return;
    m_quiet = true;
    item->setData(0, ValueRole, std::clamp(value, item->data(0, MinRole).toInt(), item->data(0, MaxRole).toInt()));
    showValue(item);
    m_quiet = false;
}

bool OptionsTree::isChecked(const QTreeWidgetItem* item) const
{
    return item && item->checkState(0) == Qt::Checked;
}

void OptionsTree::setChecked(QTreeWidgetItem* item, bool on)
{
    if (!item)
        return;
    m_quiet = true;
    item->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
    m_quiet = false;
}

QString OptionsTree::valueText(const QTreeWidgetItem* item, int value)
{
    const QString special = item->data(0, SpecialRole).toString();
    if (!special.isEmpty() && value == item->data(0, MinRole).toInt())
        return special;
    const QString suffix = item->data(0, SuffixRole).toString();
    return suffix.isEmpty() ? QString::number(value) : QStringLiteral("%1 %2").arg(value).arg(suffix);
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void OptionsTree::showValue(QTreeWidgetItem* item)
{
    item->setText(0, QStringLiteral("%1: %2").arg(item->data(0, LabelRole).toString(), valueText(item, value(item))));
}

QIcon OptionsTree::editIcon() const
{
    // a framed "I", in the text colour of the theme
    const int s = 16;
    QPixmap pm(s * 2, s * 2);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(palette().color(QPalette::Text));
    p.drawRect(1, 1, s - 3, s - 3);
    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);
    p.drawText(QRect(1, 1, s - 2, s - 2), Qt::AlignCenter, QStringLiteral("I"));
    return QIcon(pm);
}

} // namespace eMule
