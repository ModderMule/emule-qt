#include "pch.h"
/// @file StatusFilterBar.cpp
/// @brief Row of state filter buttons — implementation.

#include "controls/StatusFilterBar.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QToolButton>

#include <algorithm>

namespace eMule {

StatusFilterBar::StatusFilterBar(const QStringList& names, int current, QWidget* parent)
    : QWidget(parent)
    , m_names(names)
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(2);
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    for (int f = 0; f < m_names.size(); ++f) {
        auto* b = new QToolButton(this);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setText(m_names.at(f));
        m_group->addButton(b, f);
        row->addWidget(b);
    }
    row->addStretch(1);
    if (!m_names.isEmpty())
        if (auto* b = m_group->button(std::clamp(current, 0, int(m_names.size()) - 1)))
            b->setChecked(true);
    connect(m_group, &QButtonGroup::idClicked, this, &StatusFilterBar::filterChanged);
}

int StatusFilterBar::current() const
{
    return std::max(m_group->checkedId(), 0);
}

void StatusFilterBar::setRowOn(bool on)
{
    if (m_on == on)
        return;
    const int before = activeFilter();
    m_on = on;
    setVisible(on);
    if (activeFilter() != before)
        emit filterChanged(activeFilter());
}

void StatusFilterBar::setCounts(std::span<const int> counts)
{
    for (int f = 0; f < m_names.size(); ++f) {
        auto* b = m_group->button(f);
        if (!b)
            continue;
        const QString text = std::size_t(f) < counts.size()
            ? QStringLiteral("%1 (%2)").arg(m_names.at(f)).arg(counts[std::size_t(f)])
            : m_names.at(f);
        if (b->text() != text)
            b->setText(text);
    }
}

} // namespace eMule
