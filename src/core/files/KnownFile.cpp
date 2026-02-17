/// @file KnownFile.cpp
/// @brief Known (completed) file — partial port of MFC CKnownFile.
///
/// Core file metadata, priority, upload client tracking, and media metadata.
/// GUI-dependent code (BarShader, CxImage, FrameGrabThread) is decoupled
/// via FileNotifier signal emissions.

#include "files/KnownFile.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "crypto/AICHHashSet.h"
#include "crypto/AICHHashTree.h"
#include "crypto/MD4Hash.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadMiscUtils.h"
#include "media/MediaInfo.h"
#include "net/Packet.h"
#include "protocol/Tag.h"
#include "utils/Log.h"
#include "utils/SafeFile.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstring>
#include <ctime>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

KnownFile::KnownFile()
{
    m_utcLastModified = static_cast<time_t>(-1);
    m_completeSourcesCount = 1;
    m_completeSourcesCountLo = 1;
    m_completeSourcesCountHi = 1;
}

// ---------------------------------------------------------------------------
// setFileSize — computes data part count and ED2K part count
// ---------------------------------------------------------------------------

void KnownFile::setFileSize(EMFileSize size)
{
    ShareableFile::setFileSize(size);

    // Data part count: ceil(fileSize / PARTSIZE), minimum 1 for non-zero files
    if (size == 0) {
        m_partCount = 0;
        m_ed2kPartCount = 0;
    } else {
        m_partCount = static_cast<uint16>((static_cast<uint64>(size) + PARTSIZE - 1) / PARTSIZE);
        // ED2K part count includes an extra part when file is an exact multiple of PARTSIZE
        // (because the hash list has partCount+1 entries in that case for verification)
        m_ed2kPartCount = static_cast<uint16>(static_cast<uint64>(size) / PARTSIZE + 1);
    }
}

// ---------------------------------------------------------------------------
// setFileName
// ---------------------------------------------------------------------------

void KnownFile::setFileName(const QString& name,
                            bool replaceInvalidChars,
                            bool autoSetFileType,
                            bool removeControlChars)
{
    ShareableFile::setFileName(name, replaceInvalidChars, autoSetFileType, removeControlChars);

    // Rebuild Kad keyword list from new filename
    m_kadKeywords.clear();
    kad::getWords(fileName(), m_kadKeywords);
}

// ---------------------------------------------------------------------------
// Serialization — known.met format
// ---------------------------------------------------------------------------

bool KnownFile::loadFromFile(FileDataIO& file)
{
    // Date
    if (!loadDateFromFile(file))
        return false;

    // MD4 hashset
    if (!fileIdentifier().loadMD4HashsetFromFile(file, false))
        return false;

    // Tags
    if (!loadTagsFromFile(file))
        return false;

    // Recompute part counts from the loaded file size
    setFileSize(fileSize());

    return true;
}

bool KnownFile::loadDateFromFile(FileDataIO& file)
{
    m_utcLastModified = static_cast<time_t>(file.readUInt32());
    return true;
}

