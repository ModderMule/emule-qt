#pragma once

/// @file SourceIndex.h
/// @brief Finds the sources of one download that could be the same peer as a candidate.
///
/// A candidate finder only: it hands back every source sharing an identity key with the
/// probe, and the caller decides with UpDownClient::compare(). It may return too many,
/// never too few — as long as rekey() is called when a source's identity changes.

#include "utils/Types.h"

#include <array>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace eMule {

class UpDownClient;

class SourceIndex {
public:
    void add(UpDownClient* client);
    void remove(const UpDownClient* client);
    /// Re-file @p client under its current identity. No-op if it is not in here.
    void rekey(UpDownClient* client);
    void clear();

    [[nodiscard]] bool contains(const UpDownClient* client) const { return m_keysOf.contains(client); }
    [[nodiscard]] std::size_t size() const { return m_keysOf.size(); }

    /// Every source sharing a key with @p probe, each once. @p probe itself included
    /// if it is in here.
    void candidates(const UpDownClient* probe, std::vector<UpDownClient*>& out) const;

private:
    // One per way two clients can be the same peer: user hash; address + TCP / Kad port;
    // hybrid ID + TCP / Kad port; hybrid ID + server; IPv6 + TCP port.
    static constexpr std::size_t kMaxKeys = 7;
    struct Keys {
        std::array<uint64, kMaxKeys> key{};
        uint8 count = 0;
    };
    [[nodiscard]] static Keys keysOf(const UpDownClient& client);

    void file(UpDownClient* client, const Keys& keys);
    void unfile(const UpDownClient* client, const Keys& keys);

    std::unordered_multimap<uint64, UpDownClient*> m_byKey;
    std::unordered_map<const UpDownClient*, Keys> m_keysOf;   // as filed, not as the client is now
};

} // namespace eMule
