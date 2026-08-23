#include "pch.h"
/// @file DownloadQueue.cpp
/// @brief Download queue manager — port of MFC CDownloadQueue.
///
/// Manages in-progress downloads with IPFilter, dead source, and dedup checks.
/// Includes UDP source re-ask batching and server-based source queries.

#include "transfer/DownloadQueue.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "kademlia/Kademlia.h"
#include "client/DeadSourceList.h"
#include "client/UpDownClient.h"
#include "client/URLClient.h"
#include "net/HostResolver.h"
#include "files/KnownFileList.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "ipfilter/IPFilter.h"
#include "net/Packet.h"
#include "net/PeerVetting.h"
#include "prefs/Preferences.h"
#include "protocol/ED2KLink.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "server/Server.h"
#include "stats/Statistics.h"
#include "utils/Log.h"
#include "utils/SafeFile.h"
#include "utils/TimeUtils.h"

#include <algorithm>

#include <QDir>
#include <QDirIterator>

#include <ctime>



namespace eMule {

// ===========================================================================
// Construction / Destruction
// ===========================================================================

DownloadQueue::DownloadQueue(QObject* parent)
    : EntityList<PartFile>(parent)
{
}

DownloadQueue::~DownloadQueue()
{
    deleteAll();
}

// ===========================================================================
// init — scan temp dirs for .part.met files
// ===========================================================================

void DownloadQueue::init(const QStringList& tempDirs)
{
    for (const auto& tempDir : tempDirs) {
        QDir dir(tempDir);
        if (!dir.exists())
            continue;

        QDirIterator it(tempDir, {QStringLiteral("*.part.met")},
                        QDir::Files, QDirIterator::NoIteratorFlags);

        while (it.hasNext()) {
            it.next();
            const QString filename = it.fileName();
            const QString directory = QFileInfo(it.filePath()).absolutePath();

            auto* partFile = new PartFile;
            auto result = partFile->loadPartFile(directory, filename);

            // MFC CDownloadQueue::Init: if .met is corrupt, try .bak backup
            if (result != PartFileLoadResult::LoadSuccess) {
                const QString metPath = directory + QDir::separator() + filename;
                const QString bakPath = metPath + QStringLiteral(".bak");
                if (QFile::exists(bakPath)) {
                    logInfo(QStringLiteral("Trying backup for: %1").arg(filename));
                    QFile::remove(metPath);
                    QFile::copy(bakPath, metPath);
                    delete partFile;
                    partFile = new PartFile;
                    result = partFile->loadPartFile(directory, filename);
                    if (result == PartFileLoadResult::LoadSuccess)
                        partFile->savePartFile();
                }
            }

            if (result == PartFileLoadResult::LoadSuccess) {
                connectPartFileSignals(partFile);
                m_items.push_back(partFile);
                // "part files are always shared files" — srchybrid/DownloadQueue.cpp:109,127.
                // loadPartFile() has already latched Ready if a part verified, so this
                // is MFC's own test: GetStatus(true) == PS_READY, ignoring pause, because
                // a paused download with a complete part stays shared.
                // Not onlyAdd: a part file that comes back shareable is news the server
                // should hear, and MFC arms the republish here too (:109,127). Only the
                // bulk re-add in addPartFilesToShare() suppresses it (:73).
                if (m_sharedFileList && partFile->status(/*ignorePause=*/true) == PartFileStatus::Ready)
                    m_sharedFileList->safeAddKFile(partFile);
                // Record the state the file came back in — a download that silently
                // loads paused, or with an empty gap list (status Completing), is
                // skipped by process() and will never ask for sources.
                logInfo(QStringLiteral("Loaded part file: %1 — status=%2 paused=%3 gaps=%4 completed=%5/%6")
                            .arg(partFile->fileName())
                            .arg(static_cast<int>(partFile->status()))
                            .arg(partFile->isPaused() ? 1 : 0)
                            .arg(partFile->gapList().size())
                            .arg(static_cast<uint64>(partFile->completedSize()))
                            .arg(static_cast<uint64>(partFile->fileSize())));
            } else {
                logWarning(QStringLiteral("Failed to load part file: %1 (result=%2)")
                               .arg(filename)
                               .arg(static_cast<int>(result)));
                delete partFile;
            }
        }
    }

    sortByPriority();
}

// ===========================================================================
// addPartFilesToShare — MFC CDownloadQueue::AddPartFilesToShare
// ===========================================================================

void DownloadQueue::addPartFilesToShare()
{
    // srchybrid/DownloadQueue.cpp:68-75. Not addToSharedFiles(): that only promotes an
    // Empty file, and after a reload these are already latched Ready — they just need
    // putting back into the map the reload emptied.
    if (!m_sharedFileList)
        return;

    for (auto* file : m_items) {
        if (file && file->status(/*ignorePause=*/true) == PartFileStatus::Ready)
            m_sharedFileList->safeAddKFile(file, /*onlyAdd=*/true);
    }
}

// ===========================================================================
// File management
// ===========================================================================

void DownloadQueue::addDownload(PartFile* file, bool paused)
{
    if (!file)
        return;

    // Check for duplicate (hash-based) up front so we only do the call-specific
    // pre-work for files we will actually add.
    if (isFileExisting(file->fileHash()))
        return;

    if (paused)
        file->pauseFile();

    connectPartFileSignals(file);

    // EntityList::addEntity appends + invokes onEntityAdded() (sort/log/emit).
    // Dup already checked above, so skip the base pointer check.
    addEntity(file, /*skipDupCheck=*/true);
}

void DownloadQueue::removeFile(PartFile* file)
{
    // EntityList::removeEntity: find -> erase -> onEntityRemoved()
    // (counter update + sort + emit).
    removeEntity(file);
}

void DownloadQueue::deleteAll()
{
    // Control flow: Phase 1 detaches all sources (swap or disconnect).
    // Phase 2 disconnects each PartFileNotifier to invalidate pending
    // QueuedConnection events (onDownloadCompleted), then deletes.
    // Without the disconnect, deferred onDownloadCompleted fires after
    // the PartFile is freed → use-after-free in safeAddKFile.
    //
    // Phase 1: For each file, try to swap its sources to another pending
    // file (A4AF).  Sources that can't swap are detached cleanly.
    // This mirrors onDownloadCompleted's cleanup pattern.
    for (auto* file : m_items) {
        auto sources = file->srcList();  // copy — doSwap mutates srcList
        for (auto* client : sources) {
            if (client->swapToAnotherFile(
                    QStringLiteral("file deleted"), true, true, true)) {
                client->setDownloadState(DownloadState::None);
            } else {
                client->setDownloadState(DownloadState::None);
                client->setReqFile(nullptr);
                file->removeSource(client);
            }
            client->removeFileFromOtherLists(file);
        }

        for (auto* client : file->a4afSrcList())
            client->removeFileFromOtherLists(file);
        file->a4afSrcList().clear();
    }

    // Phase 2: Delete the PartFiles we still own.  A *completed* download was
    // handed to KnownFileList (onDownloadCompleted → safeAddKFile); it now owns
    // that object — persisting it in known.met and deleting it on teardown.
    // Freeing it here would leave a dangling pointer in KnownFileList::m_filesMap,
    // crashing the shutdown save (KnownFile::writeToFile) and double-freeing it in
    // KnownFileList::clear().  Mirrors MFC, where a completed CPartFile is owned by
    // theApp.knownfiles, not the queue (KnownFileList.cpp SafeAddKFile asserts
    // !downloadqueue->IsPartFile()).  Each remaining PartFile's destructor destroys
    // its FileNotifier, which purges any pending QueuedConnection
    // onDownloadCompleted events from firing after the file is freed.
    for (auto* file : m_items) {
        if (m_knownFileList && m_knownFileList->isFilePtrInList(file))
            continue; // owned by KnownFileList now — it saves and deletes it
        // Same reason as onEntityRemoved(): this path frees the object without going
        // through removeEntity(), so it has to unhook the shared map itself.
        if (m_sharedFileList && file->isPartFile())
            m_sharedFileList->removeFile(file);
        delete file;
    }
    m_items.clear();
}

// ===========================================================================
// Lookup
// ===========================================================================

PartFile* DownloadQueue::fileByID(const uint8* hash) const
{
    if (!hash)
        return nullptr;

    for (auto* file : m_items) {
        if (md4equ(file->fileHash(), hash))
            return file;
    }
    return nullptr;
}

PartFile* DownloadQueue::fileByIndex(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_items.size()))
        return nullptr;
    return m_items[static_cast<size_t>(index)];
}

