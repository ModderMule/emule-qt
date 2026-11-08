#pragma once

/// @file HosterWebBackend.h
/// @brief What the web page may ask of the hoster downloads.
///
/// Core never links the ed3k module, so the Hoster page talks to this interface
/// and DaemonApp installs the implementation — the TorrentWebBackend seam. Rows are
/// GetHosterItems maps. No REST routes use it.

#include <QCborArray>
#include <QString>

namespace eMule {

class HosterWebBackend {
public:
    virtual ~HosterWebBackend() = default;

    /// False without a hoster join, or while ed3k is off.
    [[nodiscard]] virtual bool available() const = 0;
    /// The GetHosterItems rows.
    [[nodiscard]] virtual QCborArray items() const = 0;
    /// Links one per line; @p package non-empty: they form a new package. Returns the count added.
    virtual int addLinks(const QString& text, const QString& package) = 0;
    /// "pause", "resume", "retry", "remove" or "delete" (HosterAction).
    virtual bool action(quint32 id, const QString& op) = 0;
};

} // namespace eMule
