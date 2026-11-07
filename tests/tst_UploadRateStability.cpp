/// @file tst_UploadRateStability.cpp
/// @brief The upload graph stays a flat line on a fast line with enough peers queued.
///
/// The real upload pipeline (UploadQueue, UploadBandwidthThrottler, disk IO, ListenSocket)
/// serves ~1.5x its slot count of downloader peers. Each peer sits behind its own simulated
/// link on a separate thread: bandwidth cap, latency + jitter, slow start and random
/// congestion / stall episodes. Sessions end and slots rotate as in the wild.
///
/// Opt-in, ~3 min per row:
///   EMULE_UPLOAD_STABILITY=1            run it
///   EMULE_UPLOAD_STABILITY_SEED=<n>     other random network (default fixed)
///   EMULE_UPLOAD_STABILITY_CSV=<file>   write the per-second series
///
/// One IP per peer (127.0.<row>.<10+i>): run `scripts/loopback-aliases.sh up` first on
/// macOS (lo0 has only 127.0.0.1), `down` to remove. Linux has all of 127/8 already.

#include "UploadPipelineFixture.h"
#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "crypto/MD4Hash.h"
#include "files/KnownFile.h"
#include "net/EMSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "transfer/UploadQueue.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"
#include "utils/TimeUtils.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <random>
#include <vector>

using namespace eMule;
using namespace eMule::testing;

namespace {

constexpr int kNetTickMs = 2;
constexpr qint64 kMss = 1460;
constexpr qint64 kInitCwnd = 10 * kMss;          // IW10
constexpr qint64 kRouterQueue = 64 * 1024;       // bottleneck buffer on top of the BDP
constexpr int kBackRcvBuf = 64 * 1024;           // proxy side of the uploader's socket
constexpr uint64 kSharedFileSize = 52ull * 20 * 1024 * 1024;   // ~1 GB: no peer sees a part 3x
constexpr uint64 kDefaultSeed = 20261107;

// ---------------------------------------------------------------------------
// Simulated access link of one remote peer
// ---------------------------------------------------------------------------

struct LinkProfile {
    double capBps = 0;     // downstream of the peer
    int oneWayMs = 0;
    int jitterMs = 0;
};

/// TCP relay peer <-> uploader. Downstream: rate cap x episode factor, cwnd-limited slow
/// start (restart after 1 s idle, halved after a stall), propagation delay + jitter, and
/// only cap x RTT + a router queue in flight — beyond that the uploader's socket backs up.
/// Upstream: delay only. Lives on the network thread.
class Link {
public:
    Link(const QHostAddress& bindAddr, quint16 uploaderPort, const LinkProfile& p, uint64 seed)
        : m_bindAddr(bindAddr), m_uploaderPort(uploaderPort), m_p(p), m_rng(seed)
    {
        const double rttSec = 2.0 * m_p.oneWayMs / 1000.0;
        m_window = static_cast<qint64>(m_p.capBps * rttSec) + kRouterQueue;
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] { onFrontConnection(); });
    }

    ~Link() { closeBoth(); }

    [[nodiscard]] quint16 frontPort() const { return m_server.serverPort(); }

    void tick(qint64 now)
    {
        const double dt = m_lastTick ? static_cast<double>(now - m_lastTick) / 1000.0 : 0.0;
        m_lastTick = now;
        updateEpisode(now);

        const bool backUp = m_back && m_back->state() == QAbstractSocket::ConnectedState;

        // upstream: requests, after the one-way delay
        while (backUp && !m_up.empty() && m_up.front().due <= now) {
            m_back->write(m_up.front().data);
            m_up.pop_front();
        }

        // downstream: take what the window allows
        if (backUp) {
            qint64 room = m_window - (m_queuedBytes + m_propBytes);
            while (room > 0 && m_back->bytesAvailable() > 0) {
                QByteArray d = m_back->read(std::min<qint64>(room, 16 * 1024));
                if (d.isEmpty())
                    break;
                room -= d.size();
                m_queuedBytes += d.size();
                m_queue.push_back(std::move(d));
            }
        }

        // bottleneck
        const double rttSec = 2.0 * m_p.oneWayMs / 1000.0;
        double rate = m_p.capBps * m_factor;
        rate = std::min(rate, static_cast<double>(m_cwnd) / rttSec);
        const double burst = std::max(rate * 0.01, static_cast<double>(kMss));
        m_tokens = std::min(m_tokens + rate * dt, burst);
        while (m_tokens >= 1.0 && !m_queue.empty()) {
            QByteArray& head = m_queue.front();
            const qint64 n = std::min<qint64>(head.size() - m_headOffset, static_cast<qint64>(m_tokens));
            const qint64 jitter = m_p.jitterMs ? std::uniform_int_distribution<int>(0, m_p.jitterMs)(m_rng) : 0;
            m_lastDue = std::max(m_lastDue, now + m_p.oneWayMs + jitter);
            m_prop.push_back({head.mid(m_headOffset, n), m_lastDue});
            m_propBytes += n;
            m_queuedBytes -= n;
            m_rttBytes += n;
            m_tokens -= static_cast<double>(n);
            m_headOffset += n;
            if (m_headOffset >= head.size()) {
                m_queue.pop_front();
                m_headOffset = 0;
            }
        }

        // slow start: double per RTT while the window is what limits
        if (now >= m_rttMark + static_cast<qint64>(rttSec * 1000.0)) {
            const qint64 capBdp = static_cast<qint64>(m_p.capBps * rttSec);
            if (m_rttBytes * 10 >= m_cwnd * 8 && m_cwnd < 2 * capBdp + kInitCwnd)
                m_cwnd *= 2;
            m_rttBytes = 0;
            m_rttMark = now;
        }

        // delivery
        const bool frontUp = m_front && m_front->state() == QAbstractSocket::ConnectedState;
        while (frontUp && !m_prop.empty() && m_prop.front().due <= now) {
            const qint64 n = m_prop.front().data.size();
            m_front->write(m_prop.front().data);
            m_propBytes -= n;
            m_prop.pop_front();
            delivered.fetch_add(static_cast<uint64>(n), std::memory_order_relaxed);
        }

        // slow-start restart after idle
        if (m_queue.empty() && m_prop.empty()) {
            if (m_idleSince == 0)
                m_idleSince = now;
            else if (now - m_idleSince > 1000)
                m_cwnd = kInitCwnd;
        } else {
            m_idleSince = 0;
        }
    }

    std::atomic<uint64> delivered{0};

