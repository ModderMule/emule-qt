#pragma once

/// @file Ed3kEd2kJoin.h
/// @brief eD2K compat (ed3k Protocol.md §14.2, §14.4): eD2K downloads fed from ed3k too.
///
/// Every running eD2K part file looks up its ed3k root by the `ed2k` ALIAS (md4 ‖ size,
/// alias.md), downloads the blocks it misses into the part file (verified under BLAKE3 by
/// the engine) and serves the pieces it holds verified. eD2K sources with a derived user
/// hash are dialled over ed3k first (hint, port rule §14.1); eD2K is the fallback.
///
/// Pair trust (§14.4): a (root, ed2k) pair is trusted once a part holding ed3k bytes passes
/// its MD4 hash. A part failing MD4 whose blocks pass under the root (aliasSearchPartFailed),
/// or a FileStatus naming another ed2k hash, makes the pair false: the root is dropped (no
/// peer blamed) and the next candidate tried.

#include "Ed3kJoinBase.h"

#include "app/AppContext.h"
#include "client/ClientStateDefs.h"

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace eMule {
class DownloadQueue;
class PartFile;
class UpDownClient;
} // namespace eMule

namespace eMule::ed3k {

class Ed3kEd2kJoin : public Ed3kJoinBase {
    Q_OBJECT

public:
    enum class Pair : std::uint8_t { Unknown, Trusted, False };
    Q_ENUM(Pair)

    Ed3kEd2kJoin(Ed3kSession& session, DownloadQueue& queue, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kEd2kJoin() override;

    void start() override;
    void stop() override;
    /// Pref ed3kJointDownloads; off: stopped (clients fall back to eD2K).
    void setEnabled(bool on);
    [[nodiscard]] bool isEnabled() const { return m_enabled; }
    /// A root another download of this node holds (plain ed3k downloads): not opened here.
    void setRootTaken(std::function<bool(const ffi::Root&)> taken) { m_rootTaken = std::move(taken); }

    /// AppContext::ed3kHint: a download source about to be asked (§14.2).
    AppContext::Ed3kHint offerHint(UpDownClient& client, bool wait);
    /// AppContext::partVerdict, queued by the daemon: eD2K part @p part of @p md4.
    void onPartVerdict(const QByteArray& md4, std::uint32_t part, bool ok);

    /// The root in use for @p md4; nullopt: none (yet).
    [[nodiscard]] std::optional<ffi::Root> rootOf(const QByteArray& md4) const;
    [[nodiscard]] Pair pairOf(const QByteArray& md4) const;
    /// Bytes written into the part file from ed3k.
    [[nodiscard]] std::uint64_t ed3kBytes(const QByteArray& md4) const;
    /// What File Details shows for a tracked file.
    struct FileInfo {
        std::optional<ffi::Root> root;
        Pair pair = Pair::Unknown;
        bool searching = false;   ///< alias search running, no root yet
        std::uint64_t bytes = 0;
        int sources = 0;          ///< ed3k streams of the root
        int transferring = 0;     ///< of them holding a slot
    };
    /// nullopt: not tracked (paused, stopped, complete, join off).
    [[nodiscard]] std::optional<FileInfo> infoOf(const QByteArray& md4) const;
    /// Gap between alias searches of a file not found (tests shorten it).
    void setSearchGap(int ms) { m_searchGapMs = ms; }
    /// How long an eD2K dial waits for the ed3k answer of a hinted source.
    void setHintTimeout(int ms) { m_hintTimeoutMs = ms; }
    /// Files of the queue taken / dropped, searches and hints due (also on a timer).
    void sync();

signals:
    void pairChanged(const QByteArray& md4, eMule::ed3k::Ed3kEd2kJoin::Pair pair);

protected:
    [[nodiscard]] std::vector<std::uint64_t> wantedBlocks(const Download& d) const override;
    void onFileBlock(const Download& d, const ffi::event::Block& b) override;
    void onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data) override;
    void onDownloadHashes(const Download& d, const ffi::Bytes& blob) override;
    [[nodiscard]] std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override;
    void onOtherEvent(const ffi::Event& event) override;
    void onStreamOpened(const ffi::Root& root, std::uint64_t stream, const ffi::Source& source) override;
    void onStreamStatus(const ffi::event::FileStatus& status) override;
    void onStreamClosed(const ffi::event::StreamClosed& closed) override;
    [[nodiscard]] bool hasOwner(const QString& owner) const override;
    void saveOwner(const QString& owner) override;

private:
    /// A hinted eD2K source: dialled over ed3k (source.hint), its client told the outcome.
    struct Hint {
        ffi::Source source;
        QPointer<UpDownClient> client;
        qint64 deadline = 0;
        std::uint64_t stream = 0;
        bool told = false;      ///< client got Failed / Untried already (eD2K goes on)
        bool answered = false;  ///< usable FileStatus came: Preferred
    };
    struct File {
        QByteArray md4;
        std::uint64_t size = 0;
        QString name;
        std::optional<ffi::Root> root;
        std::optional<std::uint32_t> search;  ///< alias search in use (open while the root is used)
        bool searching = false;        ///< no AliasFound yet
        qint64 nextSearch = 0;
        std::set<ffi::Root> refused;   ///< false pairs and taken roots
        Pair pair = Pair::Unknown;
        std::set<std::uint64_t> blocks;  ///< blocks with bytes from ed3k
        std::set<std::uint64_t> whole;   ///< blocks written whole from ed3k (servable)
        std::uint64_t bytes = 0;
        ffi::Bytes hashes;
        bool shareDirty = false;
        std::vector<Hint> hints;
    };

    void onTick();
    [[nodiscard]] PartFile* partFile(const QByteArray& md4) const;
    [[nodiscard]] static bool eligible(const PartFile* f);
    [[nodiscard]] static QString ownerOf(const QByteArray& md4) { return QString::fromLatin1(md4.toHex()); }
    [[nodiscard]] File* fileOfOwner(const QString& owner);
    [[nodiscard]] const File* fileOfOwner(const QString& owner) const;
    [[nodiscard]] File* fileOfRoot(const ffi::Root& root);
    File* ensureFile(PartFile& f);
    void forget(const QByteArray& md4);
    void startSearch(File& f);
    void onAliasFound(const ffi::event::AliasFound& found);
    /// The next candidate of the file's search; none: search ended, hints told.
    void takeNextCandidate(File& f);
    void openRoot(File& f);
    /// Stops using the root; @p falsePair: never again for this file.
    void dropRoot(File& f, bool falsePair, const QString& why);
    void setPair(File& f, Pair pair);
    /// The client may go on over eD2K (Untried: ed3k asked again later; Failed: never).
    void releaseHint(Hint& h, Ed3kRoute route);
    void releaseHints(File& f);
    void shareVerified(File& f);
    [[nodiscard]] bool blockServable(const File& f, const PartFile& pf, std::uint64_t off) const;
    [[nodiscard]] std::uint64_t blockEnd(const File& f, std::uint64_t off) const;

    DownloadQueue& m_queue;
    bool m_enabled = true;
    std::function<bool(const ffi::Root&)> m_rootTaken;
    std::map<QByteArray, File> m_files;
    std::map<std::uint32_t, QByteArray> m_searches;
    QTimer m_tick;
    int m_searchGapMs = 60 * 60 * 1000;
    int m_hintTimeoutMs = 30 * 1000;
    static constexpr int kMaxSearches = 4;
    static constexpr std::size_t kMaxWanted = 128;
};

} // namespace eMule::ed3k
