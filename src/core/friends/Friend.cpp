#include "pch.h"
/// @file Friend.cpp
/// @brief Friend data class implementation.

#include "friends/Friend.h"
#include "protocol/Tag.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <cstring>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Friend::Friend() = default;

Friend::Friend(const uint8* userHash, std::time_t lastSeen, uint32 lastUsedIP,
               uint16 lastUsedPort, std::time_t lastChatted,
               const QString& name, bool hasHash)
    : m_name(name)
    , m_lastUsedIP(lastUsedIP)
    , m_lastUsedPort(lastUsedPort)
    , m_lastSeen(lastSeen)
    , m_lastChatted(lastChatted)
{
    if (hasHash && userHash)
        md4cpy(m_userHash.data(), userHash);
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

void Friend::loadFromFile(FileDataIO& file)
{
    file.readHash16(m_userHash.data());
    m_lastUsedIP = file.readUInt32();
    m_lastUsedPort = file.readUInt16();
    m_lastSeen = static_cast<std::time_t>(file.readUInt32());
    m_lastChatted = static_cast<std::time_t>(file.readUInt32());

    const uint32 tagCount = file.readUInt32();
    for (uint32 i = 0; i < tagCount; ++i) {
        const Tag tag(file, false);
        switch (tag.nameId()) {
        case kFriendTagName:
            if (tag.isStr() && m_name.isEmpty())
                m_name = tag.strValue();
            break;
        case kFriendTagKadID:
            if (tag.isHash())
                md4cpy(m_kadID.data(), tag.hashValue());
            break;
        default:
            break;
        }
    }
}

void Friend::writeToFile(FileDataIO& file) const
{
    file.writeHash16(m_userHash.data());
    file.writeUInt32(m_lastUsedIP);
    file.writeUInt16(m_lastUsedPort);
    file.writeUInt32(static_cast<uint32>(m_lastSeen));
    file.writeUInt32(static_cast<uint32>(m_lastChatted));

    // Count tags, write placeholder, then tags, then backpatch count
    uint32 tagCount = 0;
    const qint64 tagCountPos = file.position();
    file.writeUInt32(0);

    if (!m_name.isEmpty()) {
        Tag nameTag(kFriendTagName, m_name);
        nameTag.writeTagToFile(file, UTF8Mode::OptBOM);
        ++tagCount;
    }
    if (hasKadID()) {
        Tag kadTag(kFriendTagKadID, m_kadID.data());
        kadTag.writeNewEd2kTag(file);
        ++tagCount;
    }

    // Backpatch tag count
    const qint64 endPos = file.position();
    file.seek(tagCountPos, 0); // SEEK_SET
    file.writeUInt32(tagCount);
    file.seek(endPos, 0);
}

// ---------------------------------------------------------------------------
// Hash queries
// ---------------------------------------------------------------------------

bool Friend::hasUserhash() const
{
    return !isnulmd4(m_userHash.data());
}

bool Friend::hasKadID() const
{
    return !isnulmd4(m_kadID.data());
}

// ---------------------------------------------------------------------------
// Mutators
// ---------------------------------------------------------------------------

void Friend::setUserHash(const uint8* hash)
{
    if (hash)
        md4cpy(m_userHash.data(), hash);
    else
        md4clr(m_userHash.data());
}

void Friend::setKadID(const uint8* id)
{
    if (id)
        md4cpy(m_kadID.data(), id);
    else
        md4clr(m_kadID.data());
}

} // namespace eMule
