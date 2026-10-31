#pragma once

/// @file TooltipDelayStyle.h
/// @brief Application style that applies the "Tooltip delay" and "Font size" options.

#include "prefs/Preferences.h"
#include "utils/Appearance.h"
#include "utils/ColorUtils.h"

#include <QAbstractItemView>
#include <QPalette>
#include <QPointer>
#include <QProxyStyle>
#include <QStyleFactory>
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

    using QProxyStyle::polish;

    // windows11 takes the selection colour from the accent, which can be near-black
    // (issue #62). MFC lists use the classic system colour: same here.
    void polish(QPalette& pal) override
    {
        QProxyStyle::polish(pal);
        if (!isWindows11())
            return;
        if (const QColor sel = systemHighlightColor(); sel.isValid()) {
            for (const auto group : {QPalette::Active, QPalette::Inactive})
                pal.setColor(group, QPalette::Highlight, sel);
        }
    }

    // windows11 fills selected/hovered rows of alternating lists with Highlight but
    // keeps Text: black on blue, or on a dark accent unreadable. Use a readable colour.
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget = nullptr) const override
    {
        if (element == CE_ItemViewItem && isWindows11()) {
            const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option);
            const auto* view = qobject_cast<const QAbstractItemView*>(widget);
            if (vopt && view && view->alternatingRowColors()
                && vopt->state.testAnyFlags(State_Selected | State_MouseOver)) {
                QStyleOptionViewItem copy(*vopt);
                for (const auto group : {QPalette::Active, QPalette::Inactive}) {
                    copy.palette.setColor(group, QPalette::Text,
                        readableTextOn(copy.palette.color(group, QPalette::Highlight),
                                       copy.palette.color(group, QPalette::HighlightedText)));
                }
                QProxyStyle::drawControl(element, &copy, painter, widget);
                return;
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

    // macOS draws an item check as a bare tick: an unchecked row shows nothing at all.
    // Draw a box there, Fusion's; a row the user can't toggle gets it greyed.
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget = nullptr) const override
    {
        if (element == PE_IndicatorItemViewItemCheck && isMacStyle()) {
            if (!m_checkStyle) {
                m_checkStyle = QStyleFactory::create(QStringLiteral("Fusion"));
                if (m_checkStyle)
                    m_checkStyle->setParent(const_cast<TooltipDelayStyle*>(this));
            }
            if (m_checkStyle) {
                QStyleOptionViewItem box;
                if (const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option))
                    box = *vopt;
                else
                    box.QStyleOption::operator=(*option);
                if (box.index.isValid() && !(box.index.flags() & Qt::ItemIsUserCheckable)) {
                    box.state &= ~State_Enabled;
                    box.palette.setBrush(QPalette::All, QPalette::Text,
                                         box.palette.brush(QPalette::Disabled, QPalette::Text));
                    box.palette.setBrush(QPalette::All, QPalette::Base,
                                         box.palette.brush(QPalette::Window));
                }
                m_checkStyle->drawPrimitive(element, &box, painter, widget);
                return;
            }
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

private:
    [[nodiscard]] bool isWindows11() const
    {
        const QStyle* base = baseStyle();
        return base && base->name().compare(QLatin1StringView("windows11"),
                                            Qt::CaseInsensitive) == 0;
    }

    [[nodiscard]] bool isMacStyle() const
    {
        const QStyle* base = baseStyle();
        return base && base->name().compare(QLatin1StringView("macos"),
                                            Qt::CaseInsensitive) == 0;
    }

    /// Draws item check boxes under the macOS style; owned by this.
    mutable QPointer<QStyle> m_checkStyle;
};

} // namespace eMule
