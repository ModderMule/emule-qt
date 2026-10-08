#pragma once

/// @file CountryCensus.h
/// @brief How many different peers we have seen, by country.
///
/// Peers are counted by a 128-bit identity in CardinalitySketch es, one per
/// country, plus one secondary sketch whose meaning the subclass defines.
/// The total is the union of the countries, so a peer that moved counts once
/// in it. The country is the one the address resolved to when it was seen.
///
/// The cumulative view is banked ∪ session and written absolutely, so saving
/// twice changes nothing. Subclasses: kad::KadNodeCensus, ClientCensus.
/// Daemon thread only.

#include "stats/CardinalitySketch.h"

#include <QList>
#include <QString>

#include <map>

namespace eMule {

class CountryCensus {
public:
    enum class Scope : uint8 { Session, Cumulative };

    struct CountryCount {
        QString cc;        ///< ISO 3166-1 alpha-2, empty = unknown
        uint64 count = 0;
    };

    /// Most peers first.
    [[nodiscard]] QList<CountryCount> countries(Scope scope) const;

    /// Banked + session, written absolutely: saving twice changes nothing.
    bool save();

    // The statistics reset / restore pair (Preferences::resetCumulativeStats).
    // The session keeps counting through both, as the counters do.

    /// Back the totals up, then forget the banked half.
    void reset();
    /// Swap the totals with the backup, so restoring twice is a no-op.
    bool restore();

    [[nodiscard]] QString filePath() const;
    [[nodiscard]] QString backupPath() const;

protected:
    /// @p dir holds <baseName>.dat and .bak; empty keeps it in memory only.
    /// @p magic keeps one census from reading another's file.
    CountryCensus(const QString& dir, const QString& baseName, quint32 magic);
    ~CountryCensus() = default;

    void addByCountry(uint64 lo, uint64 hi, const QString& cc);
    void addSecondary(uint64 lo, uint64 hi);

    /// Union of the countries.
    [[nodiscard]] uint64 total(Scope scope) const;
    [[nodiscard]] uint64 secondary(Scope scope) const;

private:
    struct Tally {
        std::map<uint16, CardinalitySketch> byCountry;  ///< by packed country code
        CardinalitySketch secondary;
    };

    [[nodiscard]] uint64 hashOf(uint64 lo, uint64 hi) const;
    [[nodiscard]] Tally cumulative() const;
    [[nodiscard]] bool writeTo(const QString& path, const Tally& tally) const;
    [[nodiscard]] bool readFrom(const QString& path, Tally& tally, bool adoptSalt);
    void newSalt();

    QString m_dir;
    QString m_baseName;
    quint32 m_magic;
    uint64 m_salt0 = 0;
    uint64 m_salt1 = 0;
    Tally m_session;
    Tally m_base;       ///< as loaded; the cumulative view is m_base ∪ m_session
    bool m_dirty = false;
};

} // namespace eMule
