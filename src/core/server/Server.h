#pragma once

/// @file Server.h
/// @brief ED2K server data entity — modern C++23 replacement for MFC CServer.
///
/// Pure data entity with no GUI or theApp coupling. Replaces CServer from
/// the original Server.h with scoped enums, QString, and on-demand IP
/// formatting via eMule::ipstr().

#include "utils/SafeFile.h"
#include "utils/Types.h"

#include <QString>

namespace eMule {

// ---------------------------------------------------------------------------
// Server priority enum (replaces SRV_PR_* defines)
// ---------------------------------------------------------------------------

enum class ServerPriority : uint32 {
    Normal = 0,
    High   = 1,
    Low    = 2
};

// ---------------------------------------------------------------------------
// Server TCP/UDP flag constants (replaces SRV_TCPFLG_* / SRV_UDPFLG_*)
// ---------------------------------------------------------------------------

namespace SrvTcpFlag {
    inline constexpr uint32 Compression     = 0x00000001;
    inline constexpr uint32 NewTags         = 0x00000008;
    inline constexpr uint32 Unicode         = 0x00000010;
    inline constexpr uint32 RelatedSearch   = 0x00000040;
    inline constexpr uint32 TypeTagInteger  = 0x00000080;
    inline constexpr uint32 LargeFiles      = 0x00000100;
    inline constexpr uint32 TcpObfuscation  = 0x00000400;
}

namespace SrvUdpFlag {
    inline constexpr uint32 ExtGetSources   = 0x00000001;
    inline constexpr uint32 ExtGetFiles     = 0x00000002;
    inline constexpr uint32 NewTags         = 0x00000008;
    inline constexpr uint32 Unicode         = 0x00000010;
    inline constexpr uint32 ExtGetSources2  = 0x00000020;
    inline constexpr uint32 LargeFiles      = 0x00000100;
    inline constexpr uint32 UdpObfuscation  = 0x00000200;
    inline constexpr uint32 TcpObfuscation  = 0x00000400;
}

// ---------------------------------------------------------------------------
// Server — ED2K server data entity
// ---------------------------------------------------------------------------

class Tag;

class Server {
public:
    /// Construct from IP (network byte order) and port.
    Server(uint32 ip, uint16 port);

    /// Deserialize from a server.met stream (reads ip, port, tagCount, tags).
    explicit Server(FileDataIO& data, bool optUTF8 = true);

    /// Copy constructor.
    Server(const Server& other);

    Server& operator=(const Server&) = delete;
    ~Server() = default;

    // -- Network ----------------------------------------------------------

    [[nodiscard]] uint32  ip() const        { return m_ip; }
    void setIP(uint32 ip)                   { m_ip = ip; }

    [[nodiscard]] uint16  port() const      { return m_port; }
    void setPort(uint16 port)               { m_port = port; }

    [[nodiscard]] const QString& dynIP() const  { return m_dynIP; }
    [[nodiscard]] bool hasDynIP() const         { return !m_dynIP.isEmpty(); }
    void setDynIP(const QString& dynIP)         { m_dynIP = dynIP; }

    /// Returns dynIP if set, otherwise formatted numeric IP.
    [[nodiscard]] QString address() const;

    // -- Metadata ---------------------------------------------------------

    [[nodiscard]] const QString& name() const           { return m_name; }
    void setName(const QString& name)                   { m_name = name; }

    [[nodiscard]] const QString& description() const    { return m_description; }
    void setDescription(const QString& desc)            { m_description = desc; }

    [[nodiscard]] const QString& version() const        { return m_version; }
    void setVersion(const QString& ver)                 { m_version = ver; }

    [[nodiscard]] ServerPriority preference() const     { return m_preference; }
    void setPreference(ServerPriority pref)             { m_preference = pref; }

    [[nodiscard]] bool isStaticMember() const           { return m_staticMember; }
    void setStaticMember(bool s)                        { m_staticMember = s; }

    // -- Stats ------------------------------------------------------------

    [[nodiscard]] uint32 files() const          { return m_files; }
    void setFiles(uint32 f)                     { m_files = f; }

    [[nodiscard]] uint32 users() const          { return m_users; }
    void setUsers(uint32 u)                     { m_users = u; }

    [[nodiscard]] uint32 maxUsers() const       { return m_maxUsers; }
    void setMaxUsers(uint32 m)                  { m_maxUsers = m; }

    [[nodiscard]] uint32 softFiles() const      { return m_softFiles; }
    void setSoftFiles(uint32 sf)                { m_softFiles = sf; }

    [[nodiscard]] uint32 hardFiles() const      { return m_hardFiles; }
    void setHardFiles(uint32 hf)                { m_hardFiles = hf; }

    [[nodiscard]] uint32 lowIDUsers() const     { return m_lowIDUsers; }
    void setLowIDUsers(uint32 lo)               { m_lowIDUsers = lo; }

    [[nodiscard]] uint32 ping() const           { return m_ping; }
    void setPing(uint32 p)                      { m_ping = p; }

    [[nodiscard]] uint32 failedCount() const    { return m_failedCount; }
    void setFailedCount(uint32 c)               { m_failedCount = c; }
    void incFailedCount()                       { ++m_failedCount; }
    void resetFailedCount()                     { m_failedCount = 0; }

    // -- Timing -----------------------------------------------------------

    [[nodiscard]] uint32 lastPingedTime() const         { return m_lastPingedTime; }
    void setLastPingedTime(uint32 t)                    { m_lastPingedTime = t; }

    [[nodiscard]] uint32 realLastPingedTime() const     { return m_realLastPingedTime; }
    void setRealLastPingedTime(uint32 t)                { m_realLastPingedTime = t; }

    [[nodiscard]] uint32 lastPinged() const             { return m_lastPinged; }
    void setLastPinged(uint32 lp)                       { m_lastPinged = lp; }