private:
    struct Chunk {
        QByteArray data;
        qint64 due = 0;
    };

    void onFrontConnection()
    {
        QTcpSocket* s = m_server.nextPendingConnection();
        if (!s)
            return;
        if (m_front) {   // one peer connection at a time
            s->abort();
            s->deleteLater();
            return;
        }
        m_front = s;
        m_back = new QTcpSocket();
        m_back->setReadBufferSize(kBackRcvBuf);
        m_back->bind(m_bindAddr, 0);
        m_back->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, kBackRcvBuf);
        m_back->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_front->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        QObject::connect(m_front, &QTcpSocket::readyRead, m_front, [this] {
            m_up.push_back({m_front->readAll(), m_lastTick + m_p.oneWayMs});
        });
        QObject::connect(m_front, &QTcpSocket::disconnected, m_front, [this] { closeBoth(); },
                         Qt::QueuedConnection);
        QObject::connect(m_back, &QTcpSocket::disconnected, m_back, [this] { closeBoth(); },
                         Qt::QueuedConnection);
        QObject::connect(m_back, &QTcpSocket::errorOccurred, m_back, [this] { closeBoth(); },
                         Qt::QueuedConnection);
        m_back->connectToHost(QHostAddress::LocalHost, m_uploaderPort);
    }

    void closeBoth()
    {
        for (QTcpSocket** s : {&m_front, &m_back}) {
            if (*s) {
                (*s)->disconnect();
                (*s)->abort();
                (*s)->deleteLater();
                *s = nullptr;
            }
        }
        m_queue.clear();
        m_prop.clear();
        m_up.clear();
        m_queuedBytes = m_propBytes = 0;
        m_headOffset = 0;
        m_cwnd = kInitCwnd;
    }

    void updateEpisode(qint64 now)
    {
        if (m_nextEpisode == 0)
            m_nextEpisode = now + nextGapMs();
        if (m_episodeEnd != 0 && now >= m_episodeEnd) {
            if (m_factor == 0.0)   // a stall reads as loss
                m_cwnd = std::max(kInitCwnd, m_cwnd / 2);
            m_factor = 1.0;
            m_episodeEnd = 0;
            m_nextEpisode = now + nextGapMs();
        } else if (m_episodeEnd == 0 && now >= m_nextEpisode) {
            std::uniform_real_distribution<double> u(0.0, 1.0);
            if (u(m_rng) < 0.7) {   // congestion: someone else on the peer's line
                m_factor = 0.15 + 0.25 * u(m_rng);
                m_episodeEnd = now + 2000 + static_cast<qint64>(4000 * u(m_rng));
            } else {                // stall: loss + retransmit timeout
                m_factor = 0.0;
                m_episodeEnd = now + 300 + static_cast<qint64>(1200 * u(m_rng));
            }
        }
    }

    qint64 nextGapMs() { return 2000 + static_cast<qint64>(std::exponential_distribution<double>(1.0 / 20000.0)(m_rng)); }

    QHostAddress m_bindAddr;
    quint16 m_uploaderPort;
    LinkProfile m_p;
    std::mt19937_64 m_rng;
    QTcpServer m_server;
    QTcpSocket* m_front = nullptr;
    QTcpSocket* m_back = nullptr;

    qint64 m_window = 0;
    std::deque<QByteArray> m_queue;   // bottleneck queue
    qint64 m_headOffset = 0;
    qint64 m_queuedBytes = 0;
    std::deque<Chunk> m_prop;         // on the wire
    qint64 m_propBytes = 0;
    qint64 m_lastDue = 0;
    std::deque<Chunk> m_up;

    double m_tokens = 0;
    qint64 m_cwnd = kInitCwnd;
    qint64 m_rttMark = 0;
    qint64 m_rttBytes = 0;
    qint64 m_idleSince = 0;
    qint64 m_lastTick = 0;

    double m_factor = 1.0;
    qint64 m_episodeEnd = 0;
    qint64 m_nextEpisode = 0;
};