bool KnownFile::loadTagsFromFile(FileDataIO& file)
{
    const uint32 tagCount = file.readUInt32();

    for (uint32 i = 0; i < tagCount; ++i) {
        Tag tag(file, true);
        switch (tag.nameId()) {
        case FT_FILENAME:
            if (tag.isStr()) {
                if (fileName().isEmpty())
                    setFileName(tag.strValue(), true);
            }
            break;
        case FT_FILESIZE:
            if (tag.isInt())
                setFileSize(tag.intValue());
            else if (tag.isInt64(false))
                setFileSize(tag.int64Value());
            break;
        case FT_FILESIZE_HI:
            if (tag.isInt()) {
                // Combine with existing 32-bit size
                auto hi = static_cast<uint64>(tag.intValue());
                setFileSize((hi << 32) | static_cast<uint64>(fileSize()));
            }
            break;
        case FT_ATTRANSFERRED:
            if (tag.isInt())
                statistic.setAllTimeTransferred(
                    (statistic.allTimeTransferred() & 0xFFFFFFFF00000000ULL)
                    | tag.intValue());
            else if (tag.isInt64(false))
                statistic.setAllTimeTransferred(tag.int64Value());
            break;
        case FT_ATTRANSFERREDHI:
            if (tag.isInt()) {
                auto hi = static_cast<uint64>(tag.intValue());
                statistic.setAllTimeTransferred(
                    (hi << 32)
                    | (statistic.allTimeTransferred() & 0xFFFFFFFFULL));
            }
            break;
        case FT_ATREQUESTED:
            if (tag.isInt())
                statistic.setAllTimeRequests(tag.intValue());
            break;
        case FT_ATACCEPTED:
            if (tag.isInt())
                statistic.setAllTimeAccepts(tag.intValue());
            break;
        case FT_ULPRIORITY:
            if (tag.isInt()) {
                auto val = static_cast<uint8>(tag.intValue());
                if (val == kPrAuto) {
                    m_autoUpPriority = true;
                    m_upPriority = kPrNormal;
                } else {
                    m_autoUpPriority = false;
                    if (val <= kPrVeryLow)
                        m_upPriority = val;
                    else
                        m_upPriority = kPrNormal;
                }
            }
            break;
        case FT_KADLASTPUBLISHSRC:
            if (tag.isInt())
                m_lastPublishTimeKadSrc = static_cast<time_t>(tag.intValue());
            break;
        case FT_KADLASTPUBLISHNOTES:
            if (tag.isInt())
                m_lastPublishTimeKadNotes = static_cast<time_t>(tag.intValue());
            break;
        case FT_FLAGS:
            // bit 0 = not auto-upload-priority (inverted)
            if (tag.isInt())
                m_autoUpPriority = (tag.intValue() & 0x01) == 0;
            break;
        case FT_AICH_HASH:
            if (tag.isStr()) {
                AICHHash aichHash;
                if (decodeBase32(tag.strValue(),
                                 aichHash.getRawHash(),
                                 kAICHHashSize) == kAICHHashSize)
                {
                    fileIdentifier().setAICHHash(aichHash);
                    m_aichRecoverHashSetAvailable = true;
                }
            }
            break;
        case FT_LASTSHARED:
            if (tag.isInt())
                m_timeLastSeen = static_cast<time_t>(tag.intValue());
            break;
        case FT_AICHHASHSET:
            if (tag.isBlob()) {
                const auto& blob = tag.blobValue();
                SafeMemFile hashsetFile(
                    reinterpret_cast<const uint8*>(blob.constData()),
                    blob.size());
                fileIdentifier().loadAICHHashsetFromFile(hashsetFile, false);
            }
            break;
        default:
            addTagUnique(std::move(tag));
            break;
        }
    }

    return true;
}

