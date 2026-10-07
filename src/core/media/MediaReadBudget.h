#pragma once

/// @file MediaReadBudget.h
/// @brief A cap on how much of one file the metadata parsers may read.
///
/// The parsers each bound their own buffers, but nothing bounded their sum, and
/// a library of a hundred thousand files multiplies whatever one file costs.
/// While a budget is alive on a thread, every parser read on that thread is
/// charged to it and comes back short once it is spent; the parsers already
/// treat a short read as "not this format". Without one, reads are unlimited.

#include <QtTypes>

namespace eMule {

class MediaReadBudget {
public:
    explicit MediaReadBudget(qint64 bytes);
    ~MediaReadBudget();

    MediaReadBudget(const MediaReadBudget&) = delete;
    MediaReadBudget& operator=(const MediaReadBudget&) = delete;

    /// How much of a read of @p wanted bytes is allowed; charges what it returns.
    [[nodiscard]] static qint64 take(qint64 wanted);

    [[nodiscard]] qint64 used() const { return m_used; }

private:
    qint64 m_left;
    qint64 m_used = 0;
    MediaReadBudget* m_outer;
};

} // namespace eMule
