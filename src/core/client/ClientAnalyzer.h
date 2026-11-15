#pragma once

/// @file ClientAnalyzer.h
/// @brief Behaviour score per user hash, port of WiZaRd's ClientAnalyzer (eMuleFuture
/// AntiLeech/ClientAnalyzer.cpp, v1.5).
///
/// Judges a peer by what it does to us: reask rate, source exchange give and take, chat
/// spam, faked file status, failed sessions and the transfer balance. The score multiplies
/// the queue score next to the credit ratio. Mod/nick thief detection is not repeated here:
/// a DLP verdict (DlpLoader) sets kFlagDlp. Stored in clientanalyzer.met, not clients.met.

#include "client/ClientStateDefs.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QString>

#include <array>
#include <cstddef>
#include <unordered_map>

namespace eMule {

/// One peer's record, keyed by its user hash.
struct AnalyzerRecord {
    // Sticky "very bad" flags
    static constexpr uint32 kFlagDlp = 0x01;          ///< DLP verdict: nick/mod thief, fake mod string, ...
    static constexpr uint32 kFlagUdpFnfFaker = 0x04;  ///< says "file not found" for a file it requests from us
    static constexpr uint32 kFlagFileFaker = 0x08;    ///< complete source that asks us for the same file

    static constexpr uint32 kRareFile = 25;           ///< fewer sources: a rare file
    static constexpr uint32 kXsSpamLevel = 3;         ///< too fast source requests before it counts
    static constexpr uint32 kXsExploitLevel = 10;     ///< our source requests before answers are judged
    static constexpr uint32 kXsExploitFactor = 3;     ///< asks > factor × answers: exploiter
    static constexpr uint32 kReaskBufferMs = 5 * 60 * 1000;
    static constexpr uint32 kBadUploadSessions = 3;   ///< more in a row: slot only while connected
    static constexpr int64 kBadFlagHoldS = 2 * 60 * 60;  ///< a flag cannot be cleared for 2 h

    std::array<uint8, 16> key{};
    uint32 lastSeen = 0;        ///< unix seconds
    uint32 badSince = 0;        ///< unix seconds a flag was last set
    uint32 flags = 0;
    uint32 reasks = 0;
    uint32 avgReaskMs = 0;
    uint32 spams = 0;
    uint32 xsAsks = 0;          ///< our source requests (files with ≥ kRareFile sources)
    uint32 xsAnswers = 0;       ///< its source answers
    uint32 fastXsAsks = 0;      ///< its source requests we turned down as too soon
    uint8 badUlSessions = 0;    ///< in a row
    uint8 badDlSessions = 0;    ///< in a row
    uint64 sent = 0;            ///< uploaded to it
    uint64 sentPartial = 0;     ///< of which from part files
    uint64 got = 0;             ///< downloaded from it
    uint64 gotPartial = 0;      ///< of which while it was not a complete source
    uint64 gotRare = 0;         ///< of which from rare files

    void addUploaded(uint64 bytes, bool partial);
    void addDownloaded(uint64 bytes, bool partial, uint32 sourceCount);
    void addUlSession(bool bad);
    void addDlSession(bool bad);
    void addSpam() { ++spams; }
    /// @return true when this reask made it a fast asker.
    bool noteReask(uint32 intervalMs, uint32 reaskTimeMs);
    /// @return true when this ask made it an exploiter.
    bool incXsAsks();
    /// @return true when this answer cleared the exploiter state.
    bool incXsAnswers();
    void incFastXs() { ++fastXsAsks; }
    void decFastXs() { if (fastXsAsks > 0) --fastXsAsks; }

    /// @return true if the flag was not set before.
    bool setFlag(uint32 flag, int64 now);
    /// @return true if it was set and now is cleared; held for kBadFlagHoldS after the last set.
    bool clearFlag(uint32 flag, int64 now);
    [[nodiscard]] bool isHeldBad(int64 now) const { return badSince != 0 && now - badSince < kBadFlagHoldS; }

    [[nodiscard]] bool isFastAsker(uint32 reaskTimeMs) const;
    [[nodiscard]] bool isXsSpammer() const { return fastXsAsks > kXsSpamLevel; }
    [[nodiscard]] bool isXsExploiter() const;
    [[nodiscard]] bool wantsLiveSocket() const { return badUlSessions > kBadUploadSessions; }

    /// Weighted totals: full-file uploads and part-file / rare downloads count more.
    [[nodiscard]] uint64 uploadedWeighted() const;
    [[nodiscard]] uint64 downloadedWeighted() const;

    /// Score in [0.01, 10], 1 = neutral. @p hasSomethingForUs: we download from it (on
    /// its queue or in a slot); only then the transfer balance counts.
    [[nodiscard]] float score(bool hasSomethingForUs, uint32 reaskTimeMs) const;
    /// Why the score is low, one line; empty if nothing.
    [[nodiscard]] QString reasons(bool hasSomethingForUs, uint32 reaskTimeMs) const;
    /// Worth a line in clientanalyzer.met.
    [[nodiscard]] bool isWorthKeeping(int64 now) const;
};

class ClientCredits;

/// Store of AnalyzerRecord + senders of corrupt data.
class ClientAnalyzer {
public:
    static constexpr int64 kExpireS = 150ll * 24 * 60 * 60;       ///< unseen records dropped
    static constexpr int64 kCorruptHoldS = 60ll * 24 * 60 * 60;   ///< corrupt senders kept
    static constexpr int64 kSaveIntervalMs = 18ll * 60 * 1000;

    ClientAnalyzer() = default;
    ClientAnalyzer(const ClientAnalyzer&) = delete;
    ClientAnalyzer& operator=(const ClientAnalyzer&) = delete;

    bool load(const QString& filePath);
    /// Queued on the BackgroundFileWriter; nothing if unchanged since the last save.
    void save(const QString& filePath);
    /// Saves every kSaveIntervalMs.
    void process(const QString& filePath, uint64 nowMs);

    /// The record for @p userHash, made if missing; @p created tells. Touches lastSeen.
    AnalyzerRecord& record(const uint8* userHash, bool* created = nullptr);
    [[nodiscard]] const AnalyzerRecord* find(const uint8* userHash) const;
    /// A copy for a peer not yet identified, or a fresh one.
    [[nodiscard]] AnalyzerRecord snapshot(const uint8* userHash) const;
    /// Seeds a fresh record with the credit totals, as ClientAnalyzer did.
    static void seedFromCredits(AnalyzerRecord& r, const ClientCredits& credits);

    void addCorruptSender(const uint8* userHash);
    [[nodiscard]] bool isCorruptSender(const uint8* userHash) const;

    [[nodiscard]] std::size_t count() const { return m_records.size(); }
    [[nodiscard]] std::size_t corruptSenderCount() const { return m_corrupt.size(); }

    /// Reask interval the peer's software uses; shorter averages count as fast.
    [[nodiscard]] static uint32 reaskTimeFor(ClientSoftware soft);

    /// File bytes, for tests.
    [[nodiscard]] QByteArray serialize(int64 now) const;
    bool deserialize(const QByteArray& data, int64 now);

private:
    using Key = std::array<uint8, 16>;
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept;
    };
    static Key toKey(const uint8* hash);

    std::unordered_map<Key, AnalyzerRecord, KeyHash> m_records;
    std::unordered_map<Key, uint32, KeyHash> m_corrupt;   ///< hash → unix seconds added
    uint64 m_lastSaved = 0;
    bool m_dirty = false;
};

} // namespace eMule
