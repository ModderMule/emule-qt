#pragma once

/// @file ScheduleNetworkBridge.h
/// @brief The scheduler's hands on Usenet, BitTorrent and ed3k (core can't see them).
///
/// Runtime pauses never touch the saved state: Usenet keeps `usenet.paused`, BitTorrent
/// remembers which torrents it flipped and flips back only those still as it left them.

#include "transfer/Scheduler.h"

#include <QStringList>

namespace eMule {

namespace bt {
class BtSession;
}
namespace ed3k {
class Ed3kDownloads;
}

class ScheduleNetworkBridge final : public ScheduleNetworkActions {
public:
    ScheduleNetworkBridge(bt::BtSession* bt, ed3k::Ed3kDownloads* ed3kDownloads);

    void usenetPause(bool paused, bool persistent) override;
    void usenetRestore() override;
    void usenetCategory(int category, bool paused) override;
    void btPause(bool paused, bool persistent) override;
    void btRestore() override;
    void ed3kCategory(int category, bool paused) override;

private:
    bt::BtSession* m_bt = nullptr;
    ed3k::Ed3kDownloads* m_ed3kDownloads = nullptr;
    QStringList m_btFlipped;   // torrents a runtime btPause() changed
    bool m_btFlippedToPaused = false;
};

} // namespace eMule
