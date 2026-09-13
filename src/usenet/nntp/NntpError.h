#pragma once

/// @file NntpError.h
/// @brief Failure taxonomy for the NNTP layer.
///
/// The distinction that matters is between *transport* failures and *content*
/// failures, because they route differently: a connect error or a timeout means
/// retry the same article on the **same** priority level (the server is fine,
/// this connection was not), while "no such article" means escalate to the
/// **next** level with the servers already tried excluded. NZBGet expresses this
/// as adConnectError/adRetry vs adNotFound (`daemon/nntp/ArticleDownloader.h`),
/// and getting it backwards either hammers a dead server or gives up on an
/// article that a fill server would have had.
///
/// A damaged article is a content failure, not a transport one: the body was
/// read to its terminating "." on a connection that is still in sync, and what
/// this server stores is what it will serve again. So it excludes this server
/// and asks the next, exactly like a 430 (NZBGet's RetryOnCrcError=no default).

#include <QMetaType>
#include <QString>

namespace eMule::usenet {

enum class NntpError : quint8 {
    None = 0,

    // -- Transport: the server is presumed fine, this connection is not. ------
    ConnectFailed,   ///< DNS, refused, unreachable.
    TlsFailed,       ///< Handshake or certificate rejected.
    Timeout,         ///< No response within the watchdog window.
    Disconnected,    ///< Peer closed mid-command.

    // -- Server-level: back off this server, keep the article. ---------------
    ServerUnavailable, ///< 400/502 at greeting — usually "too many connections".
    AuthFailed,        ///< 481/482/502 on AUTHINFO. Credentials, not connectivity.

    // -- Content: this server does not have it; another level might. ---------
    ArticleNotFound,   ///< 430.
    GroupNotFound,     ///< 411.
    ArticleCorrupt,    ///< yEnc decode failed: this server's copy is unusable.

    // -- Local: the article is fine; storing it is not. ----------------------
    ///
    /// A full disk, a read-only volume, a scratch file that cannot be opened.
    /// Its own value because every other one is a statement about a *server*,
    /// and routing a local fault through them is how a full disk used to back
    /// off a provider for a minute and then book the article as missing.
    WriteFailed,

    // -- Anything else. ------------------------------------------------------
    ProtocolError,     ///< Unparseable or unexpected response.
};

/// Whether the article should be retried on the *next* priority level rather
/// than on this one. Only content failures escalate — a timeout against a busy
/// server is not evidence that the article is missing.
[[nodiscard]] constexpr bool escalatesToNextLevel(NntpError e)
{
    return e == NntpError::ArticleNotFound || e == NntpError::GroupNotFound
        || e == NntpError::ArticleCorrupt;
}

/// Whether the connection is dead and must be rebuilt before reuse.
[[nodiscard]] constexpr bool isFatalToConnection(NntpError e)
{
    switch (e) {
    case NntpError::ConnectFailed:
    case NntpError::TlsFailed:
    case NntpError::Timeout:
    case NntpError::Disconnected:
    case NntpError::ServerUnavailable:
    case NntpError::AuthFailed:
    case NntpError::ProtocolError:
        return true;
    case NntpError::None:
    case NntpError::ArticleNotFound:
    case NntpError::GroupNotFound:
    // The body was read to its terminating "." and the socket is back to Ready
    // before the decoder's verdict is even looked at, so the connection is in
    // sync. Calling it fatal used to drop it *and* back the whole account off
    // for a minute over one damaged article.
    case NntpError::ArticleCorrupt:
    // The body was read to its terminating "." — the writer's failure was
    // latched and the rest of the chunks dropped on the floor, not the socket's.
    // Dropping the connection over a local disk fault would rebuild it for
    // nothing and back the account off on the way.
    case NntpError::WriteFailed:
        return false;
    }
    return true;
}

[[nodiscard]] QString describeNntpError(NntpError e);

} // namespace eMule::usenet

Q_DECLARE_METATYPE(eMule::usenet::NntpError)
