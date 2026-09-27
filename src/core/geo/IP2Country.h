#pragma once

/// @file IP2Country.h
/// @brief IP → ISO country code via a MaxMind GeoLite2-Country database.
///
/// Port of MorphXT/EastShare CIP2Country, but backed by an .mmdb instead of the
/// GeoIPCountryWhois.csv range table. The daemon resolves; the GUI only ever sees
/// the two-letter code ("cc" on the IPC rows) and maps it to a flag and a name.
///
/// Releases bundle a copy in config/ (GeoIpUpdater adopts it when newer); with the
/// user's MaxMind credentials GeoIpUpdater keeps it current. A file dropped into the
/// config dir by hand works too.

#include "net/Address.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QMutex>
#include <QString>

#include <memory>

namespace eMule {

inline constexpr auto kGeoIpDatabaseFilename = "GeoLite2-Country.mmdb";

class IP2Country {
public:
    IP2Country();
    ~IP2Country();

    IP2Country(const IP2Country&) = delete;
    IP2Country& operator=(const IP2Country&) = delete;

    /// Open (or re-open) @p path. On failure the previous database stays active.
    bool open(const QString& path, QString* error = nullptr);
    void close();

    [[nodiscard]] bool isLoaded() const;
    [[nodiscard]] QString path() const;

    /// Build date stamped into the database metadata, invalid when none is loaded.
    [[nodiscard]] QDateTime buildDate() const;

    /// Upper-case ISO 3166-1 alpha-2 code, empty for LAN/unknown/no database.
    [[nodiscard]] QString countryCode(const QHostAddress& addr) const;
    [[nodiscard]] QString countryCode(const Address& addr) const;

    /// Validate a candidate file without touching the active database.
    [[nodiscard]] static bool isValidDatabase(const QString& path, QString* error = nullptr);

    /// Build date of the database at @p path; invalid when it isn't a valid one.
    [[nodiscard]] static QDateTime buildDateOf(const QString& path);

private:
    struct Db;
    [[nodiscard]] std::shared_ptr<Db> current() const;

    mutable QMutex m_mutex;
    std::shared_ptr<Db> m_db;
    mutable QHash<QByteArray, QString> m_cache;
};

/// Country of @p addr through theApp.ip2Country; empty while no database is loaded.
[[nodiscard]] QString countryCodeOf(const Address& addr);
[[nodiscard]] QString countryCodeOf(const QHostAddress& addr);

} // namespace eMule
