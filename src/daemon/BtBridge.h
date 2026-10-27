#pragma once

/// @file BtBridge.h
/// @brief The daemon's one implementation of each BitTorrent request (IPC 800-899).
///
/// The header stays free of the bt module, like UsenetBridge.h: the daemon builds
/// without it (EMULE_BITTORRENT=OFF), and then every request answers 503.

#include "IpcMessage.h"
#include "stats/StatsHistory.h"

#include <QCborMap>
#include <QString>

#include <functional>

namespace eMule {

namespace bt {
class BtSession;
struct BtSettings;
} // namespace bt

namespace BtBridge {

/// The engine the requests go to; set by DaemonApp, null when absent.
void setSession(bt::BtSession* session);
[[nodiscard]] bt::BtSession* session();

/// Whether the daemon was built with the BitTorrent module.
[[nodiscard]] bool available();

/// Whether an opcode is one of the BitTorrent block (800-899).
[[nodiscard]] bool isBtRequest(Ipc::IpcMsgType type);

/// Answers a BitTorrent request through `reply`. False: not a BitTorrent opcode.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// One GetTorrents row, also what PushTorrentAdded/Update carry. Empty when the id
/// is unknown.
[[nodiscard]] QCborMap rowToCbor(const QString& id);

/// GetBtStatus, also what PushBtStatus carries.
[[nodiscard]] QCborMap statusToCbor();

/// Payload rates in KB/s; 0 while stopped or absent.
[[nodiscard]] EngineRates ratesKb();

/// GetStats' rateDownBt/rateUpBt, which the GUI adds to its totals.
void insertRates(QCborMap& stats);

/// GetBtStats: the session and cumulative counters plus the live figures.
[[nodiscard]] QCborMap statsToCbor();

/// Hands the app's IP filter (ranges below ipFilterLevel(), the rule of
/// IPFilter::isFiltered) to the session. `onlyIfLevelChanged`: skip when the level is the
/// one applied last (options saves). No-op without a session or filter.
void applyIpFilter(bool onlyIfLevelChanged = false);

#ifdef EMULE_HAVE_BITTORRENT
/// The session's settings from Preferences: the download dir resolved, the state dir
/// under the config dir.
[[nodiscard]] bt::BtSettings settingsFromPrefs();
#endif

} // namespace BtBridge
} // namespace eMule