// ---------------------------------------------------------------------------
// Remote downloader
// ---------------------------------------------------------------------------

struct NetStats {
    std::atomic<int> accepts{0};
    std::atomic<int> sessionsEnded{0};
    std::atomic<int> reconnects{0};
    std::atomic<int> peerLeaves{0};
};

class PeerAgent;

class PeerSocket : public EMSocket {
public:
    explicit PeerSocket(PeerAgent* agent) : m_agent(agent) {}

protected:
    bool packetReceived(Packet* packet) override;
    void onError(int errorCode) override;

private:
    PeerAgent* m_agent;
};

/// eMule-like downloader: handshake, ask for the slot, keep three 180 KB blocks
/// requested while it holds one, walk the file from a random start. Reconnects 1-3 s
/// after losing the connection and asks again. Leaves a slot after ~45 s, as peers that
/// finish, cancel or go offline do: half send OP_END_OF_DOWNLOAD and ask again 5-15 s
/// later, half drop the connection. That keeps the uploader rotating slots.
class PeerAgent {
public:
    PeerAgent(int index, uint32 clientId, uint16 helloPort, quint16 frontPort,
              const std::array<uint8, 16>& fileHash, uint64 fileSize, NetStats& stats, uint64 seed)
        : m_index(index), m_clientId(clientId), m_helloPort(helloPort), m_frontPort(frontPort),
          m_fileHash(fileHash), m_fileSize(fileSize), m_stats(stats), m_rng(seed)
    {
        for (auto& b : m_userHash)
            b = static_cast<uint8>(m_rng());
        m_userHash[5] = 14;    // eMule client marks
        m_userHash[14] = 111;
        const uint64 blocks = m_fileSize / EMBLOCKSIZE;
        m_next = (m_rng() % (blocks / 2)) * EMBLOCKSIZE;
    }

    ~PeerAgent() { dropSocket(); }

    void scheduleConnect(qint64 now, qint64 delayMs) { m_connectAt = now + delayMs; }

