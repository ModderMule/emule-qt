#include "pch.h"
/// @file ContentScrollArea.cpp
/// @brief Scroll area that reports its content's size — see ContentScrollArea.h.

#include "controls/ContentScrollArea.h"

#include "utils/DialogSizing.h"

#include <QLayout>
#include <QScrollBar>
#include <QWidget>

#include <algorithm>

namespace eMule {

ContentScrollArea::ContentScrollArea(QWidget* parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // Nothing of its own may be painted here: the area has to be invisible in the common
    // case where the dialog is tall enough and no scrollbar is ever shown.
    viewport()->setAutoFillBackground(false);

    DialogSizing::enableHeightForWidth(this);
}

QSize ContentScrollArea::sizeHint() const
{
    const QWidget* content = widget();
    if (!content)
        return QScrollArea::sizeHint();

    return content->sizeHint() + QSize(2 * frameWidth(), 2 * frameWidth());
}

int ContentScrollArea::heightForWidth(int width) const
{
    return contentHeightForWidth(width);
}

int ContentScrollArea::contentHeightForWidth(int areaWidth) const
{
    QWidget* content = widget();
    if (!content)
        return QScrollArea::heightForWidth(areaWidth);

    const int frame = 2 * frameWidth();
    int       inner = areaWidth - frame;

    // Measure as if the scrollbar were already there. It is otherwise self-fulfilling:
    // a dialog sized for the full width comes up one wrapped line short, the bar appears
    // to make up for it, and the bar's own width forces that extra line. A few pixels of
    // slack at the bottom is the better end of that trade.
    if (verticalScrollBarPolicy() != Qt::ScrollBarAlwaysOff)
        inner -= verticalScrollBar()->sizeHint().width();

    if (content->layout()) {
        int needed = DialogSizing::neededHeight(content->layout(), inner) + frame;

        // If the content is on screen and already scrolling, it has just been laid out at
        // this width and knows exactly how tall it turned out — believe that over any
        // hint. A wrapping label's hint is only right once it has been through a layout
        // at its final width, so this is what closes the gap on the second measurement.
        if (verticalScrollBar()->isVisible())
            needed = std::max(needed, content->height() + frame);

        return needed;
    }

    // -1 means the content does not track its width; its hint is then the best we have.
    const int hfw = content->heightForWidth(inner);
    return (hfw >= 0 ? hfw : content->sizeHint().height()) + frame;
}

} // namespace eMule
