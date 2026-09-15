#include "pch.h"
/// @file PriorityText.cpp
/// @brief Upload priority labels — implementation.

#include "utils/PriorityText.h"

#include <QCoreApplication>

namespace eMule {

QString uploadPriorityText(int prio, bool isAuto)
{
    // emule.rc IDS_PRIOVERYLOW, IDS_PRIO{LOW,NORMAL,HIGH}, IDS_PRIOAUTO*, IDS_PRIORELEASE
    switch (prio) {
    case 4:
        return QCoreApplication::translate("Priority", "Very Low");
    case 0:
        return isAuto ? QCoreApplication::translate("Priority", "Auto [Lo]")
                      : QCoreApplication::translate("Priority", "Low");
    case 2:
        return isAuto ? QCoreApplication::translate("Priority", "Auto [Hi]")
                      : QCoreApplication::translate("Priority", "High");
    case 3:
        return QCoreApplication::translate("Priority", "Release");
    default:
        return isAuto ? QCoreApplication::translate("Priority", "Auto [No]")
                      : QCoreApplication::translate("Priority", "Normal");
    }
}

} // namespace eMule
