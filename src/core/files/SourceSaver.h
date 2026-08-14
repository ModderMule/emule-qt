#pragma once

/// @file SourceSaver.h
/// @brief Save/Load Sources (SLS) — remembers a download's best sources across restarts.
///
/// Port of MorphXT v12.7 `CSourceSaver` (srchybrid/SourceSaver.{h,cpp}), an unofficial-mod
/// feature official eMule never had. Each part file gets a small text file listing its best
/// sources; on the next run those sources are injected back into the download instead of
/// waiting for a server or Kad lookup to rediscover them.
///
/// The on-disk format is **byte-compatible with MorphXT**, so a `Temp` folder can be shared
/// between the two clients. eMuleQt adds IPv6 addresses, user hashes and crypt options in
/// fields MorphXT skips by construction — see SourceListFile for the grammar.
///
/// File: `<TempDir>/Source Lists/<NNN.part.met>.txtsrc`

#include "net/Address.h"
#include "utils/Types.h"

#include <QString>
#include <QStringView>

#include <array>
#include <optional>
#include <vector>

namespace eMule {

class PartFile;
class UpDownClient;

// ---------------------------------------------------------------------------
// Policy constants
// ---------------------------------------------------------------------------

/// MorphXT SLS originals: 10 sources kept, a 30-minute expiry, and SLS applied only to
/// "rare" files — the list was deleted outright once GetAvailableSrcCount() > 25
/// (SourceSaver.cpp:57-62). eMuleQt keeps the file format identical but retains longer and
/// for every download, and in exchange refuses to re-inject a day-old list into a file that
/// has already found live sources of its own.
inline constexpr int kMaxSavedSourcesPerFile = 20;        // MorphXT: 10
inline constexpr int kSourceExpiryMinutes    = 24 * 60;   // MorphXT: 30
inline constexpr int kRareFileSourceLimit    = -1;        // MorphXT: 25 (gate disabled here)
inline constexpr int kSkipInjectAboveSources = 20;        // MorphXT: no such gate

/// MorphXT RESAVETIME / RELOADTIME (SourceSaver.cpp:12-13), unchanged.
inline constexpr uint32 kResaveTimeMs = 600'000;    // 10 minutes
inline constexpr uint32 kReloadTimeMs = 3'600'000;  // 60 minutes
/// Total spread of the ±15 s randomisation MorphXT applies to both timers.
inline constexpr uint32 kTimerJitterMs = 30'000;

/// Subdirectory of the temp dir holding the lists. Spelled exactly as MorphXT writes it —
/// the space and both capitals matter on a case-sensitive filesystem.
inline constexpr auto kSourceListsDirName = "Source Lists";
inline constexpr auto kSourceListSuffix   = ".txtsrc";

// ---------------------------------------------------------------------------
// SavedSource
// ---------------------------------------------------------------------------

/// One entry of a `.txtsrc` source list.
///
/// @a legacyId is stored exactly as MorphXT writes it: the raw 32-bit value `ipstr()`
/// renders byte-by-byte. Its interpretation depends on @a srcExchangeVer —
/// `ver >= 3` means a *hybrid* (host-order) user ID, `ver < 3` means a network-order IPv4.
/// `ipstr()` expects network order, so `ver >= 3` lines carry a byte-reversed dotted quad.
/// That is not a bug to fix: saving and loading are symmetric, and reversing one side alone
/// breaks compatibility in both directions. Use hybridId() to get a normalised value.
struct SavedSource {
    uint32  legacyId       = 0;   ///< network-order IPv4 (ver<3) or hybrid ID (ver>=3)
    uint16  port           = 0;
    uint32  serverIP       = 0;   ///< network byte order; 0 when unknown
    uint16  serverPort     = 0;
    uint8   srcExchangeVer = 0;
    QString expiration;           ///< "yymmddhhmm", local time, kept verbatim

    // -- eMuleQt extension fields (invisible to MorphXT) ----------------------
    Address              ipv6;
    std::array<uint8,16> userHash{};
    bool                 hasUserHash    = false;
    uint8                connectOptions = 0;   ///< the byte setConnectOptions() decodes
    uint16               kadPort        = 0;
    uint16               udpPort        = 0;
    bool                 privateLine    = false;  ///< emitted with the "#x=" prefix

    // -- ranking only, never persisted ---------------------------------------
    uint16 availableParts  = 0;
    bool   holdsNeededPart = false;

    /// Host-order user ID: the IPv4 for a HighID, the raw low value for a LowID.
    [[nodiscard]] uint32 hybridId() const;

