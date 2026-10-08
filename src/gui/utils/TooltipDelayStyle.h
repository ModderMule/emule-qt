#pragma once

/// @file TooltipDelayStyle.h
/// @brief Application style that applies the "Tooltip delay" option.

#include "prefs/Preferences.h"

#include <QProxyStyle>

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
};

} // namespace eMule
