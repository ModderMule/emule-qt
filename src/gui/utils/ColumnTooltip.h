#pragma once

/// @file ColumnTooltip.h
/// @brief Two-column layout for "Label:\tvalue" list tooltips, as MFC CToolTipCtrlX draws them.

#include <QObject>
#include <QString>

namespace eMule {

/// MFC CToolTipCtrlX (srchybrid/ToolTipCtrlX.cpp:254-336) splits each line at its
/// first ':' and draws labels and values as two columns, values starting after the
/// widest label. A plain tab only reaches the next fixed tab stop, so a long label
/// ("Accepted Requests:") pushes its value out of line. Here the tab marks the split;
/// a line without one spans both columns. Text without any tab comes back unchanged.
[[nodiscard]] QString columnTooltipHtml(const QString& text);

/// App-wide filter: item-view tooltips with tab-separated lines are shown through
/// columnTooltipHtml(). Models keep returning plain text.
class ColumnTooltipFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
};

} // namespace eMule
