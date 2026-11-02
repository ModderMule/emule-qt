#pragma once

/// @file Ed3kKeyText.h
/// @brief The ed3k key of a file as the Key columns show it (hops.md §1: which files are
/// shared under which key). The daemon sends "Node" / "Anonymous" (Ed3kHide::keyName).

#include <QAction>
#include <QCoreApplication>
#include <QMenu>
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

/// What the GUI knows of the node for the "Move to ... ed3k Key" entries (GetEd3kStatus).
struct Ed3kKeyState {
    bool running = false;     ///< the ed3k node runs
    bool anonRunning = false; ///< the anonymous key runs ("hide" map: hiding on and started)
};

/// Greys out a "Move to ... ed3k Key" entry the daemon would refuse (Ed3kHideBridge), with why.
inline void gateEd3kKeyMove(QAction* act, QMenu* menu, bool toAnon, const Ed3kKeyState& st, bool enabled)
{
    QString why;
    if (!st.running)
        why = QCoreApplication::translate("eMule::Ed3kKey", "ed3k is not running.");
    else if (toAnon && !st.anonRunning)
        why = QCoreApplication::translate("eMule::Ed3kKey",
                                          "Hiding is off: turn on \"Hide my node\" in Options > ed3k first.");
    act->setEnabled(enabled && why.isEmpty());
    if (!why.isEmpty()) {
        act->setToolTip(why);
        menu->setToolTipsVisible(true);
    }
}

} // namespace eMule
