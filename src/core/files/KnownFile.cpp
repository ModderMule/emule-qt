#include "pch.h"
/// @file KnownFile.cpp
/// @brief Known (completed) file — partial port of MFC CKnownFile.
///
/// Core file metadata, priority, upload client tracking, and media metadata.
/// GUI-dependent code (BarShader, CxImage, FrameGrabThread) is decoupled
/// via FileNotifier signal emissions.

#include "files/KnownFile.h"
#include "app/AppContext.h"
#include "files/SharedFileList.h"
#include "files/Collection.h"
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
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "utils/DiskLoadLimiter.h"
#include "utils/Log.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <vector>


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
    // MFC CKnownFile ctor: auto starts at High
    m_autoUpPriority = thePrefs.autoSharedFilesPriority();
    m_upPriority = m_autoUpPriority ? kPrHigh : kPrNormal;
}

KnownFile::~KnownFile() = default;

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

    // Rebuild Kad keyword list — include collection author key for Kad publishing
    m_kadKeywords.clear();
    if (m_collection && !m_collection->m_authorKey.isEmpty()) {
        const QString keywordsStr = m_collection->authorKeyString() + u' ' + fileName();
        kad::getWords(keywordsStr, m_kadKeywords);
    } else {
        kad::getWords(fileName(), m_kadKeywords);
    }
}

// ---------------------------------------------------------------------------
// setCollection
// ---------------------------------------------------------------------------

void KnownFile::setCollection(std::unique_ptr<Collection> coll)
{
    m_collection = std::move(coll);
    if (m_collection && !m_collection->m_authorKey.isEmpty()) {
        // Re-trigger keyword rebuild to include author key
        setFileName(fileName(), false, false, false);
    }
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
    const uint32 tagCount = readTagCount(file, kMaxFileTags);
    bool sawFlags = false;

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
            if (tag.isInt())
                setUpPriorityFromTag(tag.intValue());
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
            // Bits 3-0: metadata version. MFC KnownFile.cpp:696-707. Auto priority
            // comes from FT_ULPRIORITY alone.
            if (tag.isInt())
                sawFlags = true;
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
        case FT_MEDIAEXTRACTVER:
            if (tag.isInt())
                m_mediaExtractVer = tag.intValue();
            break;
        case FT_KADNOTECACHE:
            // Consume here (don't fall through to addTagUnique) so it isn't both
            // deserialized and re-written from the extra-tags list.
            if (tag.isBlob())
                deserializeKadNotes(tag.blobValue());
            break;
        default:
            addTagUnique(std::move(tag));
            break;
        }
    }

    // No version: the media tags are not ours to trust (MFC KnownFile.cpp:757-764).
    // Earlier builds of this client stored a priority bit in the tag (values 0 and 1),
    // with tags it had extracted itself, so those are kept and stamped.
    if (!sawFlags)
        removeMetaDataTags();
    else
        m_metaDataVer = hasMetaDataTags() ? kMetaDataVer : 0;

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
    if (m_metaDataVer > 0)
        tagCount += 1; // FT_FLAGS
    if (m_mediaExtractVer > 0)
        tagCount += 1; // FT_MEDIAEXTRACTVER

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

    // Cached Kad notes (filenames/comments) — eMuleQt private tag
    if (!m_kadNotes.empty())
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
    Tag(FT_ULPRIORITY, upPriorityTagValue()).writeNewEd2kTag(file);

    // Flags: the metadata version, only when there is one (MFC KnownFile.cpp:897-909)
    if (m_metaDataVer > 0)
        Tag(FT_FLAGS, static_cast<uint32>(m_metaDataVer & 0x0F)).writeNewEd2kTag(file);
    if (m_mediaExtractVer > 0)
        Tag(FT_MEDIAEXTRACTVER, m_mediaExtractVer).writeNewEd2kTag(file);

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

    // Cached Kad notes (filenames/comments) — eMuleQt private blob tag
    if (!m_kadNotes.empty())
        Tag(FT_KADNOTECACHE, serializeKadNotes()).writeNewEd2kTag(file);

    // Extra tags
    for (const auto& tag : tags())
        tag.writeNewEd2kTag(file, UTF8Mode::OptBOM);

    return true;
}

// ---------------------------------------------------------------------------
// Kad notes cache (filenames + comments discovered via a Kad notes search)
// ---------------------------------------------------------------------------

void KnownFile::addKadNote(const QByteArray& publisherId, const QString& fileName,
                           const QString& comment, uint8 rating, time_t now)
{
    // Need a stable 16-byte publisher key for dedup and a filename to be useful.
    if (publisherId.size() != 16 || fileName.isEmpty())
        return;

    // Insert-or-update keyed by publisher → re-running the search for the same
    // publisher refreshes its entry rather than adding a duplicate.
    KadNoteInfo& info = m_kadNotes[publisherId];
    info.fileName = fileName;
    info.comment  = comment;
    info.rating   = rating;
    info.lastSeen = now;

    pruneKadNotes();

    // Feed the aggregate too. m_kadNotes above is the display cache the notes
    // tab reads; the rating indicator is computed from AbstractFile's cache, and
    // filling only one of the two left every Kad note out of the rating — the
    // same dead-writer shape as the source ratings in PartFile.
    // This also runs updateFileRatingCommentAvail(), so the mark refreshes.
    AbstractFile::addKadNote(publisherId, rating, comment);
}

