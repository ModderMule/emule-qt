/// @file FriendList.cpp
/// @brief Friend list manager implementation.

#include "friends/FriendList.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QDir>

#include <algorithm>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

FriendList::FriendList(QObject* parent)
    : QObject(parent)
{
}

FriendList::~FriendList() = default;

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

bool FriendList::load(const QString& configDir)
{
    const QString filePath = QDir(configDir).filePath(
        QString::fromLatin1(kFriendsMetFilename));

    SafeFile file;
    if (!file.open(filePath, QIODevice::ReadOnly)) {
        logInfo(QStringLiteral("FriendList: no friends file at %1").arg(filePath));
        return false;
    }

    try {
        const uint8 header = file.readUInt8();
        if (header != MET_HEADER) {
            logWarning(QStringLiteral("FriendList: invalid header 0x%1 in %2")
                           .arg(header, 2, 16, QChar(u'0'))
                           .arg(filePath));
            return false;
        }

        const uint32 friendCount = file.readUInt32();
        m_friends.reserve(friendCount);

        for (uint32 i = 0; i < friendCount; ++i) {
            auto f = std::make_unique<Friend>();
            f->loadFromFile(file);
            m_friends.push_back(std::move(f));
        }

        logInfo(QStringLiteral("FriendList: loaded %1 friends from %2")
                    .arg(friendCount)
                    .arg(filePath));
        emit listLoaded(static_cast<int>(friendCount));
        return true;

    } catch (const FileException& ex) {
        logError(QStringLiteral("FriendList: error reading %1: %2")
                     .arg(filePath, QString::fromUtf8(ex.what())));
    }
    return false;
}

void FriendList::save(const QString& configDir) const
{
    const QString filePath = QDir(configDir).filePath(
        QString::fromLatin1(kFriendsMetFilename));

    SafeFile file;
    if (!file.open(filePath, QIODevice::WriteOnly)) {
        logError(QStringLiteral("FriendList: failed to open %1 for writing").arg(filePath));
        return;
    }

    try {
        file.writeUInt8(MET_HEADER);
        file.writeUInt32(static_cast<uint32>(m_friends.size()));

        for (const auto& f : m_friends)
            f->writeToFile(file);

        logInfo(QStringLiteral("FriendList: saved %1 friends to %2")
                    .arg(m_friends.size())
                    .arg(filePath));

    } catch (const FileException& ex) {
        logError(QStringLiteral("FriendList: error writing %1: %2")
                     .arg(filePath, QString::fromUtf8(ex.what())));
    }
}

// ---------------------------------------------------------------------------
// Friend management
// ---------------------------------------------------------------------------

Friend* FriendList::addFriend(const uint8* userHash, uint32 lastUsedIP,
                              uint16 lastUsedPort, const QString& name,
                              bool hasHash)
{
    // Require either a valid hash or a valid IP+port
    if (!hasHash && (lastUsedIP == 0 || lastUsedPort == 0))
        return nullptr;

    // Duplicate check
    if (searchFriend(hasHash ? userHash : nullptr, lastUsedIP, lastUsedPort))
        return nullptr;

    auto f = std::make_unique<Friend>(
        userHash, std::time(nullptr), lastUsedIP, lastUsedPort,
        0, name, hasHash);

    Friend* ptr = f.get();
    m_friends.push_back(std::move(f));
    emit friendAdded(ptr);
    return ptr;
}

bool FriendList::removeFriend(Friend* f)
{
    auto it = std::ranges::find_if(m_friends,
        [f](const std::unique_ptr<Friend>& p) { return p.get() == f; });

    if (it == m_friends.end())
        return false;

    const QString name = f->name();
    m_friends.erase(it);
    emit friendRemoved(name);
    return true;
}

void FriendList::removeAll()
{
    m_friends.clear();
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

Friend* FriendList::searchFriend(const uint8* userHash, uint32 ip,
                                 uint16 port) const
{
    for (const auto& f : m_friends) {
        // Hash-based friend: match by hash
        if (userHash && f->hasUserhash()) {
            if (md4equ(f->userHash().data(), userHash))
                return f.get();
        } else {
            // IP+port-based friend
            if (f->lastUsedIP() == ip && ip != 0
                && f->lastUsedPort() == port && port != 0)
            {
                return f.get();
            }
        }
    }
    return nullptr;
}

bool FriendList::isAlreadyFriend(const QString& hexUserHash) const
{
    for (const auto& f : m_friends) {
        if (f->hasUserhash()
            && hexUserHash.compare(md4str(f->userHash().data()),
                                   Qt::CaseInsensitive) == 0)
        {
            return true;
        }
    }
    return false;
}

bool FriendList::isValid(const Friend* f) const
{
    return std::ranges::any_of(m_friends,
        [f](const std::unique_ptr<Friend>& p) { return p.get() == f; });
}

void FriendList::removeAllFriendSlots()
{
    for (const auto& f : m_friends)
        f->setFriendSlot(false);
}

} // namespace eMule
