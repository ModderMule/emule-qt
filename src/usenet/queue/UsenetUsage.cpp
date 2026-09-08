#include "queue/UsenetUsage.h"

#include "queue/UsenetQueueStore.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>

#include <yaml-cpp/yaml.h>

#include <algorithm>

namespace eMule::usenet {

namespace {

constexpr int kUsageVersion = 1;

/// The reset day this month can actually hold. A plan billed on the 31st is
/// billed on the 28th in February — every billing system does this, and doing
/// anything else means February never rolls at all.
[[nodiscard]] int clampDay(int resetDay, const QDate& month)
{
    return std::clamp(resetDay, 1, month.daysInMonth());
}

[[nodiscard]] std::string toStd(const QString& s)
{
    return s.toStdString();
}

} // namespace

QDate nntpQuotaPeriodStart(int resetDay, const QDate& today)
{
    if (!today.isValid())
        return {};

    const int day = clampDay(resetDay, today);
    if (today.day() >= day)
        return {today.year(), today.month(), day};

    // Before this month's billing day, so we are still inside last month's
    // period. Only the year and month of the previous month are used; its own
    // day is irrelevant and would drift.
    const QDate prev = today.addMonths(-1);
    return {prev.year(), prev.month(), clampDay(resetDay, prev)};
}

QDate nntpQuotaNextReset(int resetDay, const QDate& today)
{
    const QDate start = nntpQuotaPeriodStart(resetDay, today);
    if (!start.isValid())
        return {};

    const QDate next = start.addMonths(1);
    return {next.year(), next.month(), clampDay(resetDay, next)};
}

QString UsenetUsageTracker::usagePath()
{
    return QDir(UsenetQueueStore::stateDir()).filePath(QStringLiteral("usage.yml"));
}

void UsenetUsageTracker::load()
{
    m_loaded = true;

    const QString path = usagePath();
    if (!QFile::exists(path))
        return;

    try {
        YAML::Node root = YAML::LoadFile(path.toStdString());
        if (!root || !root.IsMap())
            return;

        // Only a *newer* file is refused, so a key added later needs no bump.
        if (root["version"] && root["version"].as<int>(0) > kUsageVersion) {
            logWarning(QStringLiteral("Usenet: %1 is version %2, newer than %3 — ignoring")
                           .arg(path)
                           .arg(root["version"].as<int>(0))
                           .arg(kUsageVersion));
            return;
        }

        const YAML::Node accounts = root["accounts"];
        if (!accounts || !accounts.IsSequence())
            return;

        for (const auto& node : accounts) {
            if (!node.IsMap())
                continue;

            NewsServerUsage row;
            row.accountId =
                QString::fromStdString(node["id"].as<std::string>("")).trimmed();
            if (row.accountId.isEmpty())
                continue;

            row.name = QString::fromStdString(node["name"].as<std::string>(""));
            row.host = QString::fromStdString(node["host"].as<std::string>(""));
            row.kind = node["kind"].as<int>(0);
            row.periodBytes = node["periodBytes"].as<long long>(0);
            row.totalBytes = node["totalBytes"].as<long long>(0);
            row.updated = node["updated"].as<long long>(0);

            const auto start = QString::fromStdString(node["periodStart"].as<std::string>(""));
            if (!start.isEmpty())
                row.periodStart = QDate::fromString(start, Qt::ISODate);

            // Reload keeps whatever this session has already spent: the base is
            // replaced, the session is not, so base + session stays honest.
            Entry& e = m_entries[row.accountId];
            e.stored = row;
        }
    } catch (const std::exception& ex) {
        logWarning(QStringLiteral("Usenet: cannot read %1 (%2) — usage starts from zero")
                       .arg(path, QString::fromUtf8(ex.what())));
    }
}

void UsenetUsageTracker::setAccounts(const QList<NewsServer>& servers)
{
    if (!m_loaded)
        load();

    for (const NewsServer& s : servers) {
        if (s.accountId.isEmpty())
            continue;

        Entry& e = m_entries[s.accountId];
        e.resetDay = s.quotaResetDay;
        e.stored.accountId = s.accountId;
        e.stored.name = s.displayName();
        e.stored.host = s.host;
        e.stored.kind = static_cast<int>(s.quotaKind);

        // A monthly account with no period yet starts one now rather than at
        // the next rollover check, so the first period is not open-ended.
        if (s.quotaKind == NntpQuotaKind::Monthly && !e.stored.periodStart.isValid())
            e.stored.periodStart = nntpQuotaPeriodStart(s.quotaResetDay, QDate::currentDate());
    }

    // Rows for accounts no longer configured are deliberately kept: removing
    // and re-adding an account must not hand out a free allowance.
}

void UsenetUsageTracker::add(const QString& accountId, qint64 bytes)
{
    if (accountId.isEmpty() || bytes <= 0)
        return;

    Entry* e = entryFor(accountId);
    e->sessionPeriod += bytes;
    e->sessionTotal += bytes;
    m_unflushed += bytes;
}

qint64 UsenetUsageTracker::periodBytes(const QString& accountId) const
{
    const auto it = m_entries.constFind(accountId);
    if (it == m_entries.constEnd())
        return 0;
    return it->stored.periodBytes + it->sessionPeriod;
}

qint64 UsenetUsageTracker::totalBytes(const QString& accountId) const
{
    const auto it = m_entries.constFind(accountId);
    if (it == m_entries.constEnd())
        return 0;
    return it->stored.totalBytes + it->sessionTotal;
}

QDate UsenetUsageTracker::periodStart(const QString& accountId) const
{
    const auto it = m_entries.constFind(accountId);
    return it == m_entries.constEnd() ? QDate() : it->stored.periodStart;
}

QList<NewsServerUsage> UsenetUsageTracker::rows() const
{
    QList<NewsServerUsage> out;
    out.reserve(m_entries.size());
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        NewsServerUsage row = it->stored;
        row.periodBytes += it->sessionPeriod;
        row.totalBytes += it->sessionTotal;
        out.append(row);
    }
    return out;
}

