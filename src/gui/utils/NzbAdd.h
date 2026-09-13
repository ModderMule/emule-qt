#pragma once

/// @file NzbAdd.h
/// @brief Send an NZB add request, and handle the reply that is a question.
///
/// Every GUI path that queues an NZB — the file dialog, a panel drop, a window
/// drop, a Finder open, the command line, a pasted URL — ends in the same
/// exchange: the daemon may answer "you already downloaded this", which is not a
/// refusal but a question, and a yes re-sends the request with `force`. Answering
/// it in each of those places is how five call sites end up with four behaviours.
///
/// The shape is ProbeHttpCacheServer / ApplyHttpCacheConfig's, deliberately: the
/// daemon keeps no state between the question and the answer.

#include "IpcMessage.h"

#include <QString>

#include <functional>

class QWidget;

namespace eMule {

class IpcClient;

namespace gui {

/// Mirrors usenet::UsenetAddOutcome. The GUI links eMule::Core and eMule::Ipc but
/// never eMule::Usenet, so the value arrives as an int — the same reason
/// UsenetRowStatus exists beside UsenetItemStatus.
enum class NzbAddOutcome : int {
    Added = 0,
    Duplicate,          ///< in the queue and still arriving — never a question
    Invalid,
    Failed,
    AlreadyDownloaded,  ///< finished or cancelled before — worth asking about
};

/// Send @p build(false); on an "already downloaded" answer ask, and on Yes send
/// @p build(true).
///
/// @p build must produce a complete AddNzb / AddNzbUrl / GrabIndexerResult
/// request carrying the given `force` bit, so the retry differs from the first
/// attempt in that one bit and nothing else. It is not necessarily the last
/// field — AddNzb has a passphrase after it.
///
/// @p done reports whether anything was queued, once, whichever way it ended.
///
/// The dialogs open one event-loop turn after the reply. A modal opened inside
/// the reply's own call stack spins a nested event loop, and a quit arriving
/// during it unwinds every loop at once — Qt then returns into the freed socket.
/// Ed2kLinkImporter carries the same note for the same reason.
void sendNzbAdd(IpcClient* ipc, QWidget* parent, const QString& title,
                std::function<Ipc::IpcMessage(bool force)> build,
                std::function<void(bool added)> done = {});

} // namespace gui
} // namespace eMule