bool KnownFile::writeToFile(FileDataIO& file) const
{
    // Date
    file.writeUInt32(static_cast<uint32>(m_utcLastModified));

    // MD4 hashset
    fileIdentifier().writeMD4HashsetToFile(file);

    // Count tags
    uint32 tagCount = 0;

    // Mandatory: name, size
    tagCount += 1; // FT_FILENAME
    tagCount += 1; // FT_FILESIZE (32 or 64 bit)
    if (isLargeFile())
        tagCount += 1; // FT_FILESIZE_HI

    // AICH hash
    if (fileIdentifier().hasAICHHash())
        ++tagCount;

    // Last shared timestamp
    if (m_timeLastSeen > 0)
        ++tagCount;

    // Statistics
    if (statistic.allTimeTransferred() > 0)
        ++tagCount;
    if (static_cast<uint64>(statistic.allTimeTransferred()) > UINT32_MAX)
        ++tagCount; // FT_ATTRANSFERREDHI
    if (statistic.allTimeRequests() > 0)
        ++tagCount;
    if (statistic.allTimeAccepts() > 0)
        ++tagCount;

    // Priority
    tagCount += 1; // FT_ULPRIORITY
    tagCount += 1; // FT_FLAGS

    // Kad publish times
    if (m_lastPublishTimeKadSrc > 0)
        ++tagCount;
    if (m_lastPublishTimeKadNotes > 0)
        ++tagCount;

    // AICH hashset blob
    bool writeAICHHashset = fileIdentifier().hasAICHHash()
                            && fileIdentifier().hasExpectedAICHHashCount();
    if (writeAICHHashset)
        ++tagCount;

    // Extra tags
    tagCount += static_cast<uint32>(tags().size());

    file.writeUInt32(tagCount);

    // -- Write individual tags --

    // Filename
    Tag(FT_FILENAME, fileName()).writeNewEd2kTag(file, UTF8Mode::OptBOM);

    // File size
    if (isLargeFile()) {
        Tag(FT_FILESIZE, static_cast<uint32>(static_cast<uint64>(fileSize()) & 0xFFFFFFFFu))
            .writeNewEd2kTag(file);
        Tag(FT_FILESIZE_HI, static_cast<uint32>(static_cast<uint64>(fileSize()) >> 32))
            .writeNewEd2kTag(file);
    } else {
        Tag(FT_FILESIZE, static_cast<uint32>(fileSize()))
            .writeNewEd2kTag(file);
    }

    // AICH hash
    if (fileIdentifier().hasAICHHash())
        Tag(FT_AICH_HASH, fileIdentifier().getAICHHash().getString())
            .writeNewEd2kTag(file, UTF8Mode::Raw);

    // Last shared
    if (m_timeLastSeen > 0)
        Tag(FT_LASTSHARED, static_cast<uint32>(m_timeLastSeen))
            .writeNewEd2kTag(file);

    // Statistics — all-time transferred
    if (statistic.allTimeTransferred() > 0) {
        if (statistic.allTimeTransferred() > UINT32_MAX) {
            Tag(FT_ATTRANSFERRED,
                static_cast<uint32>(statistic.allTimeTransferred() & 0xFFFFFFFFu))
                .writeNewEd2kTag(file);
            Tag(FT_ATTRANSFERREDHI,
                static_cast<uint32>(statistic.allTimeTransferred() >> 32))
                .writeNewEd2kTag(file);
        } else {
            Tag(FT_ATTRANSFERRED,
                static_cast<uint32>(statistic.allTimeTransferred()))
                .writeNewEd2kTag(file);
        }
    }

    // Statistics — requests, accepts
    if (statistic.allTimeRequests() > 0)
        Tag(FT_ATREQUESTED, statistic.allTimeRequests())
            .writeNewEd2kTag(file);
    if (statistic.allTimeAccepts() > 0)
        Tag(FT_ATACCEPTED, statistic.allTimeAccepts())
            .writeNewEd2kTag(file);

    // Priority
    Tag(FT_ULPRIORITY,
        static_cast<uint32>(m_autoUpPriority ? kPrAuto : m_upPriority))
        .writeNewEd2kTag(file);

    // Flags (bit 0 = NOT auto-priority)
    Tag(FT_FLAGS, static_cast<uint32>(m_autoUpPriority ? 0u : 1u))
        .writeNewEd2kTag(file);

    // Kad timestamps
    if (m_lastPublishTimeKadSrc > 0)
        Tag(FT_KADLASTPUBLISHSRC, static_cast<uint32>(m_lastPublishTimeKadSrc))
            .writeNewEd2kTag(file);
    if (m_lastPublishTimeKadNotes > 0)
        Tag(FT_KADLASTPUBLISHNOTES, static_cast<uint32>(m_lastPublishTimeKadNotes))
            .writeNewEd2kTag(file);

    // AICH hashset blob
    if (writeAICHHashset) {
        SafeMemFile tmpFile;
        fileIdentifier().writeAICHHashsetToFile(tmpFile);
        const auto& buf = tmpFile.buffer();
        Tag(FT_AICHHASHSET,
            QByteArray(buf.constData(), buf.size()))
            .writeNewEd2kTag(file);
    }

    // Extra tags
    for (const auto& tag : tags())
        tag.writeNewEd2kTag(file, UTF8Mode::OptBOM);

    return true;
}

// ---------------------------------------------------------------------------
// Purge check
// ---------------------------------------------------------------------------

bool KnownFile::shouldPartiallyPurgeFile() const
{
    return std::time(nullptr) - m_timeLastSeen > DAY2S(31);
}

// ---------------------------------------------------------------------------
// Priority
// ---------------------------------------------------------------------------

void KnownFile::setUpPriority(uint8 priority, bool /*save*/)
{
    switch (priority) {
    case kPrVeryLow:
    case kPrLow:
    case kPrNormal:
    case kPrHigh:
    case kPrVeryHigh:
        m_upPriority = priority;
        break;
    default:
        m_upPriority = kPrNormal;
        break;
    }
    emit m_notifier.priorityChanged(m_upPriority);
}

