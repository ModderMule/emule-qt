#include "pch.h"
/// @file PriorityText.cpp
/// @brief Upload and download priority labels — implementation.

#include "utils/PriorityText.h"

#include <QCoreApplication>

namespace eMule {

namespace {

/// The scale both lists share. Only level 3 differs between them, so it comes in
/// as a parameter rather than the whole table being copied.
QString priorityText(int prio, bool isAuto, const QString& topLabel)
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
        return topLabel;
    default:
        return isAuto ? QCoreApplication::translate("Priority", "Auto [No]")
                      : QCoreApplication::translate("Priority", "Normal");
    }
}

} // namespace

QString uploadPriorityText(int prio, bool isAuto)
{
    return priorityText(prio, isAuto, QCoreApplication::translate("Priority", "Release"));
}

int priorityFromWireName(const QString& wireName)
{
    if (wireName == QLatin1String("veryLow"))  return 4;
    if (wireName == QLatin1String("low"))      return 0;
    if (wireName == QLatin1String("high"))     return 2;
    if (wireName == QLatin1String("veryHigh")) return 3;
    return 1;   // normal, and "auto", which the daemon resolves per file anyway
}

QString downloadPriorityText(const QString& wireName, bool isAuto)
{
    return priorityText(priorityFromWireName(wireName), isAuto,
                        QCoreApplication::translate("Priority", "Very High"));
}

} // namespace eMule
