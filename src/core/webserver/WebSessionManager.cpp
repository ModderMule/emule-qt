#include "pch.h"
/// @file WebSessionManager.cpp
/// @brief Session management implementation.

#include "webserver/WebSessionManager.h"

#include <QRandomGenerator>

namespace eMule {

WebSessionManager::WebSessionManager(int timeoutMinutes)
    : m_timeoutMinutes(timeoutMinutes)
{
}

QString WebSessionManager::login(const QString& passwordHash,
                                 const QString& adminHash,
                                 const QString& guestHash,
                                 bool guestEnabled)
{
    purgeExpired();

    bool isAdmin = false;

    if (!adminHash.isEmpty() && passwordHash == adminHash) {
        isAdmin = true;
    } else if (guestEnabled && !guestHash.isEmpty() && passwordHash == guestHash) {
        isAdmin = false;
    } else {
        return {};
    }

    WebSession session;
    session.id = generateSessionId();
    session.isAdmin = isAdmin;
    session.lastAccess = QDateTime::currentDateTime();

    m_sessions.insert(session.id, session);
    return session.id;
}

bool WebSessionManager::isValid(const QString& sessionId)
{
    purgeExpired();

    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end())
        return false;

    // Update last access time
    it->lastAccess = QDateTime::currentDateTime();
    return true;
}

bool WebSessionManager::isAdmin(const QString& sessionId) const
{
    auto it = m_sessions.constFind(sessionId);
    if (it == m_sessions.constEnd())
        return false;
    return it->isAdmin;
}

const WebSession* WebSessionManager::session(const QString& sessionId) const
{
    auto it = m_sessions.constFind(sessionId);
    if (it == m_sessions.constEnd())
        return nullptr;
    return &(*it);
}

void WebSessionManager::logout(const QString& sessionId)
{
    m_sessions.remove(sessionId);
}

void WebSessionManager::purgeExpired()
{
    const auto now = QDateTime::currentDateTime();
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ) {
        if (it->lastAccess.secsTo(now) > m_timeoutMinutes * 60)
            it = m_sessions.erase(it);
        else
            ++it;
    }
}

void WebSessionManager::setTimeoutMinutes(int minutes)
{
    m_timeoutMinutes = minutes;
}

QString WebSessionManager::generateSessionId() const
{
    QByteArray bytes(16, Qt::Uninitialized);
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < 16; ++i)
        bytes[i] = static_cast<char>(rng->bounded(256));
    return QString::fromLatin1(bytes.toHex());
}

} // namespace eMule
