/// @file UploadQueueStore.cpp
/// @brief Upload Queue Storage (UQS) — see UploadQueueStore.h.

#include "pch.h"

#include "transfer/UploadQueueStore.h"

#include "app/AppContext.h"
#include "client/ClientCredits.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/SharedFileList.h"
#include "friends/FriendList.h"
#include "net/PeerVetting.h"
#include "prefs/Preferences.h"
#include "transfer/UploadQueue.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"
#include "utils/TimeUtils.h"

#include <QFile>

#include <algorithm>
#include <ctime>

namespace eMule {

namespace {

/// Expiry is MAX_PURGEQUEUETIME (1 h) — deliberately the same constant
/// findBestClientInQueue() already purges on, so a record we accept is one the queue would
/// have kept anyway, and one that goes stale later is dropped by machinery that exists.
constexpr uint32 kQueueExpirySeconds = MAX_PURGEQUEUETIME / 1000;

[[nodiscard]] uint32 nowUnix()
{
    return static_cast<uint32>(std::time(nullptr));
}

} // namespace

// ---------------------------------------------------------------------------
// QueuedClientRecord
// ---------------------------------------------------------------------------

bool QueuedClientRecord::isRestorable() const
{
    // No hash means no credits (so no queue position), no dedup key, and no obfuscation
    // key — the record would restore a client strictly worse than one the peer creates
    // itself by re-asking.
    if (isnulmd4(userHash.data()))
        return false;
    if (isnulmd4(reqUpFileId.data()))
        return false;
    if (userPort == 0)
        return false;

    // Something to match or dial: a direct address, or a LowID we can reach through its
    // server or a Kad callback.
    if (!userIPv4.isNull() || !userIPv6.isNull())
        return true;
    return userIDHybrid != 0 && ((serverIP != 0 && serverPort != 0) || kadPort != 0);
}

// ---------------------------------------------------------------------------
// UploadQueueFile
// ---------------------------------------------------------------------------

UploadQueueFile::Contents UploadQueueFile::read(const QString& path)
{
    Contents out;

    if (!QFile::exists(path))
        return out;   // fresh install — not an error, and not worth a log line

    try {
        SafeFile file;
        if (!file.open(path, QIODevice::ReadOnly)) {
            logWarning(QStringLiteral("uploadqueue.met: cannot open %1").arg(path));
            return out;
        }

        const uint8 version = file.readUInt8();
        if (version != kUploadQueueFileVersion) {
            logWarning(QStringLiteral("uploadqueue.met: unsupported version %1 (expected %2) — ignoring")
                           .arg(version).arg(kUploadQueueFileVersion));
            return out;
        }

        const uint32 savedAtUnix = file.readUInt32();
        const uint16 count       = file.readUInt16();

        std::vector<QueuedClientRecord> records;
        records.reserve(count);

        for (uint16 i = 0; i < count; ++i) {
            QueuedClientRecord rec;
            file.readHash16(rec.userHash.data());
            file.readHash16(rec.reqUpFileId.data());

            rec.userIDHybrid = file.readUInt32();

            const uint32 v4 = file.readUInt32();
            if (v4 != 0)
                rec.userIPv4 = Address::fromNetworkOrder(v4);

            std::array<uint8, 16> v6{};
            file.read(v6.data(), static_cast<qint64>(v6.size()));
            if (std::any_of(v6.begin(), v6.end(), [](uint8 b) { return b != 0; }))
                rec.userIPv6 = Address::fromIPv6Bytes(v6.data());

            rec.userPort       = file.readUInt16();
            rec.serverIP       = file.readUInt32();
            rec.serverPort     = file.readUInt16();
            rec.kadPort        = file.readUInt16();
            rec.udpPort        = file.readUInt16();
            rec.connectOptions = file.readUInt8();
            rec.kadVersion     = file.readUInt8();
            rec.udpVer         = file.readUInt8();

            rec.waitedSeconds           = file.readUInt32();
            rec.sinceLastRequestSeconds = file.readUInt32();
            rec.askedCount              = file.readUInt32();

            rec.userName         = file.readString(true);
            rec.clientVersion    = file.readUInt32();
            rec.emuleVersion     = file.readUInt8();
            rec.compatibleClient = file.readUInt8();

            records.push_back(std::move(rec));
        }

        out.records    = std::move(records);
        out.savedAtUnix = savedAtUnix;
    } catch (const std::exception& e) {
        // A truncated or corrupt file yields nothing rather than a half-decoded queue:
        // records are accumulated locally and only published on a clean pass.
        logWarning(QStringLiteral("uploadqueue.met: read failed (%1) — ignoring the file")
                       .arg(QString::fromUtf8(e.what())));
        return {};
    }

    return out;
}

bool UploadQueueFile::write(const QString& path,
                            const std::vector<QueuedClientRecord>& records,
                            uint32 savedAtUnix)
{
    const QString tmpPath = path + QStringLiteral(".tmp");
    const QString bakPath = path + QStringLiteral(".bak");

    try {
        QFile::remove(tmpPath);

        {
            SafeFile file;
            if (!file.open(tmpPath, QIODevice::WriteOnly)) {
                logWarning(QStringLiteral("uploadqueue.met: cannot write %1").arg(tmpPath));
                return false;
            }

            file.writeUInt8(kUploadQueueFileVersion);
            file.writeUInt32(savedAtUnix);
            file.writeUInt16(static_cast<uint16>(records.size()));

            for (const QueuedClientRecord& rec : records) {
                file.writeHash16(rec.userHash.data());
                file.writeHash16(rec.reqUpFileId.data());

                file.writeUInt32(rec.userIDHybrid);
                file.writeUInt32(rec.userIPv4.isIPv4() ? rec.userIPv4.toNetworkUint32() : 0);

                const std::array<uint8, 16> zero{};
                const std::array<uint8, 16>& v6 =
                    rec.userIPv6.isIPv6() ? rec.userIPv6.ipv6Bytes() : zero;
                file.write(v6.data(), static_cast<qint64>(v6.size()));

                file.writeUInt16(rec.userPort);
                file.writeUInt32(rec.serverIP);
                file.writeUInt16(rec.serverPort);
                file.writeUInt16(rec.kadPort);
                file.writeUInt16(rec.udpPort);
                file.writeUInt8(rec.connectOptions);
                file.writeUInt8(rec.kadVersion);
                file.writeUInt8(rec.udpVer);

                file.writeUInt32(rec.waitedSeconds);
                file.writeUInt32(rec.sinceLastRequestSeconds);
                file.writeUInt32(rec.askedCount);

                file.writeString(rec.userName, UTF8Mode::Raw);
                file.writeUInt32(rec.clientVersion);
                file.writeUInt8(rec.emuleVersion);
                file.writeUInt8(rec.compatibleClient);
            }
        } // closed before rename

        QFile::remove(bakPath);
        if (QFile::exists(path)) {
            if (!QFile::rename(path, bakPath))
                QFile::remove(path);
        }

        if (!QFile::rename(tmpPath, path)) {
            logError(QStringLiteral("uploadqueue.met: failed to rename tmp → final"));
            if (QFile::exists(bakPath))
                QFile::rename(bakPath, path);
            return false;
        }
    } catch (const std::exception& e) {
        logError(QStringLiteral("uploadqueue.met: save failed (%1)")
                     .arg(QString::fromUtf8(e.what())));
        QFile::remove(tmpPath);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// UploadQueueStore — driver
// ---------------------------------------------------------------------------

void UploadQueueStore::process(UploadQueue* queue, const QString& path)
{
    if (!queue || path.isEmpty() || !thePrefs.rememberUploadQueue())
        return;

    if (!m_loaded) {
        // Gate: an ED2K server *or* Kad, which is exactly what theApp.isConnected() means.
        if (!theApp.isConnected())
            return;

        // Flips m_loaded itself, so the save timer below only ever runs post-restore.
        static_cast<void>(loadAndInject(queue, path));
        return;
    }

    const auto now = static_cast<uint32>(getTickCount());
    if (now - m_lastSaved > kQueueResaveTimeMs) {
        m_lastSaved = now;
        static_cast<void>(save(queue, path));
    }
}

bool UploadQueueStore::saveNow(UploadQueue* queue, const QString& path)
{
    if (!queue || path.isEmpty() || !thePrefs.rememberUploadQueue())
        return false;

    // Refusing before the load is the whole point: a daemon that is killed while still
    // offline would otherwise write its empty queue over a perfectly good file.
    if (!m_loaded)
        return false;

    return save(queue, path);
}

int UploadQueueStore::loadAndInject(UploadQueue* queue, const QString& path)
{
    if (!queue)
        return 0;

    // Marked before any early return below: a missing, expired or unreadable file still
    // counts as "the load has happened". Until this flips, saving is refused outright —
    // otherwise the first autosave would write our empty queue over the stored one.
    m_loaded    = true;
    m_lastSaved = static_cast<uint32>(getTickCount());

    const UploadQueueFile::Contents contents = UploadQueueFile::read(path);
    if (contents.records.empty())
        return 0;

    // Wall-clock gap since the file was written. Clamped because a backwards clock step
    // must not read as "saved in the future" and revive an ancient queue.
    const uint32 now     = nowUnix();
    const uint32 offline = (now > contents.savedAtUnix) ? (now - contents.savedAtUnix) : 0;

    if (offline >= kQueueExpirySeconds) {
        logInfo(QStringLiteral("Upload queue: stored queue is %1 min old — discarded")
                    .arg(offline / 60));
        return 0;
    }

    auto* clients = theApp.clientList;
    auto* shared  = theApp.sharedFileList;
    if (!clients || !shared)
        return 0;

    int added = 0;
    const auto tickNow = static_cast<uint32>(getTickCount());

    for (const QueuedClientRecord& rec : contents.records) {
        if (!rec.isRestorable())
            continue;

        const uint32 sinceRequest = rec.sinceLastRequestSeconds + offline;
        if (sinceRequest >= kQueueExpirySeconds)
            continue;   // findBestClientInQueue() would purge it on the first slot decision

        // The file is untrusted input — it may predate an IP filter update, or have been
        // written before this peer was banned.
        const Address v4 = vetPeerAddress(rec.userIPv4, theApp.ipFilter, clients);
        const Address v6 = vetPeerAddress(rec.userIPv6, theApp.ipFilter, clients);
        if (v4.isNull() && v6.isNull() && !isLowID(rec.userIDHybrid))
            continue;

        // Still sharing what they queued for? If not, the queue would drop them anyway
        // (findBestClientInQueue's noFile branch) and score() would return 0.
        KnownFile* file = shared->getFileByID(rec.reqUpFileId.data());
        if (!file)
            continue;

        // Dedup before constructing: ClientList::addClient() only checks pointer identity,
        // so a second object for a peer we already know would sit there undetected.
        // Same recipe DownloadQueue::checkAndAddSource() uses.
        const Address dedupAddr = v4.isNull() ? v6 : v4;
        if (clients->findByUserHash(rec.userHash.data(),
                                    dedupAddr.toNetworkUint32(), rec.userPort))
            continue;
        if (!dedupAddr.isNull() && clients->findByAddress(dedupAddr, rec.userPort))
            continue;

        UpDownClient* client = buildClient(rec, v4, v6);
        if (!client)
            continue;

        // Re-derived, never persisted — the pair hello processing sets up.
        if (theApp.clientCredits)
            client->setCredits(theApp.clientCredits->getCredit(rec.userHash.data()));
        if (theApp.friendList) {
            client->setFriendPtr(theApp.friendList->searchFriend(
                rec.userHash.data(), client->userAddress().toNetworkUint32(), rec.userPort));
        }

        // Rebase both clocks onto the live tick. The offline gap is deliberately added only
        // to lastUpRequest — that really is "time since we last heard from them" — and not
        // to the wait time, since they were not queued while we were down. Relative order
        // among restored clients is preserved either way.
        client->restoreWaitStartTime(rec.waitedSeconds * 1000);
        client->setLastUpRequest(tickNow - sinceRequest * 1000);

        // Mandatory: without an upload file, score() returns 0 *and* the queue's noFile
        // purge fires on the first slot decision.
        client->setUploadFileID(file);

        clients->addClient(client, /*skipDupTest=*/true);

        // Must land in the queue on this same tick — ClientList::process() reaps any client
        // that is not socketed, transferring, or on the upload queue.
        if (!queue->addRestoredClient(client)) {
            clients->removeClient(client, QStringLiteral("upload queue restore rejected"));
            delete client;
            continue;
        }

        ++added;
    }

    if (added > 0) {
        logInfo(QStringLiteral("Upload queue: restored %1 waiting client(s) from %2 stored")
                    .arg(added).arg(contents.records.size()));
    }
    return added;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

UpDownClient* UploadQueueStore::buildClient(const QueuedClientRecord& rec,
                                            const Address& v4, const Address& v6)
{
    // ed2kID=true matches every other "build a client from persisted/wire fields" site;
    // the ctor then derives m_connectAddress for a HighID peer.
    auto* client = new UpDownClient(rec.userPort, rec.userIDHybrid,
                                    rec.serverIP, rec.serverPort, nullptr, true);

    client->setUserHash(rec.userHash.data());

    if (!v4.isNull()) {
        client->setUserAddress(v4);   // also sets connectAddress
    } else if (!v6.isNull()) {
        client->setUserAddress(v6);
    }

    if (!v6.isNull()) {
        client->setUserIPv6(v6);
        client->setOpenIPv6(true);
    }

    client->setKadPort(rec.kadPort);
    client->setUDPPort(rec.udpPort);
    client->setKadVersion(rec.kadVersion);
    client->setUdpVer(rec.udpVer);

    // Both bools must be true: setConnectOptions() ANDs each stored bit with them, so
    // anything else silently discards the crypt flags and the direct-UDP-callback bit —
    // and with them our ability to dial this peer obfuscated. Mirrors the SLS restore.
    client->setConnectOptions(rec.connectOptions, true, true);

    client->setAskedCount(rec.askedCount);

    // Display only, refreshed the moment the peer says hello.
    if (!rec.userName.isEmpty())
        client->setUserName(rec.userName);
    client->setClientVersion(rec.clientVersion);
    client->setEmuleVersion(rec.emuleVersion);
    client->setCompatibleClient(rec.compatibleClient);
    client->initClientSoftwareVersion();

    return client;
}

bool UploadQueueStore::makeRecord(const UpDownClient* client, QueuedClientRecord& out)
{
    if (!client)
        return false;

    md4cpy(out.userHash.data(), client->userHash());
    md4cpy(out.reqUpFileId.data(), client->reqUpFileId());

    out.userIDHybrid = client->userIDHybrid();
    if (client->userAddress().isIPv4())
        out.userIPv4 = client->userAddress();
    if (client->userIPv6().isIPv6())
        out.userIPv6 = client->userIPv6();
    else if (client->userAddress().isIPv6())
        out.userIPv6 = client->userAddress();

    out.userPort   = client->userPort();
    out.serverIP   = client->serverAddress().toNetworkUint32();
    out.serverPort = client->serverPort();
    out.kadPort    = client->kadPort();
    out.udpPort    = client->udpPort();

    out.connectOptions = static_cast<uint8>(
        (client->supportsCryptLayer()          ? 0x01 : 0x00) |
        (client->requestsCryptLayer()          ? 0x02 : 0x00) |
        (client->requiresCryptLayer()          ? 0x04 : 0x00) |
        (client->supportsDirectUDPCallback()   ? 0x08 : 0x00));

    out.kadVersion = client->kadVersion();
    out.udpVer     = client->udpVer();

    out.waitedSeconds = client->getWaitTimeDelay() / 1000;

    const auto tickNow = static_cast<uint32>(getTickCount());
    const uint32 last  = client->lastUpRequest();
    out.sinceLastRequestSeconds = (tickNow > last) ? ((tickNow - last) / 1000) : 0;

    out.askedCount       = client->askedCount();
    out.userName         = client->userName();
    out.clientVersion    = client->clientVersion();
    out.emuleVersion     = client->emuleVersion();
    out.compatibleClient = client->compatibleClient();

    return out.isRestorable();
}

std::vector<QueuedClientRecord> UploadQueueStore::collectRecords(const UploadQueue* queue)
{
    struct Scored {
        QueuedClientRecord record;
        uint64 score = 0;
    };

    std::vector<Scored> scored;

    queue->forEachWaiting([&](UpDownClient* client) {
        if (!client || client->isBanned())
            return;

        QueuedClientRecord rec;
        if (!makeRecord(client, rec))
            return;

        scored.push_back({std::move(rec), client->score(false)});
    });

    // Best first, so a truncated list keeps the peers that waited longest / earned most,
    // and so the load path can inject in queue order without re-sorting.
    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored& a, const Scored& b) { return a.score > b.score; });

    if (scored.size() > static_cast<size_t>(kMaxSavedQueueClients))
        scored.resize(static_cast<size_t>(kMaxSavedQueueClients));

    std::vector<QueuedClientRecord> out;
    out.reserve(scored.size());
    for (Scored& s : scored)
        out.push_back(std::move(s.record));
    return out;
}

bool UploadQueueStore::save(UploadQueue* queue, const QString& path)
{
    const std::vector<QueuedClientRecord> records = collectRecords(queue);
    return UploadQueueFile::write(path, records, nowUnix());
}

} // namespace eMule
