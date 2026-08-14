#pragma once

/// @file StatsHistory.h
/// @brief Core-owned sample history for the statistics graphs and the toolbar graph.
///
/// The GUI used to collect these samples for itself, on its own timers. That meant
/// the traces died with the GUI process, and two GUIs attached to one daemon drew
/// two different pictures of the same core. Core samples once; a viewer asks for
/// whatever it is missing (the fromSeq replay in IpcClientHandler, modelled on
/// GetServerMessages).

#include "utils/Types.h"

#include <QObject>

#include <deque>
#include <vector>

namespace eMule {

/// One point of every series the three statistics graphs draw.
///
/// Field order follows MFC CStatisticsDlg::SetCurrentRate
/// (srchybrid/StatisticsDlg.cpp:569-600) — the single place MFC feeds all three
/// scopes.
struct StatsGraphSample {
    uint32 seq = 0;         ///< monotonic, 1-based; 0 means "no sample"
    uint32 timestamp = 0;   ///< epoch seconds

    // Download scope (MFC IDC_SCOPE_D)
    float downAvgSession = 0.0f;
    float downAvgTime    = 0.0f;
    float downCurrent    = 0.0f;

    // Upload scope (MFC IDC_SCOPE_U)
    float upAvgSession  = 0.0f;
    float upAvgTime     = 0.0f;
    float upCurrent     = 0.0f;
    float upNoOverhead  = 0.0f;
    float upFriend      = 0.0f;

    // Connections scope (MFC IDC_STATSSCOPE)
    uint32 connActive       = 0;
    uint32 upActive         = 0;
    uint32 upTotal          = 0;
    uint32 downTransferring = 0;
};

/// One point of the toolbar download/upload graph.
struct SpeedSample {
    uint32 seq  = 0;
    float  down = 0.0f;
    float  up   = 0.0f;
};

/// Ring-buffered sample history for every graph in the UI.
class StatsHistory : public QObject {
    Q_OBJECT

public:
    explicit StatsHistory(QObject* parent = nullptr);

    /// ~51 min at the default 3 s interval — matches MFC's scope width and
    /// StatsGraph::kMaxPoints.
    static constexpr size_t kStatsCapacity = 1024;
    /// 60 min at 1 s — the widest range the options spin offers.
    static constexpr size_t kSpeedCapacity = 3600;

    /// Take one sample. Called from the ~1 s slow path of CoreSession::onTimer().
    ///
    /// Always records a SpeedSample. Records a StatsGraphSample only once
    /// thePrefs.graphsUpdateSec() has elapsed, and not at all while that pref is 0 —
    /// MFC gates SetCurrentRate on GetTrafficOMeterInterval() the same way
    /// (srchybrid/UploadQueue.cpp:947).
    ///
    /// @param nowSecs current epoch seconds; passed in so the cadence is testable
    ///                without waiting on the wall clock.
    void sample(uint32 nowSecs);

    /// Samples newer than @p seq, oldest first. Empty once the caller is current.
    [[nodiscard]] std::vector<StatsGraphSample> statsSince(uint32 seq) const;
    [[nodiscard]] std::vector<SpeedSample> speedSince(uint32 seq) const;

    [[nodiscard]] uint32 lastStatsSeq() const { return m_statsSeq; }
    [[nodiscard]] uint32 lastSpeedSeq() const { return m_speedSeq; }

    /// Sequence of the oldest sample still held; 0 when empty. A viewer whose last
    /// seq is older than this has missed samples that have since aged out, and must
    /// clear what it has instead of appending to it.
    [[nodiscard]] uint32 oldestStatsSeq() const;
    [[nodiscard]] uint32 oldestSpeedSeq() const;

    /// Identifies this history's continuity. A viewer that sees a different value
    /// than last time must drop its own buffer. Changes when the daemon restarts
    /// (new process, new value) and when reset() renumbers the sequences — without
    /// it, a viewer holding seq 500 would sit frozen after a reset waiting for a
    /// sequence that starts again at 1.
    [[nodiscard]] uint32 epoch() const { return m_epoch; }

    /// Drop both histories and restart the sequences (Tools -> Reset statistics).
    void reset();

private:
    void takeStatsSample(uint32 nowSecs);
    void takeSpeedSample();

    std::deque<StatsGraphSample> m_stats;
    std::deque<SpeedSample> m_speed;
    uint32 m_statsSeq = 0;
    uint32 m_speedSeq = 0;
    uint32 m_lastStatsSampleTime = 0;   ///< epoch secs of the last stats sample
    uint32 m_epoch = 0;
};

} // namespace eMule
