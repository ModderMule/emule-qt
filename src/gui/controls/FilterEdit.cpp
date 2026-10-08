#include "pch.h"
/// @file FilterEdit.cpp
/// @brief List filter box — port of MFC CEditDelayed.

#include "controls/FilterEdit.h"

#include <QAction>
#include <QHeaderView>
#include <QMenu>
#include <QTimer>

namespace eMule {

FilterEdit::FilterEdit(QWidget* parent)
    : QLineEdit(parent)
{
    setClearButtonEnabled(true);

    auto* columnAction = addAction(QIcon(QStringLiteral(":/icons/SearchEdit.ico")),
                                   QLineEdit::LeadingPosition);
    connect(columnAction, &QAction::triggered, this, &FilterEdit::showColumnMenu);

    // MFC evaluates 400 ms after the last change (EditDelayed.cpp:81-97)
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(400);
    connect(m_timer, &QTimer::timeout, this, &FilterEdit::evaluate);
    connect(this, &QLineEdit::textChanged, this, [this] {
        if (hasFocus())
            m_timer->start();
        else
            evaluate();   // set from outside, or cleared by the button
    });
}

void FilterEdit::setHeader(QHeaderView* header)
{
    m_header = header;
    if (m_header && (m_column >= m_header->count() || m_header->isSectionHidden(m_column)))
        m_column = 0;
    updateHint();
}

QStringList FilterEdit::tokens(const QString& text)
{
    QStringList out = text.split(u' ', Qt::SkipEmptyParts);
    out.removeAll(QStringLiteral("-"));
    return out;
}

bool FilterEdit::matches(const QStringList& tokens, const QString& target)
{
    for (const QString& token : tokens) {
        const bool wanted = !token.startsWith(u'-');
        const bool found = target.contains(wanted ? token : token.mid(1), Qt::CaseInsensitive);
        if (wanted != found)
            return false;
    }
    return true;
}

void FilterEdit::focusOutEvent(QFocusEvent* event)
{
    if (m_timer->isActive()) {
        m_timer->stop();
        evaluate();
    }
    QLineEdit::focusOutEvent(event);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void FilterEdit::showColumnMenu()
{
    if (!m_header || !m_header->model())
        return;

    // The visible columns, in the order they are on screen (EditDelayed.cpp:211-235)
    QMenu menu(this);
    for (int visual = 0; visual < m_header->count(); ++visual) {
        const int column = m_header->logicalIndex(visual);
        if (m_header->isSectionHidden(column))
            continue;
        auto* act = menu.addAction(
            m_header->model()->headerData(column, Qt::Horizontal).toString());
        act->setCheckable(true);
        act->setChecked(column == m_column);
        connect(act, &QAction::triggered, this, [this, column] { setColumn(column); });
    }
    menu.exec(mapToGlobal(QPoint(0, height())));
}

void FilterEdit::setColumn(int column)
{
    if (column == m_column)
        return;
    m_column = column;
    updateHint();
    m_timer->stop();
    evaluate();
}

void FilterEdit::evaluate()
{
    const QStringList now = tokens(text());
    if (now == m_lastTokens && m_column == m_lastColumn)
        return;
    m_lastTokens = now;
    m_lastColumn = m_column;
    emit filterChanged(now, m_column);
}

void FilterEdit::updateHint()
{
    if (m_header && m_header->model())
        setPlaceholderText(m_header->model()->headerData(m_column, Qt::Horizontal).toString());
}

} // namespace eMule
