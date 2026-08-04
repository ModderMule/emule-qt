#pragma once

/// @file IpcFeedback.h
/// @brief Turns a rejected daemon reply into a warning box instead of silence.
///
/// IpcClient routes both Result and Error frames into the per-request callback and has no
/// fallback error signal, so a request sent with an empty callback discards the daemon's
/// rejection entirely — the button looks like a no-op. Every request whose failure the
/// user needs to know about should run its reply through checkOrWarn().

#include "IpcMessage.h"

#include <QString>

class QWidget;

namespace eMule::IpcFeedback {

/// True when @p resp reports success; otherwise shows a warning box and returns false.
///
/// Understands both reply shapes the daemon uses:
///   * `Result(false, "text")` — a semantic rejection (the handleStartSearch style)
///   * `Error(code, "text")`   — a precondition failure
/// Field 0 is the success flag in the first and the numeric code in the second;
/// QCborValue::toBool() yields false for that integer, so one check covers both, and
/// field 1 is the message either way.
///
/// @param parent    Box parent; may be null. Guard it with QPointer inside lambdas.
/// @param title     Box caption, e.g. tr("Search Kad").
/// @param fallback  Shown when the daemon sent no message of its own.
bool checkOrWarn(const Ipc::IpcMessage& resp, QWidget* parent,
                 const QString& title, const QString& fallback = {});

} // namespace eMule::IpcFeedback
