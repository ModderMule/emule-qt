#pragma once
/// @file ForeignCallbackPath.h
/// @brief Foreign callback: ask a LowID source's own server, where we are not logged in.
///
/// Agreed in andrey23127/ed2k-server#40. The source's server and LowID come from source
/// exchange, server answers or saved sources. When that server announces UDP flag
/// 0x100000 we send it 0xA6 <target id 4><0 4><our crypt 1>, obfuscated with the UDP key
/// it gave our IP in a status ping (the key proves our address), to its obfuscation port.
/// It wakes the target with 0x37 (requester ID 0) and, once the target's 0x9F gives it
/// the target's port (or after 3 s, with the last port it knows), sends us
/// 0xA7 <ip 4><udp port 2><target id 4><crypt 1><hash 16>, obfuscated with the same key.
/// 0.9.86 always sends the hash (0x80 = use the crypt layer); parseNotice reads it by length. One server, no sweep: no flag = this path does not serve the peer.
///
/// A server outside our list is used only with pref natDialUnlistedServers (default on),
/// after one obfuscated status ping confirmed it (UnlistedServers).

#include "net/natt/LugdunumNatPath.h"

namespace eMule::natt {

class ForeignCallbackPath final : public LugdunumNatPath {
public:
    static constexpr uint32 kMaxForeignRequests = 3;   // 5 s apart, inside the server's 19 s

    using LugdunumNatPath::LugdunumNatPath;

    [[nodiscard]] PathKind kind() const override { return PathKind::ForeignCallback; }
    [[nodiscard]] bool canServe(const PeerInfo& peer) const override;
    void repeat(Attempt& attempt) override;
    bool handleServerTcp(uint8 /*opcode*/, const QByteArray& /*payload*/) override { return false; }

protected:
    [[nodiscard]] bool canRequest() const override { return true; }
    [[nodiscard]] std::optional<RequestRoute> requestRoute(const Attempt& attempt) override;
    void refreshKey(const Attempt& attempt) override;
    [[nodiscard]] std::optional<uint32> noticeKey(const Endpoint& from) const override;
    /// Obfuscated only: the key ties the answer to the server we asked.
    [[nodiscard]] bool plainNotices() const override { return false; }
    void onNotice(const Endpoint& from, const uint8* data, uint32 size) override;

private:
    /// The peer's server: IPv4, not ours, a LowID there.
    [[nodiscard]] bool foreignPeer(const PeerInfo& peer) const;
};

} // namespace eMule::natt
