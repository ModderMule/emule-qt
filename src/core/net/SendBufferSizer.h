#pragma once

/// @file SendBufferSizer.h
/// @brief Grow a socket's SO_SNDBUF with its upload rate.
///
/// Used per upload slot (EMSocket) and for the one ed3k UDP socket (Ed3kSession).
/// Grows only; stops asking once the system gives less, or autotunes it already.

#include "utils/Types.h"

#include <QtGlobal>

#include <algorithm>

namespace eMule {

class SendBufferSizer {
public:
    /// Floor = one step. 1 MiB is the port's own value (MFC 128 KiB).
    static constexpr qint64 kStep = 1024 * 1024;
    static constexpr qint64 kSeconds = 2;
    /// = the read-ahead cap (UpDownClient::kMaxUploadBufferBytes)
    static constexpr qint64 kMax = 8 * 1024 * 1024;

    /// kSeconds of @p bytesPerSec in whole kStep steps, kStep..kMax. Whole steps keep
    /// callers from a getsockopt per pass.
    [[nodiscard]] static constexpr qint64 wantFor(uint64 bytesPerSec)
    {
        const auto rate = static_cast<qint64>(std::min<uint64>(bytesPerSec, kMax));
        return std::clamp((rate * kSeconds + kStep - 1) / kStep * kStep, kStep, kMax);
    }

    /// Worth asking for @p want: above every earlier ask, and not done.
    [[nodiscard]] bool needs(qint64 want) const { return !m_done && want > m_asked; }
    /// The system gave less than asked, or autotunes the buffer: no more asks.
    [[nodiscard]] bool done() const { return m_done; }

    /// Record an ask made by other means (a transport): @p got is what it gave.
    void noteResult(qint64 want, qint64 got);

    /// Raise SO_SNDBUF of @p fd to @p want. Returns the size the system reports now
    /// (Linux: twice what was set), 0 when unknown. Call only when needs(want).
    qint64 grow(qintptr fd, qint64 want);

private:
    qint64 m_asked = 0;
    bool m_done = false;
    /// SO_SNDBUF set by us (kernel autotuning is off for it)
    bool m_fixed = false;
};

} // namespace eMule
