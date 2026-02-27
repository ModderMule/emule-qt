/// @file CoreSession.cpp
/// @brief Lightweight timer driver — calls process() on core managers.
///
/// Creates and wires the upload pipeline components on start().

#include "app/CoreSession.h"
#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUDPListener.h"
#include "kademlia/KadUInt128.h"
#include "net/ClientUDPSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "transfer/UploadDiskIOThread.h"
#include "transfer/UploadQueue.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <QDir>

#include <cstring>

namespace eMule {

CoreSession::CoreSession(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &CoreSession::onTimer);
}

CoreSession::~CoreSession()
{
    stop();
    shutdownKademlia();
    shutdownUploadPipeline();
}

void CoreSession::start()
{
    logInfo(QStringLiteral("Starting core — TCP port %1, UDP port %2")
                .arg(thePrefs.port()).arg(thePrefs.udpPort()));
    initUploadPipeline();
    initKademlia();
    m_tickCounter = 0;
    m_timer.start();
}

void CoreSession::stop()
{
    m_timer.stop();
}

// ---------------------------------------------------------------------------
// initUploadPipeline — create and wire upload components
// ---------------------------------------------------------------------------

void CoreSession::initUploadPipeline()
{
    // Create KnownFileList if not already set
    if (!theApp.knownFileList) {
        m_knownFileList = std::make_unique<KnownFileList>();
        m_knownFileList->init(thePrefs.configDir());
        theApp.knownFileList = m_knownFileList.get();
    }

    // Create SharedFileList if not already set
    if (!theApp.sharedFileList) {
        m_sharedFileList = std::make_unique<SharedFileList>(theApp.knownFileList);
        theApp.sharedFileList = m_sharedFileList.get();

        // Wire server connect if available
        if (theApp.serverConnect)
            m_sharedFileList->setServerConnect(theApp.serverConnect);
    }

    // Create UploadDiskIOThread
    if (!m_uploadDiskIO) {
        m_uploadDiskIO = std::make_unique<UploadDiskIOThread>();
        m_uploadDiskIO->start();
    }

    // Create UploadBandwidthThrottler
    if (!m_uploadThrottler && !theApp.uploadBandwidthThrottler) {
        m_uploadThrottler = std::make_unique<UploadBandwidthThrottler>();
        theApp.uploadBandwidthThrottler = m_uploadThrottler.get();
        m_uploadThrottler->start();
    }

    // Create UploadQueue if not already set
    if (!theApp.uploadQueue) {
        m_uploadQueue = std::make_unique<UploadQueue>();
        theApp.uploadQueue = m_uploadQueue.get();

        // Wire components
        m_uploadQueue->setDiskIOThread(m_uploadDiskIO.get());
        m_uploadQueue->setThrottler(theApp.uploadBandwidthThrottler);
        m_uploadQueue->setSharedFileList(theApp.sharedFileList);
    }

    // Initial scan of shared files
    if (theApp.sharedFileList)
        theApp.sharedFileList->reload();
}

// ---------------------------------------------------------------------------
// shutdownUploadPipeline — stop threads and release components
// ---------------------------------------------------------------------------

