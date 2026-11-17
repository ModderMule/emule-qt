#include "pch.h"
/// @file HostCountryResolver.cpp
/// @brief Cached async hostname → country lookup.

#include "geo/HostCountryResolver.h"
#include "geo/CdnRanges.h"
#include "geo/IP2Country.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHostAddress>
#include <QHostInfo>
#include <QPointer>
#include <QSet>
#include <QThread>

#include <memory>

namespace eMule {

namespace {

constexpr qint64 kCacheTtlSecs = 60 * 60;
constexpr int kTimeoutMs = 5000;
constexpr qint64 kMissTtlSecs = 10 * 60;

struct CacheEntry {
    QString code;
    qint64 at = 0;
};

QHash<QString, CacheEntry>& cache()
{
    static QHash<QString, CacheEntry> s_cache;
    return s_cache;
}

struct Pending {
    QHash<QString, QString> result;
    int outstanding = 0;
    bool done = false;
    QPointer<QObject> context;
    std::function<void(const QHash<QString, QString>&)> callback;

    void complete()
    {
        if (done)
            return;
        done = true;
        if (context && callback)
            callback(result);
    }
};

/// A CDN's address names the CDN's edge, not the server: none
[[nodiscard]] QString countryOf(const QHostAddress& a)
{
    return isCdnAddress(a) ? QString() : countryCodeOf(a);
}

[[nodiscard]] QString codeFor(const QHostInfo& info)
{
    // First address with a known country wins; v4/v6 order is the resolver's
    for (const QHostAddress& a : info.addresses())
        if (QString cc = countryOf(a); !cc.isEmpty())
            return cc;
    return {};
}

} // namespace

void resolveHostCountries(const QStringList& hosts, QObject* context,
                          std::function<void(const QHash<QString, QString>&)> done)
{
    auto pending = std::make_shared<Pending>();
    pending->context = context;
    pending->callback = std::move(done);

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QStringList toResolve;
    for (const QString& raw : hosts) {
        const QString host = raw.trimmed();
        if (host.isEmpty() || pending->result.contains(host))
            continue;
        if (QHostAddress literal(host); !literal.isNull()) {
            pending->result.insert(host, countryOf(literal));
            continue;
        }
        if (const auto it = cache().constFind(host.toLower());
            it != cache().cend() && now - it->at < kCacheTtlSecs) {
            pending->result.insert(host, it->code);
            continue;
        }
        pending->result.insert(host, QString());
        toResolve.append(host);
    }

    if (toResolve.isEmpty()) {
        pending->complete();
        return;
    }

    pending->outstanding = static_cast<int>(toResolve.size());
    for (const QString& host : std::as_const(toResolve)) {
        QHostInfo::lookupHost(host, context, [pending, host](const QHostInfo& info) {
            const QString cc = info.error() == QHostInfo::NoError ? codeFor(info) : QString();
            // Only cache a real answer — a db that loads later should get another go
            if (!cc.isEmpty())
                cache().insert(host.toLower(), {cc, QDateTime::currentSecsSinceEpoch()});
            pending->result.insert(host, cc);
            if (--pending->outstanding == 0)
                pending->complete();
        });
    }
    QTimer::singleShot(kTimeoutMs, context, [pending] { pending->complete(); });
}

QString cachedHostCountry(const QString& rawHost)
{
    const QString host = rawHost.trimmed().toLower();
    if (host.isEmpty())
        return {};
    if (QHostAddress literal(host); !literal.isNull())
        return countryOf(literal);

    static QHash<QString, qint64> s_misses;   // host -> when a lookup came back empty
    static QSet<QString> s_inFlight;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (const auto it = cache().constFind(host); it != cache().cend() && now - it->at < kCacheTtlSecs)
        return it->code;
    auto* app = QCoreApplication::instance();
    if (!app || QThread::currentThread() != app->thread())
        return {};
    if (const auto it = s_misses.constFind(host); it != s_misses.cend() && now - *it < kMissTtlSecs)
        return {};
    if (s_inFlight.contains(host))
        return {};
    s_inFlight.insert(host);
    QHostInfo::lookupHost(host, app, [host](const QHostInfo& info) {
        s_inFlight.remove(host);
        const QString cc = info.error() == QHostInfo::NoError ? codeFor(info) : QString();
        const qint64 at = QDateTime::currentSecsSinceEpoch();
        if (cc.isEmpty()) {
            if (s_misses.size() > 4096)
                s_misses.clear();
            s_misses.insert(host, at);
        } else {
            s_misses.remove(host);
            cache().insert(host, {cc, at});
        }
    });
    return {};
}

} // namespace eMule
