#pragma once

/// @file BandwidthCoordinator.h
/// @brief Divides the global download and upload limits between eD2K, Usenet and
/// BitTorrent, twice a second.
///
/// Priority eD2K > Usenet > BitTorrent. BitTorrent is split against eD2K and Usenet
/// together (its share holds while they are busy, it borrows what they leave idle);
/// what is left for those two is then split between them by the Usenet share. Upload
/// is eD2K against BitTorrent only — Usenet does not upload. All arithmetic is
/// computePairSplit() (core/transfer/BandwidthSplit).
///
/// Publishes: thePrefs.setEd2kDownloadBudget/setEd2kUploadBudget (runtime only),
/// UsenetQueue::setRateLimit, BtSession::setRateLimits.

#include "transfer/BandwidthSplit.h"
namespace eMule::usenet {
class UsenetSession;
}

#include <QElapsedTimer>
#include <QTimer>

namespace eMule {

namespace bt {
class BtSession;
}

class BandwidthCoordinator {
public:
    /// What is in force, KB/s. 0 = unlimited.
    struct Snapshot {
        qint64 maxDownloadKb = 0;
        qint64 ed2kDownKb = 0;
        qint64 usenetDownKb = 0;
        qint64 btDownKb = 0;
        /// the upload line before the split (USS or maxUpload)
        qint64 uploadLineKb = 0;
        qint64 ed2kUpKb = 0;
        qint64 btUpKb = 0;
        /// a busy engine is held below its line by another one
        bool contended = false;

        bool operator==(const Snapshot&) const = default;
    };

    /// Either session may be null (BitTorrent not built, or a test).
    BandwidthCoordinator(usenet::UsenetSession* usenet, bt::BtSession* bt);
    ~BandwidthCoordinator();

    BandwidthCoordinator(const BandwidthCoordinator&) = delete;
    BandwidthCoordinator& operator=(const BandwidthCoordinator&) = delete;

    void start();
    /// Gives every engine the whole line back.
    void stop();

    /// One recompute; the timer calls it every kTickMs.
    void tick();

    [[nodiscard]] const Snapshot& snapshot() const { return m_snapshot; }

    /// The running one, or null; for the stats pushes.
    [[nodiscard]] static const BandwidthCoordinator* instance();

    static constexpr int kTickMs = 500;

private:
    void logChange();

    usenet::UsenetSession* m_usenet = nullptr;
    bt::BtSession* m_bt = nullptr;
    QTimer m_timer;

    DownloadPlanState m_downState;
    PairState m_upState;

    Snapshot m_snapshot;
    Snapshot m_logged;
    QElapsedTimer m_logClock;
};

} // namespace eMule
