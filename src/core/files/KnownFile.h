#pragma once

/// @file KnownFile.h
/// @brief Known (completed) file — partial port of MFC CKnownFile.
///
/// Core file metadata, priority, upload client tracking, and media metadata.
/// GUI-dependent code (BarShader, CxImage, FrameGrabThread) is decoupled:
/// KnownFile exposes data via getters and emits change notifications through
/// a FileNotifier QObject member, so the GUI layer can react without coupling.

#include "files/ShareableFile.h"
#include "files/StatisticFile.h"
#include "utils/Opcodes.h"

#include <QByteArray>
#include <QObject>

#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace eMule {

class AICHHashTree;
class Collection;
class FileDataIO;
class Packet;
class UpDownClient;
class SafeMemFile;

// ---------------------------------------------------------------------------
// FileNotifier — lightweight QObject signal emitter owned by KnownFile.
//
// KnownFile cannot inherit QObject (would break the AbstractFile hierarchy
// which supports copying). Instead, KnownFile owns a FileNotifier member
// and GUI code connects to knownFile.notifier()->signals.
// ---------------------------------------------------------------------------

class FileNotifier : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void fileUpdated();
    void priorityChanged(uint8 newPriority);
    void metadataUpdated();
    void grabFramesRequested(const QString& filePath, uint8 count,
                             double startTime, bool reduceColor, uint16 maxWidth);
};

// ---------------------------------------------------------------------------
// Priority constants (originally in PartFile.h)
// ---------------------------------------------------------------------------

inline constexpr uint8 kPrVeryLow  = 4;
inline constexpr uint8 kPrLow      = 0;
inline constexpr uint8 kPrNormal   = 1;
inline constexpr uint8 kPrHigh     = 2;
inline constexpr uint8 kPrVeryHigh = 3;
inline constexpr uint8 kPrAuto     = 5;

class KnownFile : public ShareableFile {
public:
    KnownFile();
    ~KnownFile() override;

    // Signal emitter — GUI connects here (not QObject inheritance)
    [[nodiscard]] FileNotifier* notifier() { return &m_notifier; }

    // File size override — computes part counts
    void setFileSize(EMFileSize size) override;

    // Filename override — calls base, marks Kad keyword list dirty
    void setFileName(const QString& name,
                     bool replaceInvalidChars = false,
                     bool autoSetFileType = true,
                     bool removeControlChars = false) override;

    // Serialization (known.met format)
    bool loadFromFile(FileDataIO& file);
    bool writeToFile(FileDataIO& file) const;

    // File date
    [[nodiscard]] time_t utcFileDate() const { return m_utcLastModified; }
    void setUtcFileDate(time_t date) { m_utcLastModified = date; }

    // Purge check
    [[nodiscard]] bool shouldPartiallyPurgeFile() const;
    [[nodiscard]] time_t lastSeen() const { return m_timeLastSeen; }
    void setLastSeen(time_t t) { m_timeLastSeen = t; }

    // Part counts
    [[nodiscard]] uint16 partCount() const { return m_partCount; }
    [[nodiscard]] uint16 ed2kPartCount() const { return m_ed2kPartCount; }

    /// Path of the file that actually holds the bytes.
    ///
    /// For a completed share that is the file itself; a PartFile overrides it
    /// with its `.part` temp file. Anything reading file data by offset —
    /// UploadDiskIOThread, HttpCachePublisher — goes through this rather than
    /// re-deriving the `.part.met` → `.part` rule.
    [[nodiscard]] virtual QString dataFilePath() const { return filePath(); }

    /// Do we hold every byte of part @p part?
    ///
    /// Always true for a completed shared file — a KnownFile has no gaps by
    /// definition. PartFile overrides it with its gap list. Callers that need
    /// "can I read this part off disk right now" (HttpCacheManager does) ask
    /// this instead of branching on the concrete type.
    [[nodiscard]] virtual bool isPartComplete(uint32 part) const
    {
        return part < m_partCount;
    }

    // Upload priority
    [[nodiscard]] uint8 upPriority() const { return m_upPriority; }
    void setUpPriority(uint8 priority, bool save = true);
    [[nodiscard]] bool isAutoUpPriority() const { return m_autoUpPriority; }
    void setAutoUpPriority(bool flag) { m_autoUpPriority = flag; }

