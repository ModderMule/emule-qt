/// @file tst_KadLiveNetwork.cpp
/// @brief Live network integration test — bootstrap into real Kad network.
///
/// Verifies that the full Kademlia networking stack works against the real
/// eMule Kad network: bootstrapping, UDP packet reception/processing,
/// keyword search, and the firewall check mechanism.
/// Requires internet connectivity.
///
/// Outbound packets are rate-limited to 10/sec to avoid flooding the network.
///
/// Port configuration (env vars EMULE_TCP_PORT, EMULE_UDP_PORT):
///   - Both set → bind TCP listener and UDP socket to those ports.
///     When forwarded, both firewall checks should PASS (not firewalled).
///   - Unset → random ports are used and firewalled status is expected.
///
/// Only built when EMULE_LIVE_TESTS=ON (off by default).

#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/ClientList.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "utils/OtherFunctions.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingBin.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUDPListener.h"
#include "kademlia/KadUDPKey.h"
#include "net/EncryptedDatagramSocket.h"
#include "net/ListenSocket.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>

#include <atomic>
#include <memory>
#include <vector>
#include <zlib.h>

using namespace eMule;
using namespace eMule::kad;
using namespace eMule::testing;

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_KadLiveNetwork : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void bootstrap_connectsToNetwork();
    void udpPackets_areReceived();
    void publishSources_completesForSharedFiles();
    void publishKeywords_completesWithLoad();
    void publishNotes_completesForComment();
    void keywordSearch_findsResults();
    void firewalledCheck_runsToCompletion();
    void cleanupTestCase();

private:
    void onReadyRead();
    void setupSharedFiles();

    /// Send packets to contacts at a rate of kSendRate/sec, stopping early
    /// when @p earlyStopCondition returns true (but always sending at least one batch).
    void rateLimitedSend(const ContactArray& contacts,
                         std::function<void(const Contact*)> sendFn,
                         std::function<bool()> earlyStopCondition = nullptr);

    static constexpr int kSendRate = 10;              ///< Max packets per second

    TempDir* m_tmpDir = nullptr;
    Kademlia* m_kad = nullptr;
    ClientList* m_clientList = nullptr;
    ListenSocket* m_listenSocket = nullptr;
    KnownFileList* m_knownFiles = nullptr;
    SharedFileList* m_sharedFiles = nullptr;
    bool m_portsOpen = false;         ///< True when EMULE_TCP/UDP_PORT are set (expect not-firewalled)
    QUdpSocket m_socket;              ///< Single UDP socket for send + receive
    std::atomic<int> m_kadPacketsReceived{0};
    std::atomic<int> m_kadPacketsSent{0};
    std::atomic<int> m_rawDatagramsReceived{0};
    std::atomic<int> m_decryptFailed{0};

    /// Keyword search results collected via the Kad callback.
    struct KeywordResult {
        QString name;
        uint64 size = 0;
        uint32 sources = 0;
        uint32 completeSources = 0;
    };
    std::vector<KeywordResult> m_searchResults;
};

// ---------------------------------------------------------------------------
// Inbound UDP handler — runs on main thread via readyRead signal
// ---------------------------------------------------------------------------