    void poll(qint64 now)
    {
        if (m_state == State::Idle && m_connectAt != 0 && now >= m_connectAt) {
            m_connectAt = 0;
            connectNow(now);
        } else if (m_state == State::Connecting && m_sock) {
            if (m_sock->state() == QAbstractSocket::ConnectedState && m_sock->isEncryptionLayerReady()) {
                m_sock->sendPacket(buildHello());
                m_state = State::Hello;
            } else if (now - m_stateSince > 10'000) {
                lost(now);
            }
        } else if (m_state == State::Uploading && now >= m_leaveAt) {
            m_stats.peerLeaves.fetch_add(1, std::memory_order_relaxed);
            if (m_rng() % 2 == 0) {
                m_sock->sendPacket(buildHashPacket(OP_END_OF_DOWNLOAD));
                m_state = State::Queued;
                m_pending.clear();
                m_reaskAt = now + 5000 + static_cast<qint64>(m_rng() % 10'000);
            } else {
                lost(now);
            }
        } else if (m_state == State::Queued && m_reaskAt != 0 && now >= m_reaskAt) {
            m_reaskAt = 0;
            m_sock->sendPacket(buildHashPacket(OP_STARTUPLOADREQ));
        }
    }

    void onPacket(const Packet* p)
    {
        const uint8 op = p->opcode;
        const bool ed2k = p->prot == OP_EDONKEYPROT;
        const bool emule = p->prot == OP_EMULEPROT;
        if (ed2k && op == OP_HELLOANSWER && m_state == State::Hello) {
            m_sock->sendPacket(buildEmuleInfo());
            m_state = State::Info;
        } else if (emule && op == OP_EMULEINFOANSWER && m_state == State::Info) {
            m_sock->sendPacket(buildHashPacket(OP_SETREQFILEID));
            m_sock->sendPacket(buildHashPacket(OP_REQUESTFILENAME));
            m_state = State::FileReq;
        } else if (ed2k && op == OP_FILESTATUS && m_state == State::FileReq) {
            m_sock->sendPacket(buildHashPacket(OP_STARTUPLOADREQ));
            m_state = State::Queued;
        } else if (ed2k && op == OP_ACCEPTUPLOADREQ && m_state != State::Uploading) {
            m_state = State::Uploading;
            m_pending.clear();
            m_reaskAt = 0;
            m_leaveAt = QDateTime::currentMSecsSinceEpoch() + 10'000
                      + static_cast<qint64>(std::exponential_distribution<double>(1.0 / 35'000.0)(m_rng));
            m_stats.accepts.fetch_add(1, std::memory_order_relaxed);
            requestBlocks(3);
        } else if (ed2k && op == OP_OUTOFPARTREQS) {
            if (m_state == State::Uploading)
                m_stats.sessionsEnded.fetch_add(1, std::memory_order_relaxed);
            m_state = State::Queued;
            m_pending.clear();
        } else if (ed2k && op == OP_SENDINGPART && p->size > 24) {
            uint32 start = 0, end = 0;
            std::memcpy(&start, p->pBuffer + 16, 4);
            std::memcpy(&end, p->pBuffer + 20, 4);
            onData(start, end);
        } else if (emule && op == OP_SENDINGPART_I64 && p->size > 32) {
            uint64 start = 0, end = 0;
            std::memcpy(&start, p->pBuffer + 16, 8);
            std::memcpy(&end, p->pBuffer + 24, 8);
            onData(start, end);
        }
    }

    void lost(qint64 now)
    {
        if (m_state == State::Idle)
            return;
        dropSocket();
        m_state = State::Idle;
        m_pending.clear();
        m_reaskAt = 0;
        m_stats.reconnects.fetch_add(1, std::memory_order_relaxed);
        scheduleConnect(now, 1000 + static_cast<qint64>(m_rng() % 2000));
    }

    [[nodiscard]] bool uploading() const { return m_state == State::Uploading; }

private:
    enum class State { Idle, Connecting, Hello, Info, FileReq, Queued, Uploading };

    struct Block {
        uint64 start;
        uint64 end;
        uint64 got = 0;
    };

    void connectNow(qint64 now)
    {
        dropSocket();
        m_sock = new PeerSocket(this);
        m_sock->setObfuscationConfig(thePrefs.obfuscationConfig());
        m_sock->setConnectionEncryption(true, thePrefs.userHash().data(), false);
        QObject::connect(m_sock, &QAbstractSocket::disconnected, m_sock,
                         [this] { lost(QDateTime::currentMSecsSinceEpoch()); }, Qt::QueuedConnection);
        m_sock->connectToHost(QHostAddress::LocalHost, m_frontPort);
        m_state = State::Connecting;
        m_stateSince = now;
    }

    void dropSocket()
    {
        if (m_sock) {
            m_sock->disconnect();
            m_sock->abort();
            m_sock->deleteLater();
            m_sock = nullptr;
        }
    }

    void onData(uint64 start, uint64 end)
    {
        if (m_state != State::Uploading || end <= start)
            return;
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
            if (start >= it->start && start < it->end) {
                it->got += end - start;
                if (it->got >= it->end - it->start) {
                    m_pending.erase(it);
                    requestBlocks(1);
                }
                return;
            }
        }
    }

    void requestBlocks(int n)
    {
        std::array<uint64, 3> s{}, e{};
        for (int i = 0; i < n && i < 3; ++i) {
            if (m_next + EMBLOCKSIZE > m_fileSize)
                m_next = 0;
            s[i] = m_next;
            e[i] = m_next + EMBLOCKSIZE;
            m_pending.push_back({s[i], e[i]});
            m_next += EMBLOCKSIZE;
        }
        SafeMemFile data;
        data.writeHash16(m_fileHash.data());
        for (const uint64 v : s)
            data.writeUInt32(static_cast<uint32>(v));
        for (const uint64 v : e)
            data.writeUInt32(static_cast<uint32>(v));
        m_sock->sendPacket(std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_REQUESTPARTS));
    }

    std::unique_ptr<Packet> buildHashPacket(uint8 opcode) const
    {
        SafeMemFile data;
        data.writeHash16(m_fileHash.data());
        return std::make_unique<Packet>(data, OP_EDONKEYPROT, opcode);
    }

    std::unique_ptr<Packet> buildHello() const
    {
        SafeMemFile data;
        data.writeUInt8(0x10);
        data.writeHash16(m_userHash.data());
        data.writeUInt32(m_clientId);
        data.writeUInt16(m_helloPort);
        data.writeUInt32(6);
        Tag(CT_NAME, QStringLiteral("SimPeer%1").arg(m_index)).writeTagToFile(data);
        Tag(CT_VERSION, static_cast<uint32>(EDONKEYVERSION)).writeTagToFile(data);
        const uint32 udpPorts = (static_cast<uint32>(m_helloPort + 10) << 16) | (m_helloPort + 4);
        Tag(CT_EMULE_UDPPORTS, udpPorts).writeTagToFile(data);
        const uint32 misc1 = (1u << 29) | (1u << 28) | (4u << 24) | (1u << 20)
                           | (static_cast<uint32>(SOURCEEXCHANGE2_VERSION) << 12) | (2u << 8)
                           | (1u << 4) | (1u << 2) | (1u << 1);
        Tag(CT_EMULE_MISCOPTIONS1, misc1).writeTagToFile(data);
        const uint32 misc2 = static_cast<uint32>(KADEMLIA_VERSION) | (1u << 4) | (1u << 5) | (1u << 7)
                           | (1u << 8) | (1u << 10) | (1u << 13);
        Tag(CT_EMULE_MISCOPTIONS2, misc2).writeTagToFile(data);
        const uint32 ver = (static_cast<uint32>(SEND_EMULE_VERSION_MJR) << 17)
                         | (static_cast<uint32>(SEND_EMULE_VERSION_MIN) << 10)
                         | (static_cast<uint32>(SEND_EMULE_VERSION_UPD) << 7);
        Tag(CT_EMULE_VERSION, ver).writeTagToFile(data);
        data.writeUInt32(0);
        data.writeUInt16(0);
        return std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_HELLO);
    }

