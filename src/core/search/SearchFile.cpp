#include "pch.h"
/// @file SearchFile.cpp
/// @brief Search result file entry — port of MFC CSearchFile.

#include "search/SearchFile.h"
#include "protocol/Tag.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <algorithm>
#include <cstring>

namespace eMule {

// ---------------------------------------------------------------------------
// Free function
// ---------------------------------------------------------------------------

bool isValidSearchResultClientIPPort(uint32 ip, uint16 port)
{
    // A valid search result source must have a non-zero IP and port.
    // Low IDs are allowed (server-assigned IDs), but zero is not.
    return ip != 0 && port != 0;
}

// ---------------------------------------------------------------------------
// Construction from network packet
// ---------------------------------------------------------------------------

SearchFile::SearchFile(FileDataIO& data, bool optUTF8,
                       uint32 serverIP, uint16 serverPort,
                       const QString& directory, bool kadResult)
    : m_directory(directory)
    , m_kadResult(kadResult)
{
    // Read 16-byte MD4 hash
    uint8 hash[16];
    data.readHash16(hash);
    setFileHash(hash);

    // Read client ID and port
    m_clientID = data.readUInt32();
    m_clientPort = data.readUInt16();

    // Validate client IP/port and store as initial source
    if (isValidSearchResultClientIPPort(m_clientID, m_clientPort)) {
        SClient client;
        client.ip = m_clientID;
        client.port = m_clientPort;
        client.serverIP = serverIP;
        client.serverPort = serverPort;
        m_clients.push_back(client);
    }

    // Read tags
    const uint32 tagCount = data.readUInt32();
    for (uint32 i = 0; i < tagCount; ++i) {
        Tag tag(data, optUTF8);
        convertED2KTag(tag);

        switch (tag.nameId()) {
        case FT_FILENAME:
            if (tag.isStr())
                setFileName(tag.strValue(), true);
            break;

        case FT_FILESIZE:
            if (tag.isInt())
                setFileSize(tag.intValue());
            else if (tag.isInt64(false))
                setFileSize(tag.int64Value());
            break;

        case FT_FILESIZE_HI:
            if (tag.isInt()) {
                // Combine with existing low 32 bits
                uint64 hiPart = static_cast<uint64>(tag.intValue()) << 32;
                setFileSize(static_cast<uint64>(fileSize()) | hiPart);
            }
            break;

        case FT_FILETYPE:
            if (tag.isStr())
                setFileType(tag.strValue());
            break;

        case FT_FILERATING:
            if (tag.isInt()) {
                // Rating is packed: upper bits = rating value
                m_userRating = (tag.intValue() & 0xFF) >> 1;
            }
            break;

        case FT_AICH_HASH:
            if (tag.isStr()) {
                AICHHash aichHash;
                if (decodeBase32(tag.strValue(),
                                 aichHash.getRawHash(),
                                 kAICHHashSize) == kAICHHashSize)
                {
                    fileIdentifier().setAICHHash(aichHash);
                }
            }
            break;

        case FT_SOURCES:
            if (tag.isInt())
                m_sourceCount = tag.intValue();
            break;

        case FT_COMPLETE_SOURCES:
            if (tag.isInt())
                m_completeSourceCount = tag.intValue();
            break;

        case FT_FOLDERNAME:
            if (tag.isStr())
                m_directory = tag.strValue();
            break;

        case FT_MEDIA_ARTIST:
        case FT_MEDIA_ALBUM:
        case FT_MEDIA_TITLE:
        case FT_MEDIA_LENGTH:
        case FT_MEDIA_BITRATE:
        case FT_MEDIA_CODEC:
        case FT_KADLASTPUBLISHSRC:
        case FT_PUBLISHINFO:
            addTagUnique(std::move(tag));
            break;

        default:
            addTagUnique(std::move(tag));
            break;
        }
    }

    // Store server as initial server entry
    if (serverIP != 0) {
        SServer srv;
        srv.ip = serverIP;
        srv.port = serverPort;
        srv.avail = m_sourceCount;
        m_servers.push_back(srv);
    }

    // Auto-detect file type from filename if not provided
    if (fileType().isEmpty() && !fileName().isEmpty())
        setFileType(getFileTypeByName(fileName()));
}

// ---------------------------------------------------------------------------
// Copy construction (for creating child entries)
// ---------------------------------------------------------------------------

SearchFile::SearchFile(const SearchFile* other)
    : AbstractFile(*other)
    , m_clients(other->m_clients)
    , m_servers(other->m_servers)
    , m_directory(other->m_directory)
    , m_sourceCount(other->m_sourceCount)
    , m_completeSourceCount(other->m_completeSourceCount)
    , m_kadPublishInfo(other->m_kadPublishInfo)
    , m_searchID(other->m_searchID)
    , m_spamRating(other->m_spamRating)
    , m_clientID(other->m_clientID)
    , m_clientPort(other->m_clientPort)
    , m_knownType(other->m_knownType)
    , m_kadResult(other->m_kadResult)
{
}

// ---------------------------------------------------------------------------
// AbstractFile override
// ---------------------------------------------------------------------------

void SearchFile::updateFileRatingCommentAvail(bool /*forceUpdate*/)
{
    bool hasNewComment = false;
    uint32 ratingSum = 0;
    uint32 ratingCount = 0;

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

// ---------------------------------------------------------------------------
// Source counting
// ---------------------------------------------------------------------------

void SearchFile::addSources(uint32 count)
{
    if (m_kadResult) {
        // Kad: take maximum (each node reports total availability)
        m_sourceCount = std::max(m_sourceCount, count);
    } else {
        // ED2K: additive (each server reports its own sources)
        m_sourceCount += count;
    }
}

void SearchFile::addCompleteSources(uint32 count)
{
    if (m_kadResult) {
        m_completeSourceCount = std::max(m_completeSourceCount, count);
    } else {
        m_completeSourceCount += count;
    }
}

int SearchFile::isComplete() const
{
    if (m_kadResult)
        return -1; // Unknown for Kad results

    if (m_sourceCount == 0)
        return -1;

    return (m_completeSourceCount > 0) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Client/server management
// ---------------------------------------------------------------------------

void SearchFile::addClient(const SClient& client)
{
    if (std::find(m_clients.begin(), m_clients.end(), client) == m_clients.end())
        m_clients.push_back(client);
}

void SearchFile::addServer(const SServer& server)
{
    auto it = std::find(m_servers.begin(), m_servers.end(), server);
    if (it != m_servers.end()) {
        // Update availability from existing server entry
        it->avail = std::max(it->avail, server.avail);
    } else {
        m_servers.push_back(server);
    }
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void SearchFile::storeToFile(FileDataIO& file) const
{
    // Write hash
    file.writeHash16(fileHash());

    // Write client ID/port
    file.writeUInt32(m_clientID);
    file.writeUInt16(m_clientPort);

    // Count tags to write
    uint32 tagCount = 0;
    if (!fileName().isEmpty()) ++tagCount;                    // FT_FILENAME
    ++tagCount;                                               // FT_FILESIZE
    if (static_cast<uint64>(fileSize()) > UINT32_MAX) ++tagCount; // FT_FILESIZE_HI
    if (!fileType().isEmpty()) ++tagCount;                    // FT_FILETYPE
    if (fileIdentifier().hasAICHHash()) ++tagCount;           // FT_AICH_HASH
    if (m_sourceCount > 0) ++tagCount;                        // FT_SOURCES
    if (m_completeSourceCount > 0) ++tagCount;                // FT_COMPLETE_SOURCES
    tagCount += static_cast<uint32>(tags().size());           // extra tags

    file.writeUInt32(tagCount);

    // Write mandatory tags
    if (!fileName().isEmpty())
        Tag(FT_FILENAME, fileName()).writeNewEd2kTag(file, UTF8Mode::Raw);

    if (static_cast<uint64>(fileSize()) > UINT32_MAX) {
        Tag(FT_FILESIZE, static_cast<uint32>(fileSize() & 0xFFFFFFFFu)).writeNewEd2kTag(file);
        Tag(FT_FILESIZE_HI, static_cast<uint32>(static_cast<uint64>(fileSize()) >> 32)).writeNewEd2kTag(file);
    } else {
        Tag(FT_FILESIZE, static_cast<uint32>(fileSize())).writeNewEd2kTag(file);
    }

    if (!fileType().isEmpty())
        Tag(FT_FILETYPE, fileType()).writeNewEd2kTag(file, UTF8Mode::Raw);

    if (fileIdentifier().hasAICHHash())
        Tag(FT_AICH_HASH, fileIdentifier().getAICHHash().getString())
            .writeNewEd2kTag(file, UTF8Mode::Raw);

    if (m_sourceCount > 0)
        Tag(FT_SOURCES, m_sourceCount).writeNewEd2kTag(file);

    if (m_completeSourceCount > 0)
        Tag(FT_COMPLETE_SOURCES, m_completeSourceCount).writeNewEd2kTag(file);

    // Write extra tags
    for (const auto& tag : tags())
        tag.writeNewEd2kTag(file, UTF8Mode::Raw);
}

// ---------------------------------------------------------------------------
// Private: ED2K tag conversion
// ---------------------------------------------------------------------------

void SearchFile::convertED2KTag(Tag& tag)
{
    // Convert old-style string-named tags from eDonkeyHybrid servers
    // to the newer numeric-named equivalents used internally.
    if (!tag.hasName())
        return;

    const QByteArray& name = tag.name();

    if (name == FT_ED2K_MEDIA_ARTIST) {
        tag = Tag(static_cast<uint8>(FT_MEDIA_ARTIST), tag.strValue());
    } else if (name == FT_ED2K_MEDIA_ALBUM) {
        tag = Tag(static_cast<uint8>(FT_MEDIA_ALBUM), tag.strValue());
    } else if (name == FT_ED2K_MEDIA_TITLE) {
        tag = Tag(static_cast<uint8>(FT_MEDIA_TITLE), tag.strValue());
    } else if (name == FT_ED2K_MEDIA_BITRATE) {
        if (tag.isStr()) {
            bool ok = false;
            uint32 val = tag.strValue().toUInt(&ok);
            if (ok)
                tag = Tag(static_cast<uint8>(FT_MEDIA_BITRATE), val);
        } else if (tag.isInt()) {
            tag = Tag(static_cast<uint8>(FT_MEDIA_BITRATE), tag.intValue());
        }
    } else if (name == FT_ED2K_MEDIA_CODEC) {
        tag = Tag(static_cast<uint8>(FT_MEDIA_CODEC), tag.strValue());
    } else if (name == FT_ED2K_MEDIA_LENGTH) {
        if (tag.isStr()) {
            // Parse "H:M:S" or "M:S" format to seconds
            const QString& str = tag.strValue();
            uint32 seconds = 0;
            const auto parts = str.split(u':');
            if (parts.size() == 3) {
                seconds = parts[0].toUInt() * 3600
                        + parts[1].toUInt() * 60
                        + parts[2].toUInt();
            } else if (parts.size() == 2) {
                seconds = parts[0].toUInt() * 60
                        + parts[1].toUInt();
            } else {
                bool ok = false;
                seconds = str.toUInt(&ok);
                if (!ok) seconds = 0;
            }
            tag = Tag(static_cast<uint8>(FT_MEDIA_LENGTH), seconds);
        } else if (tag.isInt()) {
            tag = Tag(static_cast<uint8>(FT_MEDIA_LENGTH), tag.intValue());
        }
    }
}

} // namespace eMule
