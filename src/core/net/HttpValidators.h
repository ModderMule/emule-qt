#pragma once

/// @file HttpValidators.h
/// @brief ETag / Last-Modified of a downloaded file, for conditional re-downloads.

#include <QByteArray>

namespace eMule {

/// What identifies the server's copy of a file: sent back as If-None-Match /
/// If-Modified-Since, so an unchanged file costs a 304 instead of a download.
struct HttpValidators {
    QByteArray etag;          ///< raw ETag header value, quotes included
    QByteArray lastModified;  ///< raw Last-Modified header value (HTTP date)

    [[nodiscard]] bool isEmpty() const { return etag.isEmpty() && lastModified.isEmpty(); }
    bool operator==(const HttpValidators&) const = default;
};

} // namespace eMule
