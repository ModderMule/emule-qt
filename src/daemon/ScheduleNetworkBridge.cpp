#include "ScheduleNetworkBridge.h"

#include "UsenetBridge.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "webserver/UsenetWebBackend.h"

#ifdef EMULE_HAVE_BITTORRENT
#include "BtSession.h"
#endif
#ifdef EMULE_HAVE_ED3K
#include "Ed3kDownloads.h"
#endif

namespace eMule {

ScheduleNetworkBridge::ScheduleNetworkBridge(bt::BtSession* bt, ed3k::Ed3kDownloads* ed3kDownloads)
    : m_bt(bt)
    , m_ed3kDownloads(ed3kDownloads)
{
}

// -- Usenet -------------------------------------------------------------------

void ScheduleNetworkBridge::usenetPause(bool paused, bool persistent)
{
    if (persistent)
        UsenetBridge::setEnginePaused(paused);
    else
        UsenetBridge::setEnginePausedRuntime(paused);
}

void ScheduleNetworkBridge::usenetRestore()
{
    // a manual click inside the window saved the pref: then this is a no-op
    UsenetBridge::setEnginePausedRuntime(thePrefs.usenetPaused());
}

void ScheduleNetworkBridge::usenetCategory(int category, bool paused)
{
    // eMule's -1 "All" is Usenet's 0; Usenet can't pick out the uncategorized (-2)
    if (category == -1)
        category = 0;
    else if (category < 0)
        return;
    UsenetBridge::applyCategoryAction(category, paused ? UsenetWebCategoryAction::Pause
                                                       : UsenetWebCategoryAction::Resume);
}

// -- BitTorrent ---------------------------------------------------------------

void ScheduleNetworkBridge::btPause(bool paused, bool persistent)
{
#ifdef EMULE_HAVE_BITTORRENT
    btRestore();   // a newer window replaces the older one's change
    if (!m_bt || !m_bt->isRunning())
        return;
    QStringList flipped;
    for (const bt::BtTorrentRow& row : m_bt->torrents()) {
        const bool isPaused = row.state == bt::BtState::Paused;
        if (isPaused == paused)
            continue;
        if (paused ? m_bt->pause(row.id) : m_bt->resume(row.id))
            flipped.append(row.id);
    }
    logInfo(QStringLiteral("Scheduler: %1 %2 torrent(s)")
                .arg(paused ? QStringLiteral("paused") : QStringLiteral("resumed"))
                .arg(flipped.size()));
    if (!persistent) {
        m_btFlipped = std::move(flipped);
        m_btFlippedToPaused = paused;
    }
#else
    Q_UNUSED(paused)
    Q_UNUSED(persistent)
#endif
}

void ScheduleNetworkBridge::btRestore()
{
#ifdef EMULE_HAVE_BITTORRENT
    const QStringList flipped = std::exchange(m_btFlipped, {});
    if (flipped.isEmpty() || !m_bt || !m_bt->isRunning())
        return;
    // only those still as the schedule left them: a torrent the user touched stays
    for (const bt::BtTorrentRow& row : m_bt->torrents()) {
        if (!flipped.contains(row.id) || (row.state == bt::BtState::Paused) != m_btFlippedToPaused)
            continue;
        if (m_btFlippedToPaused)
            m_bt->resume(row.id);
        else
            m_bt->pause(row.id);
    }
#endif
}

// -- ed3k ---------------------------------------------------------------------

void ScheduleNetworkBridge::ed3kCategory(int category, bool paused)
{
#ifdef EMULE_HAVE_ED3K
    if (!m_ed3kDownloads)
        return;
    using State = ed3k::Ed3kDownloadInfo::State;
    for (const ed3k::Ed3kDownloadInfo& info : m_ed3kDownloads->list()) {
        // -1 all, -2 the uncategorized, as eMule's Stop/Resume Category
        const bool inCategory = category == -1 || info.category == (category == -2 ? 0 : category);
        if (!inCategory)
            continue;
        if (paused && info.state == State::Downloading)
            m_ed3kDownloads->pause(info.root);
        else if (!paused && info.state == State::Paused)
            m_ed3kDownloads->resume(info.root);
    }
#else
    Q_UNUSED(category)
    Q_UNUSED(paused)
#endif
}

} // namespace eMule