// A search ID of 0 means "no search"; never match on it, or the first file in the
// queue would be handed every expiring search. MFC DownloadQueue.cpp:439-448.
PartFile* DownloadQueue::fileByKadFileSearchID(uint32 id) const
{
    if (id == 0)
        return nullptr;

    for (auto* file : m_items) {
        if (file->kadFileSearchID() == id)
            return file;
    }
    return nullptr;
}

bool DownloadQueue::isFileExisting(const uint8* hash) const
{
    return fileByID(hash) != nullptr;
}

// ===========================================================================
// Source management
// ===========================================================================

namespace {

/// True when the candidate source describes this very client. Sources arrive from
/// three directions (source exchange, Kad, and server answers) and every one of them
/// can hand us back our own address, so this is checked for all of them.
///
/// Note the two ID representations in play: the server comparisons work on the ED2K
/// (network-order) id because that is what the server assigned us, while Kad reports
/// its address in hybrid (host) order. Mixing them silently never matches.
///
/// The companion "drop low-ID sources while firewalled" rule from the original's
/// CanAddSource deliberately lives in PartFile::addClientSources instead: the
/// original applies it only to source-exchange and search results, never to Kad
/// answers, which carry buddy details this check knows nothing about.
bool isSelf(uint32 hybridID, uint16 port,
            const Address& serverAddr, uint16 serverPort)
{
    const uint32 ed2kID = isLowID(hybridID) ? hybridID : htonl(hybridID);

    if (theApp.serverConnect && theApp.serverConnect->isConnected()) {
        if (theApp.serverConnect->isLowID()) {
            // Under a LowID our "address" is only meaningful together with the server
            // that issued it.
            const Server* srv = theApp.serverConnect->currentServer();
            if (theApp.serverConnect->clientID() == ed2kID && srv &&
                srv->ipAddress() == serverAddr && srv->port() == serverPort)
                return true;
            if (theApp.serverConnect->localIP() == ed2kID)
                return true;
        } else if (theApp.serverConnect->clientID() == ed2kID &&
                   thePrefs.port() == port) {
            return true;
        }
    }

    auto* kadInst = kad::Kademlia::instance();
    if (kadInst && kadInst->isConnected() && !kadInst->isFirewalled() &&
        kadInst->getIPAddress() == hybridID && thePrefs.port() == port)
        return true;

    return false;
}

} // namespace

bool DownloadQueue::checkAndAddSource(PartFile* file, UpDownClient* source)
{
    if (!file || !source)
        return false;

    // Never add ourselves as a source for our own download.
    if (isSelf(source->userIDHybrid(), source->userPort(),
               source->serverAddress(), source->serverPort())) {
        logDebug(QStringLiteral("Source rejected — that is us: %1:%2")
                     .arg(ipstr(source->userAddress())).arg(source->userPort()));
        return false;
    }

    // A High ID *is* the peer's IPv4, so it has to be a usable one — 0.x, loopback,
    // multicast, reserved and (unless the lab-mode pref says otherwise) LAN are all
    // unreachable or bogus. A Low ID is an ID and not an address, so testing it would
    // reject every firewalled source; the IPv6-only marker kNoIPv4SourceId is
    // deliberately a Low ID for the same reason. MFC DownloadQueue.cpp:568-575.
    // m_userIDHybrid is host order for a High ID, isGoodIP() takes network order.
    if (!source->hasLowID() && !isGoodIP(htonl(source->userIDHybrid()))) {
        logDebug(QStringLiteral("Source rejected — unusable high ID: %1")
                     .arg(ipstr(htonl(source->userIDHybrid()))));
        return false;
    }

    // IPFilter check — reject filtered IPs. The Address overload covers both families
    // and keeps IPv6 sources out of the uint32 path, where they would collapse to 0.
    if (m_ipFilter && !source->userAddress().isNull()) {
        if (m_ipFilter->isFiltered(source->userAddress())) {
            logDebug(QStringLiteral("Source rejected by IPFilter: %1").arg(ipstr(source->userAddress())));
            return false;
        }
    }

    // Check dead source list
    if (m_clientList) {
        DeadSourceKey key;
        std::memcpy(key.hash.data(), source->userHash(), 16);
        key.userID = source->userIDHybrid();
        key.port = source->userPort();
        key.kadPort = source->kadPort();
        key.serverAddress = source->serverAddress();
        if (m_clientList->globalDeadSourceList.isDeadSource(key)) {
            logDebug(QStringLiteral("Source rejected — dead source: %1").arg(ipstr(source->userAddress())));
            return false;
        }
    }

    // ClientList dedup across files — check if a matching client already exists globally
    if (m_clientList) {
        UpDownClient* existing = nullptr;
        if (source->hasValidHash())
            existing = m_clientList->findByUserHash(source->userHash(), source->userAddress().toNetworkUint32(), source->userPort());
        // Address-typed lookup: findByIP() would compare toNetworkUint32(), which is 0
        // for an IPv6 source, so it matched every address-less client (server sources
        // are built without a userAddress) on the same port and rejected the IPv6
        // source as a bogus duplicate.
        if (!existing)
            existing = m_clientList->findByAddress(source->userAddress(), source->userPort());
        if (existing && existing != source) {
            logDebug(QStringLiteral("Source rejected — duplicate in ClientList: IP=%1:%2")
                         .arg(ipstr(source->userAddress())).arg(source->userPort()));
            return false;
        }
    }

    // Check if source already exists in the file's source list
    const auto& srcList = file->srcList();
    for (const auto* existing : srcList) {
        if (existing == source)
            return false;

        // Compare by user hash if available
        if (existing->hasValidHash() && source->hasValidHash()) {
            if (md4equ(existing->userHash(), source->userHash())) {
                logDebug(QStringLiteral("Source rejected — duplicate hash in file source list: IP=%1:%2")
                             .arg(ipstr(source->userAddress())).arg(source->userPort()));
                return false;
            }
        }

        // Compare by IP:port
        if (existing->userAddress() == source->userAddress() &&
            existing->userPort() == source->userPort() &&
            !existing->userAddress().isNull())
        {
            logDebug(QStringLiteral("Source rejected — duplicate IP:port in file source list: %1:%2")
                         .arg(ipstr(source->userAddress())).arg(source->userPort()));
            return false;
        }
    }

    // Check max sources per file
    if (file->sourceCount() >= thePrefs.maxSourcesPerFile()) {
        logDebug(QStringLiteral("Source rejected — max sources reached (%1/%2) for %3")
                     .arg(file->sourceCount()).arg(thePrefs.maxSourcesPerFile()).arg(file->fileName()));
        return false;
    }

    source->setReqFile(file);  // MFC: SetRequestFile(sender)
    file->addSource(source);
    if (m_clientList)
        m_clientList->addClient(source, true);  // skipDupTest=true, already checked above
    return true;
}