// ---------------------------------------------------------------------------
// ED2K publishing
// ---------------------------------------------------------------------------

void KnownFile::setPublishedED2K(bool val)
{
    m_publishedED2K = val;
    emit m_notifier.fileUpdated();
}

void KnownFile::setLastPublishTimeKadSrc(time_t t, uint32 buddyIP)
{
    m_lastPublishTimeKadSrc = t;
    m_lastBuddyIP = buddyIP;
}

// ---------------------------------------------------------------------------
// Upload client tracking
// ---------------------------------------------------------------------------

void KnownFile::addUploadingClient(UpDownClient* client)
{
    if (!client)
        return;
    if (std::ranges::find(m_uploadingClients, client) != m_uploadingClients.end())
        return; // already present
    m_uploadingClients.push_back(client);
    updateAutoUpPriority();
    emit m_notifier.fileUpdated();
}

void KnownFile::removeUploadingClient(UpDownClient* client)
{
    auto it = std::ranges::find(m_uploadingClients, client);
    if (it == m_uploadingClients.end())
        return;
    m_uploadingClients.erase(it);
    updateAutoUpPriority();
    emit m_notifier.fileUpdated();
}

// ---------------------------------------------------------------------------
// Auto-priority — ported from MFC CKnownFile::UpdateAutoUpPriority
// ---------------------------------------------------------------------------

void KnownFile::updateAutoUpPriority()
{
    if (!m_autoUpPriority)
        return;

    const auto count = m_uploadingClients.size();
    uint8 newPriority;
    if (count > 20)
        newPriority = kPrLow;
    else if (count > 1)
        newPriority = kPrNormal;
    else
        newPriority = kPrHigh;

    if (m_upPriority != newPriority) {
        m_upPriority = newPriority;
        emit m_notifier.priorityChanged(m_upPriority);
    }
}

// ---------------------------------------------------------------------------
// Media metadata
// ---------------------------------------------------------------------------

static constexpr int kMaxED2KMetaTagLen = 128;

void KnownFile::updateMetaDataTags()
{
    // Remove old media tags first
    removeMetaDataTags();

    MediaInfo info;
    if (!extractMediaInfo(filePath(), info))
        return;

    // FT_MEDIA_LENGTH — duration in seconds (as integer)
    if (info.lengthSec > 0.0) {
        auto lengthSec = static_cast<uint32>(info.lengthSec + 0.5);
        if (lengthSec > 0)
            addTagUnique(Tag(FT_MEDIA_LENGTH, lengthSec));
    }

    // FT_MEDIA_BITRATE — kbps (video bitrate, or audio if no video)
    uint32 bitrate = 0;
    if (info.videoStreamCount > 0 && info.video.bitRate > 0)
        bitrate = info.video.bitRate / 1000;
    else if (info.audioStreamCount > 0 && info.audio.avgBytesPerSec > 0)
        bitrate = info.audio.avgBytesPerSec * 8 / 1000;
    if (bitrate > 0)
        addTagUnique(Tag(FT_MEDIA_BITRATE, bitrate));

    // FT_MEDIA_CODEC — short codec identifier
    QString codec;
    if (info.videoStreamCount > 0 && info.video.codecTag != 0)
        codec = videoFormatName(info.video.codecTag);
    else if (info.audioStreamCount > 0 && info.audio.formatTag != 0)
        codec = audioFormatCodecId(info.audio.formatTag);
    if (!codec.isEmpty())
        addTagUnique(Tag(FT_MEDIA_CODEC, codec.left(kMaxED2KMetaTagLen)));

    // FT_MEDIA_ARTIST
    if (!info.author.isEmpty())
        addTagUnique(Tag(FT_MEDIA_ARTIST, info.author.left(kMaxED2KMetaTagLen)));

    // FT_MEDIA_ALBUM
    if (!info.album.isEmpty())
        addTagUnique(Tag(FT_MEDIA_ALBUM, info.album.left(kMaxED2KMetaTagLen)));

    // FT_MEDIA_TITLE
    if (!info.title.isEmpty())
        addTagUnique(Tag(FT_MEDIA_TITLE, info.title.left(kMaxED2KMetaTagLen)));

    m_metaDataVer = 1;
    emit m_notifier.metadataUpdated();
}

