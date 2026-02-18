/// @file DownloadQueue.cpp
/// @brief Download queue manager — port of MFC CDownloadQueue.
///
/// Manages in-progress downloads with IPFilter, dead source, and dedup checks.
/// Includes UDP source re-ask batching and server-based source queries.

#include "transfer/DownloadQueue.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/DeadSourceList.h"
#include "client/UpDownClient.h"
#include "crypto/FileIdentifier.h"
#include "files/KnownFileList.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "ipfilter/IPFilter.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/ED2KLink.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "server/Server.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"
#include "utils/TimeUtils.h"

#include <QDir>
#include <QDirIterator>
#include <QStorageInfo>

#include <algorithm>
#include <cstring>

namespace eMule {

// ===========================================================================
// Construction / Destruction
// ===========================================================================

DownloadQueue::DownloadQueue(QObject* parent)
    : QObject(parent)
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

            if (result != PartFileLoadResult::LoadSuccess) {
                // Try .bak backup
                const QString bakFile = filename + QStringLiteral(".bak");
                if (QFile::exists(directory + QDir::separator() + bakFile)) {
                    // Restore from backup
                    QFile::copy(directory + QDir::separator() + bakFile,
                                directory + QDir::separator() + filename);
                    result = partFile->loadPartFile(directory, filename);
                }
            }

            if (result == PartFileLoadResult::LoadSuccess) {
                connectPartFileSignals(partFile);
                m_fileList.push_back(partFile);
                logInfo(QStringLiteral("Loaded part file: %1").arg(partFile->fileName()));
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
// File management
// ===========================================================================

void DownloadQueue::addDownload(PartFile* file, bool paused)
{
    if (!file)
        return;

    // Check for duplicate
    if (isFileExisting(file->fileHash()))
        return;

    if (paused)
        file->pauseFile();

    connectPartFileSignals(file);
    m_fileList.push_back(file);
    sortByPriority();

    emit fileAdded(file);
}

void DownloadQueue::removeFile(PartFile* file)
{
    if (!file)
        return;

    auto it = std::ranges::find(m_fileList, file);
    if (it != m_fileList.end()) {
        m_fileList.erase(it);
        sortByPriority();
        emit fileRemoved(file);
    }
}

void DownloadQueue::deleteAll()
{
    for (auto* file : m_fileList)
        delete file;
    m_fileList.clear();
}

// ===========================================================================
// Lookup
// ===========================================================================

PartFile* DownloadQueue::fileByID(const uint8* hash) const
{
    if (!hash)
        return nullptr;

    for (auto* file : m_fileList) {
        if (md4equ(file->fileHash(), hash))
            return file;
    }
    return nullptr;
}

PartFile* DownloadQueue::fileByIndex(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_fileList.size()))
        return nullptr;
    return m_fileList[static_cast<size_t>(index)];
}

bool DownloadQueue::isFileExisting(const uint8* hash) const
{
    return fileByID(hash) != nullptr;
}

// ===========================================================================
// Source management
// ===========================================================================

