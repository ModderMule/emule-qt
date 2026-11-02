#pragma once

/// @file WebSessionManager.h
/// @brief Session management for the eMule web interface.
///
/// Manages login sessions with configurable timeout. The entered password is
/// SHA-256 hex; the stored one is PBKDF2 of that (crypto/SecretCompare.h).

#include <QDateTime>
#include <QHash>
#include <QString>

#include <functional>

namespace eMule {

struct WebSession {
    QString id;
    bool isAdmin = false;
    QDateTime lastAccess;
    QString language;   ///< the language menu's choice; empty follows the app
};

class WebSessionManager {
public:
    explicit WebSessionManager(int timeoutMinutes = 5);

    /// Set a session's language; empty follows the app again. False when there
    /// is no such session.
    bool setLanguage(const QString& sessionId, const QString& code);

    /// Called with the id of each session that ends (logout or expiry).
    void setSessionEndedHandler(std::function<void(const QString&)> handler)
    {
        m_onSessionEnded = std::move(handler);
    }

    /// Attempt login. Returns session ID on success, empty string on failure.
    /// @p passwordHash is SHA-256 hex of the entered password; the stored hashes
    /// are `pbkdf2$…` (or legacy SHA-256 hex).
    [[nodiscard]] QString login(const QString& passwordHash,
                                const QString& adminHash,
                                const QString& guestHash,
                                bool guestEnabled);

    /// Check if a session ID is valid (exists and not expired).
    [[nodiscard]] bool isValid(const QString& sessionId);

    /// Check if a session belongs to an admin user.
    [[nodiscard]] bool isAdmin(const QString& sessionId) const;

    /// Get session by ID. Returns nullptr if not found.
    [[nodiscard]] const WebSession* session(const QString& sessionId) const;

    /// Destroy a session (logout).
    void logout(const QString& sessionId);

    /// Remove all expired sessions.
    void purgeExpired();

    /// Sessions not yet expired. MFC CWebServer::GetSessionCount.
    [[nodiscard]] int activeCount() const;

    /// Update the session timeout.
    void setTimeoutMinutes(int minutes);

    // --- Password guessing ---------------------------------------------------
    // Three free tries per client address, then a wait that doubles from 5 s to
    // 15 min. A success clears it; an hour without a failure forgets it.
    // Also used for API keys ("api:" + address) and stream tokens ("stream:").

    static constexpr int kFreeLoginTries = 3;

    /// Seconds @p client has to wait before a password is looked at again; 0 = now.
    /// @p freeTries failures go unpunished.
    [[nodiscard]] int loginWaitSeconds(const QString& client, qint64 nowMs,
                                       int freeTries = kFreeLoginTries) const;
    void noteLoginFailure(const QString& client, qint64 nowMs);
    void noteLoginSuccess(const QString& client);

private:
    [[nodiscard]] QString generateSessionId() const;

    struct FailedLogins {
        int count = 0;
        qint64 lastMs = 0;
    };

    QHash<QString, WebSession> m_sessions;
    QHash<QString, FailedLogins> m_failedLogins;
    int m_timeoutMinutes;
    std::function<void(const QString&)> m_onSessionEnded;
};

} // namespace eMule