void KnownFile::removeMetaDataTags()
{
    static constexpr uint8 mediaTagIds[] = {
        FT_MEDIA_ARTIST, FT_MEDIA_ALBUM, FT_MEDIA_TITLE,
        FT_MEDIA_LENGTH, FT_MEDIA_BITRATE, FT_MEDIA_CODEC
    };
    for (auto id : mediaTagIds)
        deleteTag(id);
    m_metaDataVer = 0;
}

// ---------------------------------------------------------------------------
// Frame grabbing request — emits signal, core does not spawn threads
// ---------------------------------------------------------------------------

void KnownFile::requestGrabFrames(uint8 count, double startTime,
                                   bool reduceColor, uint16 maxWidth)
{
    if (getED2KFileTypeID(fileName()) != ED2KFileType::Video)
        return;
    emit m_notifier.grabFramesRequested(filePath(), count, startTime,
                                         reduceColor, maxWidth);
}

// ---------------------------------------------------------------------------
// Stubs
// ---------------------------------------------------------------------------

void KnownFile::updateFileRatingCommentAvail(bool /*forceUpdate*/)
{
    bool hasNewComment = false;
    uint32 ratingSum = 0;
    uint32 ratingCount = 0;

    // Aggregate ratings and comments from Kad notes cache
    for (const auto& [rating, comment] : m_kadNotesCache) {
        if (!comment.isEmpty())
            hasNewComment = true;
        if (rating > 0 && rating <= 5) {
            ratingSum += rating;
            ++ratingCount;
        }
    }

    bool changed = false;

    if (hasNewComment != m_hasComment) {
        m_hasComment = hasNewComment;
        changed = true;
    }

    uint32 newRating = (ratingCount > 0) ? (ratingSum / ratingCount) : 0;
    if (newRating != m_userRating) {
        m_userRating = newRating;
        changed = true;
    }

    if (changed)
        emit m_notifier.fileUpdated();
}

bool KnownFile::publishSrc()
{
    time_t tNow = std::time(nullptr);
    uint32 buddyIP = 0;

    auto* kadInst = kad::Kademlia::instance();
    if (kadInst && kadInst->isFirewalled()) {
        // TCP firewalled — check UDP firewall too
        if (kad::UDPFirewallTester::isFirewalledUDP(true)) {
            // Both TCP and UDP firewalled — need a buddy
            auto* clientList = kad::Kademlia::getClientList();
            auto* buddy = clientList ? clientList->getBuddy() : nullptr;
            if (!buddy)
                return false;

            buddyIP = buddy->userIP();
            // If buddy IP changed, reset publish time to force re-publish
            if (m_lastBuddyIP != 0 && m_lastBuddyIP != buddyIP)
                setLastPublishTimeKadSrc(0, 0);
        }
    } else {
        buddyIP = 0;
    }

    if (tNow < m_lastPublishTimeKadSrc)
        return false;

    setLastPublishTimeKadSrc(tNow + KADEMLIAREPUBLISHTIMES, buddyIP);
    return true;
}

bool KnownFile::publishNotes()
{
    // Check both the loaded comment/rating and the FT_FILERATING tag
    bool hasNotes = !getFileComment().isEmpty() || getFileRating() > 0;
    if (!hasNotes) {
        const Tag* ratingTag = getTag(FT_FILERATING);
        hasNotes = ratingTag && ratingTag->isInt() && ratingTag->intValue() > 0;
    }
    if (!hasNotes)
        return false;

    time_t tNow = time(nullptr);
    if (tNow < m_lastPublishTimeKadNotes)
        return false;

    m_lastPublishTimeKadNotes = tNow + KADEMLIAREPUBLISHTIMEN;
    return true;
}

