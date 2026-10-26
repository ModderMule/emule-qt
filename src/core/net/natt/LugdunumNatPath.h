#pragma once
/// @file LugdunumNatPath.h
/// @brief Lugdunum eserver 17.x NAT callback (login bit 0x1000, 24-byte OP_IDCHANGE).
///
/// Server UDP 0xA6 <target id 4><our id 4>, obfuscated with the server UDP key, to the
/// server's UDP obfuscation port, from the client UDP socket (the server takes our mapped
/// port from it). The target gets UDP 0xA7 or TCP 0x37:
/// <ip 4><udp port 2><requester id 4><crypt flags 1>[<hash 16> if flags & 0x80].
/// The requester hears nothing back, so the target sends its own 0xA6 naming the
/// requester (mutual callback); the requester then gets a 0xA7 with the target.

#include "net/natt/NatPath.h"

#include <optional>

namespace eMule::natt {

/// A parsed 0x37 / 0xA7 body.
struct CallbackNotice {
    Endpoint udp;       ///< requester's mapped UDP endpoint
    PeerInfo peer;      ///< requester: ID, crypt bits, hash when sent, our server
    uint8 flags = 0;
};

class LugdunumNatPath : public NatPath {
public:
    static constexpr uint64 kRepeatMs     = 5000;   // server keeps the UDP window 19 s
    static constexpr uint32 kMaxRequests  = 9;
    static constexpr uint32 kKeyRefreshAfter = 3;   // unanswered requests before a fresh key
    static constexpr uint32 kNoticeSize   = 11;
    static constexpr uint8  kFlagHasHash  = 0x80;   // eserver: hash follows; read by length

    using NatPath::NatPath;

    [[nodiscard]] PathKind kind() const override { return PathKind::Lugdunum; }
    [[nodiscard]] bool canServe(const PeerInfo& peer) const override;
    bool request(Attempt& attempt) override;
    void repeat(Attempt& attempt) override;
    bool handleServerTcp(uint8 opcode, const QByteArray& payload) override;
    bool handleServerUdp(const Endpoint& from, const uint8* data, qsizetype len) override;

    /// Shared with the ed2k-server extension, whose 0x37 is byte-identical.
    [[nodiscard]] static std::optional<CallbackNotice> parseNotice(const NatEnv& env, const uint8* data,
                                                                   uint32 size);

protected:
    /// Where an 0xA6 goes and what it carries.
    struct RequestRoute {
        Endpoint to;           ///< the server's UDP obfuscation port
        uint32 key = 0;        ///< its UDP key
        uint32 ourId = 0;      ///< our ID there, 0 = not logged in there (foreign callback)
    };

    /// We can send an 0xA6 at all (Lugdunum: logged in, we name our own ID).
    [[nodiscard]] virtual bool canRequest() const { return ourID() != 0; }
    /// Route for @p attempt; nullopt while the key is missing (and a fresh one is asked for).
    [[nodiscard]] virtual std::optional<RequestRoute> requestRoute(const Attempt& attempt);
    /// Unanswered for a while: the server may have dropped our key.
    virtual void refreshKey(const Attempt& attempt);
    /// UDP key to read a notice from @p from with; nullopt when not a server of ours.
    [[nodiscard]] virtual std::optional<uint32> noticeKey(const Endpoint& from) const;
    /// A plaintext 0xA7 is read too (eserver may send one).
    [[nodiscard]] virtual bool plainNotices() const { return true; }
    /// A 0xA7 / 0x37 body from @p from (null for TCP).
    virtual void onNotice(const Endpoint& from, const uint8* data, uint32 size);

    bool sendRequest(Attempt& attempt);
    /// The 0xA7 answer to our request: the peer's endpoint.
    void onAnswer(Attempt& mine, const CallbackNotice& n);
};

} // namespace eMule::natt
