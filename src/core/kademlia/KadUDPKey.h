#pragma once

/// @file KadUDPKey.h
/// @brief UDP verification key for Kademlia (ported from kademlia/utils/KadUDPKey.h).

#include "utils/Types.h"
#include "utils/SafeFile.h"

#include <cstdint>

namespace eMule::kad {

/// UDP key used to verify Kademlia packets, binding a key value to an IP address.
class KadUDPKey {
public:
    explicit KadUDPKey(uint32 zero = 0) noexcept
        : m_key(zero), m_ip(0) { Q_ASSERT(zero == 0); }

    KadUDPKey(uint32 key, uint32 ip) noexcept
        : m_key(key), m_ip(ip) {}

    explicit KadUDPKey(FileDataIO& file) { readFromFile(file); }

    [[nodiscard]] uint32 getKeyValue(uint32 myIP) const noexcept
    {
        return (myIP == m_ip) ? m_key : 0;
    }

    [[nodiscard]] bool isEmpty() const noexcept { return m_key == 0 || m_ip == 0; }

    void storeToFile(FileDataIO& file) const
    {
        file.writeUInt32(m_key);
        file.writeUInt32(m_ip);
    }

    void readFromFile(FileDataIO& file)
    {
        m_key = file.readUInt32();
        m_ip  = file.readUInt32();
    }

    friend bool operator==(const KadUDPKey& a, const KadUDPKey& b) noexcept
    {
        return a.getKeyValue(a.m_ip) == b.getKeyValue(b.m_ip);
    }

private:
    uint32 m_key;
    uint32 m_ip;
};

} // namespace eMule::kad
