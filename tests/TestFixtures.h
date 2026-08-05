#pragma once

/// @file TestFixtures.h
/// @brief RAII fixtures for the process-global core state a protocol test has to stand up:
///        the Kademlia singleton, the public-IPv6 tiers and lab-network mode.
///
/// Deliberately separate from TestHelpers.h, which nearly every test target includes —
/// these pull in Kad, AppContext and Preferences, and a change here should not rebuild
/// the whole suite.

#include "app/AppContext.h"
#include "client/UpDownClient.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/Kademlia.h"
#include "net/Address.h"
#include "net/ClientReqSocket.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <cstring>
#include <memory>
#include <vector>

namespace eMule::testing {

/// What Kademlia::instance() should report for the lifetime of a KadFixture.
enum class KadMode {
    Stopped,     ///< instance() non-null but isRunning() false — constructed, never started
    Firewalled,  ///< running, isFirewalled() true — the default state of fresh KadPrefs
    Open,        ///< running, isFirewalled() false
};

/// A Kademlia singleton backed by a throwaway config directory.
///
/// kad::Kademlia binds s_instance in its *constructor* and clears it in the destructor, and
/// there is no setter — so simply having one of these alive IS the singleton. start() is what
/// flips isRunning(); it binds no socket (CoreSession does that externally), it only creates a
/// KademliaUDPListener, an Indexed, a RoutingZone and the 1 s process timer.
///
/// Two different directories are in play and both are redirected here: KadPrefs takes its own
/// path, while Kademlia::start() derives the nodes.dat path from thePrefs.configDir(). Leave
/// the latter alone and a test writes nodes.dat into QDir::tempPath(), where the next test
/// binary reads it back.
///
/// Only one may be alive at a time — a second would overwrite s_instance and the first
/// destructor would then clear it out from under the second.
class KadFixture {
public:
    explicit KadFixture(KadMode mode = KadMode::Firewalled)
        : m_savedConfigDir(thePrefs.configDir())
    {
        thePrefs.setConfigDir(m_dir.path());   // must precede start()
        m_prefs = std::make_unique<kad::KadPrefs>(m_dir.path());
        if (mode != KadMode::Stopped)
            m_kad.start(m_prefs.get());        // borrows the prefs, does not own them
        if (mode == KadMode::Open) {
            // KadPrefs::firewalled() is counter-driven and stays true until two independent
            // confirmations land. setFirewalled() is the *reset* — it snapshots and zeroes
            // the counter — so calling it here would do the opposite of what it reads like.
            m_prefs->incFirewalled();
            m_prefs->incFirewalled();
        }
    }

    ~KadFixture()
    {
        // stop() destroys the RoutingZone, whose destructor writes nodes.dat — that has to
        // happen while the temp dir still exists and configDir still points into it.
        m_kad.stop();
        thePrefs.setConfigDir(m_savedConfigDir);
    }

    KadFixture(const KadFixture&) = delete;
    KadFixture& operator=(const KadFixture&) = delete;

    [[nodiscard]] kad::Kademlia& kad() { return m_kad; }
    [[nodiscard]] kad::KadPrefs& kadPrefs() { return *m_prefs; }
    [[nodiscard]] QString configDir() const { return m_dir.path(); }

private:
    QTemporaryDir m_dir;
    QString m_savedConfigDir;
    /// Declared before m_kad so it outlives it: start() borrowed the raw pointer.
    std::unique_ptr<kad::KadPrefs> m_prefs;
    kad::Kademlia m_kad;
};

/// Publishes @p addr as our public IPv6 and unwinds every global it touched.
///
/// All four tiers, the status byte, the local-interface set and the override preference live
/// on theApp/thePrefs and leak into whatever test runs next, so nothing here may be left
/// behind — tst_SourceExchange.cpp:1094 hand-rolls the same teardown.
class IPv6AdvertiseGuard {
public:
    /// A genuine global-unicast address. 2001:db8::/32 is documentation space and
    /// Address::isPublicIP() rejects it unless lab mode is on, so it cannot stand in here.
    [[nodiscard]] static Address testPublicIPv6()
    {
        return Address::fromString(QStringLiteral("2606:4700::100"));
    }

    explicit IPv6AdvertiseGuard(const Address& addr = testPublicIPv6())
        : m_savedOverridePref(thePrefs.publicIPv6Override())
        , m_savedStatus(theApp.publicIPv6Status())
        , m_savedLocal(theApp.publicIPv6Local())
        , m_savedOverride(theApp.publicIPv6Override())
    {
        thePrefs.setPublicIPv6Override(QString());
        theApp.clearPublicIPv6Observed();      // tier 1 + peer votes + status byte
        theApp.setPublicIPv6Override(Address{});
        theApp.setPublicIPv6Status(0);         // 0 = unknown, which is *not* probed-unreachable
        if (addr.isNull())
            theApp.setLocalIPv6Addresses({});
        else
            theApp.setLocalIPv6Addresses({addr});
        theApp.setPublicIPv6Local(addr);       // tier 4
    }

    ~IPv6AdvertiseGuard()
    {
        theApp.onPublicIPv6Changed = {};       // drop any hook installed under us
        theApp.clearPublicIPv6Observed();
        theApp.setPublicIPv6Status(m_savedStatus);
        theApp.setPublicIPv6Override(m_savedOverride);
        theApp.setPublicIPv6Local(m_savedLocal);
        theApp.setLocalIPv6Addresses({});
        thePrefs.setPublicIPv6Override(m_savedOverridePref);
    }

    IPv6AdvertiseGuard(const IPv6AdvertiseGuard&) = delete;
    IPv6AdvertiseGuard& operator=(const IPv6AdvertiseGuard&) = delete;