std::unique_ptr<Packet> KnownFile::createSrcInfoPacket(
    const UpDownClient* /*forClient*/, uint8 version, uint16 /*options*/) const
{
    if (m_uploadingClients.empty())
        return nullptr;

    // Build source info packet: file hash + source count + per-source data
    SafeMemFile data;

    // File hash (16 bytes)
    data.writeHash16(fileHash());

    // Limit number of sources based on protocol version
    const uint16 maxSources = (version >= 4) ? 500 : 50;
    const auto srcCount = static_cast<uint16>(
        std::min(static_cast<size_t>(maxSources), m_uploadingClients.size()));

    data.writeUInt16(srcCount);

    for (uint16 i = 0; i < srcCount; ++i) {
        const auto* client = m_uploadingClients[i];

        // Client ID (4 bytes) — use IP as client ID for high-ID clients
        data.writeUInt32(client->userIP());

        // Client port (2 bytes)
        data.writeUInt16(client->userPort());

        // Server IP (4 bytes) — 0 for now (no server tracking per-client)
        data.writeUInt32(0);

        // Server port (2 bytes)
        data.writeUInt16(0);
    }

    auto packet = std::make_unique<Packet>(data, OP_EDONKEYPROT, OP_ANSWERSOURCES);
    return packet;
}

// ---------------------------------------------------------------------------
// Hashing — createFromFile
// ---------------------------------------------------------------------------

bool KnownFile::createFromFile(const QString& directory, const QString& filename,
                               std::function<void(int)> progressCallback)
{
    const QString fullPath = directory + u'/' + filename;

    QFile file(fullPath);
    if (!file.open(QIODevice::ReadOnly)) {
        logError(QStringLiteral("KnownFile::createFromFile: cannot open '%1'").arg(fullPath));
        return false;
    }

    const uint64 length = static_cast<uint64>(file.size());
    setFileSize(length);
    setFileName(filename, true);
    setPath(directory);
    setFilePath(fullPath);

    // Set file date
    QFileInfo fi(fullPath);
    setUtcFileDate(static_cast<time_t>(fi.lastModified().toSecsSinceEpoch()));

    if (length == 0) {
        // Empty file — single null hash
        uint8 nullHash[16]{};
        MD4Hasher hasher;
        hasher.add(nullHash, 0);
        hasher.finish();
        md4cpy(nullHash, hasher.getHash());
        setFileHash(nullHash);

        updateMetaDataTags();
        updatePartsInfo();
        return true;
    }

    // Create AICH hash set for the whole file
    AICHRecoveryHashSet aichHashSet(length);

    auto& md4HashSet = fileIdentifier().getRawMD4HashSet();
    md4HashSet.clear();

    const uint16 parts = partCount();
    uint64 remaining = length;

    for (uint16 part = 0; part < parts; ++part) {
        const uint64 partLength = std::min(remaining, static_cast<uint64>(PARTSIZE));

        std::array<uint8, 16> partHash{};
        AICHHashTree* partTree = aichHashSet.m_hashTree.findHash(
            static_cast<uint64>(part) * PARTSIZE, partLength);

        createHash(file, partLength, partHash.data(), partTree);
        md4HashSet.push_back(partHash);
        remaining -= partLength;

        if (progressCallback) {
            int percent = static_cast<int>((static_cast<uint64>(part) + 1) * 100 / parts);
            progressCallback(percent);
        }
    }

    // Compute final file hash
    if (parts == 1) {
        // Single-part: file hash = part hash
        setFileHash(md4HashSet[0].data());
    } else {
        // Multi-part: compute MD4 of all part hashes
        fileIdentifier().calculateMD4HashByHashSet(false);
    }

    // Verify AICH tree
    auto hashAlg = std::unique_ptr<AICHHashAlgo>(AICHRecoveryHashSet::getNewHashAlgo());
    if (aichHashSet.m_hashTree.reCalculateHash(hashAlg.get(), false)) {
        fileIdentifier().setAICHHash(aichHashSet.getMasterHash());
        m_aichRecoverHashSetAvailable = true;
    }

    setLastSeen(std::time(nullptr));
    updateMetaDataTags();
    updatePartsInfo();

    return true;
}

// ---------------------------------------------------------------------------
// createAICHHashSetOnly
// ---------------------------------------------------------------------------

