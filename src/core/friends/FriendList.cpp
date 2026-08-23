#include "pch.h"
/// @file FriendList.cpp
/// @brief Friend list manager implementation.

#include "friends/FriendList.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QDir>


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
    const QString tmpPath = filePath + QStringLiteral(".tmp");
    const QString bakPath = filePath + QStringLiteral(".bak");

    try {
        QFile::remove(tmpPath);

        {
            SafeFile file;
            if (!file.open(tmpPath, QIODevice::WriteOnly)) {
                logError(QStringLiteral("FriendList: failed to open %1 for writing").arg(tmpPath));
                return;
            }

            file.writeUInt8(MET_HEADER);
            file.writeUInt32(static_cast<uint32>(m_friends.size()));

            for (const auto& f : m_friends)
                f->writeToFile(file);
        } // file closed before rename

        // Rotate: current → .bak
        QFile::remove(bakPath);
        if (QFile::exists(filePath)) {
            if (!QFile::rename(filePath, bakPath))
                QFile::remove(filePath);
        }

        // Rename temp → final
        if (!QFile::rename(tmpPath, filePath)) {
            logError(QStringLiteral("FriendList: failed to rename tmp → %1").arg(filePath));
            if (QFile::exists(bakPath))
                QFile::rename(bakPath, filePath);
            return;
        }

        logInfo(QStringLiteral("FriendList: saved %1 friends to %2")
                    .arg(m_friends.size())
                    .arg(filePath));

    } catch (const FileException& ex) {
        logError(QStringLiteral("FriendList: error writing %1: %2")
                     .arg(filePath, QString::fromUtf8(ex.what())));
        QFile::remove(tmpPath);
    }
}

// ---------------------------------------------------------------------------
// Friend management
// ---------------------------------------------------------------------------

Friend* FriendList::addFriend(const uint8* userHash, uint32 lastUsedIP,
                              uint16 lastUsedPort, const QString& name,
                              bool hasHash)
{
    return addFriend(userHash, Address::fromNetworkOrder(lastUsedIP), lastUsedPort,
                     name, hasHash);
}

Friend* FriendList::addFriend(const uint8* userHash, const Address& lastUsedAddress,
                              uint16 lastUsedPort, const QString& name,
                              bool hasHash)
{
    // Require either a valid hash or a valid address+port. An IPv6 peer has no IPv4 form,
    // so this must test the address itself, not its uint32 projection.
    const bool haveAddress = lastUsedAddress.isIPv6()
                             || (lastUsedAddress.isIPv4() && lastUsedAddress.toNetworkUint32() != 0);
    if (!hasHash && (!haveAddress || lastUsedPort == 0))
        return nullptr;

    // Duplicate check. searchFriend() matches on the IPv4 form, which is 0 for an IPv6
    // address — such a friend is deduplicated by hash alone.
    if (searchFriend(hasHash ? userHash : nullptr, lastUsedAddress.toNetworkUint32(), lastUsedPort))
        return nullptr;

    auto f = std::make_unique<Friend>(
        userHash, std::time(nullptr), lastUsedAddress.toNetworkUint32(), lastUsedPort,
        0, name, hasHash);
    f->setLastUsedAddress(lastUsedAddress);

    Friend* ptr = f.get();
    m_friends.push_back(std::move(f));

    // Link straight away if the peer happens to be connected, so a slot granted right after
    // adding reaches the client the upload queue scores rather than waiting for the next
    // hello. MFC srchybrid/FriendList.cpp:192 does the same from its client-taking overload.
    if (hasHash && userHash && theApp.clientList) {
        if (auto* client = theApp.clientList->findByUserHash(userHash, 0, 0))
            ptr->setLinkedClient(client);
    }

    emit friendAdded(ptr);
    return ptr;
}

bool FriendList::removeFriend(Friend* f)
{
    auto it = std::ranges::find_if(m_friends,
        [f](const std::unique_ptr<Friend>& p) { return p.get() == f; });

    if (it == m_friends.end())
        return false;

    // Unlink before erasing, as MFC does (srchybrid/FriendList.cpp:212). The destructor
    // would do it too, but doing it here keeps the client's friendPtr() from being observed
    // pointing at an entry that is already on its way out.
    f->setLinkedClient(nullptr);

    const QString name = f->name();
    m_friends.erase(it);
    emit friendRemoved(name);
    return true;
}

void FriendList::removeAll()
{
    for (const auto& f : m_friends)
        f->setLinkedClient(nullptr);
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
            if (f->lastUsedAddress().toNetworkUint32() == ip && ip != 0
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
