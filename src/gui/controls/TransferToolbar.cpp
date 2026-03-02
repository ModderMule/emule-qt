/// @file TransferToolbar.cpp
/// @brief Section header toolbar widget — implementation.

#include "controls/TransferToolbar.h"

#include <QButtonGroup>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QToolButton>

namespace eMule {

TransferToolbar::TransferToolbar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(22);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 0, 0, 0);
    m_layout->setSpacing(1);

    // Leading icon (shown before the label, e.g. current view icon)
    m_iconLabel = new QLabel;
    m_iconLabel->setFixedSize(16, 16);
    m_iconLabel->setScaledContents(true);
    m_iconLabel->hide(); // hidden until setLeadingIcon() is called
    m_layout->addWidget(m_iconLabel);

    m_label = new QLabel;
    QFont bold = m_label->font();
    bold.setBold(true);
    m_label->setFont(bold);
    m_layout->addWidget(m_label);

    // Trailing stretch — buttons are inserted before this via addButton()
    m_layout->addStretch(1);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    connect(m_group, &QButtonGroup::idClicked, this, &TransferToolbar::buttonClicked);
}

int TransferToolbar::addButton(const QIcon& icon, const QString& tooltip)
{
    auto* btn = new QToolButton;
    btn->setIcon(icon);
    btn->setToolTip(tooltip);
    btn->setCheckable(true);
    btn->setAutoRaise(true);
    btn->setFixedSize(20, 20);
    btn->setIconSize(QSize(16, 16));

    const int id = m_nextId++;
    m_group->addButton(btn, id);
    // Insert before the trailing stretch so buttons stay left-aligned
    m_layout->insertWidget(m_layout->count() - 1, btn);
    return id;
}

void TransferToolbar::checkButton(int id)
{
    if (auto* btn = m_group->button(id))
        btn->setChecked(true);
}

void TransferToolbar::setLabelText(const QString& text)
{
    m_label->setText(text);
}

void TransferToolbar::setLeadingIcon(const QIcon& icon)
{
    if (icon.isNull()) {
        m_iconLabel->hide();
    } else {
        m_iconLabel->setPixmap(icon.pixmap(16, 16));
        m_iconLabel->show();
    }
}

} // namespace eMule
