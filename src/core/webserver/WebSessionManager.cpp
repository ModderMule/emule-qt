#include "pch.h"
/// @file WebSessionManager.cpp
/// @brief Session management implementation.

#include "webserver/WebSessionManager.h"

#include <QRandomGenerator>

#include <algorithm>

namespace eMule {

WebSessionManager::WebSessionManager(int timeoutMinutes)
    : m_timeoutMinutes(timeoutMinutes)
{
}

bool WebSessionManager::setLanguage(const QString& sessionId, const QString& code)
{
    const auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end())
        return false;
    it->language = code;
    return true;
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

int WebSessionManager::activeCount() const
{
    const auto now = QDateTime::currentDateTime();
    return static_cast<int>(std::ranges::count_if(m_sessions, [&](const WebSession& s) {
        return s.lastAccess.secsTo(now) <= m_timeoutMinutes * 60;
    }));
}

void WebSessionManager::setTimeoutMinutes(int minutes)
{
    m_timeoutMinutes = minutes;
}

QString WebSessionManager::generateSessionId() const
{
    QByteArray bytes(16, Qt::Uninitialized);
    auto* rng = QRandomGenerator::system();
    for (int i = 0; i < 16; ++i)
        bytes[i] = static_cast<char>(rng->bounded(256));
    return QString::fromLatin1(bytes.toHex());
}

// ---------------------------------------------------------------------------
// Password guessing
// ---------------------------------------------------------------------------

namespace {

constexpr int kFreeLoginTries = 3;
constexpr qint64 kFirstLoginWaitMs = 5 * 1000;
constexpr qint64 kMaxLoginWaitMs = 15 * 60 * 1000;
constexpr qint64 kForgetFailuresMs = 60 * 60 * 1000;
constexpr qsizetype kMaxTrackedClients = 4096;

qint64 loginWaitMs(int failures)
{
    if (failures < kFreeLoginTries)
        return 0;
    const int doublings = std::min(failures - kFreeLoginTries, 16);
    return std::min(kFirstLoginWaitMs << doublings, kMaxLoginWaitMs);
}

} // namespace

int WebSessionManager::loginWaitSeconds(const QString& client, qint64 nowMs) const
{
    const auto it = m_failedLogins.constFind(client);
    if (it == m_failedLogins.constEnd())
        return 0;
    const qint64 left = it->lastMs + loginWaitMs(it->count) - nowMs;
    return left > 0 ? static_cast<int>((left + 999) / 1000) : 0;
}

void WebSessionManager::noteLoginFailure(const QString& client, qint64 nowMs)
{
    // Keep the table bounded: it is fed by whoever can reach the port.
    if (m_failedLogins.size() >= kMaxTrackedClients) {
        erase_if(m_failedLogins, [nowMs](const auto& entry) {
            return nowMs - entry.value().lastMs > kForgetFailuresMs;
        });
        if (m_failedLogins.size() >= kMaxTrackedClients) {
            erase_if(m_failedLogins, [](const auto& entry) {
                return entry.value().count < kFreeLoginTries;
            });
        }
    }

    FailedLogins& entry = m_failedLogins[client];
    if (nowMs - entry.lastMs > kForgetFailuresMs)
        entry.count = 0;
    ++entry.count;
    entry.lastMs = nowMs;
}

void WebSessionManager::noteLoginSuccess(const QString& client)
{
    m_failedLogins.remove(client);
}

} // namespace eMule