    static std::unique_ptr<Packet> buildEmuleInfo()
    {
        SafeMemFile data;
        data.writeUInt8((SEND_EMULE_VERSION_MJR << 4) | (SEND_EMULE_VERSION_MIN / 10));
        data.writeUInt8(EMULE_PROTOCOL);
        data.writeUInt32(6);
        Tag(static_cast<uint8>(ET_COMPRESSION), static_cast<uint32>(1)).writeTagToFile(data);
        Tag(static_cast<uint8>(ET_UDPVER), static_cast<uint32>(4)).writeTagToFile(data);
        Tag(static_cast<uint8>(ET_UDPPORT), static_cast<uint32>(0)).writeTagToFile(data);
        Tag(static_cast<uint8>(ET_SOURCEEXCHANGE), static_cast<uint32>(SOURCEEXCHANGE2_VERSION)).writeTagToFile(data);
        Tag(static_cast<uint8>(ET_COMMENTS), static_cast<uint32>(1)).writeTagToFile(data);
        Tag(static_cast<uint8>(ET_EXTENDEDREQUEST), static_cast<uint32>(2)).writeTagToFile(data);
        return std::make_unique<Packet>(data, OP_EMULEPROT, OP_EMULEINFO);
    }

    int m_index;
    uint32 m_clientId;
    uint16 m_helloPort;
    quint16 m_frontPort;
    std::array<uint8, 16> m_fileHash;
    uint64 m_fileSize;
    NetStats& m_stats;
    std::mt19937_64 m_rng;
    std::array<uint8, 16> m_userHash{};

    PeerSocket* m_sock = nullptr;
    State m_state = State::Idle;
    qint64 m_stateSince = 0;
    qint64 m_connectAt = 0;
    qint64 m_leaveAt = 0;
    qint64 m_reaskAt = 0;
    uint64 m_next = 0;
    std::vector<Block> m_pending;
};

bool PeerSocket::packetReceived(Packet* packet)
{
    m_agent->onPacket(packet);
    return true;
}

