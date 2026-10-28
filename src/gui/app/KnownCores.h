#pragma once

/// @file KnownCores.h
/// @brief Pinned TLS fingerprints of the cores this GUI connected to.
///
/// GUI-only file `{configDir}/ipc-known-cores.json`, apart from preferences.yml
/// which the daemon rewrites. Keyed by "host:port" as the user typed it.

#include <QDateTime>
#include <QString>

#include <vector>

namespace eMule {

class KnownCores {
public:
    struct Entry {
        QString hostPort;
        QString fingerprint;   ///< "AB:CD:…" SHA-256
        QDateTime firstSeen;
    };

    enum class Check { New, Match, Mismatch };

    /// Backing file; loads it. Empty path = memory only.
    explicit KnownCores(QString path = {});

    /// The GUI's store at {configDir}/ipc-known-cores.json.
    static KnownCores& instance();

    [[nodiscard]] Check check(const QString& hostPort, const QString& fingerprint) const;
    /// Fingerprint pinned for @p hostPort, empty when none.
    [[nodiscard]] QString pinned(const QString& hostPort) const;
    /// Pin (or re-pin) and save.
    void pin(const QString& hostPort, const QString& fingerprint);
    /// Forget and save.
    void remove(const QString& hostPort);
    [[nodiscard]] const std::vector<Entry>& entries() const { return m_entries; }

    /// Format "host:port" for a pin key; brackets an IPv6 literal.
    [[nodiscard]] static QString key(const QString& host, quint16 port);

private:
    void load();
    void save() const;

    QString m_path;
    std::vector<Entry> m_entries;
};

} // namespace eMule