QByteArray KnownFile::serializeKadNotes() const
{
    SafeMemFile mem;
    mem.writeUInt32(static_cast<uint32>(m_kadNotes.size()));
    for (const auto& [publisherId, info] : m_kadNotes) {
        mem.writeHash16(reinterpret_cast<const uint8*>(publisherId.constData()));
        mem.writeString(info.fileName, UTF8Mode::Raw);
        mem.writeString(info.comment, UTF8Mode::Raw);
        mem.writeUInt8(info.rating);
        mem.writeUInt32(static_cast<uint32>(info.lastSeen));
    }
    return mem.buffer();
}

void KnownFile::deserializeKadNotes(const QByteArray& blob)
{
    if (blob.isEmpty())
        return;

    try {
        SafeMemFile mem(reinterpret_cast<const uint8*>(blob.constData()), blob.size());
        const uint32 count = mem.readUInt32();
        for (uint32 i = 0; i < count && mem.position() < mem.length(); ++i) {
            uint8 publisherId[16];
            mem.readHash16(publisherId);
            KadNoteInfo info;
            info.fileName = mem.readString(true);
            info.comment  = mem.readString(true);
            info.rating   = mem.readUInt8();
            info.lastSeen = static_cast<time_t>(mem.readUInt32());
            m_kadNotes[QByteArray(reinterpret_cast<const char*>(publisherId), 16)] =
                std::move(info);
        }
    } catch (const FileException&) {
        // Truncated/corrupt cache — keep whatever parsed cleanly.
    }

    pruneKadNotes();
}

void KnownFile::pruneKadNotes()
{
    const time_t now = time(nullptr);

    // 1. Drop entries older than the configured expiry.
    const int expiryDays = thePrefs.kadFileNameExpiryDays();
    if (expiryDays > 0) {
        const time_t cutoff = now - static_cast<time_t>(expiryDays) * 86400;
        for (auto it = m_kadNotes.begin(); it != m_kadNotes.end();) {
            if (it->second.lastSeen < cutoff)
                it = m_kadNotes.erase(it);
            else
                ++it;
        }
    }

    // 2. Cap to the newest-by-lastSeen entries.
    const int maxCount = thePrefs.kadFileNameMaxCount();
    if (maxCount > 0 && static_cast<int>(m_kadNotes.size()) > maxCount) {
        std::vector<std::map<QByteArray, KadNoteInfo>::iterator> its;
        its.reserve(m_kadNotes.size());
        for (auto it = m_kadNotes.begin(); it != m_kadNotes.end(); ++it)
            its.push_back(it);
        std::sort(its.begin(), its.end(),
                  [](const auto& a, const auto& b) {
                      return a->second.lastSeen < b->second.lastSeen;
                  });
        const int toRemove = static_cast<int>(m_kadNotes.size()) - maxCount;
        for (int i = 0; i < toRemove; ++i)
            m_kadNotes.erase(its[i]);
    }
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
    noteChanged();
}

void KnownFile::noteChanged()
{
    if (theApp.sharedFileList)
        theApp.sharedFileList->noteFileChanged(fileHash());
}

void KnownFile::setKadFileSearchID(uint32 id)
{
    m_kadFileSearchID = id;
}

// ---------------------------------------------------------------------------
// ED2K publishing
// ---------------------------------------------------------------------------

void KnownFile::setPublishedED2K(bool val)
{
    m_publishedED2K = val;
    emit m_notifier.fileUpdated();
    noteChanged();
}

void KnownFile::setLastPublishTimeKadSrc(time_t t, uint32 buddyIP)
{
    const bool changed = m_lastPublishTimeKadSrc != t || m_lastBuddyIP != buddyIP;
    m_lastPublishTimeKadSrc = t;
    m_lastBuddyIP = buddyIP;
    if (changed)
        noteChanged();   // the list's "Shared eD2K|Kad" cell follows it
}

bool KnownFile::sharedInKad(time_t now, time_t lastPublish, bool kadConnected,
                            bool kadFirewalled, bool buddyMatches, bool udpOpenVerified,
                            bool ipv6Route)
{
    if (!kadConnected || now >= lastPublish)
        return false;
    if (!kadFirewalled)
        return true;
    return buddyMatches || udpOpenVerified || ipv6Route;
}

bool KnownFile::isSharedInKad() const
{
    auto* kad = kad::Kademlia::instance();
    if (!kad)
        return false;
    auto* clientList = kad::Kademlia::getClientList();
    auto* buddy = clientList ? clientList->getBuddy() : nullptr;
    const bool buddyMatches = buddy && m_lastBuddyIP == buddy->userAddress().toNetworkUint32();
    const bool udpOpen = kad->isRunning() && !kad::UDPFirewallTester::isFirewalledUDP(true)
                         && kad::UDPFirewallTester::isVerified();
    // Published without a buddy (m_lastBuddyIP 0) over the IPv6 route
    const bool ipv6Route = !buddy && m_lastBuddyIP == 0 && theApp.shouldAdvertisePublicIPv6();
    return sharedInKad(std::time(nullptr), m_lastPublishTimeKadSrc, kad->isConnected(),
                       kad->isFirewalled(), buddyMatches, udpOpen, ipv6Route);
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
    noteChanged();
}