bool UsenetUsageTracker::rollOverIfDue()
{
    const QDate today = QDate::currentDate();
    bool rolled = false;

    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        Entry& e = *it;
        if (e.stored.kind != static_cast<int>(NntpQuotaKind::Monthly))
            continue;   // a block account's bytes are prepaid; they never come back

        const QDate computed = nntpQuotaPeriodStart(e.resetDay, today);
        if (!computed.isValid())
            continue;

        if (!e.stored.periodStart.isValid()) {
            e.stored.periodStart = computed;
            continue;
        }
        if (computed == e.stored.periodStart)
            continue;

        if (computed < e.stored.periodStart) {
            // The clock moved back — an NTP correction, a restored snapshot, a
            // timezone fix. Rolling would resurrect a period already spent and
            // start charging it twice, so refuse and say so once.
            if (!e.clockBackLogged) {
                e.clockBackLogged = true;
                logWarning(QStringLiteral("Usenet: %1 usage period starts %2 but today is %3 — "
                                          "the clock moved back; not resetting the counter")
                               .arg(e.stored.name.isEmpty() ? e.stored.accountId : e.stored.name,
                                    e.stored.periodStart.toString(Qt::ISODate),
                                    today.toString(Qt::ISODate)));
            }
            continue;
        }

        logInfo(QStringLiteral("Usenet: %1 allowance reset — new period starts %2")
                    .arg(e.stored.name.isEmpty() ? e.stored.accountId : e.stored.name,
                         computed.toString(Qt::ISODate)));

        e.stored.periodStart = computed;
        e.stored.periodBytes = 0;
        e.sessionPeriod = 0;
        e.clockBackLogged = false;
        rolled = true;
    }

    return rolled;
}

bool UsenetUsageTracker::flush()
{
    if (m_entries.isEmpty())
        return true;

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << kUsageVersion;
    out << YAML::Key << "accounts" << YAML::Value << YAML::BeginSeq;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        const Entry& e = *it;
        out << YAML::BeginMap;
        out << YAML::Key << "id" << YAML::Value << toStd(e.stored.accountId);
        if (!e.stored.name.isEmpty())
            out << YAML::Key << "name" << YAML::Value << toStd(e.stored.name);
        if (!e.stored.host.isEmpty())
            out << YAML::Key << "host" << YAML::Value << toStd(e.stored.host);
        out << YAML::Key << "kind" << YAML::Value << e.stored.kind;
        if (e.stored.periodStart.isValid()) {
            out << YAML::Key << "periodStart" << YAML::Value
                << toStd(e.stored.periodStart.toString(Qt::ISODate));
        }
        // Absolute, never an increment: that is what makes a second flush a
        // no-op and an unclean exit cost only the unflushed session.
        out << YAML::Key << "periodBytes" << YAML::Value
            << static_cast<long long>(e.stored.periodBytes + e.sessionPeriod);
        out << YAML::Key << "totalBytes" << YAML::Value
            << static_cast<long long>(e.stored.totalBytes + e.sessionTotal);
        out << YAML::Key << "updated" << YAML::Value << static_cast<long long>(now);
        out << YAML::EndMap;
    }

    out << YAML::EndSeq;
    out << YAML::EndMap;

    if (!writeSidecarAtomically(usagePath(), out.c_str()))
        return false;

    m_unflushed = 0;
    return true;
}

bool UsenetUsageTracker::setUsage(const QString& accountId, qint64 newPeriodBytes,
                                  qint64 newTotalBytes)
{
    if (accountId.isEmpty())
        return false;

    const auto it = m_entries.find(accountId);
    if (it == m_entries.end())
        return false;

    // Base and session together. Clearing only the persisted half would let the
    // next absolute write emit `0 + session` and bring the old figure back.
    if (newPeriodBytes >= 0) {
        it->stored.periodBytes = newPeriodBytes;
        it->sessionPeriod = 0;
    }
    if (newTotalBytes >= 0) {
        it->stored.totalBytes = newTotalBytes;
        it->sessionTotal = 0;
    }

    return flush();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

UsenetUsageTracker::Entry* UsenetUsageTracker::entryFor(const QString& accountId)
{
    const auto it = m_entries.find(accountId);
    if (it != m_entries.end())
        return &*it;

    Entry fresh;
    fresh.stored.accountId = accountId;
    return &*m_entries.insert(accountId, fresh);
}

} // namespace eMule::usenet