    // Auto-priority (adjusts priority based on uploading client count)
    void updateAutoUpPriority();

    // ED2K publishing
    [[nodiscard]] bool publishedED2K() const { return m_publishedED2K; }
    void setPublishedED2K(bool val);

    // Kademlia
    [[nodiscard]] uint32 kadFileSearchID() const { return m_kadFileSearchID; }
    void setKadFileSearchID(uint32 id) { m_kadFileSearchID = id; }

    [[nodiscard]] time_t lastPublishTimeKadSrc() const { return m_lastPublishTimeKadSrc; }
    void setLastPublishTimeKadSrc(time_t t, uint32 buddyIP = 0);

    /// Kad keywords extracted from filename.
    [[nodiscard]] const std::vector<QString>& kadKeywords() const { return m_kadKeywords; }

    [[nodiscard]] time_t lastPublishTimeKadNotes() const { return m_lastPublishTimeKadNotes; }
    void setLastPublishTimeKadNotes(time_t t) { m_lastPublishTimeKadNotes = t; }

    [[nodiscard]] uint32 lastBuddyIP() const { return m_lastBuddyIP; }

    // Cached results of a Kad "notes" search (the only Kad lookup that returns a
    // filename for a given file hash). Keyed by the note publisher's source ID so
    // re-running the search never double-counts the same publisher. Persisted in the
    // .met record, pruned by the kadFileName{ExpiryDays,MaxCount} prefs.
    struct KadNoteInfo {
        QString fileName;
        QString comment;
        uint8   rating = 0;
        time_t  lastSeen = 0;
    };
    void addKadNote(const QByteArray& publisherId, const QString& fileName,
                    const QString& comment, uint8 rating, time_t now);
    [[nodiscard]] const std::map<QByteArray, KadNoteInfo>& kadNotes() const { return m_kadNotes; }

    // AICH
    [[nodiscard]] bool isAICHRecoverHashSetAvailable() const { return m_aichRecoverHashSetAvailable; }
    void setAICHRecoverHashSetAvailable(bool val) { m_aichRecoverHashSetAvailable = val; }

    // Metadata version
    [[nodiscard]] uint32 metaDataVer() const { return m_metaDataVer; }

    // Media metadata extraction
    void updateMetaDataTags();
    void removeMetaDataTags();

    // Frame grabbing request (emits signal only — GUI spawns thread)
    void requestGrabFrames(uint8 count, double startTime,
                           bool reduceColor, uint16 maxWidth);

    // Upload client tracking
    void addUploadingClient(UpDownClient* client);
    void removeUploadingClient(UpDownClient* client);
    [[nodiscard]] const std::vector<UpDownClient*>& uploadingClients() const { return m_uploadingClients; }
    [[nodiscard]] int uploadingClientCount() const { return static_cast<int>(m_uploadingClients.size()); }
    [[nodiscard]] bool hasUploadingClients() const { return !m_uploadingClients.empty(); }

    // Complete sources tracking
    [[nodiscard]] uint16 completeSourcesCount() const { return m_completeSourcesCount; }
    [[nodiscard]] uint16 completeSourcesCountLo() const { return m_completeSourcesCountLo; }
    [[nodiscard]] uint16 completeSourcesCountHi() const { return m_completeSourcesCountHi; }
    [[nodiscard]] time_t completeSourcesTime() const { return m_completeSourcesTime; }

    // Part frequency
    [[nodiscard]] const std::vector<uint16>& availPartFrequency() const { return m_availPartFrequency; }

    // Statistics
    StatisticFile statistic;

    // Hashing — creates MD4 hashset and AICH from disk file
    bool createFromFile(const QString& directory, const QString& filename,
                        std::function<void(int)> progressCallback = {});
    bool createAICHHashSetOnly();

    // Core hash computation
    static void createHash(QIODevice& device, uint64 length,
                           uint8* md4HashOut, AICHHashTree* aichTree);
    static bool createHashFromFile(const QString& filePath, uint64 length,
                                   uint8* md4HashOut, AICHHashTree* aichTree);
    static bool createHashFromMemory(const uint8* data, uint32 size,
                                     uint8* md4HashOut, AICHHashTree* aichTree);