void KnownFile::detachUploadingClients()
{
    const auto clients = std::move(m_uploadingClients);
    m_uploadingClients.clear();
    for (UpDownClient* client : clients)
        client->setUploadFileID(nullptr);
}

void KnownFile::removeUploadingClient(UpDownClient* client)
{
    auto it = std::ranges::find(m_uploadingClients, client);
    if (it == m_uploadingClients.end())
        return;
    m_uploadingClients.erase(it);
    updateAutoUpPriority();
    emit m_notifier.fileUpdated();
    noteChanged();
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
        noteChanged();
    }
}

// ---------------------------------------------------------------------------
// Media metadata
// ---------------------------------------------------------------------------

static constexpr int kMaxED2KMetaTagLen = 128;

namespace {

// biCompression -> published codec id. MFC KnownFile.cpp:1288-1325.
QString ed2kVideoCodec(uint32 compression)
{
    switch (compression) {
    case 0: return QStringLiteral("rgb");
    case 1: return QStringLiteral("rle8");
    case 2: return QStringLiteral("rle4");
    case 3: return QStringLiteral("bitfields");
    case 4: return QStringLiteral("jpeg");
    case 5: return QStringLiteral("png");
    default: break;
    }
    QString codec;
    for (int i = 0; i < 4; ++i) {
        const auto ch = static_cast<char>((compression >> (8 * i)) & 0xFF);
        const bool sym = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z')
                      || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '.' || ch == ' ';
        if (!sym)
            return {};
        codec += QLatin1Char(ch);
    }
    codec = codec.trimmed();
    return codec.size() < 2 ? QString() : codec.toLower();
}

} // namespace

void KnownFile::updateMetaDataTags()
{
    if (thePrefs.extractMetaData() == 0) {
        removeMetaDataTags();
        return;
    }

    // Remove old media tags first
    removeMetaDataTags();

    // Stamped before the result is known: "read, nothing found" is an answer too.
    // A file that cannot be opened right now keeps its old stamp and is tried again.
    MediaInfo info;
    const bool readable = QFileInfo(filePath()).isReadable();
    if (readable)
        m_mediaExtractVer = kMediaExtractVersion;
    if (!readable || !extractSharedMediaInfo(filePath(), info)) {
        noteChanged();
        return;
    }

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

    // FT_MEDIA_CODEC — the lowercase id peers and servers search by
    // (MFC GetED2KVideoCodec / GetED2KAudioCodec), not the display name.
    QString codec;
    if (info.videoStreamCount > 0 && info.video.codecTag != 0)
        codec = ed2kVideoCodec(info.video.codecTag);
    else if (info.videoStreamCount > 0 && !info.video.codecName.isEmpty())
        codec = info.video.codecName;
    else if (info.audioStreamCount > 0 && info.audio.formatTag != 0)
        codec = audioFormatCodecId(info.audio.formatTag);
    else if (info.audioStreamCount > 0)
        codec = info.audio.codecName;
    codec = codec.trimmed().toLower();
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

    // Only a file that got a tag carries a version (MFC KnownFile.cpp:1480-1540):
    // the version is what makes the publishers look for media tags at all.
    if (hasMetaDataTags())
        m_metaDataVer = kMetaDataVer;
    emit m_notifier.metadataUpdated();
    noteChanged();
}

void KnownFile::setUpPriorityFromTag(uint32 value)
{
    if (value == kPrAuto) {
        m_autoUpPriority = true;
        m_upPriority = kPrHigh;
        return;
    }
    m_autoUpPriority = false;
    m_upPriority = value <= kPrVeryLow ? static_cast<uint8>(value) : kPrNormal;
}

uint32 KnownFile::upPriorityTagValue() const
{
    return m_autoUpPriority ? kPrAuto : m_upPriority;
}

bool KnownFile::mediaExtractIsStale() const
{
    return thePrefs.extractMetaData() != 0 && m_mediaExtractVer < kMediaExtractVersion;
}

