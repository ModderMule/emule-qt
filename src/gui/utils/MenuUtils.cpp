#include "pch.h"
/// @file MenuUtils.cpp
/// @brief Context-menu helpers — see MenuUtils.h.

#include "utils/MenuUtils.h"

#include "prefs/Preferences.h"

#include <QAction>
#include <QFont>
#include <QLatin1String>
#include <QMenu>
#include <QString>

namespace eMule {

QIcon menuIcon(const char* resource)
{
    if (!resource || !thePrefs.useOriginalIcons())
        return {};
    return QIcon(QStringLiteral(":/icons/") + QLatin1String(resource));
}

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
