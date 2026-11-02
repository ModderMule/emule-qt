#pragma once

/// @file Ed3kKeyText.h
/// @brief The ed3k key of a file as the Key columns show it (hops.md §1: which files are
/// shared under which key). The daemon sends "Node" / "Anonymous" (Ed3kHide::keyName).

#include <QCoreApplication>
#include <QString>

namespace eMule {

/// "Node" / "Anonymous" localized; empty for anything else (not over ed3k).
[[nodiscard]] inline QString ed3kKeyText(const QString& key)
{
    if (key == QLatin1String("Anonymous"))
        return QCoreApplication::translate("eMule::Ed3kKey", "Anonymous");
    if (key == QLatin1String("Node"))
        return QCoreApplication::translate("eMule::Ed3kKey", "Node");
    return {};
}

} // namespace eMule
