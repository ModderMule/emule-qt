#include "utils/ColumnTooltip.h"

#include <QAbstractItemView>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QToolTip>

#include <algorithm>

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
    if (event->type() == QEvent::KeyPress) {
        // Ctrl dismisses an open tooltip (#276). Qt hides only on non-modifier keys.
        // macOS: Key_Control is Cmd, Key_Meta the Ctrl key; take both
        const auto* key = static_cast<QKeyEvent*>(event);
        if (!key->isAutoRepeat() && QToolTip::isVisible()
            && (key->key() == Qt::Key_Control || key->key() == Qt::Key_Meta))
            QToolTip::hideText();
        return false;
    }
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

    // Qt times the tip by its text length, markup included: the table HTML would keep
    // a long tip up for a minute or more. Time it by the plain text, Qt's own formula
    const int displayMs = 10000 + 40 * static_cast<int>(std::max<qsizetype>(0, tip.size() - 100));
    QToolTip::showText(help->globalPos(), columnTooltipHtml(tip), view->viewport(),
                       view->visualRect(index), displayMs);
    return true;
}

} // namespace eMule
