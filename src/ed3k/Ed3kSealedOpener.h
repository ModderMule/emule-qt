#pragma once

/// @file Ed3kSealedOpener.h
/// @brief Sealed set members opened into plain files (subscription.md §8.1, hidden-set.md
/// §4.2): `nonce(12) ‖ ChaCha20(k_data, nonce, plain)`, plain byte i = sealed byte 12 + i
/// (ed3k_sealed_read_at, any offset). Done in chunks on the event loop: a member may be large.
///
/// - The plain file is written as <plain>.part, renamed when done; an existing plain file is
///   never overwritten (file-set.md §5.3). The sealed file stays (it is shared, §8.1).
/// - k_data is a secret (Secret, wiped with the job), never logged.
/// - Batch 15: the other direction too (Ed3kSealedSealer, a set authored here): a plain file
///   sealed with a nonce the caller drew (random per member) into <sealed>.part, renamed when
///   done (an older file there is replaced: it is the caller's own). Both run on
///   Ed3kSealedCipher (the cipher is its own inverse: the same chunk loop).

#include "Ed3kFfi.h"

#include <QObject>
#include <QString>

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>

namespace eMule::ed3k {

/// The chunked job queue both directions share: one job at a time, kChunk per event loop turn.
class Ed3kSealedCipher : public QObject {
    Q_OBJECT

public:
    /// bytes opened per event loop turn
    static constexpr qint64 kChunk = 4 * 1024 * 1024;
    /// the nonce in front of the cipher
    static constexpr qint64 kNonce = 12;
    /// plain bytes of one sealed file at most (hidden-set.md §4.2: 2^38, the one-nonce limit)
    static constexpr qint64 kMaxPlain = qint64(1) << 38;
    using Nonce = std::array<std::uint8_t, 12>;

    ~Ed3kSealedCipher() override;

    /// A job writes @p outPath now (queued or running).
    [[nodiscard]] bool busy(const QString& outPath) const;

protected:
    explicit Ed3kSealedCipher(QObject* parent = nullptr);

    /// Queues @p inPath → @p outPath with @p kData; @p sealNonce set: seal with it (the nonce
    /// written first), else open (the nonce read from the file). False: busy with @p outPath.
    bool enqueue(const QString& inPath, const QString& outPath, const ::ed3k::Key32& kData, const std::optional<Nonce>& sealNonce);
    /// A job ended (@p ok false: the .part is removed).
    virtual void jobDone(const QString& outPath, bool ok) = 0;

private:
    struct Job;

    void schedule();
    void step();
    /// false: the job ended (finished or failed)
    bool run(Job& job);

    std::deque<std::unique_ptr<Job>> m_jobs;
    bool m_scheduled = false;
};

class Ed3kSealedOpener : public Ed3kSealedCipher {
    Q_OBJECT

public:
    explicit Ed3kSealedOpener(QObject* parent = nullptr);
    ~Ed3kSealedOpener() override;

    /// Opens @p sealedPath into @p plainPath with @p kData. False: that plain file is opened
    /// already, or is there (not overwritten).
    bool open(const QString& sealedPath, const QString& plainPath, const ::ed3k::Key32& kData);

signals:
    /// @p ok false: unreadable, short, or not written (the .part is removed).
    void opened(const QString& plainPath, bool ok);

protected:
    void jobDone(const QString& outPath, bool ok) override { emit opened(outPath, ok); }
};

/// Seals plain files of a set authored here (subscription.md §8.1: a new member with the
/// epoch's k_data; hidden-set.md §4.2: with the link's k_data).
class Ed3kSealedSealer : public Ed3kSealedCipher {
    Q_OBJECT

public:
    explicit Ed3kSealedSealer(QObject* parent = nullptr);
    ~Ed3kSealedSealer() override;

    /// Seals @p plainPath into @p sealedPath with @p kData and @p nonce (12 random bytes, one
    /// per member: never reuse one with the same key). False: busy with that path.
    bool seal(const QString& plainPath, const QString& sealedPath, const ::ed3k::Key32& kData, const Nonce& nonce);

signals:
    /// @p ok false: unreadable, too large (§4.2: 256 GiB) or not written.
    void sealed(const QString& sealedPath, bool ok);

protected:
    void jobDone(const QString& outPath, bool ok) override { emit sealed(outPath, ok); }
};

} // namespace eMule::ed3k