    // Protocol
    virtual std::unique_ptr<Packet> createSrcInfoPacket(const UpDownClient* forClient,
                                                         uint8 version, uint16 options) const;

    // Kad publishing
    bool publishSrc();
    bool publishNotes();

    void updateFileRatingCommentAvail(bool forceUpdate = false) override;
    void updatePartsInfo();

    // Collection support (for .emulecollection files shared on the network)
    [[nodiscard]] Collection* collection() const { return m_collection.get(); }
    void setCollection(std::unique_ptr<Collection> coll);

protected:
    bool loadTagsFromFile(FileDataIO& file);
    bool loadDateFromFile(FileDataIO& file);

    // Extended Source Exchange: write one source's variable tag block (tagCount + tags),
    // replacing the fixed serverIP/serverPort record so a source can carry its public IPv6.
    // Shared by KnownFile::createSrcInfoPacket and PartFile::createSrcInfoPacket.
    // @p peerSkipsUnknownTags is the *requester's* MODMISC_EXTXS_SKIPTAGS bit — only then may
    // the user-hash and crypt-options tags be added; see the comment at the emission site.
    void writeExtendedSourceExchangeData(SafeMemFile& data, const UpDownClient* src,
                                         bool peerSkipsUnknownTags) const;

    /// The whole source-exchange packet format in one place: version/ExtSX selection,
    /// header, source cap, the ID field, and either the tag block or the legacy
    /// serverIP/userHash/crypt tail. Both createSrcInfoPacket overrides funnel through
    /// here so the two sides cannot drift — they differ only in which clients they
    /// offer (@p candidates) and how they judge one usable (@p eligible), since the
    /// upload side reads upPartStatus() and the download side partStatus().
    /// @param eligible  Called per candidate before the low-ID/self filters below;
    ///                  return false to skip it. Returns nullptr if nothing qualifies.
    [[nodiscard]] std::unique_ptr<Packet> buildSrcInfoPacket(
        const UpDownClient* forClient, uint8 version,
        const std::vector<UpDownClient*>& candidates,
        const std::function<bool(const UpDownClient*)>& eligible) const;

    // Kad notes cache (re)serialization — shared by KnownFile (known.met) and
    // PartFile (.part.met) so both record formats use one implementation.
    [[nodiscard]] QByteArray serializeKadNotes() const;
    void deserializeKadNotes(const QByteArray& blob);

private:
    void pruneKadNotes();  // drop expired entries, then cap to the newest N

    /// True when `src` holds at least one chunk the requester still wants, so it is
    /// worth exchanging. `requesterParts` is the requester's upload part status; an
    /// empty one means it doesn't report chunk status, in which case any source with
    /// at least one complete part qualifies.
    [[nodiscard]] bool sourceHasNeededPart(const UpDownClient* src,
                                           const std::vector<uint8>& requesterParts) const;

    FileNotifier m_notifier;
    std::unique_ptr<Collection> m_collection;
    std::vector<UpDownClient*> m_uploadingClients;
    std::vector<uint16> m_availPartFrequency;
    std::vector<QString> m_kadKeywords;
    std::map<QByteArray, KadNoteInfo> m_kadNotes;

    time_t m_utcLastModified = static_cast<time_t>(-1);
    time_t m_timeLastSeen = 0;
    time_t m_lastPublishTimeKadSrc = 0;
    time_t m_lastPublishTimeKadNotes = 0;
    time_t m_completeSourcesTime = 0;

    uint32 m_kadFileSearchID = 0;
    uint32 m_lastBuddyIP = 0;
    uint32 m_metaDataVer = 0;

    uint16 m_partCount = 0;
    uint16 m_ed2kPartCount = 0;
    uint16 m_completeSourcesCount = 1;
    uint16 m_completeSourcesCountLo = 1;
    uint16 m_completeSourcesCountHi = 1;

    uint8 m_upPriority = kPrNormal;
    bool m_autoUpPriority = true;
    bool m_publishedED2K = false;
    bool m_aichRecoverHashSetAvailable = false;
};

} // namespace eMule