void DownloadQueue::removeSource(UpDownClient* source)
{
    if (!source)
        return;

    for (auto* file : m_items)
        file->removeSource(source);
}

void DownloadQueue::addKadSourceResult(const kad::Kademlia::KadSourceResult& result)
{
    // Unpacked into the names the body already used, so the struct conversion stayed a
    // signature change rather than a rewrite of a hundred lines of source handling.
    const uint32  searchID   = result.searchID;
    const uint8*  fileHash   = result.fileHash;
    const uint32  ip         = result.ip;
    const uint16  tcpPort    = result.tcpPort;
    const uint32  buddyIP    = result.buddyIP;
    const uint16  buddyPort  = result.buddyPort;
    const uint8   buddyCrypt = result.buddyCrypt;
    const uint8   sourceType = result.sourceType;
    const uint8*  buddyHash  = result.buddyHash;
    const uint8*  clientHash = result.clientHash;
    const uint16  udpPort    = result.udpPort;
    const uint8*  sourceIPv6 = result.sourceIPv6;
    const uint8*  buddyIPv6  = result.buddyIPv6;

    Q_UNUSED(searchID);

    // Safety: file must be in download queue (MFC DownloadQueue.cpp:1512-1513)
    PartFile* file = fileByID(fileHash);
    if (!file)
        return;

    // Don't add sources to stopped files or beyond max (MFC:1516)
    if (file->isStopped() || file->sourceCount() >= thePrefs.maxSourcesPerFile())
        return;

    // IP filter on source IP (MFC:1519-1524)
    uint32 ed2kIP = htonl(ip);
    if (m_ipFilter && ip != 0 && m_ipFilter->isFiltered(ed2kIP)) {
        logDebug(QStringLiteral("addKadSourceResult: IP %1 filtered").arg(ip));
        return;
    }

    // Self-loop check (MFC:1525-1526)
    auto* kadInst = kad::Kademlia::instance();
    if (kadInst && ip == kadInst->getIPAddress() && tcpPort == thePrefs.port())
        return;

    // Common finalization: checkAndAddSource + tryToConnect or delete
    auto finalizeSource = [&](UpDownClient* client) {
        // Attach any IPv6 the Kad result carried so the connection logic can prefer it.
        if (sourceIPv6) {
            client->setUserIPv6(Address::fromIPv6Bytes(sourceIPv6));
            client->setOpenIPv6(true);
        }
        if (buddyIPv6)
            client->setBuddyIPv6(Address::fromIPv6Bytes(buddyIPv6));
        if (checkAndAddSource(file, client)) {
            logDebug(QStringLiteral("addKadSourceResult: source ADDED type=%1 file=%2 totalSources=%3")
                         .arg(sourceType).arg(file->fileName()).arg(file->sourceCount()));
            client->tryToConnect();
        } else {
            logDebug(QStringLiteral("addKadSourceResult: source REJECTED type=%1 IP=%2:%3")
                         .arg(sourceType).arg(ip).arg(tcpPort));
            delete client;
        }
    };

    switch (sourceType) {
    case 4:
    case 1: {
        // Non-firewalled users (MFC DownloadQueue.cpp:1530-1548)
        if (tcpPort == 0) {
            logDebug(QStringLiteral("addKadSourceResult: ignored type %1 — no TCP port, IP=%2")
                         .arg(sourceType).arg(ip));
            return;
        }
        auto* client = new UpDownClient(tcpPort, ip, 0, 0, file);
        client->setSourceFrom(SourceFrom::Kademlia);
        client->setKadPort(udpPort);
        if (clientHash)
            client->setUserHash(clientHash);
        client->setUserAddress(Address::fromHostOrder(ip));
        // callback=false unlike MFC, which uses one shared call with the default true:
        // a source that is reachable on TCP has no callback to offer, and a publisher
        // that is not firewalled never sets bit 3 in the first place.
        client->setConnectOptions(buddyCrypt, true, false);
        logDebug(QStringLiteral("Kad HighID source for %1: type=%2 IP=%3:%4 crypt=0x%5")
                     .arg(file->fileName()).arg(sourceType).arg(ip).arg(tcpPort)
                     .arg(buddyCrypt, 2, 16, QLatin1Char('0')));
        finalizeSource(client);
        break;
    }

    case 5:
    case 3: {
        // Firewalled with buddy callback (MFC DownloadQueue.cpp:1553-1582)
        if (theApp.isFirewalled()) {
            logDebug(QStringLiteral("addKadSourceResult: skipping FW source type %1 — we are firewalled")
                         .arg(sourceType));
            return;
        }
        // buddyIP is FT_SERVERIP, which travels in network order — unlike the source
        // IP above, which needs the htonl. Same split as MFC (srchybrid/DownloadQueue
        // .cpp:1519 swaps the source IP, :1560 passes the buddy IP straight through),
        // and isFiltered(uint32) wants network order (IPFilter.h).
        if (m_ipFilter && m_ipFilter->isFiltered(buddyIP)) {
            logDebug(QStringLiteral("addKadSourceResult: buddy IP %1 filtered").arg(ipstr(buddyIP)));
            return;
        }
        if (m_clientList && m_clientList->isBannedClient(Address::fromNetworkOrder(buddyIP))) {
            logDebug(QStringLiteral("addKadSourceResult: buddy IP %1 banned").arg(ipstr(buddyIP)));
            return;
        }
        // clientID=1: "We set the clientID to 1 as a Kad user only has 1 buddy" (MFC:1570)
        auto* client = new UpDownClient(tcpPort, 1, 0, 0, file);
        client->setSourceFrom(SourceFrom::Kademlia);
        client->setKadPort(udpPort);
        if (clientHash)
            client->setUserHash(clientHash);
        if (buddyHash)
            client->setBuddyID(buddyHash);
        client->setBuddyAddress(Address::fromNetworkOrder(buddyIP));
        client->setBuddyPort(buddyPort);
        client->setConnectOptions(buddyCrypt, true, true);
        logDebug(QStringLiteral("Kad FW source for %1: type=%2 buddy=%3:%4")
                     .arg(file->fileName()).arg(sourceType).arg(buddyIP).arg(buddyPort));
        finalizeSource(client);
        break;
    }

    case 6: {
        // Direct UDP callback (MFC DownloadQueue.cpp:1584-1601)
        if (theApp.isFirewalled()) {
            logDebug(QStringLiteral("addKadSourceResult: skipping type 6 — we are firewalled"));
            return;
        }
        if ((buddyCrypt & 0x08) == 0) {
            logDebug(QStringLiteral("addKadSourceResult: type 6 direct callback flag not set (crypt=0x%1)")
                         .arg(buddyCrypt, 2, 16, QLatin1Char('0')));
            return;
        }
        auto* client = new UpDownClient(tcpPort, 1, 0, 0, file);
        client->setSourceFrom(SourceFrom::Kademlia);
        client->setKadPort(udpPort);
        client->setConnectAddress(Address::fromHostOrder(ip));  // IP for UDP, not TCP (MFC:1596)
        if (clientHash)
            client->setUserHash(clientHash);
        // callback=true, or setConnectOptions ANDs bit 3 away again and this source is
        // left with no route at all — the check six lines up admitted it precisely
        // because that bit was set. MFC reaches the same place through the default
        // argument (srchybrid/DownloadQueue.cpp:1605). supportsDirectUDPCallback()
        // still needs the user hash and the Kad port set above, so all three are here.
        client->setConnectOptions(buddyCrypt, true, true);
        logDebug(QStringLiteral("Kad UDP callback source for %1: type=6 IP=%2:%3")
                     .arg(file->fileName()).arg(ip).arg(tcpPort));
        finalizeSource(client);
        break;
    }

    default:
        logDebug(QStringLiteral("addKadSourceResult: unknown source type %1").arg(sourceType));
        return;
    }
}