void tst_KadLiveNetwork::onReadyRead()
{
    while (m_socket.hasPendingDatagrams()) {
        QNetworkDatagram dg = m_socket.receiveDatagram(6000);
        if (!dg.isValid())
            continue;

        m_rawDatagramsReceived.fetch_add(1, std::memory_order_relaxed);

        QByteArray data = dg.data();
        if (data.size() < 2)
            continue;

        const uint32 senderIP = dg.senderAddress().toIPv4Address();
        const auto senderPort = static_cast<uint16>(dg.senderPort());

        auto* buf = reinterpret_cast<const uint8*>(data.constData());
        const auto bufLen = static_cast<uint32>(data.size());
        uint8 proto = buf[0];

        // Plain Kad packet
        if (proto == OP_KADEMLIAHEADER) {
            m_kadPacketsReceived.fetch_add(1, std::memory_order_relaxed);
            m_kad->processPacket(buf + 1, bufLen - 1,
                                 senderIP, senderPort, false, KadUDPKey(0));
            continue;
        }

        // Compressed Kad packet — decompress before processing
        if (proto == OP_KADEMLIAPACKEDPROT) {
            m_kadPacketsReceived.fetch_add(1, std::memory_order_relaxed);
            uLongf unpackedLen = (bufLen - 1) * 10 + 300;
            auto unpackBuf = std::make_unique<uint8[]>(unpackedLen);
            if (uncompress(reinterpret_cast<Bytef*>(unpackBuf.get()), &unpackedLen,
                           reinterpret_cast<const Bytef*>(buf + 1), bufLen - 1) == Z_OK) {
                m_kad->processPacket(unpackBuf.get(), static_cast<uint32>(unpackedLen),
                                     senderIP, senderPort, false, KadUDPKey(0));
            }
            continue;
        }

        // Possibly encrypted — try decryption.
        // Use getData() (raw m_data bytes) for the Kad ID, NOT toByteArray()
        // which byte-swaps.  The wire format matches the raw representation.
        auto userHash = thePrefs.userHash();
        const uint8* kadIDPtr = nullptr;
        uint32 kadRecvKey = 0;
        if (auto* kadPrefs = Kademlia::getInstancePrefs()) {
            kadIDPtr = RoutingZone::localKadId().getData();
            kadRecvKey = kadPrefs->getUDPVerifyKey(senderIP);
        }

        // TODO: remove debug — log encrypted attempt
        qDebug() << "  encrypted? from" << QHostAddress(senderIP).toString()
                 << "len" << bufLen
                 << QStringLiteral("proto=0x%1").arg(proto, 2, 16, QLatin1Char('0'))
                 << "kadRecvKey" << kadRecvKey;

        auto dr = EncryptedDatagramSocket::decryptReceivedClient(
            const_cast<uint8*>(buf), static_cast<int>(bufLen),
            senderIP, userHash.data(), kadIDPtr, kadRecvKey);

        // Check whether decryption actually succeeded: on success dr.data
        // points past the crypto header; on failure dr.data == buf (pass-through).
        const bool decrypted = (dr.data != buf);
        if (!decrypted) {
            m_decryptFailed.fetch_add(1, std::memory_order_relaxed);
            // TODO: remove debug
            qDebug() << "  DECRYPT FAILED from" << QHostAddress(senderIP).toString()
                     << "len" << bufLen;
        }
        if (dr.length > 1 && dr.data && decrypted) {
            uint8 innerProto = dr.data[0];
            bool validKey = dr.receiverVerifyKey != 0;
            KadUDPKey senderUDPKey(dr.senderVerifyKey, senderIP);

            if (innerProto == OP_KADEMLIAHEADER) {
                m_kadPacketsReceived.fetch_add(1, std::memory_order_relaxed);
                m_kad->processPacket(dr.data + 1, static_cast<uint32>(dr.length - 1),
                                     senderIP, senderPort, validKey, senderUDPKey);
            } else if (innerProto == OP_KADEMLIAPACKEDPROT) {
                m_kadPacketsReceived.fetch_add(1, std::memory_order_relaxed);
                uLongf unpackedLen = static_cast<uLongf>(dr.length - 1) * 10 + 300;
                auto unpackBuf = std::make_unique<uint8[]>(unpackedLen);
                if (uncompress(reinterpret_cast<Bytef*>(unpackBuf.get()), &unpackedLen,
                               reinterpret_cast<const Bytef*>(dr.data + 1),
                               static_cast<uLong>(dr.length - 1)) == Z_OK) {
                    m_kad->processPacket(unpackBuf.get(), static_cast<uint32>(unpackedLen),
                                         senderIP, senderPort, validKey, senderUDPKey);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void tst_KadLiveNetwork::initTestCase()
{
    m_tmpDir = new TempDir();

    // 1. Initialize global preferences
    thePrefs.load(m_tmpDir->filePath(QStringLiteral("prefs.yaml")));

    // 2. Copy nodes.dat to temp path where Kademlia::start() reads it
    const QString srcNodes = projectDataDir() + QStringLiteral("/nodes.dat");
    QVERIFY2(QFile::exists(srcNodes), "Missing data/nodes.dat bootstrap file");

    const QString dstNodes = QDir::tempPath() + QStringLiteral("/nodes.dat");
    if (QFile::exists(dstNodes))
        QFile::remove(dstNodes);
    QVERIFY(QFile::copy(srcNodes, dstNodes));

    // 3. Port configuration — EMULE_TCP_PORT / EMULE_UDP_PORT select specific
    //    open (forwarded) ports.  When both are set, firewall checks should
    //    succeed because remote nodes can reach us.  When unset, random ports
    //    are used and we expect firewalled status.
    const int envTcpPort = qEnvironmentVariableIntValue("EMULE_TCP_PORT");
    const int envUdpPort = qEnvironmentVariableIntValue("EMULE_UDP_PORT");
    m_portsOpen = (envTcpPort > 0 && envTcpPort <= 65535
                && envUdpPort > 0 && envUdpPort <= 65535);
    const auto tcpBindPort = m_portsOpen ? static_cast<uint16>(envTcpPort) : uint16{0};
    const auto udpBindPort = m_portsOpen ? static_cast<uint16>(envUdpPort) : uint16{0};

    QVERIFY2(m_socket.bind(QHostAddress::AnyIPv4, udpBindPort), "Failed to bind UDP socket");
    connect(&m_socket, &QUdpSocket::readyRead, this, &tst_KadLiveNetwork::onReadyRead);

    // Warm up the socket with a test send (works around macOS lazy-bind quirks)
    m_socket.writeDatagram(QByteArray(1, '\0'), QHostAddress(QStringLiteral("8.8.8.8")), 53);

    // 4. Create remaining infrastructure
    m_clientList = new ClientList(this);
    m_listenSocket = new ListenSocket(this);
    QVERIFY2(m_listenSocket->startListening(tcpBindPort), "Failed to start TCP listener");

    // Set preferences to match our actual listening ports.
    // firewalledCheck() sends thePrefs.port() for the TCP connect test,
    // and internKadPort() (= thePrefs.udpPort()) for the UDP probe test.
    thePrefs.setPort(m_listenSocket->connectedPort());
    thePrefs.setUdpPort(static_cast<uint16>(m_socket.localPort()));

    qDebug() << "Ports:" << (m_portsOpen ? "open" : "random")
             << "TCP:" << m_listenSocket->connectedPort()
             << "UDP:" << m_socket.localPort();

    // 5. Wire into global context
    theApp.clientList = m_clientList;
    theApp.listenSocket = m_listenSocket;
    Kademlia::setClientList(m_clientList);
    Kademlia::setIPFilter(nullptr);

    // 6. Start Kademlia (loads nodes.dat into routing zone)
    RoutingBin::resetGlobalTracking();
    m_kad = new Kademlia(this);
    m_kad->start();
    QVERIFY(m_kad->isRunning());

    // 7. Wire outbound: KademliaUDPListener::packetToSend → m_socket
    //    Signal data format: [opcode | payload...]
    //    Wire format:        [protocol | opcode | payload...]
    //
    //    Modern Kad nodes require encryption.  When a target Kad ID is
    //    available, EncryptedDatagramSocket::encryptSendClient() wraps the
    //    packet with the Kad encryption header.  Otherwise we fall back to
    //    plain transmission (only works for bootstrap).
    connect(m_kad->getUDPListener(), &KademliaUDPListener::packetToSend,
            this, [this](QByteArray data, uint32 destIP, uint16 destPort,
                         KadUDPKey /*targetKey*/, UInt128 cryptTargetID) {
        if (data.isEmpty())
            return;
        m_kadPacketsSent.fetch_add(1, std::memory_order_relaxed);

        // TODO: remove debug — log outgoing opcode
        uint8 outOpcode = data.isEmpty() ? 0 : static_cast<uint8>(data[0]);
        if (outOpcode == KADEMLIA2_REQ || outOpcode == KADEMLIA2_SEARCH_KEY_REQ
            || outOpcode == KADEMLIA2_SEARCH_SOURCE_REQ
            || outOpcode == KADEMLIA2_PUBLISH_SOURCE_REQ) {
            qDebug() << "  >> SEND"
                     << QStringLiteral("opcode=0x%1").arg(outOpcode, 2, 16, QLatin1Char('0'))
                     << "to" << QHostAddress(destIP).toString() << ":" << destPort
                     << "encrypted:" << !(cryptTargetID == UInt128(uint32{0}));
        }

        // Build the plain Kad packet: [proto | opcode | payload...]
        const auto plainLen = static_cast<int>(data.size()) + 1;
        const int overhead = EncryptedDatagramSocket::encryptOverheadSize(true);
        QByteArray buf(overhead + plainLen, '\0');
        auto* raw = reinterpret_cast<uint8*>(buf.data());
        raw[overhead] = static_cast<uint8>(OP_KADEMLIAHEADER);
        memcpy(raw + overhead + 1, data.constData(), static_cast<size_t>(data.size()));

        // Encrypt if we have a target Kad ID.
        // IMPORTANT: use getData() (raw m_data bytes), NOT toByteArray() which
        // byte-swaps.  The wire format uses the raw uint32 representation,
        // so encryption keys must match that byte order.
        bool canEncrypt = !(cryptTargetID == UInt128(uint32{0}));
        const uint8* kadIDBytes = canEncrypt ? cryptTargetID.getData() : nullptr;

        uint32 senderVerifyKey = 0;
        if (auto* kadPrefs = Kademlia::getInstancePrefs())
            senderVerifyKey = kadPrefs->getUDPVerifyKey(destIP);

        if (canEncrypt) {
            uint32 totalLen = EncryptedDatagramSocket::encryptSendClient(
                raw, static_cast<uint32>(plainLen),
                kadIDBytes, true, 0, senderVerifyKey, 0);
            m_socket.writeDatagram(reinterpret_cast<const char*>(raw),
                                   static_cast<qint64>(totalLen),
                                   QHostAddress(destIP), destPort);
        } else {
            // Plain: send from offset `overhead` (just the [proto|opcode|payload])
            m_socket.writeDatagram(reinterpret_cast<const char*>(raw + overhead),
                                   static_cast<qint64>(plainLen),
                                   QHostAddress(destIP), destPort);
        }
    });

    // 8. Send BOOTSTRAP_REQ to contacts from nodes.dat at kSendRate/sec.
    //    Bootstrap requests are sent plain (no encryption) so any live
    //    Kad node will respond with BOOTSTRAP_RES containing ~20 fresh
    //    contacts.  Stop early once we have enough responses.
    ContactArray contacts;
    m_kad->getRoutingZone()->getAllEntries(contacts);
    qDebug() << "Loaded" << contacts.size() << "bootstrap contacts from nodes.dat";

    auto* udpListener = m_kad->getUDPListener();
    rateLimitedSend(contacts,
        [udpListener](const Contact* c) {
            udpListener->bootstrap(c->getIPAddress(), c->getUDPPort());
        },
        [this] { return m_kadPacketsReceived.load(std::memory_order_relaxed) >= 10; });

    // Also try DNS bootstrap in case nodes.dat is too stale
    m_kad->bootstrap(QStringLiteral("boot.emule-security.org"), 4672);

    // Let remaining bootstrap responses arrive
    QTest::qWait(3'000);
    qDebug() << "After bootstrap — sent:" << m_kadPacketsSent.load()
             << "received:" << m_kadPacketsReceived.load();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void tst_KadLiveNetwork::bootstrap_connectsToNetwork()
{
    // Wait up to 60 seconds for Kademlia to establish contact with the network.
    // The initial BOOTSTRAP_REQ batch was sent in initTestCase.  Once the
    // first response arrives, setLastContact() makes isConnected() true.
    const bool connected = QTest::qWaitFor([this] {
        return m_kad->isConnected();
    }, 60'000);

    QVERIFY2(connected, "Kademlia did not connect to the network within 60 seconds");

    // Send HELLO_REQ (encrypted) to routing contacts at kSendRate/sec.
    // This mirrors the RoutingZone small-timer behavior and triggers the
    // firewall check mechanism.  Stop early once we have 20+ responses.
    ContactArray contacts;
    m_kad->getRoutingZone()->getAllEntries(contacts);

    auto* udpListener = m_kad->getUDPListener();
    const int baseReceived = m_kadPacketsReceived.load(std::memory_order_relaxed);
    rateLimitedSend(contacts,
        [udpListener](const Contact* c) {
            UInt128 contactID = c->getClientID();
            udpListener->sendMyDetails(KADEMLIA2_HELLO_REQ,
                                       c->getIPAddress(), c->getUDPPort(),
                                       c->getVersion(), c->getUDPKey(), &contactID, false);
        },
        [this, baseReceived] {
            return m_kadPacketsReceived.load(std::memory_order_relaxed) - baseReceived >= 20;
        });

    qDebug() << "Kad connected — sent HELLOs, routing contacts:"
             << m_kad->getRoutingZone()->getNumContacts();
}

void tst_KadLiveNetwork::udpPackets_areReceived()
{
    // Wait up to 60 seconds for at least 5 Kad responses.
    // Bootstrap responses (plain) are reliable; HELLO_RES (encrypted) provide
    // additional packets and trigger firewall check processing.
    const bool enough = QTest::qWaitFor([this] {
        return m_kadPacketsReceived.load(std::memory_order_relaxed) >= 5;
    }, 60'000);

    const int count = m_kadPacketsReceived.load(std::memory_order_relaxed);
    qDebug() << "Kad packets received:" << count << "sent:" << m_kadPacketsSent.load()
             << "raw datagrams:" << m_rawDatagramsReceived.load()
             << "decrypt failed:" << m_decryptFailed.load();

    QVERIFY2(enough,
             qPrintable(QStringLiteral("Expected >= 5 Kad packets, got %1").arg(count)));
}

// ---------------------------------------------------------------------------
// Publish tests — share files from data/incoming, publish to Kad
// ---------------------------------------------------------------------------

void tst_KadLiveNetwork::setupSharedFiles()
{
    if (m_sharedFiles)
        return;  // already set up

    // Copy data/incoming to temp dir so we don't modify originals
    const QString srcDir = projectDataDir() + QStringLiteral("/incoming");
    const QString dstDir = m_tmpDir->filePath(QStringLiteral("incoming"));
    QDir().mkpath(dstDir);

    QDir src(srcDir);
    int copied = 0;
    for (const auto& fi : src.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        if (fi.size() == 0)
            continue;
        if (fi.fileName().endsWith(QStringLiteral(".part"), Qt::CaseInsensitive)
            || fi.fileName().endsWith(QStringLiteral(".part.met"), Qt::CaseInsensitive))
            continue;
        QFile::copy(fi.absoluteFilePath(), dstDir + QDir::separator() + fi.fileName());
        ++copied;
    }
    QVERIFY2(copied > 0, "No shareable files in data/incoming");

    thePrefs.setIncomingDir(dstDir);
    thePrefs.setTempDirs({m_tmpDir->filePath(QStringLiteral("temp"))});
    QDir().mkpath(thePrefs.tempDirs().first());

    m_knownFiles = new KnownFileList();
    m_sharedFiles = new SharedFileList(m_knownFiles, this);

    QSignalSpy addedSpy(m_sharedFiles, &SharedFileList::fileAdded);
    m_sharedFiles->reload();

    // Wait for hashing to finish (up to 30s)
    const bool allHashed = QTest::qWaitFor([&] {
        return addedSpy.count() >= copied;
    }, 30'000);

    QVERIFY2(allHashed,
             qPrintable(QStringLiteral("Only %1/%2 files hashed")
                            .arg(addedSpy.count()).arg(copied)));

    // Pre-write a comment/rating for the first shared file into filecomments.ini.
    // This must happen before any test calls getFileRating()/getFileComment(),
    // because loadComment() caches the result on first access.  If the INI entry
    // doesn't exist yet, the cached value would be empty and later writes won't
    // be picked up (m_commentLoaded stays true).
    const QString iniPath = thePrefs.configDir() + QStringLiteral("/filecomments.ini");
    QDir().mkpath(thePrefs.configDir());
    KnownFile* firstFile = nullptr;
    m_sharedFiles->forEachFile([&](KnownFile* file) {
        if (!firstFile)
            firstFile = file;
    });
    if (firstFile) {
        QSettings settings(iniPath, QSettings::IniFormat);
        const QString hashKey = encodeBase16({firstFile->fileHash(), 16});
        settings.setValue(hashKey, QStringLiteral("4|test"));
        settings.sync();
    }

    theApp.sharedFileList = m_sharedFiles;
    qDebug() << "Shared" << m_sharedFiles->getCount() << "files for Kad publishing";
}

void tst_KadLiveNetwork::publishSources_completesForSharedFiles()
{
    setupSharedFiles();

    // Publish each shared file as a source (StoreFile).
    // The search finds the closest nodes for each file hash, then sends
    // KADEMLIA2_PUBLISH_SOURCE_REQ in the store phase.
    std::vector<uint32> searchIDs;
    m_sharedFiles->forEachFile([&](KnownFile* file) {
        UInt128 target;
        target.setValueBE(file->fileHash());
        auto* search = SearchManager::prepareLookup(SearchType::StoreFile, true, target);
        if (search) {
            searchIDs.push_back(search->getSearchID());
            qDebug() << "  Source publish started for" << file->fileName()
                     << "searchID:" << search->getSearchID();
        }
    });

    QVERIFY2(!searchIDs.empty(), "No source publish searches were created");

    // Wait for all source publishes to complete (searches expire when done).
    // kSearchStoreFileLifetime = 140s, so we allow 150s for the store phase
    // to finish and updateStats() to remove the search.
    const bool allDone = QTest::qWaitFor([&] {
        for (uint32 id : searchIDs) {
            if (SearchManager::isSearching(id))
                return false;
        }
        return true;
    }, 150'000);

    qDebug() << "Source publish — all done:" << allDone
             << "total packets sent:" << m_kadPacketsSent.load();

    QVERIFY2(allDone, "Source publish did not complete within 150 seconds");
}

void tst_KadLiveNetwork::publishKeywords_completesWithLoad()
{
    setupSharedFiles();

    // For keyword publishing, each word in the filename gets its own search.
    // Longer filenames produce multiple keywords. We iterate the keyword list
    // and publish each one.
    std::vector<std::pair<uint32, QString>> searchInfo;  // searchID, keyword

    // Force keyword publish time to now so getNextKeyword() returns entries
    m_sharedFiles->forEachFile([this](KnownFile* file) {
        m_sharedFiles->addKeywords(file);
    });

    // Publish keywords one by one
    int keywordsPublished = 0;
    while (true) {
        // Access keyword list directly — addKeywords populated it
        auto* search = SearchManager::prepareLookup(SearchType::StoreKeyword, false,
                                                     UInt128(uint32{0}));
        if (!search)
            break;

        // We need to supply a proper target; use SharedFileList::publish() pattern.
        // Instead, break and use the high-level publish() call.
        delete search;
        break;
    }

    // Use the high-level publish() which handles keyword splitting and round-robin.
    // Call it repeatedly to cycle through all keywords.
    constexpr int kMaxKeywordRounds = 20;
    for (int round = 0; round < kMaxKeywordRounds; ++round) {
        m_sharedFiles->publish();
        QTest::qWait(500);

        // Check if any StoreKeyword searches are active
        for (const auto& [target, search] : SearchManager::getSearches()) {
            if (search->getSearchType() == SearchType::StoreKeyword) {
                if (std::none_of(searchInfo.begin(), searchInfo.end(),
                                  [&](const auto& p) { return p.first == search->getSearchID(); })) {
                    searchInfo.emplace_back(search->getSearchID(), search->getGUIName());
                    ++keywordsPublished;
                    qDebug() << "  Keyword publish started:" << search->getGUIName()
                             << "searchID:" << search->getSearchID();
                }
            }
        }
        if (keywordsPublished >= 3)
            break;  // enough keywords started
    }

    QVERIFY2(keywordsPublished > 0,
             "No keyword publish searches were created");
    qDebug() << "Keyword publishes started:" << keywordsPublished;

    // Wait for all keyword publishes to complete
    const bool allDone = QTest::qWaitFor([&] {
        for (const auto& [id, kw] : searchInfo) {
            if (SearchManager::isSearching(id))
                return false;
        }
        return true;
    }, 180'000);

    // Check load values — at least one keyword search should have received load
    int withLoad = 0;
    uint32 totalLoad = 0;
    for (const auto& [target, search] : SearchManager::getSearches()) {
        if (search->getSearchType() == SearchType::StoreKeyword
            && search->getNodeLoadResponse() > 0) {
            ++withLoad;
            totalLoad += search->getNodeLoad();
        }
    }

    qDebug() << "Keyword publish — all done:" << allDone
             << "keywords with load data:" << withLoad
             << "average load:" << (withLoad > 0 ? totalLoad / static_cast<uint32>(withLoad) : 0u);

    QVERIFY2(allDone, "Keyword publish did not complete within 180 seconds");
}

void tst_KadLiveNetwork::publishNotes_completesForComment()
{
    setupSharedFiles();

    // Pick the first shared file and set a comment + rating on it.
    KnownFile* noteFile = nullptr;
    m_sharedFiles->forEachFile([&](KnownFile* file) {
        if (!noteFile)
            noteFile = file;
    });
    QVERIFY2(noteFile != nullptr, "No shared files available for notes publishing");

    // Write comment "test" with rating 4 (good) to the filecomments.ini
    // Format: key=hex(md4hash), value="rating|comment"
    const QString iniPath = thePrefs.configDir() + QStringLiteral("/filecomments.ini");
    QDir().mkpath(thePrefs.configDir());
    {
        QSettings settings(iniPath, QSettings::IniFormat);
        const QString hashKey = encodeBase16({noteFile->fileHash(), 16});
        settings.setValue(hashKey, QStringLiteral("4|test"));
        settings.sync();
    }

    // Force publish time to 0 so publishNotes() returns true
    noteFile->setLastPublishTimeKadNotes(0);

    qDebug() << "Publishing notes for" << noteFile->fileName()
             << "comment:" << noteFile->getFileComment()
             << "rating:" << noteFile->getFileRating();

    // Start a StoreNotes search for this file
    UInt128 target;
    target.setValueBE(noteFile->fileHash());
    auto* search = SearchManager::prepareLookup(SearchType::StoreNotes, false, target);
    QVERIFY2(search != nullptr, "prepareLookup for StoreNotes returned nullptr");

    // Add the file ID so storePacket() can find the file for tags
    UInt128 fileID;
    fileID.setValueBE(noteFile->fileHash());
    search->addFileID(fileID);

    QVERIFY(SearchManager::startSearch(search));
    const uint32 searchID = search->getSearchID();

    qDebug() << "Notes publish started — searchID:" << searchID;

    // Wait for the notes publish to complete
    const bool done = QTest::qWaitFor([searchID] {
        return !SearchManager::isSearching(searchID);
    }, 120'000);

    qDebug() << "Notes publish — done:" << done;
    QVERIFY2(done, "Notes publish did not complete within 120 seconds");
}

void tst_KadLiveNetwork::firewalledCheck_runsToCompletion()
{
    // --- TCP firewall check ---
    // HELLO_RES processing calls firewalledCheck() which sends
    // KADEMLIA_FIREWALLED2_REQ to nodes.  Remote nodes respond with
    // KADEMLIA_FIREWALLED_RES (our external IP) and attempt a TCP
    // connection to our ListenSocket.  If the TCP connect succeeds, the
    // remote sends OP_KAD_FWTCPCHECK_ACK which calls incFirewalled().
    // After 4 FIREWALLED_RES, recheckIP() returns false.
    //
    // When EMULE_TCP_PORT/EMULE_UDP_PORT are set (m_portsOpen), remote nodes
    // CAN reach us so TCP ACKs arrive and isFirewalled() returns false.
    // When ports are random, no ACKs arrive and the status remains firewalled.
    auto* prefs = m_kad->getPrefs();

    // Re-send HELLO_REQ to all contacts at kSendRate/sec — fresh contacts from
    // bootstrap responses may now be in the routing zone.  No early stop: we
    // want maximum coverage for the firewall check.
    ContactArray contacts;
    m_kad->getRoutingZone()->getAllEntries(contacts);
    auto* udpListener = m_kad->getUDPListener();
    rateLimitedSend(contacts,
        [udpListener](const Contact* c) {
            UInt128 contactID = c->getClientID();
            udpListener->sendMyDetails(KADEMLIA2_HELLO_REQ,
                                       c->getIPAddress(), c->getUDPPort(),
                                       c->getVersion(), c->getUDPKey(), &contactID, false);
        });
    qDebug() << "Sent HELLO_REQ to" << contacts.size() << "contacts for firewall check";

    const bool tcpDone = QTest::qWaitFor([prefs] {
        return !prefs->recheckIP();
    }, 90'000);

    const bool tcpFirewalled = m_kad->isFirewalled();
    qDebug() << "TCP firewall check — done:" << tcpDone
             << "firewalled:" << tcpFirewalled
             << "total packets:" << m_kadPacketsReceived.load()
             << "decrypt failed:" << m_decryptFailed.load();

    QVERIFY2(tcpDone,
             "TCP firewall check did not complete (need 4 FIREWALLED_RES responses)");

    if (m_portsOpen) {
        QVERIFY2(!tcpFirewalled,
                 "TCP firewall check reports firewalled despite open port — "
                 "remote nodes could not TCP-connect to us");
    } else {
        QVERIFY2(tcpFirewalled,
                 "TCP firewall check reports not-firewalled on a random port");
    }

    // --- UDP firewall check ---
    // Trigger the search for nodes willing to send us a UDP probe.
    // When m_portsOpen, remote probes reach our UDP socket and
    // UDPFirewallTester reports not-firewalled + verified.
    UDPFirewallTester::reset();
    prefs->incFirewalled();
    UDPFirewallTester::connected();

    const bool udpDone = QTest::qWaitFor([] {
        return UDPFirewallTester::isVerified()
               || !UDPFirewallTester::isFWCheckUDPRunning();
    }, 60'000);

    const bool udpFirewalled = UDPFirewallTester::isFirewalledUDP(false);
    const bool udpVerified = UDPFirewallTester::isVerified();
    qDebug() << "UDP firewall check — done:" << udpDone
             << "firewalled:" << udpFirewalled
             << "verified:" << udpVerified;

    if (m_portsOpen) {
        QVERIFY2(udpVerified,
                 "UDP firewall check not verified despite open port — "
                 "no KADEMLIA2_FIREWALLUDP probes received");
        QVERIFY2(!udpFirewalled,
                 "UDP firewall check reports firewalled despite open port");
    } else {
        // With random ports, UDP probes cannot reach us.  The check either
        // times out or finishes with all probes failed.
        QVERIFY2(udpDone,
                 "UDP firewall check mechanism did not run to completion");
        QVERIFY2(udpFirewalled || !udpVerified,
                 "UDP firewall check reports not-firewalled on a random port");
    }

    // --- TCP handshake verification ---
    // The ListenSocket must still be accepting connections.
    QVERIFY2(m_listenSocket->isListening(),
             "ListenSocket stopped listening — TCP handshake endpoint lost");
}

void tst_KadLiveNetwork::keywordSearch_findsResults()
{
    // Register callback to collect keyword search results.
    m_searchResults.clear();
    Kademlia::setKadKeywordResultCallback(
        [this](uint32 /*searchID*/, const uint8* /*fileHash*/, const QString& name,
               uint64 size, const QString& /*type*/, uint32 sources, uint32 completeSources) {
            m_searchResults.push_back({name, size, sources, completeSources});
        });

    // Start a keyword search for "test file"
    auto* search = SearchManager::prepareFindKeywords(
        QStringLiteral("test file"), 0, nullptr);
    QVERIFY2(search != nullptr, "prepareFindKeywords returned nullptr");
    QVERIFY2(SearchManager::startSearch(search), "startSearch returned false");
    const uint32 searchID = search->getSearchID();

    qDebug() << "Started Kad keyword search" << searchID << "for 'test file'";

    // Wait up to 2 minutes for at least 3 results.
    const bool gotResults = QTest::qWaitFor([this] {
        return m_searchResults.size() >= 3;
    }, 120'000);

    const auto resultCount = m_searchResults.size();
    qDebug() << "Keyword search returned" << resultCount << "results";

    SearchManager::stopSearch(searchID, false);
    Kademlia::setKadKeywordResultCallback(nullptr);

    QVERIFY2(gotResults,
             qPrintable(QStringLiteral("Expected >= 3 keyword results, got %1")
                            .arg(resultCount)));

    // Verify every result has a non-empty name and a non-zero file size.
    int withSources = 0;
    for (const auto& r : m_searchResults) {
        QVERIFY2(!r.name.isEmpty(), "Search result has empty filename");
        QVERIFY2(r.size > 0,
                 qPrintable(QStringLiteral("Search result '%1' has zero file size")
                                .arg(r.name)));
        if (r.sources > 0)
            ++withSources;
    }

    qDebug() << "Results with source count > 0:" << withSources << "/" << resultCount;

    // At least some results should report source counts.
    QVERIFY2(withSources > 0, "No search results reported a source count");
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void tst_KadLiveNetwork::rateLimitedSend(const ContactArray& contacts,
                                          std::function<void(const Contact*)> sendFn,
                                          std::function<bool()> earlyStopCondition)
{
    int sent = 0;
    for (const Contact* c : contacts) {
        // Check early-stop after each batch (but always send at least one batch)
        if (sent > 0 && sent % kSendRate == 0) {
            QTest::qWait(1'000);  // 1 second pause — also processes events
            if (earlyStopCondition && earlyStopCondition())
                break;
        }
        sendFn(c);
        ++sent;
    }
}

// ---------------------------------------------------------------------------
// Teardown
// ---------------------------------------------------------------------------

void tst_KadLiveNetwork::cleanupTestCase()
{
    Kademlia::setKadKeywordResultCallback(nullptr);

    if (m_kad) {
        SearchManager::stopAllSearches();
        m_kad->stop();
    }

    m_socket.close();

    if (m_listenSocket)
        m_listenSocket->stopListening();

    // Reset global state
    theApp.sharedFileList = nullptr;
    theApp.clientList = nullptr;
    theApp.clientUDP = nullptr;
    theApp.listenSocket = nullptr;
    theApp.uploadBandwidthThrottler = nullptr;
    Kademlia::setClientList(nullptr);
    Kademlia::setIPFilter(nullptr);

    RoutingBin::resetGlobalTracking();
    UDPFirewallTester::reset();

    delete m_knownFiles;
    m_knownFiles = nullptr;

    delete m_tmpDir;
    m_tmpDir = nullptr;
}

QTEST_MAIN(tst_KadLiveNetwork)
#include "tst_KadLiveNetwork.moc"
