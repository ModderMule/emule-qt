#pragma once

/// @file SortArrowStyle.h
/// @brief Header style that draws MFC's double sort arrow.
///
/// A list column with two sort values (AbstractListView::setSortValueColumn) shows a
/// double arrow while its second value is the sort key. Qt has one arrow; this draws it
/// twice, side by side, when the header carries the property.

#include "utils/TooltipDelayStyle.h"

#include <QStyleOption>
#include <QWidget>

namespace eMule {

class SortArrowStyle : public TooltipDelayStyle {
public:
    static constexpr const char* kDoubleArrowProperty = "emuleDoubleSortArrow";

    /// Wraps the application style; @p parent owns it.
    explicit SortArrowStyle(QObject* parent)
    {
        setParent(parent);
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget) const override
    {
        if (element != PE_IndicatorHeaderArrow || !widget
            || !widget->property(kDoubleArrowProperty).toBool()) {
            TooltipDelayStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        // Two arrows half an arrow apart, centred on where the one would be
        const int shift = qMax(3, option->rect.width() / 3);
        for (const int dx : {-shift, shift}) {
            QStyleOptionHeader shifted;
            if (const auto* header = qstyleoption_cast<const QStyleOptionHeader*>(option))
                shifted = *header;
            else
                shifted.QStyleOption::operator=(*option);
            shifted.rect.translate(dx, 0);
            TooltipDelayStyle::drawPrimitive(element, &shifted, painter, widget);
        }
    }
};

} // namespace eMule
