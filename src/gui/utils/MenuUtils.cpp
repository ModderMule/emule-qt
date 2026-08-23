#include "pch.h"
/// @file MenuUtils.cpp
/// @brief Context-menu helpers — see MenuUtils.h.

#include "utils/MenuUtils.h"

#include <QAction>
#include <QFont>
#include <QMenu>

namespace eMule {

void setMenuDefaultAction(QMenu* menu, QAction* action)
{
    if (!menu)
        return;

    menu->setDefaultAction(action);
    if (!action)
        return;

    QFont font = action->font();
    font.setBold(true);
    action->setFont(font);
}

} // namespace eMule
