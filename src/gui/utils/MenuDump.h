#pragma once

/// @file MenuDump.h
/// @brief Dev aid (EMULE_DUMP_ED3K_MENUS): context menus as log lines, no clicking.
///
/// 1: the panels log the menu of their ed3k rows every 20 s; 2: Transfers also acts
/// (Swap A4AF, Source Limit through its own dialog) once.

#include <QAction>
#include <QMenu>
#include <QStringList>

namespace eMule::MenuDump {

[[nodiscard]] inline int level()
{
    return qEnvironmentVariableIntValue("EMULE_DUMP_ED3K_MENUS");
}

/// "Item | (greyed) | Sub {a <data> | b}"; an action's data (e.g. a web service URL) in <>
[[nodiscard]] inline QString text(const QMenu* menu)
{
    QStringList items;
    for (const QAction* a : menu->actions()) {
        if (a->isSeparator() || !a->isVisible())
            continue;
        QString t = a->text();
        if (a->menu())
            t += QStringLiteral(" {%1}").arg(text(a->menu()));
        else if (a->data().isValid())
            t += QStringLiteral(" <%1>").arg(a->data().toString());
        const bool on = a->isEnabled() && (!a->menu() || a->menu()->isEnabled());
        items << (on ? t : QStringLiteral("(%1)").arg(t));
    }
    return items.join(QStringLiteral(" | "));
}

/// The action titled @p title in @p menu or its submenus
[[nodiscard]] inline QAction* find(const QMenu* menu, const QString& title)
{
    for (QAction* a : menu->actions()) {
        if (a->text() == title)
            return a;
        if (a->menu())
            if (QAction* sub = find(a->menu(), title))
                return sub;
    }
    return nullptr;
}

} // namespace eMule::MenuDump