void PeerSocket::onError(int /*errorCode*/)
{
    // the agent reconnects; from the event loop, it deletes this socket
    QMetaObject::invokeMethod(this, [agent = m_agent] { agent->lost(QDateTime::currentMSecsSinceEpoch()); },
                              Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// The remote side: all links and peers, on their own thread
// ---------------------------------------------------------------------------

struct NetParams {
    int peers = 0;
    int ipBase = 0;            // 127.0.<ipBase>.<10+i>
    quint16 uploaderPort = 0;
    std::array<uint8, 16> fileHash{};
    uint64 fileSize = 0;
    uint64 seed = kDefaultSeed;
};

class NetSim {
public:
    /// Build on the calling (network) thread.
    void start(const NetParams& prm)
    {
        std::mt19937_64 rng(prm.seed);
        // Peer downstream: log-normal around 300 KB/s, 48 KB/s .. 2 MB/s
        std::lognormal_distribution<double> capDist(std::log(300.0 * 1024), 0.7);
        std::uniform_int_distribution<int> owdDist(10, 150);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (int i = 0; i < prm.peers; ++i) {
            LinkProfile lp;
            lp.capBps = std::clamp(capDist(rng), 48.0 * 1024, 2048.0 * 1024);
            lp.oneWayMs = owdDist(rng);
            lp.jitterMs = std::min(15, lp.oneWayMs / 10);
            const int last = 10 + i;
            const QHostAddress ip(QStringLiteral("127.0.%1.%2").arg(prm.ipBase).arg(last));
            m_links.push_back(std::make_unique<Link>(ip, prm.uploaderPort, lp, rng()));
            const uint32 clientId = (127u << 24) | (static_cast<uint32>(prm.ipBase) << 8) | static_cast<uint32>(last);
            m_agents.push_back(std::make_unique<PeerAgent>(i, clientId, static_cast<uint16>(21000 + i),
                                                           m_links.back()->frontPort(), prm.fileHash,
                                                           prm.fileSize, stats, rng()));
            m_agents.back()->scheduleConnect(now, 50 * i + static_cast<qint64>(rng() % 50));
            capSum += lp.capBps;
        }
        m_timer = std::make_unique<QTimer>();
        m_timer->setTimerType(Qt::PreciseTimer);
        QObject::connect(m_timer.get(), &QTimer::timeout, m_timer.get(), [this] { tick(); });
        m_timer->start(kNetTickMs);
    }

    void stop()
    {
        m_timer.reset();
        m_agents.clear();
        m_links.clear();
    }

    [[nodiscard]] uint64 delivered() const
    {
        uint64 sum = 0;
        for (const auto& l : m_links)
            sum += l->delivered.load(std::memory_order_relaxed);
        return sum;
    }

    NetStats stats;
    double capSum = 0;
    std::atomic<int> uploadingPeers{0};

private:
    void tick()
    {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        int up = 0;
        for (auto& a : m_agents) {
            a->poll(now);
            up += a->uploading() ? 1 : 0;
        }
        uploadingPeers.store(up, std::memory_order_relaxed);
        for (auto& l : m_links)
            l->tick(now);
    }

    std::vector<std::unique_ptr<Link>> m_links;
    std::vector<std::unique_ptr<PeerAgent>> m_agents;
    std::unique_ptr<QTimer> m_timer;
};

struct Sample {
    int sec;
    uint32 graph;       // UploadQueue::datarate(), what the graph plots
    uint32 sent1s;      // what the throttler sent in the last second
    uint32 delivered1s; // what reached the peers in the last second
    int slotCount;
    int waiting;
    int peersUploading;
};

struct SeriesStats {
    double mean = 0, min = 0, max = 0, sd = 0;
};

SeriesStats statsOf(const std::vector<double>& v)
{
    SeriesStats s;
    if (v.empty())
        return s;
    s.min = *std::ranges::min_element(v);
    s.max = *std::ranges::max_element(v);
    for (const double x : v)
        s.mean += x;
    s.mean /= static_cast<double>(v.size());
    for (const double x : v)
        s.sd += (x - s.mean) * (x - s.mean);
    s.sd = std::sqrt(s.sd / static_cast<double>(v.size()));
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class tst_UploadRateStability : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void flatGraphOnFastLine_data();
    void flatGraphOnFastLine();
    void cleanupTestCase();

private:
    void createSharedFile();

    UploadPipelineFixture m_pipe;
    bool m_enabled = false;
    std::array<uint8, 16> m_fileHash{};
    int m_row = 0;
};

void tst_UploadRateStability::initTestCase()
{
    m_enabled = qEnvironmentVariable("EMULE_UPLOAD_STABILITY") == QLatin1StringView("1");
    if (!m_enabled)
        QSKIP("opt-in, ~3 min per row: set EMULE_UPLOAD_STABILITY=1");

    // Each peer needs its own IP (3-per-IP gate). macOS has only 127.0.0.1 on lo0.
    for (const char* ip : {"127.0.1.10", "127.0.2.10"}) {
        QTcpSocket probe;
        QVERIFY2(probe.bind(QHostAddress(QString::fromLatin1(ip)), 0),
                 qPrintable(QStringLiteral("%1 not bindable: run scripts/loopback-aliases.sh up").arg(QLatin1String(ip))));
    }

    // core debug lines hit QTest's 2000-message cap and hide the row summaries
    QLoggingCategory::setFilterRules(QStringLiteral("emule.*.debug=false"));

    m_pipe.setup(this);
    m_pipe.processTimer.stop();   // it flushes sockets unthrottled; the throttler sends here
    createSharedFile();

    m_pipe.uploadQueue->setThrottler(m_pipe.throttler);
    m_pipe.throttler->setUploadQueue(m_pipe.uploadQueue);
    m_pipe.throttler->setDiskIOThread(m_pipe.diskIO);
    // EMSocket deregisters from the throttler through this global, see tst_MockPeerUpload
    theApp.uploadBandwidthThrottler = m_pipe.throttler;
}

void tst_UploadRateStability::createSharedFile()
{
    // The 20 MB random file over and over: incompressible, and big enough that no peer
    // is served a part a third time (RepeatChunkGuard bans that).
    const QString src = projectDataDir() + QStringLiteral("/incoming/eMuleQt-testfile-20MB-random.bin");
    QFile in(src);
    QVERIFY2(in.open(QIODevice::ReadOnly), qPrintable(src));
    const QByteArray chunk = in.readAll();
    QVERIFY(!chunk.isEmpty());

    const QString path = m_pipe.tmpDir->filePath(QStringLiteral("stability-1GB.bin"));
    QFile out(path);
    QVERIFY(out.open(QIODevice::WriteOnly));
    uint64 written = 0;
    while (written < kSharedFileSize) {
        const auto n = static_cast<qint64>(std::min<uint64>(chunk.size(), kSharedFileSize - written));
        QCOMPARE(out.write(chunk.constData(), n), n);
        written += static_cast<uint64>(n);
    }
    out.close();

    // Fake part hashes: the uploader never checks them, and hashing 1 GB would dominate
    std::mt19937_64 rng(1);
    std::vector<std::array<uint8, 16>> partHashes((kSharedFileSize + PARTSIZE - 1) / PARTSIZE);
    for (auto& h : partHashes)
        for (auto& b : h)
            b = static_cast<uint8>(rng());
    MD4Hasher fileHasher;
    for (const auto& h : partHashes)
        fileHasher.add(h.data(), 16);
    fileHasher.finish();
    std::memcpy(m_fileHash.data(), fileHasher.getHash(), 16);

    auto* known = new KnownFile();
    known->setFileName(QFileInfo(path).fileName());
    known->setFileSize(EMFileSize(kSharedFileSize));
    known->setFileHash(m_fileHash.data());
    known->setFilePath(path);
    known->setPath(QFileInfo(path).absolutePath() + QDir::separator());
    known->fileIdentifier().setMD4HashSet(partHashes);
    QVERIFY(m_pipe.knownFiles->safeAddKFile(known));
    QVERIFY(m_pipe.sharedFiles->safeAddKFile(known));
}

void tst_UploadRateStability::flatGraphOnFastLine_data()
{
    QTest::addColumn<int>("limitKB");
    QTest::addColumn<int>("targetKB");
    QTest::addColumn<int>("measureSec");

    QTest::newRow("6MB-128KB") << 6144 << 128 << 120;
    QTest::newRow("6MB-256KB") << 6144 << 256 << 120;
}

void tst_UploadRateStability::flatGraphOnFastLine()
{
    QFETCH(int, limitKB);
    QFETCH(int, targetKB);
    QFETCH(int, measureSec);

    thePrefs.setDynUpEnabled(false);
    thePrefs.setMaxUpload(static_cast<uint32>(limitKB));
    thePrefs.setMaxGraphUploadRate(static_cast<uint32>(limitKB));
    thePrefs.setUploadSlotTargetRate(static_cast<uint32>(targetKB));
    thePrefs.setUploadUnderfillSlots(false);

    const double limit = limitKB * 1024.0;
    const int ladder = static_cast<int>(m_pipe.uploadQueue->ladderSlotCount(0));
    const int peers = ladder * 3 / 2;   // enough left waiting to rotate slots

    NetParams prm;
    prm.peers = peers;
    prm.ipBase = ++m_row;
    prm.uploaderPort = m_pipe.listenSocket->connectedPort();
    prm.fileHash = m_fileHash;
    prm.fileSize = kSharedFileSize;
    bool seedOk = false;
    const qulonglong envSeed = qEnvironmentVariable("EMULE_UPLOAD_STABILITY_SEED").toULongLong(&seedOk);
    prm.seed = (seedOk ? envSeed : kDefaultSeed) + static_cast<uint64>(m_row);

    // Remote world on its own thread
    QThread netThread;
    QObject anchor;
    anchor.moveToThread(&netThread);
    netThread.start();
    auto sim = std::make_unique<NetSim>();
    QMetaObject::invokeMethod(&anchor, [&] { sim->start(prm); }, Qt::BlockingQueuedConnection);
    const auto stopSim = qScopeGuard([&] {
        QMetaObject::invokeMethod(&anchor, [&] { sim->stop(); }, Qt::BlockingQueuedConnection);
        netThread.quit();
        netThread.wait();
    });
    qInfo("%d peers, %d slots by the ladder, peer capacity sum %.0f KB/s, seed %llu", peers, ladder,
          sim->capSum / 1024.0, static_cast<unsigned long long>(prm.seed));

    // CoreSession's cadence: queue every 100 ms, sockets and clients every second
    int tick = 0;
    QTimer process;
    process.setTimerType(Qt::PreciseTimer);
    connect(&process, &QTimer::timeout, this, [&] {
        m_pipe.uploadQueue->process();
        if (++tick % 10 == 0) {
            m_pipe.listenSocket->process();
            m_pipe.clientList->process();
        }
    });
    process.start(100);

    // Warm-up: the line fills, then the graph's 30 s window is all "full" time
    QElapsedTimer clock;
    clock.start();
    qint64 fullSince = -1;
    while (clock.elapsed() < 120'000) {
        QTest::qWait(500);
        if (m_pipe.uploadQueue->datarate() >= limit * 0.9) {
            if (fullSince < 0)
                fullSince = clock.elapsed();
        } else {
            fullSince = -1;
        }
        if (fullSince >= 0 && clock.elapsed() - fullSince >= 30'000)
            break;
    }
    const qint64 warmupMs = clock.elapsed();
    qInfo("warm-up %lld ms, %d slots, %d waiting", static_cast<long long>(warmupMs),
          m_pipe.uploadQueue->uploadQueueLength(), m_pipe.uploadQueue->waitingUserCount());
    QVERIFY2(fullSince >= 0, qPrintable(QStringLiteral("line never filled: %1 KB/s of %2 after %3 s, %4 slots")
                                            .arg(m_pipe.uploadQueue->datarate() / 1024)
                                            .arg(limitKB).arg(warmupMs / 1000)
                                            .arg(m_pipe.uploadQueue->uploadQueueLength())));

    // Measure, once a second
    std::vector<Sample> samples;
    const int accepts0 = sim->stats.accepts.load();
    const int ended0 = sim->stats.sessionsEnded.load();
    const int reconnects0 = sim->stats.reconnects.load();
    const int leaves0 = sim->stats.peerLeaves.load();
    uint64 lastDelivered = sim->delivered();
    const qint64 t0 = clock.elapsed();
    for (int sec = 1; sec <= measureSec; ++sec) {
        while (clock.elapsed() < t0 + sec * 1000LL)
            QTest::qWait(static_cast<int>(std::max<qint64>(1, t0 + sec * 1000LL - clock.elapsed())));
        const uint64 delivered = sim->delivered();
        samples.push_back({sec, m_pipe.uploadQueue->datarate(), m_pipe.uploadQueue->datarateOver(1000),
                           static_cast<uint32>(delivered - lastDelivered),
                           m_pipe.uploadQueue->uploadQueueLength(), m_pipe.uploadQueue->waitingUserCount(),
                           sim->uploadingPeers.load()});
        lastDelivered = delivered;
    }
    process.stop();

    // Report
    std::vector<double> graph, sent, deliv, slotSeries;
    for (const Sample& s : samples) {
        graph.push_back(s.graph / limit);
        sent.push_back(s.sent1s / limit);
        deliv.push_back(s.delivered1s / limit);
        slotSeries.push_back(s.slotCount);
    }
    const SeriesStats g = statsOf(graph), s1 = statsOf(sent), d1 = statsOf(deliv), sl = statsOf(slotSeries);
    const auto low1s = std::ranges::count_if(sent, [](double x) { return x < 0.85; });
    qInfo("graph (30 s avg) of limit: mean %.3f min %.3f max %.3f sd %.4f", g.mean, g.min, g.max, g.sd);
    qInfo("sent 1 s of limit:         mean %.3f min %.3f max %.3f sd %.4f, %lld s below 85%%",
          s1.mean, s1.min, s1.max, s1.sd, static_cast<long long>(low1s));
    qInfo("delivered 1 s of limit:    mean %.3f min %.3f max %.3f sd %.4f", d1.mean, d1.min, d1.max, d1.sd);
    const int slotStarts = sim->stats.accepts.load() - accepts0;
    qInfo("slots: mean %.1f min %.0f max %.0f; slot starts %d, sessions ended %d, peers left %d, reconnects %d",
          sl.mean, sl.min, sl.max, slotStarts, sim->stats.sessionsEnded.load() - ended0,
          sim->stats.peerLeaves.load() - leaves0, sim->stats.reconnects.load() - reconnects0);

    if (const QString csv = qEnvironmentVariable("EMULE_UPLOAD_STABILITY_CSV"); !csv.isEmpty()) {
        QFile f(QStringLiteral("%1.%2.csv").arg(csv, QString::fromLatin1(QTest::currentDataTag())));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            f.write("sec,graphKB,sent1sKB,delivered1sKB,slots,waiting,peersUploading\n");
            for (const Sample& s : samples)
                f.write(QStringLiteral("%1,%2,%3,%4,%5,%6,%7\n").arg(s.sec).arg(s.graph / 1024)
                            .arg(s.sent1s / 1024).arg(s.delivered1s / 1024).arg(s.slotCount)
                            .arg(s.waiting).arg(s.peersUploading).toLatin1());
        }
    }

    // The point is a flat line *while slots rotate*
    QVERIFY2(slotStarts >= ladder, qPrintable(QStringLiteral("only %1 slot starts").arg(slotStarts)));

    // Flat line: the graph stays near the limit and does not swing
    QVERIFY2(g.min >= 0.90, qPrintable(QStringLiteral("graph dropped to %1% of the limit").arg(g.min * 100, 0, 'f', 1)));
    QVERIFY2(g.max <= 1.05, qPrintable(QStringLiteral("graph overshot to %1% of the limit").arg(g.max * 100, 0, 'f', 1)));
    QVERIFY2(g.max - g.min <= 0.06,
             qPrintable(QStringLiteral("graph swings %1% of the limit").arg((g.max - g.min) * 100, 0, 'f', 1)));
    // Second by second: no dips worth seeing
    QVERIFY2(s1.mean >= 0.93, qPrintable(QStringLiteral("1 s mean %1%").arg(s1.mean * 100, 0, 'f', 1)));
    QVERIFY2(s1.min >= 0.70, qPrintable(QStringLiteral("1 s rate dipped to %1%").arg(s1.min * 100, 0, 'f', 1)));
    QVERIFY2(low1s * 20 <= static_cast<qsizetype>(sent.size()),
             qPrintable(QStringLiteral("%1 of %2 s below 85%").arg(low1s).arg(sent.size())));
    QVERIFY2(s1.sd <= 0.05, qPrintable(QStringLiteral("1 s rate sd %1%").arg(s1.sd * 100, 0, 'f', 1)));
}

void tst_UploadRateStability::cleanupTestCase()
{
    if (!m_enabled)
        return;
    // Let the queue see the disconnects before the throttler goes
    for (int i = 0; i < 20; ++i) {
        m_pipe.uploadQueue->process();
        QTest::qWait(50);
    }
    m_pipe.uploadQueue->setThrottler(nullptr);
    m_pipe.throttler->endThread();
    theApp.uploadBandwidthThrottler = nullptr;
    m_pipe.teardown();
}

QTEST_GUILESS_MAIN(tst_UploadRateStability)
#include "tst_UploadRateStability.moc"