    [[nodiscard]] uint32 lastDescPingedCount() const    { return m_lastDescPingedCount; }
    void setLastDescPingedCount(bool reset);

    [[nodiscard]] uint32 challenge() const              { return m_challenge; }
    void setChallenge(uint32 c)                         { m_challenge = c; }

    [[nodiscard]] uint32 descReqChallenge() const       { return m_descReqChallenge; }
    void setDescReqChallenge(uint32 c)                  { m_descReqChallenge = c; }

    // -- Flags ------------------------------------------------------------

    [[nodiscard]] uint32 tcpFlags() const       { return m_tcpFlags; }
    void setTCPFlags(uint32 f)                  { m_tcpFlags = f; }

    [[nodiscard]] uint32 udpFlags() const       { return m_udpFlags; }
    void setUDPFlags(uint32 f)                  { m_udpFlags = f; }

    // -- Crypto -----------------------------------------------------------

    [[nodiscard]] uint16 obfuscationPortTCP() const     { return m_obfuscationPortTCP; }
    void setObfuscationPortTCP(uint16 p)                { m_obfuscationPortTCP = p; }

    [[nodiscard]] uint16 obfuscationPortUDP() const     { return m_obfuscationPortUDP; }
    void setObfuscationPortUDP(uint16 p)                { m_obfuscationPortUDP = p; }

    [[nodiscard]] uint32 serverKeyUDP() const           { return m_serverKeyUDP; }
    void setServerKeyUDP(uint32 key)                    { m_serverKeyUDP = key; }

    [[nodiscard]] uint32 serverKeyUDPIP() const         { return m_serverKeyUDPIP; }
    void setServerKeyUDPIP(uint32 ip)                   { m_serverKeyUDPIP = ip; }

    [[nodiscard]] bool cryptPingReplyPending() const    { return m_cryptPingReplyPending; }
    void setCryptPingReplyPending(bool v)               { m_cryptPingReplyPending = v; }

    [[nodiscard]] bool triedCrypt() const               { return m_triedCryptOnce; }
    void setTriedCrypt(bool v)                          { m_triedCryptOnce = v; }

    // -- Aux --------------------------------------------------------------

    [[nodiscard]] const QString& auxPortsList() const   { return m_auxPortsList; }
    void setAuxPortsList(const QString& ap)             { m_auxPortsList = ap; }

    // -- Capability flag queries ------------------------------------------

    [[nodiscard]] bool supportsZlib() const             { return (m_tcpFlags & SrvTcpFlag::Compression) != 0; }
    [[nodiscard]] bool supportsNewTags() const          { return (m_tcpFlags & SrvTcpFlag::NewTags) != 0; }
    [[nodiscard]] bool supportsUnicode() const          { return (m_tcpFlags & SrvTcpFlag::Unicode) != 0; }
    [[nodiscard]] bool supportsRelatedSearch() const    { return (m_tcpFlags & SrvTcpFlag::RelatedSearch) != 0; }
    [[nodiscard]] bool supportsLargeFilesTCP() const    { return (m_tcpFlags & SrvTcpFlag::LargeFiles) != 0; }
    [[nodiscard]] bool supportsLargeFilesUDP() const    { return (m_udpFlags & SrvUdpFlag::LargeFiles) != 0; }
    [[nodiscard]] bool supportsObfuscationUDP() const   { return (m_udpFlags & SrvUdpFlag::UdpObfuscation) != 0; }
    [[nodiscard]] bool supportsGetSourcesObfuscation() const { return (m_tcpFlags & SrvTcpFlag::TcpObfuscation) != 0; }
    [[nodiscard]] bool supportsObfuscationTCP() const   { return m_obfuscationPortTCP != 0 && (supportsObfuscationUDP() || supportsGetSourcesObfuscation()); }

    /// Check if we hold a valid UDP key for the given client IP.
    [[nodiscard]] bool hasValidUDPKey(uint32 clientIP) const { return m_serverKeyUDP != 0 && m_serverKeyUDPIP == clientIP; }

    // -- Serialization ----------------------------------------------------

    /// Apply a deserialized tag to the appropriate server property.
    void addTagFromFile(const Tag& tag);

    /// Write all non-default server properties as tags to the file.
    /// Returns the number of tags written.
    uint32 writeTags(FileDataIO& file) const;

private:
    // Network
    uint32  m_ip = 0;
    uint16  m_port = 0;
    QString m_dynIP;

    // Metadata
    QString m_name;
    QString m_description;
    QString m_version;
    ServerPriority m_preference = ServerPriority::Normal;
    bool    m_staticMember = false;

    // Stats
    uint32  m_files = 0;
    uint32  m_users = 0;
    uint32  m_maxUsers = 0;
    uint32  m_softFiles = 0;
    uint32  m_hardFiles = 0;
    uint32  m_lowIDUsers = 0;
    uint32  m_ping = 0;
    uint32  m_failedCount = 0;

    // Timing
    uint32  m_lastPingedTime = 0;
    uint32  m_realLastPingedTime = 0;
    uint32  m_lastPinged = 0;
    uint32  m_lastDescPingedCount = 0;
    uint32  m_challenge = 0;
    uint32  m_descReqChallenge = 0;

    // Flags
    uint32  m_tcpFlags = 0;
    uint32  m_udpFlags = 0;

    // Crypto
    uint16  m_obfuscationPortTCP = 0;
    uint16  m_obfuscationPortUDP = 0;
    uint32  m_serverKeyUDP = 0;
    uint32  m_serverKeyUDPIP = 0;
    bool    m_cryptPingReplyPending = false;
    bool    m_triedCryptOnce = false;

    // Aux
    QString m_auxPortsList;
};

} // namespace eMule
