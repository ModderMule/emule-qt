#pragma once

/// @file TooltipDelayStyle.h
/// @brief Application style that applies the "Tooltip delay" and "Font size" options.

#include "prefs/Preferences.h"
#include "utils/Appearance.h"

#include <QProxyStyle>
#include <QStyleOption>

#include <algorithm>

namespace eMule {

/// MFC sets the delay on its list tooltips (GetToolTipDelay, seconds, 0-32). Qt has
/// one wake-up delay for every tooltip, so the option applies to all of them. Read
/// at each query: a change in Options takes effect at once.
class TooltipDelayStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* returnData = nullptr) const override
    {
        if (hint == SH_ToolTip_WakeUpDelay)
            return thePrefs.tooltipDelay() * 1000;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    // macOS keeps native heights for headers and edit fields; a scaled-up font clips there
    QSize sizeFromContents(ContentsType type, const QStyleOption* option,
                           const QSize& contentsSize, const QWidget* widget = nullptr) const override
    {
        QSize size = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
        if (widget && fontScale() > 1.0) {
            switch (type) {
            case CT_HeaderSection:
            case CT_LineEdit:
            case CT_SpinBox:
            case CT_ComboBox:
                size.setHeight(std::max(size.height(), widget->fontMetrics().height() + 6));
                break;
            case CT_PushButton:
                if (const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option)) {
                    const int text = widget->fontMetrics().horizontalAdvance(button->text);
                    size.setWidth(std::max(size.width(), text + 2 * widget->fontMetrics().height()));
                }
                break;
            default:
                break;
            }
        }
        return size;
    }
};

} // namespace eMule