// ===========================================================================
// addKadNoteResult
// ===========================================================================

void DownloadQueue::addKadNoteResult(const uint8* fileHash, const uint8* publisherId,
                                     const QString& name, uint8 rating,
                                     const QString& comment)
{
    if (!fileHash || !publisherId || name.isEmpty())
        return;

    const QByteArray pub(reinterpret_cast<const char*>(publisherId), 16);
    const time_t now = time(nullptr);

    // A notes result may target an in-progress download or an already-completed file.
    if (PartFile* pf = fileByID(fileHash)) {
        pf->addKadNote(pub, name, comment, rating, now);
        pf->savePartFile();  // cheap single-file write; survives restart/completion
        return;
    }

    if (m_knownFileList) {
        if (KnownFile* kf = m_knownFileList->findKnownFileByID(fileHash)) {
            kf->addKadNote(pub, name, comment, rating, now);
            m_knownFileList->save();  // persist into known.met
        }
    }
}

// ===========================================================================
// addDownloadFromED2KLink
// ===========================================================================

bool DownloadQueue::addDownloadFromED2KLink(const QString& link, const QString& tempDir,
                                             uint32 category, bool paused)
{
    auto parsed = parseED2KLink(link);
    if (!parsed) {
        logWarning(QStringLiteral("addDownloadFromED2KLink: failed to parse link"));
        return false;
    }

    auto* fileLink = std::get_if<ED2KFileLink>(&*parsed);
    if (!fileLink) {
        logWarning(QStringLiteral("addDownloadFromED2KLink: not a file link"));
        return false;
    }

    // Check for duplicate
    if (isFileExisting(fileLink->hash.data())) {
        logInfo(QStringLiteral("addDownloadFromED2KLink: file already exists: %1").arg(fileLink->name));
        // The download is already here, but the link may still carry sources and an
        // AICH hash we lack. MFC seeds an existing download the same way
        // (srchybrid/DownloadQueue.cpp:271-289).
        if (PartFile* existing = fileByID(fileLink->hash.data());
            existing && static_cast<uint64>(existing->fileSize()) == fileLink->size)
        {
            if (fileLink->hasValidAICHHash && !existing->fileIdentifier().hasAICHHash())
                existing->fileIdentifier().setAICHHash(fileLink->aichHash);
            addLinkSources(existing, fileLink->hostnameSources);
        }
        return false;
    }

    auto* partFile = new PartFile(category);
    partFile->setFileName(fileLink->name, true);
    partFile->setFileSize(fileLink->size);
    partFile->setFileHash(fileLink->hash.data());

    if (fileLink->hasValidAICHHash)
        partFile->fileIdentifier().setAICHHash(fileLink->aichHash);

    if (fileLink->hashset)
        partFile->fileIdentifier().loadMD4HashsetFromFile(*fileLink->hashset, true);

    if (!partFile->createPartFile(tempDir)) {
        logError(QStringLiteral("addDownloadFromED2KLink: failed to create part file for %1")
                     .arg(fileLink->name));
        delete partFile;
        return false;
    }

    addDownload(partFile, paused);

    // After addDownload: the file must be queued before sources are attached.
    addLinkSources(partFile, fileLink->hostnameSources);
    return true;
}

// ===========================================================================
// addServerSourceResult
// ===========================================================================

void DownloadQueue::addServerSourceResult(const uint8* data, uint32 size, bool obfuscated)
{
    // TCP packet format: fileHash[16] + sourceCount[1] + per-source data.
    if (!data || size < 17)
        return;

    PartFile* file = fileByID(data);
    if (!file)
        return;

    // Sources from the connected server carry no server address of their own;
    // low-ID ones are reached via that server, so stamp its IP/port.
    uint32 srvIP = 0;
    uint16 srvPort = 0;
    if (m_serverConnect) {
        if (auto* srv = m_serverConnect->currentServer()) {
            srvIP = srv->ipAddress().toNetworkUint32();
            srvPort = srv->port();
        }
    }

    parseServerSourceBlock(file, data, size, /*offset=*/16, obfuscated, srvIP, srvPort);
}

void DownloadQueue::addUDPGlobalSources(const uint8* data, uint32 size, const Endpoint& from)
{
    // MFC: CUDPSocket::ProcessPacket() OP_GLOBFOUNDSOURCES — UDPSocket.cpp:268.
    // One datagram may pack several files' source blocks, each
    // [fileHash16][count][ source(6)... ], separated by the 2-byte marker
    // OP_EDONKEYPROT, OP_GLOBFOUNDSOURCES. UDP sources are never obfuscated.
    if (!data || size < 17)
        return;

    // Attribute the sources to the server that actually answered. Fall back to
    // the sender's TCP port (UDP - 4) if it is not (yet) in our list.
    uint32 srvIP = from.address().toNetworkUint32();
    uint16 srvPort = (from.port() >= 4) ? static_cast<uint16>(from.port() - 4) : from.port();
    if (theApp.serverList) {
        if (auto* srv = theApp.serverList->findByIPUdp(srvIP, from.port(), true)) {
            srvIP = srv->ipAddress().toNetworkUint32();
            srvPort = srv->port();
        }
    }

    uint32 offset = 0;
    while (offset + 17 <= size) {
        PartFile* file = fileByID(data + offset);  // null → sources skipped, offset still advances
        offset += 16;
        offset = parseServerSourceBlock(file, data, size, offset, /*obfuscated=*/false, srvIP, srvPort);

        // Continue only across the OP_EDONKEYPROT, OP_GLOBFOUNDSOURCES separator.
        if (offset + 2 > size)
            break;
        if (data[offset] != OP_EDONKEYPROT || data[offset + 1] != OP_GLOBFOUNDSOURCES)
            break;
        offset += 2;
    }
}

uint32 DownloadQueue::parseServerSourceBlock(PartFile* file, const uint8* data, uint32 size,
                                             uint32 offset, bool obfuscated,
                                             uint32 srvIP, uint16 srvPort)
{
    if (offset >= size)
        return size;

    const uint8 sourceCount = data[offset];
    ++offset;

    for (uint8 i = 0; i < sourceCount; ++i) {
        // Each source: userId[4] + port[2] = 6 bytes minimum.
        if (offset + 6 > size)
            return size;  // truncated — no further block is parseable

        uint32 userId = 0;
        uint16 port = 0;
        std::memcpy(&userId, data + offset, 4);
        std::memcpy(&port, data + offset + 4, 2);
        offset += 6;

        uint8 cryptFlags = 0;
        std::array<uint8, 16> userHash{};
        bool hasHash = false;

        if (obfuscated) {
            if (offset + 1 > size)
                return size;
            cryptFlags = data[offset];
            offset += 1;

            if ((cryptFlags & 0x80) != 0) {
                if (offset + 16 > size)
                    return size;
                std::memcpy(userHash.data(), data + offset, 16);
                offset += 16;
                hasHash = true;
            }
        }

        // IPv6 sentinel (S3a): ClientID 0xFFFFFFFF marks an inline IPv6 source; the 16
        // IPv6 bytes are the LAST field, after any obfuscation fields above. Consuming
        // them keeps the rest of the source list in sync — the mandatory counterpart to
        // advertising v6 capability at login.
        std::array<uint8, 16> ipv6{};
        bool hasIPv6 = false;
        if (userId == IPV6_SOURCE_SENTINEL) {
            if (offset + 16 > size)
                return size;  // truncated
            std::memcpy(ipv6.data(), data + offset, 16);
            offset += 16;
            hasIPv6 = true;
        }

        // file == null means the block is for a file we don't have — the offset
        // still advances (skipping the sources) so the next block is found.
        if (file) {
            if (hasIPv6)
                addServerSourceClientIPv6(file, ipv6.data(), port, obfuscated, cryptFlags,
                                          userHash.data(), hasHash);
            else
                addServerSourceClient(file, userId, port, obfuscated, cryptFlags,
                                      userHash.data(), hasHash, srvIP, srvPort);
        }
    }

    return offset;
}

