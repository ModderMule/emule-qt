#pragma once

/// @file LocalCopyScan.h
/// @brief "Locate Files..." for eD2K downloads (#251): the folders the user picks are
/// walked for files of the size of an unfinished download; only those are hashed (eD2K
/// MD4), and one whose file hash matches is imported part by part (startPartImport), each
/// part checked against its hash like downloaded data. A download still without a hash
/// set takes the copy's: the matching file hash proves it.
/// ed3k downloads: Ed3kLocalScan, the same walk (walkLocalCopies).

#include "files/LocalCopyWalk.h"
#include "utils/Types.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace eMule {

class LocalCopyScan : public QObject {
    Q_OBJECT

public:
    using Md4 = std::array<uint8, 16>;
    struct Wanted {
        Md4 hash{};
        uint64 size = 0;
    };
    struct Hashed {
        Md4 fileHash{};
        std::vector<Md4> partHashes;   ///< as a hash set of the file's size has them
    };

    explicit LocalCopyScan(QObject* parent = nullptr);
    ~LocalCopyScan() override;

    /// Scans @p folders (recursively) on a worker for the unfinished eD2K downloads; a
    /// found copy is imported. false: running already, or nothing to look for
    /// (finished() all the same).
    bool start(const QStringList& folders);
    void cancel();
    [[nodiscard]] bool running() const { return m_running; }

    /// Downloads a copy can go to: unfinished, not importing or completing.
    [[nodiscard]] static std::vector<Wanted> wantedDownloads();
    /// The walk and hashes alone (blocking; tests, the worker): @p found(path, hashed)
    /// for each file whose eD2K hash is one of @p wanted.
    static LocalScanResult scan(const QStringList& folders, const std::vector<Wanted>& wanted,
                                const std::atomic<bool>& cancel,
                                const std::function<void(const QString&, const Hashed&)>& found);
    /// eD2K hash and part hashes of @p path, read as @p size bytes. nullopt: read error,
    /// file changed, or cancelled.
    [[nodiscard]] static std::optional<Hashed> hashFile(const QString& path, uint64 size, const std::atomic<bool>& cancel);
    /// A found copy into its download (core thread): hash set taken when missing, then
    /// the import started. false: no such download, or it takes nothing from there.
    static bool takeCopy(const QString& path, const Hashed& hashed);

signals:
    void finished(const eMule::LocalScanResult& result);

private:
    std::shared_ptr<std::atomic<bool>> m_cancel;
    bool m_running = false;
};

} // namespace eMule
