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
/// UsenetQueue::setRateLimit, BtSession::setRateLimits, Ed3kSession::setLimits.
///
/// ed3k (packmule) rides on eD2K's share (socket-modes.md "Mode A in eMuleQt"): it may use
/// all of it (ed3k_set_limits), its traffic joins eD2K's demand, its upload goes to the
/// throttler (noteBytesSentOutsideTheLoop: in the upload rate, off the budget) and eD2K's
/// download budget drops by what ed3k receives.

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
namespace ed3k {
class Ed3kSession;
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
        /// ed3k's limits (eD2K's share before ed3k's use is taken off) and wire rates, B/s
        qint64 ed3kDownKb = 0;
        qint64 ed3kUpKb = 0;
        qint64 ed3kDownRate = 0;
        qint64 ed3kUpRate = 0;
        /// a busy engine is held below its line by another one
        bool contended = false;

        bool operator==(const Snapshot&) const = default;
    };

    /// Either session may be null (BitTorrent not built, or a test).
    BandwidthCoordinator(usenet::UsenetSession* usenet, bt::BtSession* bt);
    ~BandwidthCoordinator();

    BandwidthCoordinator(const BandwidthCoordinator&) = delete;
    BandwidthCoordinator& operator=(const BandwidthCoordinator&) = delete;

    /// The ed3k node, or null (not built, a test). Set before start().
    void setEd3k(ed3k::Ed3kSession* session) { m_ed3k = session; }

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
    /// ed3k's wire bytes since the last tick: smoothed rates (B/s) into m_ed3k*Rate, sent
    /// bytes to the throttler. False: not running.
    bool sampleEd3k();
    /// ed3k's upload slot count from its rate, and its held slots into eD2K's slot gate.
    void applyEd3kSlots(bool ed3kRunning);

    usenet::UsenetSession* m_usenet = nullptr;
    bt::BtSession* m_bt = nullptr;
    ed3k::Ed3kSession* m_ed3k = nullptr;
#ifdef EMULE_HAVE_ED3K
    quint64 m_ed3kSent = 0;
    quint64 m_ed3kRecv = 0;
    QElapsedTimer m_ed3kClock;
#endif
    qint64 m_ed3kUpRate = 0;
    qint64 m_ed3kDownRate = 0;
    QTimer m_timer;

    DownloadPlanState m_downState;
    PairState m_upState;

    Snapshot m_snapshot;
    Snapshot m_logged;
    QElapsedTimer m_logClock;
};

} // namespace eMule