void DownloadQueue::addServerSourceClient(PartFile* file, uint32 userId, uint16 port,
                                          bool obfuscated, uint8 cryptFlags,
                                          const uint8* userHash, bool hasHash,
                                          uint32 srvIP, uint16 srvPort)
{
    // Validate source. Mirror CPartFile::AddSources / CanAddSource
    // (MFC PartFile.cpp:2478-2503). Server sources are built without a
    // userAddress, so checkAndAddSource's ipfilter is skipped and it has no
    // ban check — do both here, plus the firewalled-LowID drop (kept out of
    // checkAndAddSource because Kad buddy sources use clientID=1 as a LowID
    // marker). userId is ED2K/network order, and a low ID stays low in either
    // byte order, so the raw value tests correctly.

    // Two firewalled clients can never connect, so a low-ID source is dead weight.
    if (isLowID(userId) && theApp.isFirewalled())
        return;

    if (!isLowID(userId)) {
        if (!isGoodIP(userId))
            return;
        if (m_ipFilter && m_ipFilter->isFiltered(userId))
            return;
        if (m_clientList &&
            m_clientList->isBannedClient(Address::fromNetworkOrder(userId)))
            return;
    }

    if (file->sourceCount() >= static_cast<int>(thePrefs.maxSourcesPerFile()))
        return;

    auto* client = makeSourceClient(file, userId, Address(), port,
                                    SourceFrom::Server, srvIP, srvPort);

    if (obfuscated)
        client->setConnectOptions(cryptFlags, true, false);

    if (hasHash)
        client->setUserHash(userHash);

    if (checkAndAddSource(file, client))
        client->tryToConnect();
    else
        delete client;
}

void DownloadQueue::addServerSourceClientIPv6(PartFile* file, const uint8* ipv6, uint16 port,
                                              bool obfuscated, uint8 cryptFlags,
                                              const uint8* userHash, bool hasHash)
{
    const Address v6 = Address::fromIPv6Bytes(ipv6);

    // Same vetting the IPv4 twin does above. The IP filter itself runs downstream in
    // checkAndAddSource — makeSourceClient points userAddress at the IPv6 for a source
    // with no usable IPv4 — but isGoodIP and the ban list have no such coverage.
    if (!isGoodIP(v6))
        return;
    if (m_clientList && m_clientList->isBannedClient(v6))
        return;

    // Unlike an IPv4 LowID source, an IPv6 source is NOT dropped when we are
    // IPv4-firewalled: it is directly reachable over IPv6 regardless of our ED2K ID.
    if (file->sourceCount() >= static_cast<int>(thePrefs.maxSourcesPerFile()))
        return;

    auto* client = makeSourceClient(file, kNoIPv4SourceId, v6, port, SourceFrom::Server);

    if (obfuscated)
        client->setConnectOptions(cryptFlags, true, false);
    if (hasHash)
        client->setUserHash(userHash);

    if (checkAndAddSource(file, client))
        client->tryToConnect();
    else
        delete client;
}

UpDownClient* DownloadQueue::makeSourceClient(PartFile* file, uint32 ed2kUserId,
                                              const Address& v6, uint16 port,
                                              SourceFrom from, uint32 srvIP, uint16 srvPort)
{
    auto* client = new UpDownClient(port, ed2kUserId, srvIP, srvPort, file, true);
    client->setSourceFrom(from);

    if (!v6.isNull()) {
        client->setUserIPv6(v6);
        client->setOpenIPv6(true);

        // With no usable IPv4 the ID is only a LowID marker, so point userAddress and
        // connectAddress at the IPv6 — tryToConnect() then dials it directly.
        if (ed2kUserId == kNoIPv4SourceId)
            client->setUserAddress(v6);
    }

    return client;
}

// ===========================================================================
// eD2K link sources
// ===========================================================================

void DownloadQueue::addLinkSources(PartFile* file, const std::vector<ED2KLinkSource>& sources)
{
    if (!file || sources.empty())
        return;

    // Re-look the file up by hash after an async resolve: the download may be gone by
    // then. MFC does the same in its hostname-source callback (DownloadQueue.cpp:1477).
    std::array<uint8, 16> fileHash{};
    md4cpy(fileHash.data(), file->fileHash());

    int dnsBudget = kMaxLinkDnsSources;
    int skippedForDns = 0;

    for (const auto& src : sources) {
        if (file->sourceCount() >= static_cast<int>(thePrefs.maxSourcesPerFile()))
            break;
        if (src.port == 0)
            continue;

        if (!src.url.isEmpty()) {
            addLinkUrlSource(file, src);
            continue;
        }

        if (!src.address.isNull()) {
            addLinkPeerSource(file,
                              src.address.isIPv4() ? src.address : Address(),
                              src.address.isIPv6() ? src.address : Address(),
                              src.port);
            continue;
        }

        if (dnsBudget <= 0) {
            ++skippedForDns;
            continue;
        }
        --dnsBudget;

        if (!m_hostResolver)
            m_hostResolver = new HostResolver(this);

        const uint16 port = src.port;
        const QString host = src.hostname;
        m_hostResolver->resolve(host, HostResolver::Preference::Any, this,
            [this, fileHash, host, port](const HostResolver::Result& result) {
                if (!result.ok()) {
                    logDebug(QStringLiteral("Link source %1: %2").arg(host, result.errorString));
                    return;
                }
                PartFile* target = fileByID(fileHash.data());
                if (!target)
                    return;
                // One client per host, carrying whichever families resolved.
                addLinkPeerSource(target, result.firstIPv4(), result.firstIPv6(), port);
            });
    }

    if (skippedForDns > 0) {
        logInfo(QStringLiteral("eD2K link: ignored %1 hostname source(s) beyond the "
                               "per-link lookup limit of %2")
                    .arg(skippedForDns).arg(kMaxLinkDnsSources));
    }
}

void DownloadQueue::addLinkPeerSource(PartFile* file, const Address& v4, const Address& v6,
                                      uint16 port)
{
    if (!file || port == 0)
        return;

    // A link is untrusted input, so vet each family independently and keep whichever
    // survives — same rule as ExtSX (PartFile::addClientSources).
    const Address usableV4 = vetPeerAddress(v4);
    const Address usableV6 = vetPeerAddress(v6);

    if (usableV4.isNull() && usableV6.isNull())
        return;

    const uint32 userId = usableV4.isNull() ? kNoIPv4SourceId : usableV4.toNetworkUint32();
    addVettedSource(file, userId, usableV6, port, SourceFrom::Link);
}

