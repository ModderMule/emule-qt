#include "controls/FitTextTabBar.h"

#include <QStyle>
#include <QStyleOptionTab>

namespace eMule {

FitTextTabBar::FitTextTabBar(QWidget* parent)
    : QTabBar(parent)
{
    setElideMode(Qt::ElideNone);
    setUsesScrollButtons(true); // macOS style defaults to no arrows
    setExpanding(false);
}

QSize FitTextTabBar::tabSizeHint(int index) const
{
    QSize hint = QTabBar::tabSizeHint(index);

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

} // namespace eMule
