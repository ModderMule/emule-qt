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

#include <QObject>

#include <ctime>
#include <functional>
#include <memory>
#include <vector>

namespace eMule {

class AICHHashTree;
class FileDataIO;
class Packet;
class UpDownClient;

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
    ~KnownFile() override = default;

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
    std::unique_ptr<Packet> createSrcInfoPacket(const UpDownClient* forClient,
                                                 uint8 version, uint16 options) const;

    // Kad publishing
    bool publishSrc();
    bool publishNotes();

    void updateFileRatingCommentAvail(bool forceUpdate = false) override;
    void updatePartsInfo();

protected:
    bool loadTagsFromFile(FileDataIO& file);
    bool loadDateFromFile(FileDataIO& file);

private:
    FileNotifier m_notifier;
    std::vector<UpDownClient*> m_uploadingClients;
    std::vector<uint16> m_availPartFrequency;
    std::vector<QString> m_kadKeywords;

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
