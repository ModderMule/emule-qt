#include "BandwidthCoordinator.h"

#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "queue/UsenetQueue.h"
#include "UsenetSession.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "transfer/UploadQueue.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#ifdef EMULE_HAVE_BITTORRENT
#include "BtSession.h"
#endif

#ifdef EMULE_HAVE_ED3K
#include "Ed3kSession.h"
#endif

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace eMule {

namespace {

/// Rate windows; matched to Usenet's TickRateWindow (2 s) rather than the queues'
/// MFC averages, so freed bandwidth is lent promptly.
constexpr uint32 kRateWindowMs = 2000;
constexpr uint32 kRecentWindowMs = 500;

/// Log gate: a move smaller than this, in percent of the old cap, is measurement
/// noise; and never more often than kLogMinIntervalMs.
constexpr qint64 kLogChangePercent = 10;
constexpr qint64 kLogMinIntervalMs = 5000;
/// ed3k upload slots: KiB/s each, and the count's bounds (Protocol.md §12.5: 9 to 55 KiB/s
/// per slot, at least 2)
constexpr qint64 kEd3kSlotKb = 25;
constexpr qint64 kEd3kMinSlots = 2;
constexpr qint64 kEd3kMaxSlots = 20;

BandwidthCoordinator* s_instance = nullptr;

bool movedMaterially(qint64 oldKb, qint64 newKb)
{
    return std::abs(newKb - oldKb) * 100 > std::max<qint64>(oldKb, 1) * kLogChangePercent;
}

/// Tighter of two KB/s caps where 0 means none.
qint64 tighter(qint64 aKb, qint64 bKb)
{
    if (aKb == 0)
        return bKb;
    return bKb == 0 ? aKb : std::min(aKb, bKb);
}

} // namespace

BandwidthCoordinator::BandwidthCoordinator(usenet::UsenetSession* usenet, bt::BtSession* bt)
    : m_usenet(usenet)
    , m_bt(bt)
{
    m_timer.setInterval(kTickMs);
    QObject::connect(&m_timer, &QTimer::timeout, [this] { tick(); });
}

BandwidthCoordinator::~BandwidthCoordinator()
{
    stop();
}

void BandwidthCoordinator::start()
{
    s_instance = this;
    m_timer.start();
    tick();
}

void BandwidthCoordinator::stop()
{
    if (s_instance == this)
        s_instance = nullptr;
    m_timer.stop();
    thePrefs.setEd2kDownloadBudget(-1);
    thePrefs.setEd2kUploadBudget(-1);
    if (m_usenet && m_usenet->queue())
        m_usenet->queue()->setRateLimit(0);
#ifdef EMULE_HAVE_BITTORRENT
    if (m_bt)
        m_bt->setRateLimits(thePrefs.btMaxDownloadKb(), thePrefs.btMaxUploadKb());
#endif
    if (theApp.uploadQueue)
        theApp.uploadQueue->setForeignSlots(0);
#ifdef EMULE_HAVE_ED3K
    if (m_ed3k) {
        m_ed3k->setUploadSlots(0);   // from its upload limit again
        // the global limits, unshared (0 = none)
        const uint32 line = UploadQueue::lineCapKB();
        m_ed3k->setLimits(line == UNLIMITED ? 0 : quint64(line) * 1024, quint64(thePrefs.maxDownload()) * 1024);
    }
#endif
    m_downState = {};
    m_upState = {};
}

const BandwidthCoordinator* BandwidthCoordinator::instance()
{
    return s_instance;
}

void BandwidthCoordinator::tick()
{
    // -- Demand ---------------------------------------------------------------
    // Core runs on this thread, so the queues are read directly. hasActiveTransfers
    // counts sources actually sending, which a throttle slows but never zeroes.
    EngineDemand ed2kDown;
    if (theApp.downloadQueue) {
        ed2kDown.active = theApp.downloadQueue->hasActiveTransfers();
        ed2kDown.rateBytesPerSec = theApp.downloadQueue->datarateOver(kRateWindowMs);
        ed2kDown.recentBytesPerSec = theApp.downloadQueue->datarateOver(kRecentWindowMs);
    }
    EngineDemand ed2kUp;
    if (theApp.uploadQueue) {
        ed2kUp.active = theApp.uploadQueue->hasActiveUploads();
        ed2kUp.rateBytesPerSec = theApp.uploadQueue->datarateOver(kRateWindowMs);
        ed2kUp.recentBytesPerSec = theApp.uploadQueue->datarateOver(kRecentWindowMs);
    }

    // A stopped engine is idle: it lends its whole share
    usenet::UsenetQueue* usenetQueue = m_usenet ? m_usenet->queue() : nullptr;
    const bool usenetRunning = usenetQueue && m_usenet->isRunning();
    EngineDemand usenetDown;
    if (usenetRunning)
        usenetDown = {usenetQueue->hasActiveDownloads(), usenetQueue->currentRate(),
                      usenetQueue->recentRate()};

    EngineDemand btDown;
    EngineDemand btUp;
#ifdef EMULE_HAVE_BITTORRENT
    if (m_bt && m_bt->isRunning()) {
        const bt::BtDemand d = m_bt->demand();
        btDown = {d.downActive, d.downRate, d.downRecent};
        btUp = {d.upActive, d.upRate, d.upRecent};
    }
#endif

    // ed3k joins eD2K's demand; its upload is in eD2K's rate already (the throttler counts it)
    const bool ed3kRunning = sampleEd3k();
    if (ed3kRunning) {
        ed2kDown.active = ed2kDown.active || m_ed3kDownRate > 0;
        ed2kDown.rateBytesPerSec += m_ed3kDownRate;
        ed2kDown.recentBytesPerSec += m_ed3kDownRate;
        ed2kUp.active = ed2kUp.active || m_ed3kUpRate > 0;
    }

    // -- Download ---------------------------------------------------------------
    Snapshot s;
    const uint32 maxDown = thePrefs.maxDownload();
    s.maxDownloadKb = maxDown;
    const DownloadPlan down = planDownload(maxDown, thePrefs.usenetDownloadSharePercent(),
                                           thePrefs.btDownloadSharePercent(), ed2kDown,
                                           usenetDown, btDown, &m_downState);
    // replaced: eD2K yields what ed3k receives of their share
    // thePrefs.setEd2kDownloadBudget(down.ed2kBudgetKb);
    // s.ed2kDownKb = down.ed2kBudgetKb >= 0 ? down.ed2kBudgetKb : maxDown;
    const qint64 emuleDownKb = down.ed2kBudgetKb >= 0 ? down.ed2kBudgetKb : qint64(maxDown);   // 0 = none
    qint64 ed2kBudgetKb = down.ed2kBudgetKb;
    if (ed3kRunning && emuleDownKb > 0 && m_ed3kDownRate > 0)
        ed2kBudgetKb = std::max<qint64>(1, emuleDownKb - m_ed3kDownRate / 1024);
    thePrefs.setEd2kDownloadBudget(ed2kBudgetKb);
    s.ed2kDownKb = ed2kBudgetKb >= 0 ? ed2kBudgetKb : maxDown;
    if (usenetRunning) {
        // its 0 is unlimited, so it always gets the number
        usenetQueue->setRateLimit(down.usenetCapKb * 1024);
        s.usenetDownKb = down.usenetCapKb;
    }
    s.btDownKb = tighter(down.btCapKb, thePrefs.btMaxDownloadKb());

    // -- Upload -----------------------------------------------------------------
    const uint32 line = UploadQueue::lineCapKB();
    s.uploadLineKb = line == UNLIMITED ? 0 : line;
    const UploadPlan up = planUpload(s.uploadLineKb, thePrefs.btUploadSharePercent(), ed2kUp,
                                     btUp, &m_upState);
    thePrefs.setEd2kUploadBudget(up.ed2kBudgetKb);
    s.ed2kUpKb = up.ed2kBudgetKb >= 0 ? up.ed2kBudgetKb : s.uploadLineKb;
    s.btUpKb = tighter(up.btCapKb, thePrefs.btMaxUploadKb());

#ifdef EMULE_HAVE_BITTORRENT
    if (m_bt)
        m_bt->setRateLimits(static_cast<quint32>(s.btDownKb), static_cast<quint32>(s.btUpKb));
#endif

    // ed3k: all of eMule's share; the throttler and eD2K's budget keep the sum inside it
    s.ed3kDownKb = emuleDownKb;
    s.ed3kUpKb = s.ed2kUpKb;
    s.ed3kDownRate = ed3kRunning ? m_ed3kDownRate : 0;
    s.ed3kUpRate = ed3kRunning ? m_ed3kUpRate : 0;
#ifdef EMULE_HAVE_ED3K
    if (m_ed3k)
        m_ed3k->setLimits(quint64(s.ed3kUpKb) * 1024, quint64(s.ed3kDownKb) * 1024);
#endif
    applyEd3kSlots(ed3kRunning);

    const auto held = [](const EngineDemand& d, qint64 capKb, qint64 lineKb) {
        return d.active && lineKb > 0 && capKb < lineKb;
    };
    s.contended = held(ed2kDown, s.ed2kDownKb, maxDown) || held(usenetDown, s.usenetDownKb, maxDown)
        || held(btDown, down.btCapKb, maxDown) || held(ed2kUp, s.ed2kUpKb, s.uploadLineKb)
        || held(btUp, up.btCapKb, s.uploadLineKb);

    m_snapshot = s;
    logChange();
}

bool BandwidthCoordinator::sampleEd3k()
{
#ifdef EMULE_HAVE_ED3K
    if (!m_ed3k || !m_ed3k->isRunning()) {
        m_ed3kUpRate = m_ed3kDownRate = 0;
        return false;
    }
    // cumulative over restarts; a dead engine may lose its last counts: never negative
    const ed3k::Ed3kTraffic t = m_ed3k->traffic();
    const quint64 sent = t.sent >= m_ed3kSent ? t.sent - m_ed3kSent : 0;
    const quint64 recv = t.recv >= m_ed3kRecv ? t.recv - m_ed3kRecv : 0;
    m_ed3kSent = t.sent;
    m_ed3kRecv = t.recv;
    if (!m_ed3kClock.isValid()) {
        m_ed3kClock.start();   // the first sample is the baseline
        return true;
    }
    const qint64 ms = m_ed3kClock.restart();
    // past the throttler's loop: in the upload rate, off eD2K's budget
    if (sent > 0 && theApp.uploadBandwidthThrottler)
        theApp.uploadBandwidthThrottler->noteBytesSentOutsideTheLoop(
            static_cast<uint32>(std::min<quint64>(sent, std::numeric_limits<uint32>::max())));
    if (ms > 0) {
        // half the last value: a 500 ms tick alone is noisy
        m_ed3kUpRate = (m_ed3kUpRate + qint64(sent * 1000 / quint64(ms))) / 2;
        m_ed3kDownRate = (m_ed3kDownRate + qint64(recv * 1000 / quint64(ms))) / 2;
    }
    return true;
#else
    return false;
#endif
}

void BandwidthCoordinator::applyEd3kSlots(bool ed3kRunning)
{
    int held = 0;
#ifdef EMULE_HAVE_ED3K
    if (m_ed3k && ed3kRunning) {
        // eD2K and ed3k slots counted together on one line: ed3k gets slots for what it
        // sends (about 25 KiB/s each, Protocol.md §12.5: 2 at least), eD2K counts its holders
        const qint64 perSlot = qint64(kEd3kSlotKb) * 1024;
        const qint64 want = (m_ed3kUpRate + perSlot - 1) / perSlot;
        m_ed3k->setUploadSlots(std::uint32_t(std::clamp<qint64>(want, kEd3kMinSlots, kEd3kMaxSlots)));
        if (const auto q = m_ed3k->uploadQueueStats())
            held = int(q->holders);
    }
#else
    Q_UNUSED(ed3kRunning)
#endif
    if (theApp.uploadQueue)
        theApp.uploadQueue->setForeignSlots(held);
}

void BandwidthCoordinator::logChange()
{
    const Snapshot& s = m_snapshot;
    // Nothing contended then or now: a run where engines never compete says nothing
    if (!s.contended && !m_logged.contended)
        return;
    // Against what was last *logged*, so drift in small steps still surfaces
    const bool changed = s.contended != m_logged.contended
        || s.maxDownloadKb != m_logged.maxDownloadKb
        || s.uploadLineKb != m_logged.uploadLineKb
        || movedMaterially(m_logged.ed2kDownKb, s.ed2kDownKb)
        || movedMaterially(m_logged.usenetDownKb, s.usenetDownKb)
        || movedMaterially(m_logged.btDownKb, s.btDownKb)
        || movedMaterially(m_logged.ed2kUpKb, s.ed2kUpKb)
        || movedMaterially(m_logged.btUpKb, s.btUpKb);
    if (!changed)
        return;
    if (m_logClock.isValid() && m_logClock.elapsed() < kLogMinIntervalMs)
        return;

    if (s.contended) {
        logInfo(QStringLiteral("Bandwidth: limits shared — download %1 KB/s: eD2K up to %2, "
                               "Usenet up to %3, BitTorrent up to %4; upload %5 KB/s: eD2K up "
                               "to %6, BitTorrent up to %7 (0 = unlimited)")
                    .arg(s.maxDownloadKb).arg(s.ed2kDownKb).arg(s.usenetDownKb).arg(s.btDownKb)
                    .arg(s.uploadLineKb).arg(s.ed2kUpKb).arg(s.btUpKb));
    } else {
        logInfo(QStringLiteral("Bandwidth: limits no longer shared — each engine may use all of them"));
    }
    m_logged = s;
    m_logClock.start();
}

} // namespace eMule
