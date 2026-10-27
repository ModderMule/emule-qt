#include "BandwidthCoordinator.h"

#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "queue/UsenetQueue.h"
#include "UsenetSession.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#ifdef EMULE_HAVE_BITTORRENT
#include "BtSession.h"
#endif

#include <algorithm>
#include <cstdlib>

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

    // -- Download ---------------------------------------------------------------
    Snapshot s;
    const uint32 maxDown = thePrefs.maxDownload();
    s.maxDownloadKb = maxDown;
    const DownloadPlan down = planDownload(maxDown, thePrefs.usenetDownloadSharePercent(),
                                           thePrefs.btDownloadSharePercent(), ed2kDown,
                                           usenetDown, btDown, &m_downState);
    thePrefs.setEd2kDownloadBudget(down.ed2kBudgetKb);
    s.ed2kDownKb = down.ed2kBudgetKb >= 0 ? down.ed2kBudgetKb : maxDown;
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

    const auto held = [](const EngineDemand& d, qint64 capKb, qint64 lineKb) {
        return d.active && lineKb > 0 && capKb < lineKb;
    };
    s.contended = held(ed2kDown, s.ed2kDownKb, maxDown) || held(usenetDown, s.usenetDownKb, maxDown)
        || held(btDown, down.btCapKb, maxDown) || held(ed2kUp, s.ed2kUpKb, s.uploadLineKb)
        || held(btUp, up.btCapKb, s.uploadLineKb);

    m_snapshot = s;
    logChange();
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
