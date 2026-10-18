#include "utils/ColumnTooltip.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QToolTip>

namespace eMule {

QString columnTooltipHtml(const QString& text)
{
    if (!text.contains(u'\t'))
        return text;

    // white-space:pre keeps each row on one line, like the MFC control
    static const QString cell = QStringLiteral("<td style=\"white-space:pre\">%1</td>");
    QString html = QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\">");
    for (const QString& line : text.split(u'\n')) {
        html += QStringLiteral("<tr>");
        if (const qsizetype tab = line.indexOf(u'\t'); tab >= 0) {
            // MFC iMiddleMargin = 6 px between the columns
            html += QStringLiteral("<td style=\"white-space:pre; padding-right:6px\">%1</td>")
                        .arg(line.left(tab).toHtmlEscaped());
            html += cell.arg(line.mid(tab + 1).trimmed().toHtmlEscaped());
        } else {
            html += QStringLiteral("<td colspan=\"2\" style=\"white-space:pre\">%1</td>")
                        .arg(line.isEmpty() ? QStringLiteral("&nbsp;") : line.toHtmlEscaped());
        }
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table>");
    return html;
}

bool ColumnTooltipFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::ToolTip)
        return false;
    auto* view = qobject_cast<QAbstractItemView*>(watched->parent());
    if (!view || view->viewport() != watched)
        return false;

    const auto* help = static_cast<QHelpEvent*>(event);
    const QModelIndex index = view->indexAt(help->pos());
    if (!index.isValid())
        return false;
    const QString tip = index.data(Qt::ToolTipRole).toString();
    if (!tip.contains(u'\t'))
        return false;   // let the view's delegate show it as usual

    QToolTip::showText(help->globalPos(), columnTooltipHtml(tip), view->viewport(),
                       view->visualRect(index));
    return true;
}

} // namespace eMule