void CoreSession::shutdownUploadPipeline()
{
    // Stop disk IO thread
    if (m_uploadDiskIO) {
        m_uploadDiskIO->endThread();
        m_uploadDiskIO->wait();
    }

    // Stop bandwidth throttler
    if (m_uploadThrottler) {
        m_uploadThrottler->endThread();
        m_uploadThrottler->wait();
    }

    // Clear theApp pointers before destroying owned objects
    if (m_uploadQueue && theApp.uploadQueue == m_uploadQueue.get())
        theApp.uploadQueue = nullptr;
    if (m_sharedFileList && theApp.sharedFileList == m_sharedFileList.get())
        theApp.sharedFileList = nullptr;
    if (m_knownFileList && theApp.knownFileList == m_knownFileList.get())
        theApp.knownFileList = nullptr;
    if (m_uploadThrottler && theApp.uploadBandwidthThrottler == m_uploadThrottler.get())
        theApp.uploadBandwidthThrottler = nullptr;

    m_uploadQueue.reset();
    m_uploadDiskIO.reset();
    m_uploadThrottler.reset();
    m_sharedFileList.reset();
    m_knownFileList.reset();
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
        if (theApp.clientCredits) {
            const QString creditsPath = QDir(thePrefs.configDir()).filePath(
                QStringLiteral("clients.met"));
            theApp.clientCredits->process(creditsPath);  // auto-save every 13 min
        }
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

// ---------------------------------------------------------------------------
// initKademlia — create and start Kademlia if enabled
// ---------------------------------------------------------------------------

void CoreSession::initKademlia()
{
    if (!thePrefs.kadEnabled() || m_kademlia)
        return;

    // 1. Create and bind the shared UDP socket (client + Kad traffic).
    m_clientUDP = std::make_unique<ClientUDPSocket>();
    const uint16 udpPort = static_cast<uint16>(thePrefs.udpPort());
    if (!m_clientUDP->rebind(udpPort)) {
        logError(QStringLiteral("Failed to bind client UDP socket on port %1 — Kademlia disabled")
                     .arg(udpPort));
        m_clientUDP.reset();
        return;
    }
    theApp.clientUDP = m_clientUDP.get();
    logInfo(QStringLiteral("Client UDP socket bound on port %1").arg(udpPort));

    // 2. Create and start Kademlia (no internal socket binding).
    m_kademlia = std::make_unique<kad::Kademlia>();
    m_kademlia->start();

    if (!m_kademlia->isRunning()) {
        m_kademlia.reset();
        theApp.clientUDP = nullptr;
        m_clientUDP.reset();
        return;
    }

    auto* listener = m_kademlia->getUDPListener();
    auto* udp = m_clientUDP.get();

    // 3. Receive bridge: ClientUDPSocket → KademliaUDPListener
    //    Reconstruct [opcode][payload] buffer for processPacket().
    connect(udp, &ClientUDPSocket::kadPacketReceived,
        listener, [listener](uint8 opcode, const uint8* data, uint32 size,
                             uint32 senderIP, uint16 senderPort,
                             bool validReceiverKey, uint32 receiverVerifyKey) {
            QByteArray buf(1 + static_cast<qsizetype>(size), Qt::Uninitialized);
            buf[0] = static_cast<char>(opcode);
            if (size > 0)
                std::memcpy(buf.data() + 1, data, size);
            listener->processPacket(reinterpret_cast<const uint8*>(buf.constData()),
                                    static_cast<uint32>(buf.size()),
                                    senderIP, senderPort,
                                    validReceiverKey,
                                    kad::KadUDPKey(receiverVerifyKey, senderIP));
        });

    // 4. Send bridge: KademliaUDPListener → ClientUDPSocket
    //    Build a Packet from the raw [opcode][payload] and queue it for sending.
    connect(listener, &kad::KademliaUDPListener::packetToSend,
        udp, [udp](QByteArray data, uint32 destIP, uint16 destPort,
                    kad::KadUDPKey targetKey, kad::UInt128 cryptTargetID) {
            if (data.isEmpty())
                return;

            auto pkt = std::make_unique<Packet>(OP_KADEMLIAHEADER);
            pkt->opcode = static_cast<uint8>(data[0]);
            if (data.size() > 1) {
                pkt->size = static_cast<uint32>(data.size() - 1);
                pkt->pBuffer = new char[pkt->size];
                std::memcpy(pkt->pBuffer, data.constData() + 1, pkt->size);
            }

            // Determine encryption parameters
            const uint8* targetHash = nullptr;
            uint32 receiverVerifyKey = 0;
            bool hasTarget = !(cryptTargetID == kad::UInt128());
            if (hasTarget) {
                targetHash = cryptTargetID.getData();
            } else {
                auto* prefs = kad::Kademlia::getInstancePrefs();
                receiverVerifyKey = targetKey.getKeyValue(
                    prefs ? prefs->ipAddress() : 0);
            }

            udp->sendPacket(std::move(pkt), destIP, destPort,
                            hasTarget || (receiverVerifyKey != 0),
                            hasTarget ? targetHash : nullptr,
                            true, receiverVerifyKey);
        });

    logInfo(QStringLiteral("Kademlia started."));
}

// ---------------------------------------------------------------------------
// shutdownKademlia — stop and destroy Kademlia
// ---------------------------------------------------------------------------

void CoreSession::shutdownKademlia()
{
    if (m_kademlia)
        m_kademlia->stop();
    m_kademlia.reset();

    if (theApp.clientUDP == m_clientUDP.get())
        theApp.clientUDP = nullptr;
    m_clientUDP.reset();
}

} // namespace eMule
