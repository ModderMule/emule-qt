#pragma once

/// @file SharedFilesFetch.h
/// @brief Fetch the daemon's whole shared-files list, a page at a time.

#include <QCborArray>

#include <functional>

class QObject;

namespace eMule {

class IpcClient;

/// A share of some ten thousand files does not fit one IPC frame, so the list is
/// asked for in pages and handed over when the last one is in.
/// @p done gets ok = false when a request failed or the connection dropped; it is
/// not called at all once @p context is gone.
void fetchSharedFileRows(IpcClient* ipc, QObject* context,
                         std::function<void(bool ok, const QCborArray& rows)> done);

/// The same for every file known.met remembers (hash, fileName, fileSize only).
void fetchKnownFileRows(IpcClient* ipc, QObject* context,
                        std::function<void(bool ok, const QCborArray& rows)> done);

} // namespace eMule
