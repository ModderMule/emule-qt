#include "pch.h"
/// @file Server.cpp
/// @brief ED2K server data entity implementation — port of CServer from MFC.

#include "Server.h"
#include "protocol/Tag.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"

namespace eMule {

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

Server::Server(uint32 ip, uint16 port)
    : m_ip(ip)
    , m_port(port)
{
}

Server::Server(FileDataIO& data, bool optUTF8)
{
    m_ip   = data.readUInt32();
    m_port = data.readUInt16();

    const uint32 tagCount = data.readUInt32();
    for (uint32 i = 0; i < tagCount; ++i) {
        Tag tag(data, optUTF8);
        addTagFromFile(tag);
    }
}

Server::Server(const Server& other)
    : m_ip(other.m_ip)
    , m_port(other.m_port)
    , m_dynIP(other.m_dynIP)
    , m_name(other.m_name)
    , m_description(other.m_description)
    , m_version(other.m_version)
    , m_preference(other.m_preference)
    , m_staticMember(other.m_staticMember)
    , m_files(other.m_files)
    , m_users(other.m_users)
    , m_maxUsers(other.m_maxUsers)
    , m_softFiles(other.m_softFiles)
    , m_hardFiles(other.m_hardFiles)
    , m_lowIDUsers(other.m_lowIDUsers)
    , m_ping(other.m_ping)
    , m_failedCount(other.m_failedCount)
    , m_lastPingedTime(other.m_lastPingedTime)
    , m_realLastPingedTime(other.m_realLastPingedTime)
    , m_lastPinged(other.m_lastPinged)
    , m_lastDescPingedCount(other.m_lastDescPingedCount)
    , m_challenge(other.m_challenge)
    , m_descReqChallenge(other.m_descReqChallenge)
    , m_tcpFlags(other.m_tcpFlags)
    , m_udpFlags(other.m_udpFlags)
    , m_obfuscationPortTCP(other.m_obfuscationPortTCP)
    , m_obfuscationPortUDP(other.m_obfuscationPortUDP)
    , m_serverKeyUDP(other.m_serverKeyUDP)
    , m_serverKeyUDPIP(other.m_serverKeyUDPIP)
    , m_cryptPingReplyPending(other.m_cryptPingReplyPending)
    , m_triedCryptOnce(other.m_triedCryptOnce)
    , m_auxPortsList(other.m_auxPortsList)
{
}

// ---------------------------------------------------------------------------
// address()
// ---------------------------------------------------------------------------

QString Server::address() const
{
    return m_dynIP.isEmpty() ? ipstr(m_ip) : m_dynIP;
}

// ---------------------------------------------------------------------------
// setLastDescPingedCount()
// ---------------------------------------------------------------------------

void Server::setLastDescPingedCount(bool reset)
{
    if (reset)
        m_lastDescPingedCount = 0;
    else
        ++m_lastDescPingedCount;
}

// ---------------------------------------------------------------------------
// addTagFromFile() — apply a deserialized tag to server properties
// ---------------------------------------------------------------------------

void Server::addTagFromFile(const Tag& tag)
{
    switch (tag.nameId()) {
    case ST_SERVERNAME:
        if (tag.isStr() && m_name.isEmpty())
            m_name = tag.strValue();
        break;
    case ST_DESCRIPTION:
        if (tag.isStr() && m_description.isEmpty())
            m_description = tag.strValue();
        break;
    case ST_PING:
        if (tag.isInt())
            m_ping = tag.intValue();
        break;
    case ST_FAIL:
        if (tag.isInt())
            m_failedCount = tag.intValue();
        break;
    case ST_PREFERENCE:
        if (tag.isInt())
            m_preference = static_cast<ServerPriority>(tag.intValue());
        break;
    case ST_DYNIP:
        if (tag.isStr() && !tag.strValue().isEmpty() && m_dynIP.isEmpty()) {
            m_dynIP = tag.strValue();
            m_ip = 0;  // reset outdated IP when dynIP is set
        }
        break;
    case ST_PORT:
        // Port override from tags — silently accept
        if (tag.isInt())
            m_port = static_cast<uint16>(tag.intValue());
        break;
    case ST_IP:
        // IP tag — silently accept (read from struct, tag ignored)
        break;
    case ST_MAXUSERS:
        if (tag.isInt())
            m_maxUsers = tag.intValue();
        break;
    case ST_SOFTFILES:
        if (tag.isInt())
            m_softFiles = tag.intValue();
        break;
    case ST_HARDFILES:
        if (tag.isInt())
            m_hardFiles = tag.intValue();
        break;
    case ST_LASTPING:
        if (tag.isInt())
            m_lastPingedTime = tag.intValue();
        break;
    case ST_VERSION:
        if (tag.isStr()) {
            if (m_version.isEmpty())
                m_version = tag.strValue();
        } else if (tag.isInt()) {
            // Integer version: major.minor format
            m_version = QStringLiteral("%1.%2")
                .arg(tag.intValue() >> 16)
                .arg(tag.intValue() & 0xFFFF, 2, 10, QChar(u'0'));
        }
        break;
    case ST_UDPFLAGS:
        if (tag.isInt())
            m_udpFlags = tag.intValue();
        break;
    case ST_AUXPORTSLIST:
        if (tag.isStr())
            m_auxPortsList = tag.strValue();
        break;
    case ST_LOWIDUSERS:
        if (tag.isInt())
            m_lowIDUsers = tag.intValue();
        break;
    case ST_UDPKEY:
        if (tag.isInt())
            m_serverKeyUDP = tag.intValue();
        break;
    case ST_UDPKEYIP:
        if (tag.isInt())
            m_serverKeyUDPIP = tag.intValue();
        break;
    case ST_TCPPORTOBFUSCATION:
        if (tag.isInt())
            m_obfuscationPortTCP = static_cast<uint16>(tag.intValue());
        break;
    case ST_UDPPORTOBFUSCATION:
        if (tag.isInt())
            m_obfuscationPortUDP = static_cast<uint16>(tag.intValue());
        break;
    default:
        // Handle legacy string-named tags: "files" and "users"
        if (tag.nameId() == 0 && tag.name() == QByteArray("files")) {
            if (tag.isInt())
                m_files = tag.intValue();
        } else if (tag.nameId() == 0 && tag.name() == QByteArray("users")) {
            if (tag.isInt())
                m_users = tag.intValue();
        } else {
            logWarning(QStringLiteral("Unknown server.met tag: nameId=0x%1")
                .arg(tag.nameId(), 2, 16, QChar(u'0')));
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// writeTags() — serialize non-default properties as tags
// ---------------------------------------------------------------------------

uint32 Server::writeTags(FileDataIO& file) const
{
    uint32 count = 0;

    if (!m_name.isEmpty()) {
        Tag(ST_SERVERNAME, m_name).writeNewEd2kTag(file, UTF8Mode::OptBOM);
        ++count;
    }

    if (!m_dynIP.isEmpty()) {
        Tag(ST_DYNIP, m_dynIP).writeNewEd2kTag(file, UTF8Mode::OptBOM);
        ++count;
    }

    if (!m_description.isEmpty()) {
        Tag(ST_DESCRIPTION, m_description).writeNewEd2kTag(file, UTF8Mode::OptBOM);
        ++count;
    }

    if (m_failedCount != 0) {
        Tag(ST_FAIL, m_failedCount).writeNewEd2kTag(file);
        ++count;
    }

    if (m_preference != ServerPriority::Normal) {
        Tag(ST_PREFERENCE, static_cast<uint32>(m_preference)).writeNewEd2kTag(file);
        ++count;
    }

    if (m_users != 0) {
        Tag(QByteArray("users"), m_users).writeTagToFile(file);
        ++count;
    }

    if (m_files != 0) {
        Tag(QByteArray("files"), m_files).writeTagToFile(file);
        ++count;
    }

    if (m_ping != 0) {
        Tag(ST_PING, m_ping).writeNewEd2kTag(file);
        ++count;
    }

    if (m_lastPingedTime != 0) {
        Tag(ST_LASTPING, m_lastPingedTime).writeNewEd2kTag(file);
        ++count;
    }

    if (m_maxUsers != 0) {
        Tag(ST_MAXUSERS, m_maxUsers).writeNewEd2kTag(file);
        ++count;
    }

    if (m_softFiles != 0) {
        Tag(ST_SOFTFILES, m_softFiles).writeNewEd2kTag(file);
        ++count;
    }

    if (m_hardFiles != 0) {
        Tag(ST_HARDFILES, m_hardFiles).writeNewEd2kTag(file);
        ++count;
    }

    if (!m_version.isEmpty()) {
        Tag(ST_VERSION, m_version).writeNewEd2kTag(file, UTF8Mode::OptBOM);
        ++count;
    }

    if (m_udpFlags != 0) {
        Tag(ST_UDPFLAGS, m_udpFlags).writeNewEd2kTag(file);
        ++count;
    }

    if (m_lowIDUsers != 0) {
        Tag(ST_LOWIDUSERS, m_lowIDUsers).writeNewEd2kTag(file);
        ++count;
    }

    if (m_serverKeyUDP != 0) {
        Tag(ST_UDPKEY, m_serverKeyUDP).writeNewEd2kTag(file);
        ++count;
    }

    if (m_serverKeyUDPIP != 0) {
        Tag(ST_UDPKEYIP, m_serverKeyUDPIP).writeNewEd2kTag(file);
        ++count;
    }

    if (m_obfuscationPortTCP != 0) {
        Tag(ST_TCPPORTOBFUSCATION, static_cast<uint32>(m_obfuscationPortTCP)).writeNewEd2kTag(file);
        ++count;
    }

    if (m_obfuscationPortUDP != 0) {
        Tag(ST_UDPPORTOBFUSCATION, static_cast<uint32>(m_obfuscationPortUDP)).writeNewEd2kTag(file);
        ++count;
    }

    if (!m_auxPortsList.isEmpty()) {
        Tag(ST_AUXPORTSLIST, m_auxPortsList).writeNewEd2kTag(file, UTF8Mode::OptBOM);
        ++count;
    }

    return count;
}

} // namespace eMule