bool KnownFile::createAICHHashSetOnly()
{
    const QString path = filePath();
    if (path.isEmpty())
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const uint64 length = static_cast<uint64>(file.size());
    if (length != static_cast<uint64>(fileSize()))
        return false;

    AICHRecoveryHashSet aichHashSet(length);
    uint64 remaining = length;
    const uint16 parts = partCount();

    for (uint16 part = 0; part < parts; ++part) {
        const uint64 partLength = std::min(remaining, static_cast<uint64>(PARTSIZE));

        AICHHashTree* partTree = aichHashSet.m_hashTree.findHash(
            static_cast<uint64>(part) * PARTSIZE, partLength);

        // Only feed AICH, discard MD4
        uint8 dummyHash[16]{};
        createHash(file, partLength, dummyHash, partTree);
        remaining -= partLength;
    }

    auto hashAlg = std::unique_ptr<AICHHashAlgo>(AICHRecoveryHashSet::getNewHashAlgo());
    if (aichHashSet.m_hashTree.reCalculateHash(hashAlg.get(), false)) {
        fileIdentifier().setAICHHash(aichHashSet.getMasterHash());
        m_aichRecoverHashSetAvailable = true;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Core hash computation
// ---------------------------------------------------------------------------

void KnownFile::createHash(QIODevice& device, uint64 length,
                           uint8* md4HashOut, AICHHashTree* aichTree)
{
    static constexpr uint32 kReadBlockSize = 8 * 1024; // 8 KB

    auto hashAlg = std::unique_ptr<AICHHashAlgo>(AICHRecoveryHashSet::getNewHashAlgo());

    MD4Hasher md4Hasher;
    uint8 buf[kReadBlockSize];
    uint64 read = 0;
    uint64 aichPos = 0; // position within current EMBLOCKSIZE boundary

    while (read < length) {
        const uint64 toRead = std::min(static_cast<uint64>(kReadBlockSize), length - read);
        const qint64 got = device.read(reinterpret_cast<char*>(buf), static_cast<qint64>(toRead));
        if (got <= 0)
            break;

        md4Hasher.add(buf, static_cast<std::size_t>(got));

        if (aichTree && hashAlg) {
            hashAlg->add(buf, static_cast<uint32>(got));
            aichPos += static_cast<uint64>(got);

            // When we cross an EMBLOCKSIZE boundary, flush AICH block
            if (aichPos >= EMBLOCKSIZE) {
                AICHHash blockHash;
                hashAlg->finish(blockHash);
                aichTree->setBlockHash(EMBLOCKSIZE, read + static_cast<uint64>(got) - aichPos, hashAlg.get());
                aichPos -= EMBLOCKSIZE;
                hashAlg->reset();
                if (aichPos > 0) {
                    // Feed leftover bytes from current read
                    // (they were already added above, recalculate after reset)
                }
            }
        }

        read += static_cast<uint64>(got);
    }

    md4Hasher.finish();
    md4cpy(md4HashOut, md4Hasher.getHash());

    // Flush remaining AICH data
    if (aichTree && hashAlg && aichPos > 0) {
        AICHHash blockHash;
        hashAlg->finish(blockHash);
        aichTree->setBlockHash(aichPos, read - aichPos, hashAlg.get());
    }
}

bool KnownFile::createHashFromFile(const QString& filePath, uint64 length,
                                   uint8* md4HashOut, AICHHashTree* aichTree)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    createHash(file, length, md4HashOut, aichTree);
    return true;
}

bool KnownFile::createHashFromMemory(const uint8* data, uint32 size,
                                     uint8* md4HashOut, AICHHashTree* aichTree)
{
    QByteArray ba(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    QBuffer buffer(&ba);
    buffer.open(QIODevice::ReadOnly);

    createHash(buffer, size, md4HashOut, aichTree);
    return true;
}

// ---------------------------------------------------------------------------
// updatePartsInfo
// ---------------------------------------------------------------------------

void KnownFile::updatePartsInfo()
{
    m_availPartFrequency.resize(m_partCount, 0);
    std::fill(m_availPartFrequency.begin(), m_availPartFrequency.end(), 0);

    // Aggregate part availability from all uploading clients
    for (const auto* client : m_uploadingClients) {
        const auto& status = client->partStatus();
        const auto statusSize = static_cast<uint16>(status.size());
        const uint16 limit = std::min(m_partCount, statusSize);
        for (uint16 i = 0; i < limit; ++i) {
            if (status[i] != 0)
                ++m_availPartFrequency[i];
        }
    }

    emit m_notifier.fileUpdated();
}

} // namespace eMule
