#pragma once

/// @file Ed3kLocalScan.h
/// @brief #224 folder scan (BiglyBT's "Locate Files", docs/biglybt-locate-files.md): the
/// folders the user picks are walked for files of the size of an open ed3k download of any
/// network; only those are hashed (BLAKE3), and one whose root matches is a found copy
/// (Ed3kJoinBase::addFoundCopy): every join copies from it, the downloads of it start now.
/// The root is the proof (BiglyBT: piece hashes); the copy is verified again as it arrives.

#include "Ed3kFfi.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

class Ed3kSession;

class Ed3kLocalScan : public QObject {
    Q_OBJECT

public:
    struct Result {
        int files = 0;        ///< files walked
        int candidates = 0;   ///< of a wanted size
        int hashed = 0;
        int matched = 0;      ///< roots found
        QStringList paths;    ///< the files found
        bool cancelled = false;
    };
    using Wanted = std::vector<std::pair<ffi::Root, std::uint64_t>>;

    explicit Ed3kLocalScan(Ed3kSession& session, QObject* parent = nullptr);
    ~Ed3kLocalScan() override;

    /// Scans @p folders (recursively) on a worker for the open downloads of the session
    /// (Ed3kJoinBase::wantedFiles) and @p extra roots; roots with a local copy already are
    /// left out. false: running already, or nothing to look for (finished() all the same).
    bool start(const QStringList& folders, const Wanted& extra = {});
    void cancel();
    [[nodiscard]] bool running() const { return m_running; }

    /// The walk and hashes alone (blocking; tests, the worker): every file under @p folders
    /// with a size in @p wanted hashed, @p found(root, path, size, mtimeMs) for each match.
    /// Files in the folder most candidates are in go first (one release over scattered copies).
    static Result scan(const QStringList& folders, const Wanted& wanted, const std::atomic<bool>& cancel,
                       const std::function<void(const ffi::Root&, const QString&, std::uint64_t, qint64)>& found);

signals:
    void finished(const eMule::ed3k::Ed3kLocalScan::Result& result);

private:
    Ed3kSession& m_session;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    bool m_running = false;
};

} // namespace eMule::ed3k
