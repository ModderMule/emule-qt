#pragma once

/// @file Ed3kSealedOpener.h
/// @brief Sealed set members opened into plain files (subscription.md §8.1, hidden-set.md
/// §4.2): `nonce(12) ‖ ChaCha20(k_data, nonce, plain)`, plain byte i = sealed byte 12 + i
/// (ed3k_sealed_read_at, any offset). Done in chunks on the event loop: a member may be large.
///
/// - The plain file is written as <plain>.part, renamed when done; an existing plain file is
///   never overwritten (file-set.md §5.3). The sealed file stays (it is shared, §8.1).
/// - k_data is a secret (Secret, wiped with the job), never logged.

#include "Ed3kFfi.h"

#include <QObject>
#include <QString>

#include <deque>
#include <memory>

namespace eMule::ed3k {

class Ed3kSealedOpener : public QObject {
    Q_OBJECT

public:
    /// bytes opened per event loop turn
    static constexpr qint64 kChunk = 4 * 1024 * 1024;
    /// the nonce in front of the cipher
    static constexpr qint64 kNonce = 12;

    explicit Ed3kSealedOpener(QObject* parent = nullptr);
    ~Ed3kSealedOpener() override;

    /// Opens @p sealedPath into @p plainPath with @p kData. False: that plain file is opened
    /// already, or is there (not overwritten).
    bool open(const QString& sealedPath, const QString& plainPath, const ::ed3k::Key32& kData);
    [[nodiscard]] bool busy(const QString& plainPath) const;

signals:
    /// @p ok false: unreadable, short, or not written (the .part is removed).
    void opened(const QString& plainPath, bool ok);

private:
    struct Job;

    void schedule();
    void step();
    /// false: the job ended (finished or failed)
    bool run(Job& job);

    std::deque<std::unique_ptr<Job>> m_jobs;
    bool m_scheduled = false;
};

} // namespace eMule::ed3k
