#pragma once

/// @file AbstractFile.h
/// @brief Abstract base class for all file types — replaces MFC CAbstractFile.
///
/// Hierarchy: AbstractFile → ShareableFile → KnownFile → PartFile
///                         → CollectionFile
///                         → SearchFile
///
/// Kademlia notes and comment loading from INI are stubbed (need Modules 10/16).

#include "crypto/FileIdentifier.h"
#include "protocol/Tag.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/Types.h"

#include <QString>

#include <vector>

namespace eMule {

class AbstractFile {
public:
    AbstractFile();
    explicit AbstractFile(const AbstractFile& other);
    AbstractFile& operator=(const AbstractFile& other);
    virtual ~AbstractFile() = default;

    // Filename
    [[nodiscard]] const QString& fileName() const { return m_fileName; }
    virtual void setFileName(const QString& name,
                             bool replaceInvalidChars = false,
                             bool autoSetFileType = true,
                             bool removeControlChars = false);

    // File type (ED2K type string: "Audio", "Video", etc.)
    [[nodiscard]] const QString& fileType() const { return m_fileType; }
    virtual void setFileType(const QString& type);
    [[nodiscard]] QString fileTypeDisplayStr() const;

    // File identifier (MD4 + AICH hashes)
    [[nodiscard]] FileIdentifier& fileIdentifier() { return m_fileIdentifier; }
    [[nodiscard]] const FileIdentifier& fileIdentifier() const { return m_fileIdentifier; }
    [[nodiscard]] const uint8* fileHash() const { return m_fileIdentifier.getMD4Hash(); }
    void setFileHash(const uint8* hash) { m_fileIdentifier.setMD4Hash(hash); }
    [[nodiscard]] bool hasNullHash() const;

    // ED2K link generation
    [[nodiscard]] QString getED2kLink(bool hashset = false, bool html = false) const;

    // File size
    [[nodiscard]] EMFileSize fileSize() const { return m_fileSize; }
    virtual void setFileSize(EMFileSize size) { m_fileSize = size; }
    [[nodiscard]] bool isLargeFile() const { return static_cast<uint64>(m_fileSize) > OLD_MAX_EMULE_FILE_SIZE; }
    [[nodiscard]] virtual bool isPartFile() const { return false; }

    // Tag access
    [[nodiscard]] uint32 getIntTagValue(uint8 tagId) const;
    [[nodiscard]] uint32 getIntTagValue(const QByteArray& tagName) const;
    [[nodiscard]] bool   getIntTagValue(uint8 tagId, uint32& value) const;
    [[nodiscard]] uint64 getInt64TagValue(uint8 tagId) const;
    [[nodiscard]] uint64 getInt64TagValue(const QByteArray& tagName) const;
    [[nodiscard]] bool   getInt64TagValue(uint8 tagId, uint64& value) const;
    void setIntTagValue(uint8 tagId, uint32 value);
    void setInt64TagValue(uint8 tagId, uint64 value);
    [[nodiscard]] const QString& getStrTagValue(uint8 tagId) const;
    [[nodiscard]] const QString& getStrTagValue(const QByteArray& tagName) const;
    void setStrTagValue(uint8 tagId, const QString& value);

    [[nodiscard]] const Tag* getTag(uint8 tagId, uint8 tagType) const;
    [[nodiscard]] const Tag* getTag(const QByteArray& tagName, uint8 tagType) const;
    [[nodiscard]] const Tag* getTag(uint8 tagId) const;
    [[nodiscard]] const Tag* getTag(const QByteArray& tagName) const;

    [[nodiscard]] const std::vector<Tag>& tags() const { return m_tags; }

    void addTagUnique(Tag tag);
    void deleteTag(uint8 tagId);
    void clearTags();
    void copyTags(const std::vector<Tag>& tags);

    // Comment / rating
    [[nodiscard]] bool hasComment() const { return m_hasComment; }
    void setHasComment(bool val) { m_hasComment = val; }
    [[nodiscard]] uint32 userRating(bool kadSearchIndicator = false) const
    {
        return (kadSearchIndicator && m_kadCommentSearchRunning) ? 6 : m_userRating;
    }
    [[nodiscard]] bool hasRating() const { return m_userRating > 0; }
    [[nodiscard]] bool hasBadRating() const { return hasRating() && m_userRating < 2; }
    void setUserRating(uint32 rating) { m_userRating = rating; }

    const QString& getFileComment();
    uint32 getFileRating();

    /// @note Stubbed — needs Preferences for INI file path.
    void loadComment();

    virtual void updateFileRatingCommentAvail(bool forceUpdate = false) = 0;

    // Kad notes cache (rating, comment pairs from Kad search results)
    void addKadNote(uint8 rating, const QString& comment);
    void clearKadNotes();
    [[nodiscard]] const std::vector<std::pair<uint8, QString>>& kadNotesCache() const { return m_kadNotesCache; }

    // Kad comment search state
    [[nodiscard]] bool isKadCommentSearchRunning() const { return m_kadCommentSearchRunning; }
    void setKadCommentSearchRunning(bool val);

protected:
    std::vector<Tag> m_tags;
    EMFileSize m_fileSize = 0;              // must be before m_fileIdentifier (init order)
    FileIdentifier m_fileIdentifier;
    QString m_fileName;
    QString m_comment;
    QString m_fileType;
    uint32 m_rating = 0;
    uint32 m_userRating = 0;
    std::vector<std::pair<uint8, QString>> m_kadNotesCache;
    bool m_commentLoaded = false;
    bool m_hasComment = false;
    bool m_kadCommentSearchRunning = false;
};

} // namespace eMule