bool DownloadQueue::checkAndAddSource(PartFile* file, UpDownClient* source)
{
    if (!file || !source)
        return false;

    // IPFilter check — reject filtered IPs
    if (m_ipFilter && source->userIP() != 0) {
        if (m_ipFilter->isFiltered(source->userIP())) {
            logDebug(QStringLiteral("Source rejected by IPFilter: %1").arg(source->userIP()));
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
        key.serverIP = source->serverIP();
        if (m_clientList->globalDeadSourceList.isDeadSource(key)) {
            logDebug(QStringLiteral("Source rejected — dead source: %1").arg(source->userIP()));
            return false;
        }
    }

    // ClientList dedup across files — check if a matching client already exists globally
    if (m_clientList) {
        UpDownClient* existing = nullptr;
        if (source->hasValidHash())
            existing = m_clientList->findByUserHash(source->userHash(), source->userIP(), source->userPort());
        if (!existing && source->userIP() != 0)
            existing = m_clientList->findByIP(source->userIP(), source->userPort());
        if (existing && existing != source) {
            // A global client already exists — don't add duplicate source
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
            if (md4equ(existing->userHash(), source->userHash()))
                return false;
        }

        // Compare by IP:port
        if (existing->userIP() == source->userIP() &&
            existing->userPort() == source->userPort() &&
            existing->userIP() != 0)
        {
            return false;
        }
    }

    // Check max sources per file
    if (file->sourceCount() >= thePrefs.maxSourcesPerFile())
        return false;

    file->addSource(source);
    return true;
}

void DownloadQueue::removeSource(UpDownClient* source)
{
    if (!source)
        return;

    for (auto* file : m_fileList)
        file->removeSource(source);
}

void DownloadQueue::addKadSourceResult(uint32 searchID, const uint8* fileHash,
                                        uint32 ip, uint16 tcpPort,
                                        uint32 buddyIP, uint16 buddyPort,
                                        uint8 buddyCrypt)
{
    Q_UNUSED(searchID);

    PartFile* file = fileByID(fileHash);
    if (!file)
        return;

    // Don't exceed max sources per file
    if (file->sourceCount() >= thePrefs.maxSourcesPerFile())
        return;

    // Create a new client for this Kad source
    auto* client = new UpDownClient(tcpPort, 0, 0, 0, file);
    client->setConnectIP(ip);
    client->setSourceFrom(SourceFrom::Kademlia);

    // Set buddy info for firewalled sources
    if (buddyIP != 0) {
        client->setBuddyIP(buddyIP);
        client->setBuddyPort(buddyPort);
        // Decode buddy crypt options
        client->setConnectOptions(buddyCrypt, true, true);
    }

    // IPFilter + dead source + dedup checks
    if (checkAndAddSource(file, client)) {
        logDebug(QStringLiteral("Kad source added for %1: IP=%2:%3")
                     .arg(file->fileName())
                     .arg(ip).arg(tcpPort));
        client->tryToConnect();
    } else {
        delete client;
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
    logInfo(QStringLiteral("addDownloadFromED2KLink: started download: %1").arg(fileLink->name));
    return true;
}

// ===========================================================================
// addServerSourceResult
// ===========================================================================

void DownloadQueue::addServerSourceResult(const uint8* data, uint32 size, bool obfuscated)
{
    // Packet format: fileHash[16] + sourceCount[1] + per-source data
    if (!data || size < 17)
        return;

    // Extract file hash (first 16 bytes)
    PartFile* file = fileByID(data);
    if (!file)
        return;

    const uint8 sourceCount = data[16];
    uint32 offset = 17;

    // Get server IP/port for low-ID source construction
    uint32 srvIP = 0;
    uint16 srvPort = 0;
    if (m_serverConnect) {
        if (auto* srv = m_serverConnect->currentServer()) {
            srvIP = srv->ip();
            srvPort = srv->port();
        }
    }

    for (uint8 i = 0; i < sourceCount; ++i) {
        // Each source: userId[4] + port[2] = 6 bytes minimum
        if (offset + 6 > size)
            break;

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
                break;
            cryptFlags = data[offset];
            offset += 1;

            if ((cryptFlags & 0x80) != 0) {
                if (offset + 16 > size)
                    break;
                std::memcpy(userHash.data(), data + offset, 16);
                offset += 16;
                hasHash = true;
            }
        }

        // Validate source
        if (!isLowID(userId) && !isGoodIP(userId))
            continue;

        if (file->sourceCount() >= static_cast<int>(thePrefs.maxSourcesPerFile()))
            break;

        auto* client = new UpDownClient(port, userId, srvIP, srvPort, file, true);
        client->setSourceFrom(SourceFrom::Server);

        if (obfuscated)
            client->setConnectOptions(cryptFlags, true, false);

        if (hasHash)
            client->setUserHash(userHash.data());

        if (checkAndAddSource(file, client)) {
            client->tryToConnect();
        } else {
            delete client;
        }
    }
}

// ===========================================================================
// Queue operations
// ===========================================================================

void DownloadQueue::startNextFile(int category)
{
    PartFile* bestFile = nullptr;

    for (auto* file : m_fileList) {
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
    std::ranges::sort(m_fileList, [](const PartFile* a, const PartFile* b) {
        // rightFileHasHigherPrio(a, b) returns true when b has higher prio.
        // We want higher-prio files first, so swap the arguments.
        return PartFile::rightFileHasHigherPrio(b, a);
    });
}

void DownloadQueue::process()
{
    m_datarate = 0;
    m_udCounter = (m_udCounter + 1) % 10;

    for (auto* file : m_fileList) {
        if (file->status() != PartFileStatus::Ready &&
            file->status() != PartFileStatus::Empty)
            continue;

        const uint32 rate = file->process(0, m_udCounter);
        m_datarate += rate;

        // Check for completion
        if (file->status() == PartFileStatus::Complete)
            emit fileCompleted(file);
    }

    // UDP source request batching — send re-ask requests via UDP
    const uint32 curTick = static_cast<uint32>(getTickCount());
    if (curTick >= m_lastUDPSourceRequestTime + FILEREASKTIME) {
        m_lastUDPSourceRequestTime = curTick;
        for (auto* file : m_fileList) {
            if (file->status() != PartFileStatus::Ready &&
                file->status() != PartFileStatus::Empty)
                continue;
            // Trigger UDP re-asks for each source that supports UDP
            for (auto* src : file->srcList()) {
                if (src->supportsUDP() && src->isSourceRequestAllowed())
                    src->udpReaskForDownload();
            }
        }
    }

    // Server-based source queries via UDP
    if (m_serverConnect && m_serverConnect->isConnected() &&
        curTick >= m_lastServerSourceRequestTime + UDPSERVERREASKTIME)
    {
        m_lastServerSourceRequestTime = curTick;
        if (theApp.serverList) {
            for (auto* file : m_fileList) {
                if (file->status() != PartFileStatus::Ready &&
                    file->status() != PartFileStatus::Empty)
                    continue;
                if (file->sourceCount() >= static_cast<int>(thePrefs.maxSourcesPerFile()))
                    continue;

                // Send OP_GLOBGETSOURCES2 to a few servers for each file needing sources
                const uint8* hash = file->fileHash();
                const uint64 fsize = file->fileSize();
                // Build packet: 16 bytes hash + 4 bytes size (or 8 for large files)
                bool largeFile = (fsize > UINT32_MAX);
                uint32 pktSize = largeFile ? 24u : 20u;
                auto pkt = std::make_unique<Packet>(
                    largeFile ? OP_GLOBGETSOURCES2 : OP_GLOBGETSOURCES,
                    pktSize, OP_EDONKEYPROT);
                md4cpy(pkt->pBuffer, hash);
                if (largeFile) {
                    std::memcpy(pkt->pBuffer + 16, &fsize, 8);
                } else {
                    uint32 fsize32 = static_cast<uint32>(fsize);
                    std::memcpy(pkt->pBuffer + 16, &fsize32, 4);
                }
                // Send to a server from the list
                Server* srv = theApp.serverList->nextStatServer();
                if (srv) {
                    m_serverConnect->sendUDPPacket(
                        std::move(pkt), *srv, static_cast<uint16>(srv->port() + 4));
                }
            }
        }
    }
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

void DownloadQueue::setCatStatus(uint32 category, bool paused)
{
    for (auto* file : m_fileList) {
        if (file->category() == category) {
            if (paused)
                file->pauseFile();
            else
                file->resumeFile();
        }
    }
}

void DownloadQueue::onDownloadCompleted(PartFile* file)
{
    if (!file)
        return;

    logInfo(QStringLiteral("DownloadQueue: file completed: %1").arg(file->fileName()));

    // Add to KnownFileList
    if (m_knownFileList)
        m_knownFileList->safeAddKFile(file);

    // Add to SharedFileList
    if (m_sharedFileList)
        m_sharedFileList->safeAddKFile(file);

    // Remove from download queue
    auto it = std::ranges::find(m_fileList, file);
    if (it != m_fileList.end()) {
        m_fileList.erase(it);
        sortByPriority();
    }

    emit fileCompleted(file);
}

} // namespace eMule
