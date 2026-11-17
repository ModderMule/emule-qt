#pragma once

/// @file IpcStreamBridge.h
/// @brief File bytes over the IPC link (StatStream 1230, ReadStream 1231) and the pairing
/// identity (GetIpcIdentity 1232), for remote clients such as the qDonkey phone app: one
/// port, one TLS session, one token. Same sources as the web server's preview route.

#include "ipc/IpcMessage.h"

#include <QString>
#include <QtGlobal>

#include <functional>

class QObject;

namespace eMule::IpcStreamBridge {

using Reply = std::function<void(const Ipc::IpcMessage&)>;

/// 1230-1232. False: not one of them. @p owner outlives a waiting read or the reply is
/// dropped (the client went away).
bool handleRequest(const Ipc::IpcMessage& msg, QObject* owner, const Reply& reply);

/// Largest ReadStream chunk
inline constexpr qint64 kMaxChunk = 1024 * 1024;

} // namespace eMule::IpcStreamBridge