    /// A server dial-back probed our address and it failed: we still know the address, but
    /// must not publish it. shouldAdvertisePublicIPv6() == confident && !probedUnreachable.
    void setProbedUnreachable() { theApp.setPublicIPv6Status(IPV6ST_HAVE | IPV6ST_PROBED); }
    void setProbedReachable()
    {
        theApp.setPublicIPv6Status(IPV6ST_HAVE | IPV6ST_PROBED | IPV6ST_REACHABLE);
    }

private:
    QString m_savedOverridePref;
    uint8 m_savedStatus;
    Address m_savedLocal;
    Address m_savedOverride;
};

/// Both halves of "this is a private test network": Address::labNetworkMode() for IPv6 and
/// thePrefs.filterLANIPs() for IPv4. CoreSession::initLocalIPv6() derives the former from the
/// latter, so a test that needs a loopback or ULA peer accepted has to set both by hand.
/// Wraps the existing ScopedLabNetworkMode rather than re-rolling its save/restore.
class LabModeGuard {
public:
    explicit LabModeGuard(bool enabled)
        : m_lab(enabled)
        , m_savedFilter(thePrefs.filterLANIPs())
    {
        thePrefs.setFilterLANIPs(!enabled);
    }
    ~LabModeGuard() { thePrefs.setFilterLANIPs(m_savedFilter); }

    LabModeGuard(const LabModeGuard&) = delete;
    LabModeGuard& operator=(const LabModeGuard&) = delete;

private:
    ScopedLabNetworkMode m_lab;
    bool m_savedFilter;
};

/// Connect a ClientReqSocket to a throwaway local server so the client counts as connected
/// and sendPacket() has somewhere to write; returns the accepted server-side socket to read
/// from, or nullptr on failure.
///
/// UpDownClient::setSocket() is a plain pointer assignment — the client never owns its
/// socket — so unwind with `client.setSocket(nullptr)` before `server` leaves scope. The
/// ClientReqSocket itself is heap-allocated and deliberately left to the process exit,
/// which is what the per-test copies of this helper always did.
inline QTcpSocket* wireLoopbackSocket(QTcpServer& server, UpDownClient& client)
{
    if (!server.listen(QHostAddress::LocalHost, 0))
        return nullptr;
    auto* sock = new ClientReqSocket();
    sock->connectToHost(QHostAddress::LocalHost, server.serverPort());
    if (!sock->waitForConnected(5000) || !server.waitForNewConnection(5000)) {
        delete sock;
        return nullptr;
    }
    client.setSocket(sock);
    return server.nextPendingConnection();
}

/// Wait until at least @p want bytes are queued on @p peer.
///
/// The event loop has to spin: EMSocket::sendPacket() queues control packets and flushes
/// them from a QTimer::singleShot(0), so a bare waitForReadyRead() times out with the
/// packet still sitting in the queue.
inline bool waitForBytes(QTcpSocket* peer, qint64 want, int ms = 2000)
{
    QDeadlineTimer deadline(ms);
    while (peer->bytesAvailable() < want && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        peer->waitForReadyRead(10);
    }
    return peer->bytesAvailable() >= want;
}

/// Feed @p client a minimal hello that advertises MODMISC_IPV6.
///
/// UpDownClient::m_supportsIPv6 has no setter — the CT_MOD_MISCOPTIONS branch of hello parsing
/// (UpDownClient.cpp:946) is the only way to set it.
inline void feedIPv6CapableHello(UpDownClient& client, uint8 hashByte = 0xD1)
{
    SafeMemFile data;
    data.writeUInt8(16);
    uint8 hash[16];
    std::memset(hash, hashByte, sizeof(hash));
    data.writeHash16(hash);
    data.writeUInt32(0);        // LowID — nothing here dials out
    data.writeUInt16(4662);
    data.writeUInt32(2);        // tag count
    Tag(CT_VERSION, static_cast<uint32>(EDONKEYVERSION)).writeTagToFile(data);
    Tag(CT_MOD_MISCOPTIONS, static_cast<uint32>(MODMISC_IPV6)).writeTagToFile(data);
    data.writeUInt32(0);        // server IP
    data.writeUInt16(0);        // server port

    const auto& buf = data.buffer();
    client.processHelloPacket(reinterpret_cast<const uint8*>(buf.constData()),
                              static_cast<uint32>(buf.size()));
}

/// Feed @p client a minimal OP_EMULEINFO that makes supportsUDP() true.
///
/// m_udpVer has no setter either — ET_UDPVER inside a mule-info packet is the only way in
/// (UpDownClient.cpp:1374), and without it a source is never considered for a UDP re-ask.
inline void feedMuleInfoUDP(UpDownClient& client, uint16 udpPort, uint8 udpVer = 4)
{
    std::vector<Tag> tags;
    tags.emplace_back(ET_COMPRESSION, static_cast<uint32>(1));
    tags.emplace_back(ET_UDPVER, static_cast<uint32>(udpVer));
    tags.emplace_back(ET_UDPPORT, static_cast<uint32>(udpPort));

    SafeMemFile data;
    data.writeUInt8(0x01);                  // eMule protocol version
    data.writeUInt8(EMULE_PROTOCOL);
    data.writeUInt32(static_cast<uint32>(tags.size()));
    for (const auto& tag : tags)
        tag.writeTagToFile(data);

    const auto& buf = data.buffer();
    client.processMuleInfoPacket(reinterpret_cast<const uint8*>(buf.constData()),
                                 static_cast<uint32>(buf.size()));
}

} // namespace eMule::testing
