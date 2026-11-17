#include "pch.h"
#include "utils/PanelLayout.h"

#include <QFrame>
#include <QScrollArea>
#include <QSplitter>
#include <QStyle>

namespace eMule::PanelLayout {

namespace {
// title/tab row + header + ~3 list rows
constexpr int kPaneMinTextLines = 6;
} // namespace

QScrollArea* scrollableColumn(QWidget* column)
{
    auto* scroll = new QScrollArea;
    scroll->setWidget(column);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(false);
    const int barWidth = scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    scroll->setMinimumWidth(column->minimumWidth() + barWidth);
    if (column->maximumWidth() < QWIDGETSIZE_MAX - barWidth)
        scroll->setMaximumWidth(column->maximumWidth() + barWidth);
    return scroll;
}

void setPaneMinHeights(QSplitter* splitter)
{
    const int minHeight = splitter->fontMetrics().lineSpacing() * kPaneMinTextLines;
    for (int i = 0; i < splitter->count(); ++i)
        splitter->widget(i)->setMinimumHeight(minHeight);
}

} // namespace eMule::PanelLayout
