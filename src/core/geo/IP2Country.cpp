#include "pch.h"
/// @file IP2Country.cpp
/// @brief GeoLite2-Country lookup via libmaxminddb.

#include "geo/IP2Country.h"
#include "app/AppContext.h"
#include "utils/Log.h"

#include <maxminddb.h>

#include <QMutexLocker>
#include <QTimeZone>
#include <QtEndian>

#include <cstring>

namespace eMule {

namespace {

/// Cache is cheap insurance against the list polls; clear rather than evict.
constexpr qsizetype kMaxCacheEntries = 20000;

/// 16-byte v6 form (v4 as ::ffff:a.b.c.d) — one key space for both families.
[[nodiscard]] QByteArray cacheKey(const QHostAddress& addr)
{
    const Q_IPV6ADDR v6 = addr.toIPv6Address();
    return QByteArray(reinterpret_cast<const char*>(v6.c), 16);
}

[[nodiscard]] QString readIsoCode(MMDB_entry_s& entry, const char* branch)
{
    MMDB_entry_data_s data{};
    if (MMDB_get_value(&entry, &data, branch, "iso_code", nullptr) != MMDB_SUCCESS
        || !data.has_data || data.type != MMDB_DATA_TYPE_UTF8_STRING)
        return {};
    return QString::fromUtf8(data.utf8_string, static_cast<qsizetype>(data.data_size)).toUpper();
}

} // namespace

struct IP2Country::Db {
    MMDB_s mmdb{};
    QString path;

    ~Db() { MMDB_close(&mmdb); }
};

IP2Country::IP2Country() = default;

IP2Country::~IP2Country() = default;

bool IP2Country::open(const QString& path, QString* error)
{
    auto db = std::make_shared<Db>();
    const QByteArray native = QFile::encodeName(path);
    const int rc = MMDB_open(native.constData(), MMDB_MODE_MMAP, &db->mmdb);
    if (rc != MMDB_SUCCESS) {
        // MMDB_open leaves nothing to close on failure
        std::memset(&db->mmdb, 0, sizeof(db->mmdb));
        if (error)
            *error = QString::fromUtf8(MMDB_strerror(rc));
        return false;
    }
    db->path = path;

    QMutexLocker lock(&m_mutex);
    m_db = std::move(db);
    m_cache.clear();
    return true;
}

void IP2Country::close()
{
    QMutexLocker lock(&m_mutex);
    m_db.reset();
    m_cache.clear();
}

bool IP2Country::isLoaded() const
{
    return current() != nullptr;
}

QString IP2Country::path() const
{
    const auto db = current();
    return db ? db->path : QString();
}

QDateTime IP2Country::buildDate() const
{
    const auto db = current();
    if (!db)
        return {};
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(db->mmdb.metadata.build_epoch),
                                         QTimeZone::UTC);
}

QString IP2Country::countryCode(const Address& addr) const
{
    if (addr.isNull())
        return {};
    return countryCode(addr.toQHostAddress());
}

QString IP2Country::countryCode(const QHostAddress& addr) const
{
    if (addr.isNull() || addr.isLoopback() || addr.isPrivateUse() || addr.isLinkLocal()
        || addr == QHostAddress::AnyIPv4 || addr == QHostAddress::AnyIPv6)
        return {};

    const QByteArray key = cacheKey(addr);
    std::shared_ptr<Db> db;
    {
        QMutexLocker lock(&m_mutex);
        if (!m_db)
            return {};
        if (const auto it = m_cache.constFind(key); it != m_cache.cend())
            return *it;
        db = m_db;
    }

    // A v4-mapped v6 address must go in as plain v4: the tree lookup for a
    // sockaddr_in6 in ::ffff:0:0/96 is not guaranteed on every database.
    bool isV4 = false;
    const quint32 v4 = addr.toIPv4Address(&isV4);

    sockaddr_storage storage{};
    if (isV4) {
        auto* sin = reinterpret_cast<sockaddr_in*>(&storage);
        sin->sin_family = AF_INET;
        sin->sin_addr.s_addr = qToBigEndian(v4);
    } else {
        auto* sin6 = reinterpret_cast<sockaddr_in6*>(&storage);
        sin6->sin6_family = AF_INET6;
        const Q_IPV6ADDR v6 = addr.toIPv6Address();
        std::memcpy(&sin6->sin6_addr, v6.c, 16);
    }

    int mmdbError = MMDB_SUCCESS;
    MMDB_lookup_result_s result =
        MMDB_lookup_sockaddr(&db->mmdb, reinterpret_cast<const sockaddr*>(&storage), &mmdbError);

    QString code;
    if (mmdbError == MMDB_SUCCESS && result.found_entry) {
        code = readIsoCode(result.entry, "country");
        if (code.isEmpty())
            code = readIsoCode(result.entry, "registered_country");
    }

    QMutexLocker lock(&m_mutex);
    if (m_db == db) {   // don't cache an answer from a database just replaced
        if (m_cache.size() >= kMaxCacheEntries)
            m_cache.clear();
        m_cache.insert(key, code);
    }
    return code;
}

bool IP2Country::isValidDatabase(const QString& path, QString* error)
{
    MMDB_s mmdb{};
    const QByteArray native = QFile::encodeName(path);
    const int rc = MMDB_open(native.constData(), MMDB_MODE_MMAP, &mmdb);
    if (rc != MMDB_SUCCESS) {
        if (error)
            *error = QString::fromUtf8(MMDB_strerror(rc));
        return false;
    }
    const QByteArray type(mmdb.metadata.database_type ? mmdb.metadata.database_type : "");
    MMDB_close(&mmdb);
    if (!type.contains("Country")) {
        if (error)
            *error = QStringLiteral("not a country database (%1)").arg(QString::fromLatin1(type));
        return false;
    }
    return true;
}

QDateTime IP2Country::buildDateOf(const QString& path)
{
    if (!isValidDatabase(path))
        return {};
    MMDB_s mmdb{};
    const QByteArray native = QFile::encodeName(path);
    if (MMDB_open(native.constData(), MMDB_MODE_MMAP, &mmdb) != MMDB_SUCCESS)
        return {};
    const auto epoch = static_cast<qint64>(mmdb.metadata.build_epoch);
    MMDB_close(&mmdb);
    return QDateTime::fromSecsSinceEpoch(epoch, QTimeZone::UTC);
}

std::shared_ptr<IP2Country::Db> IP2Country::current() const
{
    QMutexLocker lock(&m_mutex);
    return m_db;
}

QString countryCodeOf(const Address& addr)
{
    return theApp.ip2Country ? theApp.ip2Country->countryCode(addr) : QString();
}

QString countryCodeOf(const QHostAddress& addr)
{
    return theApp.ip2Country ? theApp.ip2Country->countryCode(addr) : QString();
}

} // namespace eMule
