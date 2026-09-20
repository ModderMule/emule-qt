#include "pch.h"
/// @file Friend.cpp
/// @brief Friend data class implementation.

#include "friends/Friend.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "friends/FriendList.h"
#include "kademlia/Kademlia.h"
#include "net/EMSocket.h"
#include "protocol/Tag.h"
#include "utils/Log.h"
#include "utils/TimeUtils.h"
#include "utils/OtherFunctions.h"


namespace eMule {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Friend::Friend() = default;

Friend::Friend(const uint8* userHash, std::time_t lastSeen, uint32 lastUsedIP,
               uint16 lastUsedPort, std::time_t lastChatted,
               const QString& name, bool hasHash)
    : m_name(name)
    , m_lastUsedAddress(Address::fromNetworkOrder(lastUsedIP))
    , m_lastUsedPort(lastUsedPort)
    , m_lastSeen(lastSeen)
    , m_lastChatted(lastChatted)
{
    if (hasHash && userHash)
        md4cpy(m_userHash.data(), userHash);
}

Friend::~Friend()
{
    // A Kad lookup outlives us otherwise, and answers into freed memory.
    // MFC srchybrid/Friend.cpp:90-92.
    if (auto* kad = kad::Kademlia::instance())
        kad->cancelClientSearch(*this);

    // Validated, because a friend entry can outlive the client it was linked to — a client
    // destroyed without going through UpDownClient's destructor unlink would otherwise be
    // dereferenced here. MFC srchybrid/Friend.cpp:85-89 does the same.
    if (UpDownClient* client = linkedClient(true)) {
        client->setFriendSlot(false);
        client->setFriendPtr(nullptr);
    }
    m_linkedClient = nullptr;
}

// ---------------------------------------------------------------------------
// Linked client
// ---------------------------------------------------------------------------

UpDownClient* Friend::linkedClient(bool validate) const
{
    if (validate && m_linkedClient && theApp.clientList
        && !theApp.clientList->isValidClient(m_linkedClient))
    {
        return nullptr;
    }
    return m_linkedClient;
}

void Friend::setLinkedClient(UpDownClient* client)
{
    // MFC srchybrid/Friend.cpp:171-198.
    if (client == m_linkedClient)
        return;

    if (client) {
        // Carry the slot across: from our own flag when nothing was linked, otherwise from
        // the client that held it, since that one was authoritative.
        client->setFriendSlot(m_linkedClient ? m_linkedClient->friendSlot() : m_friendSlot);

        m_lastSeen = std::time(nullptr);
        m_lastUsedAddress = client->connectAddress();
        m_lastUsedPort = client->userPort();
        m_name = client->userName();
        md4cpy(m_userHash.data(), client->userHash());

        client->setFriendPtr(this);
    } else if (m_linkedClient) {
        // Going offline — take the flag back so it survives until the friend reconnects.
        m_friendSlot = m_linkedClient->friendSlot();
    }

    if (m_linkedClient) {
        // The old client is no longer this friend, so it must not keep the slot.
        m_linkedClient->setFriendSlot(false);
        m_linkedClient->setFriendPtr(nullptr);
    }

    m_linkedClient = client;
}

// ---------------------------------------------------------------------------
// Connecting — MFC srchybrid/Friend.cpp:210-437
// ---------------------------------------------------------------------------

UpDownClient* Friend::clientForChatSession()
{
    UpDownClient* client = linkedClient(true);

    if (!client) {
        if (m_lastUsedAddress.isNull() || m_lastUsedPort == 0)
            return nullptr;   // nothing to dial

        // Built from the stored address rather than a user ID: setUserAddress() carries an
        // IPv6 friend, where the uint32 form would collapse to 0.0.0.0.
        client = new UpDownClient(m_lastUsedPort, m_lastUsedAddress.toNetworkUint32(),
                                  0, 0, nullptr, /*ed2kID*/ true);
        client->setUserAddress(m_lastUsedAddress);
        client->setUserName(m_name);
        if (hasUserhash())
            client->setUserHash(m_userHash.data());

        if (theApp.clientList)
            theApp.clientList->addClient(client);
        setLinkedClient(client);
    }

    client->setChatState(ChatState::Chatting);
    return client;
}

bool Friend::tryToConnect()
{
    if (isTryingToConnect())
        return true;   // an attempt is already running; its result covers this caller too

    const bool haveAddress = !m_lastUsedAddress.isNull() && m_lastUsedPort != 0;
    if (!hasKadID() && !haveAddress) {
        reportProgress(ChatConnectProgress::Connecting);
        finishConnecting(false);
        return false;
    }

    UpDownClient* client = clientForChatSession();
    if (!client) {
        reportProgress(ChatConnectProgress::Connecting);
        finishConnecting(false);
        return false;
    }

    m_connectState = FriendConnectState::Connecting;
    client->setChatState(ChatState::Connecting);
    reportProgress(ChatConnectProgress::Connecting);

    if (client->socket() && client->socket()->isConnected()) {
        // Already connected — the only open question is secure identification.
        updateFriendConnectionState(FriendConnectReport::Established);
        return true;
    }

    client->tryToConnect(/*ignoreMaxCon*/ true);
    return true;
}

void Friend::updateFriendConnectionState(FriendConnectReport report)
{
    if (m_connectState == FriendConnectState::None
        || (linkedClient(true) == nullptr && report != FriendConnectReport::Deleted))
    {
        return;   // no attempt of ours is running
    }

    switch (report) {
    case FriendConnectReport::Established:
    case FriendConnectReport::UserHashVerified:
        // The credits test is MFC's (srchybrid/BaseClient.cpp HasPassedSecureIdent returns
        // false without them): a peer that has shown us nothing cannot have passed. Our
        // own helper is more permissive — it lets passIfUnavailable stand in for a missing
        // credits object — which here would declare the friend authenticated before we had
        // so much as spoken to it.
        if (linkedClient()->credits() && linkedClient()->hasPassedSecureIdent(true)) {
            m_connectState = FriendConnectState::None;
            finishConnecting(true);
            findKadID();   // last: a routing-table hit answers synchronously
        } else {
            reportProgress(ChatConnectProgress::Authenticating);
            m_connectState = FriendConnectState::Auth;
        }
        break;

    case FriendConnectReport::Disconnected:
        // The stored address failed. If we know the friend's Kad ID, it may simply have
        // moved — ask Kad where it is now, at most once every ten minutes.
        if (m_connectState == FriendConnectState::Connecting && hasKadID()) {
            auto* kad = kad::Kademlia::instance();
            const uint32 now = static_cast<uint32>(getTickCount());
            if (kad && kad->isRunning() && kad->isConnected()
                && (m_lastKadSearch == 0 || now >= m_lastKadSearch + MIN2MS(10)))
            {
                m_connectState = FriendConnectState::KadSearching;
                m_lastKadSearch = now;
                reportProgress(ChatConnectProgress::SearchingKad);
                kad->findIPByNodeID(*this, m_kadID.data());
                break;
            }
        }
        m_connectState = FriendConnectState::None;
        finishConnecting(false);
        break;

    case FriendConnectReport::UserHashFailed: {
        // Somebody else answers at that address. Drop this client object — we want nothing
        // to do with it — and start a fresh one carrying the hash we are looking for.
        UpDownClient* old = m_linkedClient;
        QString pending;
        if (old) {
            pending = old->takePendingChatMessage();
            old->setChatState(ChatState::None);
        }
        setLinkedClient(nullptr);
        if (UpDownClient* fresh = clientForChatSession()) {
            fresh->setChatState(ChatState::Connecting);
            if (!pending.isEmpty())
                fresh->setPendingChatMessage(pending);
        }
        m_connectState = FriendConnectState::None;
        finishConnecting(false);
        break;
    }

    case FriendConnectReport::SecureIdentFailed:
        // It has the right hash but cannot prove it. Searching Kad for another address
        // would only find the same impostor.
        m_connectState = FriendConnectState::None;
        finishConnecting(false);
        break;

    case FriendConnectReport::Deleted:
        m_connectState = FriendConnectState::None;
        finishConnecting(false, /*touchClient*/ false);
        break;
    }
}

void Friend::endChatSession()
{
    // Cancel first: an attempt left running lands in finishConnecting(true), which puts
    // the client straight back into Chatting and sends the text the user abandoned.
    if (isTryingToConnect()) {
        if (m_connectState == FriendConnectState::KadSearching) {
            if (auto* kad = kad::Kademlia::instance())
                kad->cancelClientSearch(*this);
        }
        // Deleted is the one report that unwinds without touching the client's chat
        // state — which is ours to clear, below.
        updateFriendConnectionState(FriendConnectReport::Deleted);
    }

    if (UpDownClient* client = linkedClient(true))
        client->endChatSession();
}

void Friend::findKadID()
{
    auto* kad = kad::Kademlia::instance();
    UpDownClient* client = linkedClient(true);
    if (hasKadID() || !kad || !kad->isRunning() || !client)
        return;
    if (client->kadPort() == 0 || client->kadVersion() < KADEMLIA_VERSION2_47a)
        return;

    logDebug(QStringLiteral("Searching Kad ID for friend %1 by address %2")
                 .arg(m_name.isEmpty() ? QStringLiteral("(unknown)") : m_name)
                 .arg(ipstr(client->connectAddress())));
    kad->findNodeIDByIP(*this, client->connectAddress().toUint32(),
                        client->userPort(), client->kadPort());
}

bool Friend::sendOrQueueChatMessage(const QString& message)
{
    UpDownClient* client = clientForChatSession();
    if (!client)
        return false;

    if (client->socket() && client->socket()->isConnected()) {
        client->sendChatMessage(message);
        return true;
    }

    // Park it and dial: the text is sent from onHandshakeCompleted() once the session is
    // up. It lives on the client, so a client swapped out under UserHashFailed carries it.
    client->setPendingChatMessage(message);
    return tryToConnect();
}

void Friend::kadSearchNodeIDByIPResult(kad::KadClientSearchResult status, const uchar* nodeID)
{
    if (theApp.friendList && !theApp.friendList->isValid(this))
        return;

    if (status == kad::KadClientSearchResult::Succeeded && nodeID) {
        logDebug(QStringLiteral("Fetched Kad ID for friend %1")
                     .arg(m_name.isEmpty() ? QStringLiteral("(unknown)") : m_name));
        md4cpy(m_kadID.data(), nodeID);
    }
}

void Friend::kadSearchIPByNodeIDResult(kad::KadClientSearchResult status, uint32 ip, uint16 port)
{
    if (theApp.friendList && !theApp.friendList->isValid(this))
        return;
    if (m_connectState != FriendConnectState::KadSearching)
        return;

    UpDownClient* client = linkedClient(true);
    if (status == kad::KadClientSearchResult::Succeeded && client) {
        const Address found = Address::fromHostOrder(ip);   // Kad hands out host order
        // The same address that just failed is no news.
        if (client->connectAddress() != found || client->userPort() != port) {
            logDebug(QStringLiteral("Kad found friend %1 at %2:%3")
                         .arg(m_name.isEmpty() ? QStringLiteral("(unknown)") : m_name)
                         .arg(ipstr(found)).arg(port));
            reportProgress(ChatConnectProgress::FoundInKad);

            m_lastUsedAddress = found;
            m_lastUsedPort = port;
            client->setConnectAddress(found);
            client->setUserAddress(found);
            client->setUserPort(port);

            m_connectState = FriendConnectState::Connecting;
            client->setChatState(ChatState::Connecting);
            reportProgress(ChatConnectProgress::Connecting);
            client->tryToConnect(/*ignoreMaxCon*/ true);
            return;
        }
    }

    m_connectState = FriendConnectState::None;
    finishConnecting(false);
}

// ---------------------------------------------------------------------------
// Friend slot
// ---------------------------------------------------------------------------

bool Friend::friendSlot() const
{
    return m_linkedClient ? m_linkedClient->friendSlot() : m_friendSlot;
}

void Friend::setFriendSlot(bool val)
{
    if (m_linkedClient)
        m_linkedClient->setFriendSlot(val);

    m_friendSlot = val;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

void Friend::loadFromFile(FileDataIO& file)
{
    file.readHash16(m_userHash.data());
    m_lastUsedAddress = Address::fromNetworkOrder(file.readUInt32());
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
    file.writeUInt32(m_lastUsedAddress.toNetworkUint32());
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

// ===========================================================================
// Private
// ===========================================================================

void Friend::reportProgress(ChatConnectProgress step) const
{
    if (theApp.friendList && theApp.friendList->isValid(this))
        theApp.friendList->emitConnectionProgress(const_cast<Friend*>(this), step);
}

void Friend::finishConnecting(bool success, bool touchClient)
{
    m_connectState = FriendConnectState::None;

    if (touchClient) {
        if (UpDownClient* client = linkedClient(true)) {
            if (success) {
                client->setChatState(ChatState::Chatting);
                // What the caller was trying to say in the first place.
                client->sendPendingChatMessage();
            } else if (client->chatState() == ChatState::Connecting) {
                client->setChatState(ChatState::UnableToConnect);
            }
        }
    }

    reportProgress(success ? ChatConnectProgress::Connected : ChatConnectProgress::Failed);

    if (theApp.friendList && theApp.friendList->isValid(this))
        theApp.friendList->emitConnectingResult(this, success);
}

} // namespace eMule
