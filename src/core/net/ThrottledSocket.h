#pragma once

/// @file ThrottledSocket.h
/// @brief Bandwidth throttling interfaces — replaces MFC ThrottledSocket.h.
///
/// Pure abstract interfaces used by the upload bandwidth throttler to control
/// how much data each socket may send per time slice.

#include "utils/Types.h"

namespace eMule {

/// Result of a throttled send operation.
struct SocketSentBytes {
    uint32 sentBytesStandardPackets = 0;
    uint32 sentBytesControlPackets = 0;
    bool success = false;
};

/// Abstract interface for sockets that send control-priority data.
class ThrottledControlSocket {
public:
    virtual SocketSentBytes sendControlData(uint32 maxNumberOfBytesToSend, uint32 minFragSize) = 0;

protected:
    virtual ~ThrottledControlSocket() = default;
};

/// Abstract interface for sockets that send both file data and control data.
class ThrottledFileSocket : public ThrottledControlSocket {
public:
    virtual SocketSentBytes sendFileAndControlData(uint32 maxNumberOfBytesToSend, uint32 minFragSize) = 0;
    [[nodiscard]] virtual uint32 getLastCalledSend() const = 0;
    [[nodiscard]] virtual uint32 getNeededBytes() = 0;
    [[nodiscard]] virtual bool isBusyExtensiveCheck() = 0;
    [[nodiscard]] virtual bool isBusyQuickCheck() const = 0;
    [[nodiscard]] virtual bool isEnoughFileDataQueued(uint32 nMinFilePayloadBytes) const = 0;
    [[nodiscard]] virtual bool hasQueues(bool onlyStandardPackets = false) const = 0;
    virtual bool useBigSendBuffer() { return false; }
};

} // namespace eMule
