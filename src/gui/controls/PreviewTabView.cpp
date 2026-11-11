#include "pch.h"

/// @file PreviewTabView.cpp
/// @brief Note-or-content page of on-demand Details tabs — see PreviewTabView.h.

#include "controls/PreviewTabView.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace eMule {

PreviewTabView::PreviewTabView(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);

    m_message = new QLabel(m_stack);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_stack->addWidget(m_message);
}

void PreviewTabView::showMessage(const QString& text)
{
    m_message->setText(text);
    m_stack->setCurrentWidget(m_message);
}

QString PreviewTabView::message() const
{
    return m_stack->currentWidget() == m_message ? m_message->text() : QString();
}

void PreviewTabView::setContentPage(QWidget* page)
{
    page->setParent(m_stack);
    m_stack->addWidget(page);
}

void PreviewTabView::showContent()
{
    if (m_stack->count() > 1)
        m_stack->setCurrentIndex(1);
}

} // namespace eMule