Address DownloadQueue::vetPeerAddress(const Address& addr) const
{
    // Body lives in net/PeerVetting.h so the upload queue store runs the same rules
    // against its own untrusted on-disk records. Our injected members are passed in
    // rather than the theApp globals so the unit tests keep their isolation.
    return eMule::vetPeerAddress(addr, m_ipFilter, m_clientList);
}

bool DownloadQueue::addVettedSource(PartFile* file, uint32 ed2kUserId, const Address& v6,
                                    uint16 port, SourceFrom from, const SourceHints& hints)
{
    if (!file || port == 0)
        return false;

    // Two firewalled IPv4 peers can never connect, but an IPv6 peer is dialable
    // whatever our ED2K ID is, so only drop when IPv4 is all we have.
    if (v6.isNull() && theApp.isFirewalled() && isLowID(ed2kUserId))
        return false;

    if (file->sourceCount() >= static_cast<int>(thePrefs.maxSourcesPerFile()))
        return false;

    auto* client = makeSourceClient(file, ed2kUserId, v6, port, from,
                                    hints.serverIP, hints.serverPort);

    if (hints.userHash)
        client->setUserHash(hints.userHash);
    if (hints.connectOptions != 0)
        client->setConnectOptions(hints.connectOptions, true, true);
    if (hints.kadPort != 0)
        client->setKadPort(hints.kadPort);
    if (hints.udpPort != 0)
        client->setUDPPort(hints.udpPort);

    if (!checkAndAddSource(file, client)) {
        delete client;
        return false;
    }

    client->tryToConnect();
    return true;
}

void DownloadQueue::addLinkUrlSource(PartFile* file, const ED2KLinkSource& source)
{
    if (!file || source.url.isEmpty())
        return;

    // A literal host is vetted here; a hostname is resolved by URLClient itself.
    if (!source.address.isNull()) {
        if (!isGoodIP(source.address)
            || (m_ipFilter && m_ipFilter->isFiltered(source.address))
            || (m_clientList && m_clientList->isBannedClient(source.address)))
            return;
    }

    if (file->sourceCount() >= static_cast<int>(thePrefs.maxSourcesPerFile()))
        return;

    auto* client = new URLClient();
    if (!client->setUrl(source.url, source.address)) {
        delete client;
        return;
    }
    client->setRequestFile(file);
    client->setSourceFrom(SourceFrom::Link);

    if (checkAndAddSource(file, client))
        client->tryToConnect();
    else
        delete client;
}

// ===========================================================================
// Queue operations
// ===========================================================================

void DownloadQueue::startNextFile(int category)
{
    PartFile* bestFile = nullptr;

    for (auto* file : m_items) {
        if (!file->isPaused() && !file->isStopped())
            continue;

        if (category >= 0 && file->category() != static_cast<uint32>(category))
            continue;

        if (!bestFile || PartFile::rightFileHasHigherPrio(bestFile, file))
            bestFile = file;
    }

    if (bestFile) {
        bestFile->resumeFile();
        logInfo(QStringLiteral("Started next file: %1").arg(bestFile->fileName()));
    }
}

void DownloadQueue::sortByPriority()
{
    std::ranges::sort(m_items, [](const PartFile* a, const PartFile* b) {
        // rightFileHasHigherPrio(a, b) returns true when b has higher prio.
        // We want higher-prio files first, so swap the arguments.
        return PartFile::rightFileHasHigherPrio(b, a);
    });
}

void DownloadQueue::process()
{
    const uint32 curTick = static_cast<uint32>(getTickCount());

    // Prune samples older than 10 seconds
    while (!m_averageDRList.empty() &&
           (curTick - m_averageDRList.front().timestamp) > 10000) {
        m_averageDRList.pop_front();
    }

    // Average rates across the window
    if (m_averageDRList.size() > 1) {
        uint64 sum = 0;
        for (const auto& s : m_averageDRList)
            sum += s.dataLen;
        m_datarate = static_cast<uint32>(sum / m_averageDRList.size());
    } else {
        m_datarate = 0;
    }

    m_udCounter = (m_udCounter + 1) % 10;
    uint32 curTickRate = 0;

    // MFC CDownloadQueue::Process() lines 348-357: compute proportional
    // download speed percentage (50-200) based on how close we are to the limit.
    uint32 downspeed = 0;
    const uint32 maxDown = thePrefs.maxDownload(); // KB/s, 0 = unlimited
    if (maxDown > 0 && m_datarate > 1500) {
        const uint64 maxDownBytes = static_cast<uint64>(maxDown) * 1024;
        downspeed = static_cast<uint32>(maxDownBytes * 100 / (m_datarate + 1));
        if (downspeed < 50)
            downspeed = 50;
        else if (downspeed > 200)
            downspeed = 200;
    }

    for (auto* file : m_items) {
        if (file->status() != PartFileStatus::Ready &&
            file->status() != PartFileStatus::Empty)
            continue;

        const uint32 rate = file->process(downspeed, m_udCounter);
        curTickRate += rate;

        // Check for completion
        if (file->status() == PartFileStatus::Complete)
            emit fileCompleted(file);
    }

    // Add this tick's rate to the averaging window
    m_averageDRList.push_back({curTickRate, curTick});

    // UDP file re-asks. MFC has no queue-wide re-ask window here: CDownloadQueue::Process()
    // stamps only the two *server* UDP timers (DownloadQueue.cpp:397-410). The file re-ask
    // clock is per (client, file) — SetLastAskedTime() writes m_fileReaskTimes[m_reqfile]
    // (UpdownClient.h:285) and GetTimeUntilReask() measures FILEREASKTIME from it
    // (DownloadClient.cpp:1882). One shared stamp measured process uptime instead: starting
    // from 0 against a boot-relative getTickCount() it never fired below 29 minutes of
    // uptime, fired on the very first tick above it, and then re-asked every source of every
    // file in one synchronized burst.
    //
    // Once per second, matching MFC's Process() cadence — this runs at 10 Hz.
    if (m_udCounter == 0) {
        for (auto* file : m_items) {
            if (file->status() != PartFileStatus::Ready &&
                file->status() != PartFileStatus::Empty)
                continue;
            for (auto* src : file->srcList()) {
                // The download-side natural send point for a queued OP_CHANGE_CLIENT_IP.
                src->flushPendingIPChange();

                // No isSourceRequestAllowed() gate here: udpReaskForDownload() owns that
                // decision and *returns* when a source request is allowed, because then a
                // TCP connection is preferable — MFC DownloadClient.cpp:1352. Requiring it
                // to be true before calling inverted the test, so direct-reachable sources
                // hit that early return every time and never got an OP_REASKFILEPING.
                if (!src->supportsUDP())
                    continue;

                // MFC PartFile.cpp:2337-2339. A UDP re-ask only for a source sitting on the
                // peer's queue, only inside the last two minutes before its TCP re-ask falls
                // due (and not in the final second), and only when we have not just tried to
                // dial it. "Allow up to 1 min for UDP to respond. If we are within one min
                // of TCP re-ask, do not try."
                //
                // The window is deliberately disjoint from the TCP branch in
                // PartFile::process(), which fires at timeUntilReask() == 0. Sharing that
                // one instant — which this used to do — means the same second sends a
                // re-ask datagram and then charges it as failed at the head of
                // askForDownload(). That drives m_failedUDPPackets past the 30 % abort and
                // silently disables UDP re-asks for the peer for good.
                if (src->downloadState() != DownloadState::OnQueue)
                    continue;
                const uint32 untilReask = src->timeUntilReask(file);
                if (untilReask < MIN2MS(2) && untilReask > SEC2MS(1)
                    && curTick >= src->lastTriedToConnect() + MIN2MS(20))
                {
                    src->udpReaskForDownload();
                }
            }
        }
    }

    // Server-based source queries via UDP — port of the SendNextUDPPacket
    // trigger in CDownloadQueue::Process(). m_lastUdpSearchTime is stamped only
    // by stopUDPRequests(), so once a pass starts this fires every tick until the
    // whole server list has been walked once, then idles for UDPSERVERREASKTIME.
    if (m_serverConnect && m_serverConnect->isConnected() && theApp.serverList &&
        (m_lastUdpSearchTime == 0 || curTick >= m_lastUdpSearchTime + UDPSERVERREASKTIME))
    {
        sendNextUDPPacket();
    }
}

