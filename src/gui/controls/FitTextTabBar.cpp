#include "controls/FitTextTabBar.h"

#include <QStyle>
#include <QStyleOptionTab>

#include <algorithm>

namespace eMule {

FitTextTabBar::FitTextTabBar(QWidget* parent)
    : QTabBar(parent)
{
    setElideMode(Qt::ElideNone);
    setUsesScrollButtons(true); // macOS style defaults to no arrows
    setExpanding(false);
}

void FitTextTabBar::setMinimumTabWidth(int width)
{
    m_minTabWidth = width;
    updateGeometry();
}

QSize FitTextTabBar::tabSizeHint(int index) const
{
    QSize hint = QTabBar::tabSizeHint(index);
    hint.setWidth(std::max(hint.width(), m_minTabWidth));

    // Ask the style how much text room a tab of this size really gets
    QStyleOptionTab opt;
    initStyleOption(&opt, index);
    opt.rect = QRect(QPoint(), hint);
    const int textRoom = style()->subElementRect(QStyle::SE_TabBarTabText, &opt, this).width();
    const int textWidth = fontMetrics().horizontalAdvance(tabText(index));
    if (textWidth > textRoom)
        hint.rwidth() += textWidth - textRoom;
    return hint;
}

void FitTextTabBar::tabLayoutChange()
{
    QTabBar::tabLayoutChange();
    // setTabVisible() on a hidden bar skips updateGeometry(); the layout then
    // keeps the old, wider hint and macOS centers the tabs in it
    updateGeometry();
}

} // namespace eMule
