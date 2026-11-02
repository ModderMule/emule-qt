#pragma once

/// @file StreamTokenStore.h
/// @brief The `?token=` values of the stream routes (preview, Incoming).
///
/// Each token belongs to a scope: a web session id, or "gui" for the GUI's own
/// token. A web page mints one per render; logout revokes its session's scope.
/// Thread-safe: the GUI token is read from the IPC handler too.

#include <QHash>
#include <QList>
#include <QMutex>
#include <QString>
#include <QStringView>

#include <chrono>
#include <functional>
#include <optional>

namespace eMule {

class StreamTokenStore {
public:
    using Clock = std::function<qint64()>;   ///< ms, monotonic enough

    /// At most this many live tokens per scope; minting more drops the oldest.
    static constexpr qsizetype kMaxPerScope = 16;

    /// @p clock defaults to QDateTime::currentMSecsSinceEpoch.
    explicit StreamTokenStore(Clock clock = {});

    /// A new random token for @p scope. @p idleTtl 0 = never idles out.
    [[nodiscard]] QString mint(const QString& scope, std::chrono::milliseconds idleTtl);

    /// The scope of @p token, refreshing its last use; nullopt when unknown or idle.
    [[nodiscard]] std::optional<QString> check(QStringView token);

    /// Mark every token of @p scope as just used (a page that is still open).
    void touchScope(const QString& scope);

    void revokeScope(const QString& scope);
    void clear();

    /// Live tokens in @p scope (tests).
    [[nodiscard]] qsizetype count(const QString& scope) const;

private:
    struct Entry {
        QString token;
        qint64 lastUseMs = 0;
        qint64 idleMs = 0;   ///< 0 = no idle expiry
    };

    [[nodiscard]] qint64 now() const;
    void dropExpired(qint64 nowMs);

    Clock m_clock;
    mutable QMutex m_mutex;
    QHash<QString, QList<Entry>> m_byScope;
};

} // namespace eMule