    /// MorphXT CSourceData::Compare() (SourceSaver.cpp:52-53), but on the *normalised* id so
    /// two records written under different srcExchangeVers still match, plus a hash compare
    /// so a source that changed address is still recognised.
    [[nodiscard]] bool sameSourceAs(const SavedSource& other) const;
};

// ---------------------------------------------------------------------------
// SourceListFile
// ---------------------------------------------------------------------------

/// Text codec for the MorphXT `.txtsrc` format plus the eMuleQt extension fields.
///
/// Deliberately free of PartFile / DownloadQueue / Preferences so the on-disk format can be
/// tested in isolation, and in both directions.
///
/// Grammar — CRLF lines, `#` comments, MorphXT ignores everything after the `;`:
/// @code
/// #format: a.b.c.d:port,expirationdate(yymmddhhmm);
/// #ed2k://|file|Name.ext|12345|HASH|/
/// #emuleqt-sls: 1
/// 1.2.3.4:4662,2608131230,4,5.6.7.8:4661;v6=[2001:db8::1],h=A1B2…F0,co=5
/// #x=[2001:db8::1]:4662,2608131230,4,0.0.0.0:0;h=A1B2…F0,co=5
/// @endcode
///
/// A record goes on a `#x=` private line — which MorphXT skips as a comment — when it has no
/// usable IPv4, or when it requires obfuscation. MorphXT drops crypt-required sources because
/// it cannot store the user hash; we can, but a MorphXT reader consuming such a line would
/// build a hash-less client and fail the handshake, so it must not see them.
///
/// Extension keys (unknown keys are ignored, now and forever):
///   `v6` bracketed IPv6 · `h` 32-hex user hash · `co` connect-options byte ·
///   `kp` Kad UDP port · `up` client UDP port
class SourceListFile {
public:
    /// Prefix marking an eMuleQt-only record. MorphXT skips it (its parser tests line[0]=='#').
    static constexpr QLatin1StringView kPrivatePrefix{"#x="};

    /// Format one record as a single line, without the trailing CRLF.
    [[nodiscard]] static QString formatRecord(const SavedSource& src);

    /// Parse one line. Returns nullopt for comments, blanks and malformed records —
    /// a bad line is always skipped on its own, never fatal to the file.
    /// Expiry is *not* checked here; read() does that.
    [[nodiscard]] static std::optional<SavedSource> parseRecord(QStringView line);

    /// Read a list file. A missing file yields an empty vector without logging.
    [[nodiscard]] static std::vector<SavedSource> read(const QString& path,
                                                      bool dropExpired = true);

    /// Write a list file atomically (temp + rename), creating the directory if needed.
    /// @p ed2kLink goes into the second header line, as MorphXT does, purely as a
    /// met-recovery aid — neither parser reads it back.
    static bool write(const QString& path, const std::vector<SavedSource>& records,
                      const QString& ed2kLink);

    /// "yymmddhhmm" @p minutesFromNow in the future, local time (MorphXT CalcExpiration).
    [[nodiscard]] static QString calcExpiration(int minutesFromNow);

    /// True when @p expiration is malformed or already in the past (MorphXT IsExpired).
    [[nodiscard]] static bool isExpired(QStringView expiration);
};

// ---------------------------------------------------------------------------
// SourceSaver
// ---------------------------------------------------------------------------

/// Per-PartFile Save/Load Sources driver — port of MorphXT CSourceSaver.
///
/// Held by value in PartFile (mirroring `CPartFile::m_sourcesaver`, MorphXT PartFile.h:427)
/// so the resave/reload timers and their jitter are per file: 200 downloads then spread their
/// writes instead of all firing on the same tick.
class SourceSaver {
public:
    SourceSaver();

    /// Periodic tick from PartFile::process(). Honours the 10-minute resave timer and the
    /// 60-minute reload timer. Returns true when a list was written.
    bool process(PartFile* file);

    /// Write now, ignoring the resave timer — the clean-shutdown path, where the live source
    /// list would otherwise be lost in favour of a stale one up to 10 minutes old.
    bool saveNow(PartFile* file);

    /// Read the list and inject its non-expired entries into @p file.
    /// Returns the number of sources actually added.
    int loadAndInject(PartFile* file);

    /// Delete the list for a part file that is being cancelled or has completed.
    static void removeFile(const PartFile* file);
    static void removeFile(const QString& tmpPath, const QString& partMetFilename);

    /// `<tmpPath>/Source Lists/<partMetFilename>.txtsrc`, or empty if either part is empty.
    [[nodiscard]] static QString filePath(const QString& tmpPath,
                                          const QString& partMetFilename);

    /// mkpath the "Source Lists" subdirectory of every configured temp dir. A category may
    /// still point a download at some other directory, which is why write() also mkpaths.
    static void ensureDirectories(const QStringList& tempDirs);

private:
    [[nodiscard]] static bool saveList(PartFile* file, const QString& path,
                                       const std::vector<SavedSource>& previous);
    [[nodiscard]] static std::vector<SavedSource> collectBestSources(const PartFile* file,
                                                                     int maxToSave);
    [[nodiscard]] static std::optional<SavedSource> makeRecord(const UpDownClient* client,
                                                               const PartFile* file,
                                                               const QString& expiration);
    static void mergePrevious(std::vector<SavedSource>& out,
                              const std::vector<SavedSource>& previous, int maxToSave);
    [[nodiscard]] static int injectRecords(PartFile* file,
                                           const std::vector<SavedSource>& records);
    [[nodiscard]] static int32 jitter();

    uint32 m_lastSaved  = 0;
    uint32 m_lastLoaded = 0;
};

} // namespace eMule