// ===========================================================================
// Global UDP source acquisition — port of CDownloadQueue::SendNextUDPPacket()
// ===========================================================================

namespace {
constexpr uint32 kMaxRequestsPerServer        = 35;   // MAX_REQUESTS_PER_SERVER
constexpr uint32 kMaxUdpPacketData            = 510;  // MAX_UDP_PACKET_DATA
constexpr uint32 kBytesPerFileG1              = 16;   // BYTES_PER_FILE_G1
constexpr uint32 kBytesPerFileG2              = 20;   // BYTES_PER_FILE_G2
constexpr uint32 kAdditionalBytesPerLargeFile = 8;    // ADDITIONAL_BYTES_PER_LARGEFILE
} // namespace

// Walk the server list ONCE per pass, batching several files' hashes into one
// OP_GLOBGETSOURCES(2) datagram per server. Sends at most one datagram per call;
// called every tick from process() until the whole list has been covered, then
// stopUDPRequests() stamps m_lastUdpSearchTime and the pass idles. Non-wrapping
// getSuccServer() is what makes a pass terminate (unlike the old nextStatServer()).
bool DownloadQueue::sendNextUDPPacket()
{
    // Sources returned by global getsources carry no user hash, so they are
    // unusable when the crypt layer is required — don't bother asking (MFC).
    if (m_items.empty() || !m_serverConnect || !m_serverConnect->isConnected()
        || thePrefs.cryptLayerRequired())
        return false;

    ServerList* sl = theApp.serverList;
    if (!sl)
        return false;

    // The connected server is already queried over TCP — skip it over UDP.
    Server* connected = m_serverConnect->currentServer();
    if (connected)
        connected = sl->findByAddress(connected->address(), connected->port());

    // True while `s` is still a live entry (a server may be reaped between ticks).
    auto stillListed = [&](const Server* s) -> bool {
        if (!s)
            return false;
        for (const auto& up : sl->servers())
            if (up.get() == s)
                return true;
        return false;
    };

    // Advance to the next queryable server, skipping the connected + dead ones;
    // returns false (and ends the pass) once getSuccServer() runs off the tail.
    auto nextServer = [&]() -> bool {
        m_requestsSentToServer = 0;
        do {
            m_curUdpServer = sl->getSuccServer(m_curUdpServer);
            if (!m_curUdpServer) {
                stopUDPRequests();
                return false;
            }
        } while (m_curUdpServer == connected
                 || m_curUdpServer->failedCount() >= thePrefs.deadServerRetries());
        return true;
    };

    // The next download after the m_lastUdpFile cursor (or the head when the
    // cursor is null / no longer present). Sets outTail when the cursor was the
    // last item, signalling "this server's files are exhausted, switch server".
    auto stepFile = [&](bool& outTail) -> PartFile* {
        outTail = false;
        if (m_items.empty())
            return nullptr;
        if (m_lastUdpFile == nullptr)
            return m_items.front();
        auto it = std::find(m_items.begin(), m_items.end(), m_lastUdpFile);
        if (it == m_items.end())
            return m_items.front();
        if (++it == m_items.end()) {
            outTail = true;
            return nullptr;
        }
        return *it;
    };

    if (m_curUdpServer && !stillListed(m_curUdpServer))
        m_curUdpServer = nullptr;
    if (!m_curUdpServer && !nextServer())
        return false;

    bool ext2 = (m_curUdpServer->udpFlags() & SrvUdpFlag::ExtGetSources2) != 0;
    bool serverLarge = m_curUdpServer->supportsLargeFilesUDP();

    bool sent = false;
    SafeMemFile data;
    uint32 nFiles = 0;
    uint32 nLarge = 0;

    while (!isMaxFilesPerUDPServerPacketReached(nFiles, nLarge) && !sent) {
        PartFile* nextfile = nullptr;
        while (!sent &&
               !(nextfile && (nextfile->status() == PartFileStatus::Ready ||
                              nextfile->status() == PartFileStatus::Empty)))
        {
            bool tail = false;
            PartFile* cand = stepFile(tail);
            if (tail) {
                // Finished this server's files — flush any pending batch to it,
                // then move on to the next server (from its head).
                if (data.length() > 0) {
                    // Packet(SafeMemFile&) consumes the buffer via takeBuffer(),
                    // leaving `data` empty; on success `sent` is set and we break
                    // out below, so `data` is never written again this call.
                    if (sendGlobGetSourcesUDPPacket(data, ext2, nFiles, nLarge))
                        sent = true;
                    nFiles = 0;
                    nLarge = 0;
                }
                if (!nextServer())
                    return false;
                ++m_searchedServers;
                if (sent) {
                    m_lastUdpFile = nullptr;
                    break;
                }
                ext2 = (m_curUdpServer->udpFlags() & SrvUdpFlag::ExtGetSources2) != 0;
                serverLarge = m_curUdpServer->supportsLargeFilesUDP();
                nextfile = m_items.empty() ? nullptr : m_items.front();
            } else {
                nextfile = cand;
            }
            m_lastUdpFile = nextfile;
        }

        if (!sent && nextfile) {
            const uint64 fsize = nextfile->fileSize();
            const bool isLarge = fsize > UINT32_MAX;
            // Port of CPartFile::GetMaxSourcePerFileUDP() (max-sources tracked
            // globally via the pref in this port).
            const uint32 udpCap = std::min<uint32>(
                (static_cast<uint32>(thePrefs.maxSourcesPerFile()) * 3) / 4,
                MAX_SOURCES_FILE_UDP);
            if (static_cast<uint32>(nextfile->sourceCount()) < udpCap
                && (serverLarge || !isLarge))
            {
                data.writeHash16(nextfile->fileHash());
                // GETSOURCES2 carries the size; GETSOURCES1 is hash-only. The
                // format is chosen by SERVER capability, never by file size.
                if (ext2) {
                    if (isLarge) {
                        ++nLarge;
                        data.writeUInt32(0);
                        data.writeUInt64(fsize);
                    } else {
                        data.writeUInt32(static_cast<uint32>(fsize));
                    }
                }
                ++nFiles;
            }
        }
    }

    if (!sent && data.length() > 0)
        sendGlobGetSourcesUDPPacket(data, ext2, nFiles, nLarge);

    // Max 35 requests to one server per pass; when the queue is longer, rotate
    // the tail window to the head so the next server gets fresh files, then
    // advance the server (port of the MAX_REQUESTS_PER_SERVER block).
    if (m_requestsSentToServer >= kMaxRequestsPerServer) {
        if (m_items.size() > kMaxRequestsPerServer)
            std::rotate(m_items.begin(),
                        m_items.end() - kMaxRequestsPerServer, m_items.end());
        if (!nextServer())
            return false;
        ++m_searchedServers;
        m_lastUdpFile = nullptr;
    }

    return true;
}

