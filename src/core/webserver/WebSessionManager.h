#pragma once

/// @file WebSessionManager.h
/// @brief Session management for the eMule web interface.
///
/// Manages login sessions with configurable timeout. Passwords are compared
/// as SHA-256 hex hashes.

#include <QDateTime>
#include <QHash>
#include <QString>

namespace eMule {

struct WebSession {
    QString id;
    bool isAdmin = false;
    QDateTime lastAccess;
};

class WebSessionManager {
public:
    explicit WebSessionManager(int timeoutMinutes = 5);

    /// Attempt login. Returns session ID on success, empty string on failure.
    /// @p passwordHash is SHA-256 hex of the entered password.
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

    /// Update the session timeout.
    void setTimeoutMinutes(int minutes);

private:
    [[nodiscard]] QString generateSessionId() const;

    QHash<QString, WebSession> m_sessions;
    int m_timeoutMinutes;
};

} // namespace eMule