bool KnownFile::hasMetaDataTags() const
{
    static constexpr uint8 kMediaTags[] = {FT_MEDIA_LENGTH, FT_MEDIA_BITRATE, FT_MEDIA_CODEC,
                                           FT_MEDIA_ARTIST, FT_MEDIA_ALBUM, FT_MEDIA_TITLE};
    return std::ranges::any_of(kMediaTags, [this](uint8 id) { return getTag(id) != nullptr; });
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
// Rating / publishing
// ---------------------------------------------------------------------------

void KnownFile::updateFileRatingCommentAvail(bool /*forceUpdate*/)
{
    bool hasNewComment = false;
    uint32 ratingSum = 0;
    uint32 ratingCount = 0;

    // Aggregate ratings and comments from Kad notes cache
    for (const auto& [publisherId, note] : m_kadNotesCache) {
        if (!note.comment.isEmpty())
            hasNewComment = true;
        if (note.rating > 0 && note.rating <= 5) {
            ratingSum += note.rating;
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

    if (changed) {
        emit m_notifier.fileUpdated();
        noteChanged();
    }
}

// ===========================================================================
// Comment / rating — the user's own
// ===========================================================================
//
// MFC KnownFile.cpp:1161-1185. Both setters do the same three things beyond the
// assignment, and all three are what makes a posted comment actually travel:
// persist it, re-arm the Kad notes publish, and dirty every uploader so
// sendCommentInfo() puts it on the wire.
//
// Note the getter call before the comparison. getFileComment()/getFileRating()
// lazily loadComment() on first access and never reload, so assigning first would
// let a later lazy load quietly overwrite what the user just typed.

void KnownFile::setFileComment(const QString& comment)
{
    const QString trimmed = comment.left(MAXFILECOMMENTLEN);
    if (getFileComment() == trimmed)
        return;

    m_comment = trimmed;
    setLastPublishTimeKadNotes(0);
    saveComment();
    markUploadersCommentDirty();
}

void KnownFile::setFileRating(uint32 rating)
{
    if (rating > 5 || getFileRating() == rating)
        return;

    m_rating = rating;
    setLastPublishTimeKadNotes(0);
    saveComment();
    markUploadersCommentDirty();
}

bool KnownFile::publishSrc()
{
    time_t tNow = std::time(nullptr);
    uint32 buddyIP = 0;

    // No direct route in: publish only with a buddy. MFC KnownFile.cpp:1561-1584.
    if (kad::Kademlia::instance() && theApp.isFirewalled()
        && (kad::UDPFirewallTester::isFirewalledUDP(true) || !kad::UDPFirewallTester::isVerified())) {
        auto* clientList = kad::Kademlia::getClientList();
        auto* buddy = clientList ? clientList->getBuddy() : nullptr;
        // Not MFC: without a buddy a public IPv6 still gets a record out (buddyIP 0)
        if (!buddy && !theApp.shouldAdvertisePublicIPv6())
            return false;

        if (buddy)
            buddyIP = buddy->userAddress().toNetworkUint32();
        // New buddy: the published record names the old one, republish now
        if (buddy && buddyIP != m_lastBuddyIP) {
            setLastPublishTimeKadSrc(tNow + KADEMLIAREPUBLISHTIMES, buddyIP);
            return true;
        }
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
    const UpDownClient* forClient, uint8 version, uint16 /*options*/) const
{
    if (!forClient || m_uploadingClients.empty())
        return nullptr;

    // The answer describes this file, so the requester has to have asked for it. MFC
    // writes the client's upload file ID into the packet and relies on this guard for
    // the two to agree (srchybrid/KnownFile.cpp:1012-1017).
    if (!md4equ(forClient->reqUpFileId(), fileHash())) {
        logDebug(QStringLiteral("createSrcInfoPacket: requester's upload file is not %1")
                     .arg(fileName()));
        return nullptr;
    }

    // The requester must either report no chunk status at all, or one sized for this
    // file. Anything else means the needed-parts comparison below would read two
    // differently-shaped bitmaps against each other.
    const auto& clientParts = forClient->upPartStatus();
    if (!(forClient->upPartCount() == 0 && clientParts.empty()) &&
        !(forClient->upPartCount() == partCount() && !clientParts.empty()))
    {
        logDebug(QStringLiteral("createSrcInfoPacket: requester part count %1 does not "
                                "match file part count %2 for %3")
                     .arg(forClient->upPartCount()).arg(partCount()).arg(fileName()));
        return nullptr;
    }

    // Upload side: candidates are clients we are serving, judged on their upload part
    // status. A client merely connecting or banned is not a source worth handing out,
    // and a URL source can't be described by an ed2k source record at all.
    return buildSrcInfoPacket(
        forClient, version, m_uploadingClients,
        [this, &clientParts](const UpDownClient* client) {
            if (client->uploadState() != UploadState::Uploading &&
                client->uploadState() != UploadState::OnUploadQueue)
                return false;
            if (!client->isEd2kClient())
                return false;
            return sourceHasNeededPart(client, clientParts);
        });
}

std::unique_ptr<Packet> KnownFile::buildSrcInfoPacket(
    const UpDownClient* forClient, uint8 version,
    const std::vector<UpDownClient*>& candidates,
    const std::function<bool(const UpDownClient*)>& eligible) const
{
    // Only answer in SX2 when the peer actually asked for it; otherwise fall back to
    // the SX1 version it announced at handshake. SX1 carries no version byte and uses
    // its own opcode, so the two formats must not be mixed.
    uint8 usedVersion;
    bool isSX2;
    bool extSX = false;
    if (forClient->supportsSourceExchange2() && version > 0) {
        isSX2 = true;
        if (forClient->supportsExtendedXS()) {
            // Extended SX: the version byte stays at 1 and each per-source record is a
            // self-describing tag block (no fixed serverIP/port, no userHash/crypt tail),
            // which lets a source carry its public IPv6.
            usedVersion = SOURCEEXCHANGEEXT_VERSION;
            extSX = true;
        } else {
            usedVersion = std::min(version, static_cast<uint8>(SOURCEEXCHANGE2_VERSION));
        }
    } else {
        usedVersion = forClient->sourceExchange1Ver();
        isSX2 = false;
    }

    SafeMemFile data;

    // SX2 header: version byte. SX1 has none.
    if (isSX2)
        data.writeUInt8(usedVersion);

    // File hash (16 bytes)
    data.writeHash16(fileHash());

    // Placeholder for source count — seeked back to once the real count is known.
    // The count must match the number of records actually written, or receivers
    // fail their `count * entrySize == dataSize` check and drop the whole packet.
    const auto countPos = data.position();
    data.writeUInt16(0);

    // 500 for every version and format — MFC PartFile.cpp:3731, KnownFile.cpp:1141. A 50 cap
    // for v1-v3 peers had no reference counterpart and starved them of sources.
    constexpr uint16 maxSources = 500;
    uint16 count = 0;

    for (const auto* client : candidates) {
        if (count >= maxSources)
            break;

        // Skip low-ID clients and the requester itself. Exception: on the ExtSX path a
        // LowID client that has a reachable public IPv6 is kept — the receiver reaches
        // it directly over IPv6 (carried in the tag block below).
        if ((client->hasLowID() && !(extSX && client->openIPv6())) || client == forClient)
            continue;

        // An address-less client can't be described — except on the ExtSX path, where
        // the tag block carries an IPv6 that stands on its own.
        if (client->userAddress().isNull() && !(extSX && client->openIPv6()))
            continue;

        if (!eligible(client))
            continue;

        // v3+ sends IDs in hybrid (host order) format so that high-ID clients with an
        // address ending in .0 aren't falsely read back as low-ID.
        //
        // ExtSX runs at version 1 but still uses htonl(hybrid) rather than the address,
        // matching the reference: for a LowID or IPv6-only source the two disagree, and
        // the reference's reader normalizes the hybrid form back out.
        uint32 wireId;
        if (usedVersion >= 3)
            wireId = client->userIDHybrid();
        else if (extSX)
            wireId = htonl(client->userIDHybrid());
        else
            wireId = client->userAddress().toNetworkUint32();

        data.writeUInt32(wireId);
        data.writeUInt16(client->userPort());

        if (extSX) {
            writeExtendedSourceExchangeData(data, client, forClient->supportsExtSXSkipTags());
        } else {
            data.writeUInt32(client->serverAddress().toNetworkUint32());
            data.writeUInt16(client->serverPort());

            if (usedVersion >= 2)
                data.writeHash16(client->userHash());

            if (usedVersion >= 4) {
                // Bit 3 (direct UDP callback) is deliberately never set: the SX record
                // carries no Kad UDP port, so the receiver can't act on it and forces
                // it off anyway (setConnectOptions(..., callback=false)).
                uint8 cryptOpts = 0;
                if (client->supportsCryptLayer())
                    cryptOpts |= 0x01;
                if (client->requestsCryptLayer())
                    cryptOpts |= 0x02;
                if (client->requiresCryptLayer())
                    cryptOpts |= 0x04;
                data.writeUInt8(cryptOpts);
            }
        }

        ++count;
    }

    if (count == 0)
        return nullptr;

    // Seek back and write actual count
    const auto endPos = data.position();
    data.seek(static_cast<int>(countPos), SEEK_SET);
    data.writeUInt16(count);
    data.seek(static_cast<int>(endPos), SEEK_SET);

    auto packet = std::make_unique<Packet>(
        data, OP_EMULEPROT, isSX2 ? OP_ANSWERSOURCES2 : OP_ANSWERSOURCES);
    if (packet->size > 354)
        packet->packPacket();

    return packet;
}

void KnownFile::writeExtendedSourceExchangeData(SafeMemFile& data, const UpDownClient* src,
                                                bool peerSkipsUnknownTags) const
{
    // Assemble the per-source tag list, then write count + tags. This block is a strict
    // superset of the classic serverIP/serverPort record: the server info rides as tags,
    // and a reachable public IPv6 is added as CT_MOD_IP_V6. Tags are written in the
    // optimized ed2k form; a reader skips any tag it does not recognise by type, so the
    // format stays forward-compatible.
    std::vector<Tag> tags;
    const uint32 serverIp = src->serverAddress().toNetworkUint32();
    if (serverIp != 0) {
        tags.emplace_back(CT_EMULE_SERVERIP, serverIp);
        tags.emplace_back(CT_EMULE_SERVERTCP, static_cast<uint32>(src->serverPort()));
    }
    // isIPv6() as well as isPublicIP(): the tag is 16 raw bytes with no family marker, so
    // an address that is not actually IPv6 would go out as 16 bytes of nothing.
    if (src->openIPv6() && src->userIPv6().isIPv6() && src->userIPv6().isPublicIP())
        tags.emplace_back(CT_MOD_IP_V6, src->userIPv6().ipv6Bytes().data());

    // The user hash and crypt options lived in the classic record's version-gated tail
    // (>= 2 and >= 4). ExtSX pins the version at 1, so a source learned this way arrives
    // with no hash — breaking credits, secure identification and obfuscated-UDP keying to
    // it — and with unknown crypt capability. Carry both as tags instead.
    //
    // Strictly gated on the requester advertising MODMISC_EXTXS_SKIPTAGS. The tag format
    // says a reader must skip what it does not recognise, but the compatibility target's
    // reader records every unknown tag in an error string and then returns out of the whole
    // source-exchange parse, so one of these tags in the first record would cost it every
    // source in the packet. Peers without the bit keep getting the exact bytes they get today.
    if (peerSkipsUnknownTags) {
        if (src->hasValidHash())
            tags.emplace_back(CT_EMULE_USERHASH, src->userHash());

        // Same bit layout as the classic v4 tail; bit 3 (direct UDP callback) is deliberately
        // never set, since the record carries no Kad UDP port for the receiver to act on.
        uint8 cryptOpts = 0;
        if (src->supportsCryptLayer())
            cryptOpts |= 0x01;
        if (src->requestsCryptLayer())
            cryptOpts |= 0x02;
        if (src->requiresCryptLayer())
            cryptOpts |= 0x04;
        if (cryptOpts != 0)
            tags.emplace_back(CT_EMULE_CONOPTS, static_cast<uint32>(cryptOpts));
    }

    data.writeUInt8(static_cast<uint8>(tags.size()));
    for (const auto& tag : tags)
        tag.writeNewEd2kTag(data);
}

// ---------------------------------------------------------------------------
// Hashing — createFromFile
// ---------------------------------------------------------------------------

bool KnownFile::createFromFile(const QString& directory, const QString& filename,
                               std::function<void(int)> progressCallback,
                               const FileStamp* scanned, bool* changedSinceScan)
{
    if (changedSinceScan)
        *changedSinceScan = false;

    const QString fullPath = directory + u'/' + filename;

    QFile file(fullPath);
    if (!file.open(QIODevice::ReadOnly)) {
        logError(QStringLiteral("KnownFile::createFromFile: cannot open '%1'").arg(fullPath));
        return false;
    }

    // Size and date of the file we actually hold open, taken right before the read:
    // the scan that queued it may be minutes old.
    const uint64 length = static_cast<uint64>(file.size());
    const auto openedDate = static_cast<time_t>(
        file.fileTime(QFileDevice::FileModificationTime).toSecsSinceEpoch());
    if (scanned && (scanned->size != length || scanned->mtime != openedDate)) {
        logInfo(QStringLiteral("File changed since it was scanned: %1").arg(fullPath));
        if (changedSinceScan)
            *changedSinceScan = true;
        return false;
    }

    setFileSize(length);
    setFileName(filename, true);
    setPath(directory);
    setFilePath(fullPath);
    setUtcFileDate(openedDate);

    QFileInfo fi(fullPath);

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

        if (!createHash(file, partLength, partHash.data(), partTree)) {
            logError(QStringLiteral("KnownFile::createFromFile: read error in '%1'").arg(fullPath));
            return false;
        }
        md4HashSet.push_back(partHash);
        remaining -= partLength;

        if (progressCallback) {
            int percent = static_cast<int>((static_cast<uint64>(part) + 1) * 100 / parts);
            progressCallback(percent);
        }
    }

    // A file ending on a part boundary carries one more hash, that of no data — MFC
    // CreateFromFile, srchybrid/KnownFile.cpp:419-430. AICH has no such part.
    if (length % PARTSIZE == 0) {
        std::array<uint8, 16> emptyHash{};
        if (!createHash(file, 0, emptyHash.data(), nullptr)) {
            logError(QStringLiteral("KnownFile::createFromFile: read error in '%1'").arg(fullPath));
            return false;
        }
        md4HashSet.push_back(emptyHash);
    }

    // Still being written: the hash would be stored under a size and date the file no
    // longer has. The caller tries again later.
    fi.refresh();
    if (static_cast<uint64>(fi.size()) != length
        || static_cast<time_t>(fi.lastModified().toSecsSinceEpoch()) != utcFileDate()) {
        logInfo(QStringLiteral("File changed while it was hashed: %1").arg(fullPath));
        return false;
    }

    // Compute final file hash
    if (md4HashSet.size() == 1) {
        // Smaller than a part: the whole-file hash IS the single part hash, and eMule
        // stores no part hashset in that case. An empty hashset is what satisfies
        // HasExpectedMD4HashCount() here (getTheoreticalMD4PartHashCount() == 0 for
        // a file smaller than PARTSIZE). Keeping the 1-entry hashset would write
        // parts=1 to known.met, which fails to reload (calculateMD4HashByHashSet
        // rejects size <= 1) — the "known.met: corrupt entry" on startup.
        setFileHash(md4HashSet[0].data());
        md4HashSet.clear();
    } else {
        // Multi-part: compute MD4 of all part hashes
        fileIdentifier().calculateMD4HashByHashSet(false);
    }

    // Finish the AICH tree, keep its part hashes and store the recovery set. Without
    // the store nobody can ever be served recovery data for this file — MFC
    // CreateFromFile, srchybrid/KnownFile.cpp:456-470.
    aichHashSet.reCalculateHash(false);
    m_aichRecoverHashSetAvailable = adoptAndStoreAICHHashSet(aichHashSet);

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
    AICHRecoveryHashSet aichHashSet(static_cast<uint64>(fileSize()));
    if (!buildAICHHashSet(filePath(), static_cast<uint64>(fileSize()), aichHashSet))
        return false;

    m_aichRecoverHashSetAvailable = adoptAndStoreAICHHashSet(aichHashSet);
    return m_aichRecoverHashSetAvailable;
}

bool KnownFile::buildAICHHashSet(const QString& path, uint64 expectedSize,
                                 AICHRecoveryHashSet& out)
{
    if (path.isEmpty() || expectedSize == 0)
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    if (static_cast<uint64>(file.size()) != expectedSize)
        return false;

    uint64 remaining = expectedSize;
    for (uint64 start = 0; remaining > 0; start += PARTSIZE) {
        const uint64 partLength = std::min(remaining, static_cast<uint64>(PARTSIZE));
        AICHHashTree* partTree = out.m_hashTree.findHash(start, partLength);

        // Only feed AICH, discard MD4
        uint8 dummyHash[16]{};
        if (!createHash(file, partLength, dummyHash, partTree))
            return false;
        remaining -= partLength;
    }

    out.reCalculateHash(false);
    if (!out.verifyHashTree(true))
        return false;
    out.setStatus(EAICHStatus::HashSetComplete);
    return true;
}

bool KnownFile::adoptAndStoreAICHHashSet(AICHRecoveryHashSet& hashSet)
{
    if (!hashSet.verifyHashTree(true)) {
        logWarning(QStringLiteral("Failed to calculate AICH hashset for %1").arg(fileName()));
        return false;
    }
    hashSet.setStatus(EAICHStatus::HashSetComplete);
    fileIdentifier().setAICHHash(hashSet.getMasterHash());
    if (!fileIdentifier().setAICHHashSet(hashSet))
        logDebug(QStringLiteral("Failed to store AICH part hashset for %1").arg(fileName()));

    // No store configured (a bare KnownFile, a unit test): nothing to save to, and
    // nothing to serve recovery data from.
    if (!AICHRecoveryHashSet::hasKnown2MetPath())
        return false;
    if (!hashSet.saveHashSet()) {   // frees the set
        logWarning(QStringLiteral("Failed to save AICH hashset for %1").arg(fileName()));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Core hash computation
// ---------------------------------------------------------------------------

bool KnownFile::createHash(QIODevice& device, uint64 length,
                           uint8* md4HashOut, AICHHashTree* aichTree)
{
    static constexpr uint32 kReadBlockSize = 64 * 1024;
    // the AICH split below handles one block boundary per read
    static_assert(kReadBlockSize <= EMBLOCKSIZE);

    auto hashAlg = aichTree ? std::unique_ptr<AICHHashAlgo>(AICHRecoveryHashSet::getNewHashAlgo())
                            : nullptr;

    MD4Hasher md4Hasher;
    const auto buffer = std::make_unique_for_overwrite<uint8[]>(kReadBlockSize);
    uint8* const buf = buffer.get();
    uint64 read = 0;
    uint64 blockStart = 0;   ///< file offset of the AICH block being hashed
    uint64 blockFilled = 0;  ///< bytes of that block already fed to hashAlg
    DiskLoadLimiter diskLoad;

    while (read < length) {
        const uint64 toRead = std::min(static_cast<uint64>(kReadBlockSize), length - read);
        qint64 got = 0;
        {
            const DiskLoadLimiter::Read timed(diskLoad);
            got = device.read(reinterpret_cast<char*>(buf), static_cast<qint64>(toRead));
        }
        if (got <= 0)
            break;

        md4Hasher.add(buf, static_cast<std::size_t>(got));

        // An AICH block is a hash of exactly EMBLOCKSIZE bytes, and the read size does not divide
        // it, so a read that straddles the boundary has to be split — the tail belongs to
        // the next block. MFC srchybrid/KnownFile.cpp:945-959. Feeding the whole chunk and
        // dropping the remainder, as this used to, put a few KB of the next block into
        // every block hash and started the next one late: our AICH hashes matched nothing
        // but themselves.
        if (aichTree && hashAlg) {
            const auto chunk = static_cast<uint64>(got);
            if (blockFilled + chunk >= EMBLOCKSIZE) {
                const uint64 toComplete = EMBLOCKSIZE - blockFilled;
                hashAlg->add(buf, static_cast<uint32>(toComplete));
                aichTree->setBlockHash(EMBLOCKSIZE, blockStart, hashAlg.get());
                blockStart += EMBLOCKSIZE;
                hashAlg->reset();
                blockFilled = chunk - toComplete;
                if (blockFilled > 0)
                    hashAlg->add(buf + toComplete, static_cast<uint32>(blockFilled));
            } else {
                hashAlg->add(buf, static_cast<uint32>(chunk));
                blockFilled += chunk;
            }
        }

        read += static_cast<uint64>(got);
    }

    md4Hasher.finish();
    if (md4HashOut)
        md4cpy(md4HashOut, md4Hasher.getHash());

    // The last, short block, then the tree itself — MFC finishes both inside CreateHash
    // (srchybrid/KnownFile.cpp:966-974), so the root hash is ready when this returns.
    if (aichTree && hashAlg) {
        if (blockFilled > 0)
            aichTree->setBlockHash(blockFilled, blockStart, hashAlg.get());
        aichTree->reCalculateHash(hashAlg.get(), false);
    }

    // Short of what was asked for: a read error or a file that shrank. The hash of
    // what did arrive is not the hash of the file.
    return read == length;
}

bool KnownFile::createHashFromFile(const QString& filePath, uint64 length,
                                   uint8* md4HashOut, AICHHashTree* aichTree)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    return createHash(file, length, md4HashOut, aichTree);
}

bool KnownFile::createHashFromMemory(const uint8* data, uint32 size,
                                     uint8* md4HashOut, AICHHashTree* aichTree)
{
    QByteArray ba(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    QBuffer buffer(&ba);
    buffer.open(QIODevice::ReadOnly);

    return createHash(buffer, size, md4HashOut, aichTree);
}

// ---------------------------------------------------------------------------
// updatePartsInfo
// ---------------------------------------------------------------------------

void KnownFile::updatePartsInfo()
{
    const time_t now = std::time(nullptr);
    const bool refresh = completeSourcesDue(now);

    m_availPartFrequency.assign(m_partCount, 0);

    // What the peers downloading this file from us say they hold. The download-side
    // partStatus() used to be read here, which is that peer's view of some *other*
    // file it is downloading from us — MFC reads m_abyUpPartStatus (KnownFile.cpp:221).
    std::vector<uint16> peerCounts;
    for (const auto* client : m_uploadingClients) {
        const auto& status = client->upPartStatus();
        // A peer that hasn't reported for this file yet (or reported for a different
        // one) contributes nothing — MFC KnownFile.cpp:221.
        if (client->upPartCount() != m_partCount || status.size() < m_partCount)
            continue;
        for (uint16 i = 0; i < m_partCount; ++i) {
            if (status[i] != 0)
                ++m_availPartFrequency[i];
        }
        if (refresh)
            peerCounts.push_back(client->upCompleteSourcesCount());
    }

    if (refresh) {
        // Sources holding every part. MFC's loop at KnownFile.cpp:222 runs `--i > 0`
        // and so never counts part 0, which then forces this minimum to 0; counting
        // every part is what the code means and what CPartFile does.
        uint16 seen = 0;
        if (m_partCount > 0) {
            seen = *std::min_element(m_availPartFrequency.begin(),
                                     m_availPartFrequency.end());
        }
        updateCompleteSourceCounts(peerCounts, seen, /*blend*/ false);
    }

    emit m_notifier.fileUpdated();
    noteChanged();
}

// ---------------------------------------------------------------------------
// updateCompleteSourceCounts — protected
// ---------------------------------------------------------------------------

void KnownFile::updateCompleteSourceCounts(std::vector<uint16>& peerCounts,
                                           uint16 seen, bool blend)
{
    m_completeSourcesCountLo = m_completeSourcesCountHi = 0;
    m_completeSourcesCount = seen;

    // A complete file adds itself to the sample; a part file is not a complete source.
    peerCounts.push_back(blend ? seen : static_cast<uint16>(seen + 1));
    std::sort(peerCounts.begin(), peerCounts.end());

    const std::size_t n = peerCounts.size();
    const uint16 mid  = peerCounts[n >> 1];
    const uint16 high = peerCounts[(n * 3) >> 2];
    const uint16 top  = peerCounts[(n * 7) >> 3];

    if (!blend) {
        // Complete file: trust what the network reports (MFC KnownFile.cpp:262-289).
        if (n < 20) {
            m_completeSourcesCountLo = std::max(mid, m_completeSourcesCount);
            m_completeSourcesCount = m_completeSourcesCountLo;
            m_completeSourcesCountHi = high;
        } else {
            m_completeSourcesCountLo = m_completeSourcesCount;
            m_completeSourcesCount = std::max(high, m_completeSourcesCountLo);
            m_completeSourcesCountHi = top;
        }
    } else if (n < 5) {
        // Too few opinions to average — use what we see (MFC PartFile.cpp:2617-2620).
        m_completeSourcesCountHi = m_completeSourcesCountLo = m_completeSourcesCount;
    } else if (n < 20) {
        m_completeSourcesCountLo = (mid < m_completeSourcesCount)
            ? m_completeSourcesCount
            : static_cast<uint16>((mid * 4 + m_completeSourcesCount) / 5);
        m_completeSourcesCount = m_completeSourcesCountLo;
        m_completeSourcesCountHi =
            static_cast<uint16>((high * 4 + m_completeSourcesCount) / 5);
    } else {
        m_completeSourcesCountLo = m_completeSourcesCount;
        m_completeSourcesCount = std::max<uint16>(
            static_cast<uint16>((high * 4 + m_completeSourcesCount) / 5),
            m_completeSourcesCountLo);
        m_completeSourcesCountHi =
            static_cast<uint16>((top * 4 + m_completeSourcesCount) / 5);
    }

    if (m_completeSourcesCountHi < m_completeSourcesCount)
        m_completeSourcesCountHi = m_completeSourcesCount;

    m_completeSourcesTime = std::time(nullptr) + MIN2S(1);
}

// ---------------------------------------------------------------------------
// sourceHasNeededPart — private
// ---------------------------------------------------------------------------

bool KnownFile::sourceHasNeededPart(const UpDownClient* src,
                                    const std::vector<uint8>& requesterParts) const
{
    const auto& srcParts = src->upPartStatus();

    // A client that doesn't report chunk status tells us nothing either way, so send
    // it and let the requester find out — same as the original.
    if (srcParts.empty())
        return true;

    if (requesterParts.empty()) {
        // The requester didn't report chunk status, so we can't diff against it.
        // Settle for any source holding at least one complete part.
        return std::any_of(srcParts.begin(), srcParts.end(),
                           [](uint8 have) { return have != 0; });
    }

    // Mismatched bitmaps can't be compared meaningfully; treat as not needed.
    if (srcParts.size() != requesterParts.size())
        return false;

    for (size_t i = 0; i < srcParts.size(); ++i) {
        if (srcParts[i] != 0 && requesterParts[i] == 0)
            return true;
    }
    return false;
}

void KnownFile::markUploadersCommentDirty()
{
    // Unconditional, exactly as MFC: sendCommentInfo() is where the peer's
    // acceptCommentVer is checked, and a peer that cannot take a comment costs
    // nothing but a flag here.
    for (auto* client : m_uploadingClients)
        if (client)
            client->setCommentDirty(true);
}

} // namespace eMule
