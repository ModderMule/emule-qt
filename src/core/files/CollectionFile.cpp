#include "pch.h"
/// @file CollectionFile.cpp
/// @brief Collection file entry — port of MFC CCollectionFile.

#include "files/CollectionFile.h"
#include "protocol/ED2KLink.h"
#include "protocol/Tag.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

namespace eMule {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CollectionFile::CollectionFile() = default;

CollectionFile::CollectionFile(FileDataIO& data)
{
    const uint32 tagCount = data.readUInt32();
    for (uint32 i = 0; i < tagCount; ++i) {
        Tag tag(data, true);
        switch (tag.nameId()) {
        case FT_FILEHASH:
            if (tag.isHash())
                setFileHash(tag.hashValue());
            break;
        case FT_FILESIZE:
            if (tag.isInt())
                setFileSize(tag.intValue());
            else if (tag.isInt64(false))
                setFileSize(tag.int64Value());
            break;
        case FT_FILENAME:
            if (tag.isStr())
                setFileName(tag.strValue(), true);
            break;
        case FT_FILETYPE:
            if (tag.isStr())
                setFileType(tag.strValue());
            break;
        case FT_AICH_HASH:
            if (tag.isStr()) {
                AICHHash aichHash;
                if (decodeBase32(tag.strValue(),
                                 aichHash.getRawHash(),
                                 kAICHHashSize) == kAICHHashSize)
                {
                    fileIdentifier().setAICHHash(aichHash);
                    m_hasCollectionExtraInfo = true;
                }
            }
            break;
        case FT_COLLECTIONAUTHOR:
        case FT_COLLECTIONAUTHORKEY:
            addTagUnique(std::move(tag));
            break;
        default:
            addTagUnique(std::move(tag));
            break;
        }
    }
}

CollectionFile::CollectionFile(const AbstractFile* file)
{
    if (!file)
        return;

    setFileHash(file->fileHash());
    setFileSize(file->fileSize());
    setFileName(file->fileName(), true);
    if (!file->fileType().isEmpty())
        setFileType(file->fileType());

    if (file->fileIdentifier().hasAICHHash()) {
        fileIdentifier().setAICHHash(file->fileIdentifier().getAICHHash());
        m_hasCollectionExtraInfo = true;
    }
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

bool CollectionFile::writeCollectionInfo(FileDataIO& data) const
{
    // Count tags to write: hash + size + name + optional type + optional AICH
    uint32 tagCount = 3; // hash, size, name are mandatory
    if (!fileType().isEmpty())
        ++tagCount;
    if (fileIdentifier().hasAICHHash())
        ++tagCount;
    // Add extra tags (author, etc.)
    tagCount += static_cast<uint32>(tags().size());

    data.writeUInt32(tagCount);

    // Hash tag
    Tag(FT_FILEHASH, fileHash()).writeNewEd2kTag(data);

    // Size tag
    if (static_cast<uint64>(fileSize()) > UINT32_MAX)
        Tag(FT_FILESIZE, static_cast<uint64>(fileSize())).writeNewEd2kTag(data);
    else
        Tag(FT_FILESIZE, static_cast<uint32>(fileSize())).writeNewEd2kTag(data);

    // Name tag
    Tag(FT_FILENAME, fileName()).writeNewEd2kTag(data, UTF8Mode::Raw);

    // Optional type tag
    if (!fileType().isEmpty())
        Tag(FT_FILETYPE, fileType()).writeNewEd2kTag(data, UTF8Mode::Raw);

    // Optional AICH hash tag
    if (fileIdentifier().hasAICHHash())
        Tag(FT_AICH_HASH, fileIdentifier().getAICHHash().getString())
            .writeNewEd2kTag(data, UTF8Mode::Raw);

    // Write extra tags
    for (const auto& tag : tags())
        tag.writeNewEd2kTag(data, UTF8Mode::Raw);

    return true;
}

// ---------------------------------------------------------------------------
// ED2K link initialization
// ---------------------------------------------------------------------------

bool CollectionFile::initFromLink(const QString& link)
{
    auto parsed = parseED2KLink(link);
    if (!parsed)
        return false;

    auto* fileLink = std::get_if<ED2KFileLink>(&*parsed);
    if (!fileLink)
        return false;

    setFileHash(fileLink->hash.data());
    setFileSize(fileLink->size);
    setFileName(fileLink->name, true);

    if (fileLink->hasValidAICHHash) {
        fileIdentifier().setAICHHash(fileLink->aichHash);
        m_hasCollectionExtraInfo = true;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Comment/rating
// ---------------------------------------------------------------------------

void CollectionFile::updateFileRatingCommentAvail(bool /*forceUpdate*/)
{
    bool hasNewComment = false;
    uint32 ratingSum = 0;
    uint32 ratingCount = 0;

    // Aggregate from Kad notes cache
    for (const auto& [rating, comment] : m_kadNotesCache) {
        if (!comment.isEmpty())
            hasNewComment = true;
        if (rating > 0 && rating <= 5) {
            ratingSum += rating;
            ++ratingCount;
        }
    }

    m_hasComment = hasNewComment;
    m_userRating = (ratingCount > 0) ? (ratingSum / ratingCount) : 0;
}

} // namespace eMule