bool DownloadQueue::isMaxFilesPerUDPServerPacketReached(uint32 nFiles,
                                                        uint32 nIncludedLargeFiles) const
{
    if (m_curUdpServer && (m_curUdpServer->udpFlags() & SrvUdpFlag::ExtGetSources)) {
        const uint32 bytesPerFile =
            (m_curUdpServer->udpFlags() & SrvUdpFlag::ExtGetSources2) ? kBytesPerFileG2
                                                                      : kBytesPerFileG1;
        const uint32 usedBytes =
            nFiles * bytesPerFile + nIncludedLargeFiles * kAdditionalBytesPerLargeFile;
        return (m_requestsSentToServer >= kMaxRequestsPerServer)
            || (usedBytes >= kMaxUdpPacketData);
    }
    // Old servers without extended getsources take one hash per packet.
    return nFiles != 0;
}

bool DownloadQueue::sendGlobGetSourcesUDPPacket(SafeMemFile& data, bool ext2Packet,
                                                uint32 nFiles, uint32 nIncludedLargeFiles)
{
    if (!m_curUdpServer || !m_serverConnect)
        return false;

    auto pkt = std::make_unique<Packet>(
        data, OP_EDONKEYPROT, ext2Packet ? OP_GLOBGETSOURCES2 : OP_GLOBGETSOURCES);
    const uint32 pktSize = pkt->size;

    if (theApp.statistics)
        theApp.statistics->addUpDataOverheadServer(pktSize);

    logDebug(QStringLiteral("DownloadQueue: sending %1 to server %2:%3 (%4 files, %5 large)")
                 .arg(ext2Packet ? QStringLiteral("OP_GlobGetSources2")
                                 : QStringLiteral("OP_GlobGetSources"))
                 .arg(m_curUdpServer->address())
                 .arg(m_curUdpServer->port())
                 .arg(nFiles)
                 .arg(nIncludedLargeFiles));

    m_serverConnect->sendUDPPacket(std::move(pkt), *m_curUdpServer,
                                   static_cast<uint16>(m_curUdpServer->port() + 4));
    m_requestsSentToServer += nFiles;
    return true;
}

void DownloadQueue::stopUDPRequests()
{
    m_curUdpServer = nullptr;
    m_lastUdpFile = nullptr;
    m_searchedServers = 0;
    m_requestsSentToServer = 0;
    m_lastUdpSearchTime = static_cast<uint32>(getTickCount());
}

// ===========================================================================
// File completion integration
// ===========================================================================

void DownloadQueue::connectPartFileSignals(PartFile* file)
{
    if (!file)
        return;

    connect(file->partNotifier(), &PartFileNotifier::downloadCompleted,
            this, [this, file]() { onDownloadCompleted(file); },
            Qt::QueuedConnection);
}

// EntityList hooks — invoked from addEntity()/removeEntity() after the list
// mutation. They carry the queue-specific side-effects that used to live inline
// in addDownload()/removeFile().

void DownloadQueue::onEntityAdded(PartFile* file)
{
    sortByPriority();
    logInfo(QStringLiteral("Download started: %1").arg(file->fileName()));
    emit fileAdded(file);
}

void DownloadQueue::onEntityRemoved(PartFile* file)
{
    // Keep the global-UDP-source file cursor from dangling on the freed file.
    // (getSuccServer() already tolerates a removed server cursor.)
    if (file == m_lastUdpFile)
        m_lastUdpFile = nullptr;

    // Part files are shared files now, so a file leaving the queue has to leave the
    // shared map with it — the caller deletes the object next, and a stale pointer in
    // SharedFileList::m_map would be read by the next server offer or Kad publish.
    // Only while it is still a part file: a finished download is a legitimate shared
    // file owned by KnownFileList, and "Clear Completed" routes through here too.
    // MFC removes at srchybrid/PartFile.cpp:3083. Marking the hash unshared is
    // harmless — it is not a re-add gate, and safeAddKFile() clears it.
    if (m_sharedFileList && file->isPartFile())
        m_sharedFileList->removeFile(file);

    if (file->status() != PartFileStatus::Complete)
        ++m_failedDownCount;
    sortByPriority();
    emit fileRemoved(file);
}

void DownloadQueue::setCatStatus(uint32 category, bool paused)
{
    for (auto* file : m_items) {
        if (file->category() == category) {
            if (paused)
                file->pauseFile();
            else
                file->resumeFile();
        }
    }
}

bool DownloadQueue::hasActiveTransfers() const
{
    for (const auto* file : m_items) {
        if (file->transferringSrcCount() > 0)
            return true;
    }
    return false;
}

uint32 DownloadQueue::averageDownTime() const
{
    return m_successfulDownCount > 0
        ? static_cast<uint32>(m_totalDownTime / m_successfulDownCount)
        : 0;
}

void DownloadQueue::onDownloadCompleted(PartFile* file)
{
    if (!file)
        return;

    ++m_successfulDownCount;
    m_totalDownTime += file->dlActiveTime();

    logInfo(QStringLiteral("Download completed: %1").arg(file->fileName()));

    // Phase 1: Active sources — try to swap each to another pending file.
    // Send OP_CANCELTRANSFER first so the server stops uploading the
    // completed file's data, freeing the TCP buffer for control packets.
    // swapToAnotherFile() requires m_reqFile != nullptr, so call before cleanup.
    // doSwap() mutates srcList, so iterate a copy.
    auto sources = file->srcList();
    for (auto* client : sources) {
        client->sendCancelTransfer();

        if (client->swapToAnotherFile(
                QStringLiteral("download completed"),
                /*ignoreNoNeeded=*/true,
                /*ignoreSuspensions=*/true,
                /*removeCompletely=*/true))
        {
            // Swap succeeded — set state to None so PartFile::process()
            // picks up this source and re-initiates the download via
            // tryToConnect().  (MFC DoSwap: SetDownloadState(DS_NONE))
            client->setDownloadState(DownloadState::None);
        } else {
            // No other file available — just disconnect
            client->setDownloadState(DownloadState::None);
            client->setReqFile(nullptr);
            file->removeSource(client);
        }
        // Purge completed file from client's other-requests lists
        client->removeFileFromOtherLists(file);
    }

    // Phase 2: A4AF sources — already serving another file, just clean stale refs.
    auto a4afSources = file->a4afSrcList();
    for (auto* client : a4afSources) {
        client->removeFileFromOtherLists(file);
    }
    file->a4afSrcList().clear();

    // Add to KnownFileList
    if (m_knownFileList)
        m_knownFileList->safeAddKFile(file);

    // Add to SharedFileList
    if (m_sharedFileList)
        m_sharedFileList->safeAddKFile(file);

    // Keep completed file in the queue so it remains visible in the UI.
    // It will be skipped by process() loops (status != Ready/Empty).
    // Explicit removal happens via "Clear Completed" → removeFile().

    emit fileCompleted(file);
}

// ===========================================================================
// Kad file request rate-limiter
// ===========================================================================

bool DownloadQueue::doKademliaFileRequest() const
{
    return static_cast<uint32>(getTickCount()) >= m_lastKademliaFileRequest + KADEMLIAASKTIME;
}

void DownloadQueue::setLastKademliaFileRequest()
{
    m_lastKademliaFileRequest = static_cast<uint32>(getTickCount());
}

} // namespace eMule
