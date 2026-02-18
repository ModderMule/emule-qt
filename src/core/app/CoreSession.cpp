/// @file CoreSession.cpp
/// @brief Lightweight timer driver — calls process() on core managers.

#include "app/CoreSession.h"
#include "app/AppContext.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "net/ListenSocket.h"
#include "stats/Statistics.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"

namespace eMule {

CoreSession::CoreSession(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &CoreSession::onTimer);
}

void CoreSession::start()
{
    m_tickCounter = 0;
    m_timer.start();
}

void CoreSession::stop()
{
    m_timer.stop();
}

// ---------------------------------------------------------------------------
// onTimer — called every 100ms
// ---------------------------------------------------------------------------

void CoreSession::onTimer()
{
    ++m_tickCounter;

    // Fast path — every 100ms tick
    if (theApp.downloadQueue)
        theApp.downloadQueue->process();
    if (theApp.uploadQueue)
        theApp.uploadQueue->process();

    // Slow path — every 10th tick (~1s)
    if (m_tickCounter % 10 == 0) {
        if (theApp.listenSocket)
            theApp.listenSocket->process();
        if (theApp.knownFileList)
            theApp.knownFileList->process();
        if (theApp.sharedFileList)
            theApp.sharedFileList->process();
        if (theApp.statistics)
            theApp.statistics->updateConnectionStats(0.0f, 0.0f);
    }
}

} // namespace eMule
