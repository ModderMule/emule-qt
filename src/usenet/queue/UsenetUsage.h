#pragma once

/// @file UsenetUsage.h
/// @brief What each provider account has spent, and when its allowance resets.
///
/// Providers meter bytes; NNTP has no command that reports the figure back, so
/// it has to be measured here — the same situation as retention, and with the
/// same consequence: the number is ours, not theirs, and it will read a few
/// percent under a provider's own dashboard because TLS framing and TCP headers
/// are not visible from inside a QSslSocket.
///
/// Lives in queue/ rather than nntp/ because it is policy, not protocol. nntp/
/// speaks to servers; this decides what we are willing to spend on them.
///
/// A plain class, no moc needed — no signals, no parent, and every call happens
/// on the daemon thread. (Do not write the four-letter macro name in this
/// comment: scripts/sync_module_vcxproj.py greps the whole file for it and
/// would file this header as a moc input.)
///
/// **The counter is "base + session, written absolutely".** load() reads the
/// base, add() only ever touches the session, and flush() writes their sum — so
/// flushing twice changes nothing and a flush that never happened loses only
/// the session. That shape is copied from Statistics::flushCumulativeToPrefs()
/// for a reason: the increment-on-save shape it replaced lost a whole session
/// to a kill -9 and double-counted the moment it was put on a timer.

#include "nntp/NewsServer.h"

#include <QDate>
#include <QHash>
#include <QList>
#include <QString>

namespace eMule::usenet {

/// Start of the billing period containing @p today for a reset day of 1-31.
///
/// A month shorter than the reset day rolls over on its last day, which is what
/// every billing system does with a 31st.
[[nodiscard]] QDate nntpQuotaPeriodStart(int resetDay, const QDate& today);

/// The next reset at or after @p today — what the GUI shows and what a stalled
/// item's status names. Derived here so the short-month clamp exists once.
[[nodiscard]] QDate nntpQuotaNextReset(int resetDay, const QDate& today);

/// One account's meter, as persisted.
struct NewsServerUsage {
    /// NewsServer::accountId. The identity, deliberately not key(): key() is
    /// host:port/user, so switching a provider to TLS — which changes the port
    /// — would otherwise start a fresh meter and quietly over-spend.
    QString accountId;

    /// Context for a human reading usage.yml or an orphaned row. Never identity.
    QString name;
    QString host;

    /// Mirrors NntpQuotaKind as of the last write, so a rollover can be decided
    /// for an account that has since been removed from the configuration.
    int kind = 0;

    /// First day of the period `periodBytes` belongs to. Invalid for a block
    /// account, which has no period.
    QDate periodStart;

    qint64 periodBytes = 0;   ///< What an allowance is compared against.
    qint64 totalBytes = 0;    ///< All time. Never cleared by a rollover.
    qint64 updated = 0;       ///< Epoch seconds, for diagnostics only.
};

class UsenetUsageTracker {
public:
    /// Where the sidecar lives. Beside the .nzbstate files, which are globbed
    /// by extension, so a .yml among them is never mistaken for a queue item.
    [[nodiscard]] static QString usagePath();

    /// Read the sidecar into the base. Safe to call twice; the session is not
    /// disturbed, so a reload cannot double-count what this run has spent.
    void load();

    /// Take the current account list, for context and for the rollover rules.
    /// **Keeps every counter** — this runs on every settings save, and a
    /// re-seed here would silently zero the meters each time the user pressed
    /// OK in Options.
    void setAccounts(const QList<NewsServer>& servers);

    /// Charge @p bytes to @p accountId. Only the session half moves.
    void add(const QString& accountId, qint64 bytes);

    [[nodiscard]] qint64 periodBytes(const QString& accountId) const;
    [[nodiscard]] qint64 totalBytes(const QString& accountId) const;
    [[nodiscard]] QDate periodStart(const QString& accountId) const;

    /// Every row, base + session applied, for the IPC report.
    [[nodiscard]] QList<NewsServerUsage> rows() const;

    /// Roll every monthly account whose period has ended. Idempotent: the test
    /// is against the *computed* period start, not a "have I rolled" flag, so
    /// running it four times a second is free and a daemon that was off across
    /// four reset days still rolls exactly once.
    /// @return true when anything rolled, so the caller can unpark and dispatch.
    bool rollOverIfDue();

    /// Write base + session absolutely. Cheap to call when nothing changed.
    bool flush();

    /// Overwrite one account's figures — what a user does after switching
    /// plans, topping up a block, or finding our count disagrees with their
    /// provider's. -1 leaves a figure alone, so a plain reset is (0, -1).
    /// Zeroes base **and** session together: clearing only the persisted half
    /// would let the next absolute write resurrect the old total.
    bool setUsage(const QString& accountId, qint64 newPeriodBytes, qint64 newTotalBytes);

    /// Bytes charged since the last flush. Lets the caller flush on volume as
    /// well as on time — a minute at 5 MB/s is 300 MB of prepaid block credit
    /// to lose to an unclean exit.
    [[nodiscard]] qint64 unflushedBytes() const { return m_unflushed; }

private:
    struct Entry {
        NewsServerUsage stored;      ///< the base, as last read or written
        qint64 sessionPeriod = 0;
        qint64 sessionTotal = 0;
        int resetDay = 1;            ///< from the live config; 1 when unknown
        bool clockBackLogged = false;
    };

    [[nodiscard]] Entry* entryFor(const QString& accountId);

    QHash<QString, Entry> m_entries;
    qint64 m_unflushed = 0;
    bool m_loaded = false;
};

} // namespace eMule::usenet
