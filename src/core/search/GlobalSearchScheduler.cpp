#include "pch.h"
/// @file GlobalSearchScheduler.cpp
/// @brief Paced ED2K global (UDP) search — port of MFC's global search timer.

#include "search/GlobalSearchScheduler.h"

#include "app/AppContext.h"
#include "net/Address.h"
#include "prefs/Preferences.h"
#include "search/SearchExprParser.h"
#include "search/SearchList.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <QTimer>

namespace eMule {

namespace {

/// One server per tick. MFC: SetTimer(TimerGlobalSearch, 750, NULL) —
/// srchybrid/SearchResultsWnd.cpp:247.
constexpr int kSweepIntervalMs = 750;

/// How long to wait for the connected server's TCP answer before sweeping anyway.
/// MFC: SetTimer(TimerServerTimeout, SEC2MS(50), NULL) — srchybrid/SearchResultsWnd.cpp:1259.
constexpr int kLocalAnswerTimeoutMs = 50 * 1000;

} // namespace

// ---------------------------------------------------------------------------
// nextGlobalSearchTarget
// ---------------------------------------------------------------------------

Server* nextGlobalSearchTarget(ServerList& list, const Server* connected,
                               uint32 deadServerRetries, uint32& examined)
{
    const auto count = static_cast<uint32>(list.serverCount());

    Server* toask = nullptr;
    while (++examined < count) {
        toask = list.nextSearchServer();
        if (toask == nullptr
            || (toask != connected && toask->failedCount() < deadServerRetries))
            break;
        toask = nullptr;
    }
    return toask;
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

GlobalSearchScheduler::GlobalSearchScheduler(QObject* parent)
    : QObject(parent)
    , m_localTimeout(new QTimer(this))
    , m_sweepTimer(new QTimer(this))
{
    m_localTimeout->setSingleShot(true);
    m_localTimeout->setInterval(kLocalAnswerTimeoutMs);
    connect(m_localTimeout, &QTimer::timeout, this, [this] {
        logServerVerbose(QStringLiteral("Global search: local server did not answer within "
                                        "%1s — starting the UDP sweep anyway")
                             .arg(kLocalAnswerTimeoutMs / 1000));
        beginSweep();
    });

    m_sweepTimer->setInterval(kSweepIntervalMs);
    connect(m_sweepTimer, &QTimer::timeout, this, &GlobalSearchScheduler::onSweepTick);
}

GlobalSearchScheduler::~GlobalSearchScheduler() = default;

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

void GlobalSearchScheduler::start(uint32 searchID, QByteArray searchTerms,
                                  bool is64BitSearch, bool awaitLocalAnswer)
{
    cancel();   // one ED2K search at a time, as in MFC (SearchResultsWnd.cpp:1225)

    if (searchID == 0 || searchTerms.isEmpty())
        return;

    m_searchID = searchID;
    m_searchTerms = std::move(searchTerms);
    m_is64BitSearch = is64BitSearch;
    m_examined = 0;

    // Priorities order the list by usefulness, so a sweep that respects them should
    // restart from the top rather than continue where the last search left off.
    // MFC: srchybrid/SearchResultsWnd.cpp:1261-1262.
    if (thePrefs.useServerPriorities() && theApp.serverList)
        theApp.serverList->resetSearchServerPos();

    if (awaitLocalAnswer) {
        m_localTimeout->start();
        emitProgress(true);
    } else {
        beginSweep();
    }
}

void GlobalSearchScheduler::cancel()
{
    if (m_searchID == 0)
        return;

    m_localTimeout->stop();
    m_sweepTimer->stop();

    emitProgress(false);

    m_searchID = 0;
    m_searchTerms.clear();
    m_is64BitSearch = false;
    m_examined = 0;
}

void GlobalSearchScheduler::cancelSearch(uint32 searchID)
{
    if (searchID != 0 && searchID == m_searchID)
        cancel();
}

bool GlobalSearchScheduler::isRunning() const
{
    return m_searchID != 0;
}

void GlobalSearchScheduler::onLocalAnswerReceived()
{
    if (m_searchID == 0 || !m_localTimeout->isActive())
        return;

    m_localTimeout->stop();
    beginSweep();
}

void GlobalSearchScheduler::onResultCountChanged(uint32 searchID)
{
    if (m_searchID == 0 || searchID != m_searchID || !theApp.searchList)
        return;

    if (theApp.searchList->resultCount(searchID) > MAX_RESULTS) {
        logServerVerbose(QStringLiteral("Global search: stopping the UDP sweep after %1 "
                                        "results — the query is too broad")
                             .arg(theApp.searchList->resultCount(searchID)));
        cancel();
    }
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void GlobalSearchScheduler::beginSweep()
{
    if (m_searchID == 0)
        return;

    const size_t count = theApp.serverList ? theApp.serverList->serverCount() : 0;
    if (count == 0) {
        cancel();
        return;
    }

    logServerVerbose(QStringLiteral("UDP Global Search: sweeping %1 servers, one every %2ms")
                         .arg(count)
                         .arg(kSweepIntervalMs));
    m_sweepTimer->start();
    emitProgress(true);
}

void GlobalSearchScheduler::onSweepTick()
{
    if (m_searchID == 0 || !theApp.serverList) {
        cancel();
        return;
    }

    const Server* connected = theApp.serverConnect ? theApp.serverConnect->currentServer()
                                                   : nullptr;
    Server* toask = nextGlobalSearchTarget(*theApp.serverList, connected,
                                           thePrefs.deadServerRetries(), m_examined);
    if (toask == nullptr) {
        // Walked the whole list — MFC ends the search here (SearchResultsWnd.cpp:307-309).
        cancel();
        return;
    }

    // The opcode comes from the server's UDP flags; a 64-bit search is skipped for
    // servers without large-file support, which returns nullptr.
    auto pkt = buildGlobalSearchPacket(*toask, m_searchTerms, m_is64BitSearch);
    if (pkt) {
        if (theApp.searchList)
            theApp.searchList->addSentUDPRequestIP(m_searchID,
                                                   toask->ipAddress().toNetworkUint32());

        const auto udpPort = static_cast<uint16>(toask->port() + 4);
        logServerVerbose(QStringLiteral("  -> %1 (%2:%3) UDP:%4 opcode=0x%5 (%6 of %7)")
                             .arg(toask->name())
                             .arg(ipstr(toask->ipAddress()))
                             .arg(toask->port())
                             .arg(udpPort)
                             .arg(pkt->opcode, 2, 16, QLatin1Char('0'))
                             .arg(m_examined)
                             .arg(theApp.serverList->serverCount()));
        if (theApp.serverConnect)
            theApp.serverConnect->sendUDPPacket(std::move(pkt), *toask, udpPort);
    }

    emitProgress(true);
}

void GlobalSearchScheduler::emitProgress(bool running)
{
    const auto total = static_cast<uint32>(theApp.serverList ? theApp.serverList->serverCount() : 0);
    emit progress(m_searchID, m_examined, total, running);
}

} // namespace eMule
