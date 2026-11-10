#pragma once

/// @file Ed3kLiveFiles.h
/// @brief Channel files of live streams (live.md §1.2) on the ed3k side: an own channel file
/// shared from memory and published under its keywords like any file (Ed3kLiveBroadcast),
/// and found ones fetched into memory (Ed3kSearch rows, Ed3kLive::watchFound).
///
/// - A channel file is at most 64 KiB, begins with the magic `ed3klive` 0x01; its name ends
///   in `.ed3klive`. parse() reads the description for rows and the codec check; the core
///   verifies the signature when it is watched (liveWatchFile).
/// - Fetched files are kept by root (the newest kCacheMax): a watch after a search fetches
///   nothing again.
/// - What is published is published again when ed3k starts again.
/// - Key (hops.md §1): an own channel file goes under the key of its broadcast (recorded in
///   Ed3kHide); one of the anonymous key is shared only while hiding runs. Fetches follow the
///   hiding switch (anonCapable: Ed3kHide::keyFor), like the channel search that found them.

#include "Ed3kJoinBase.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QTimer;

namespace eMule::ed3k {

/// The description in a channel file (live.md §1.2 `Channel`), unverified.
struct Ed3kLiveChannelInfo {
    QByteArray key;           ///< 32 B channel key
    QByteArray id;            ///< 1 to 16 B
    QString title, comment, lang, media, codecs;
    std::uint32_t kbps = 0;
    std::uint32_t segmentMs = 0;
    std::uint64_t created = 0;
};

class Ed3kLiveFiles : public Ed3kJoinBase {
    Q_OBJECT

public:
    /// §1.2: a channel file is at most this large
    static constexpr std::uint64_t kFileMax = 64 * 1024;
    /// the suggested name ends in this (§1.2)
    static inline const QString kSuffix = QStringLiteral(".ed3klive");
    static constexpr std::size_t kCacheMax = 64;
    static constexpr int kFetchTimeoutMs = 60 * 1000;

    /// @p tag names it in log lines ("live", "live files").
    Ed3kLiveFiles(Ed3kSession& session, const QString& configDir, const QString& tag, QObject* parent = nullptr);
    ~Ed3kLiveFiles() override;

    void start() override;
    void stop() override;

    /// The magic `ed3klive` 0x01 (§1.2).
    [[nodiscard]] static bool hasMagic(QByteArrayView file);
    /// The description of a channel file; nullopt: no magic, too large or not framed.
    [[nodiscard]] static std::optional<Ed3kLiveChannelInfo> parse(QByteArrayView file);
    /// "<title>.ed3klive" without characters a file name can't hold.
    [[nodiscard]] static QString fileNameOf(const QString& title);

    /// Shares @p file under its root and publishes it with its keywords (name @p name). Its
    /// root; nullopt: not shared.
    // was: std::optional<::ed3k::Root> publish(const QByteArray& file, const QString& name);
    /// @p key: the key it is shared under (the broadcast's); Anon while not hiding: kept,
    /// shared once hiding runs.
    std::optional<::ed3k::Root> publish(const QByteArray& file, const QString& name,
                                        ::ed3k::KeyKind key = ::ed3k::KeyKind::Node);
    /// No longer shared nor published again.
    void withdraw(const ::ed3k::Root& root);
    [[nodiscard]] bool isPublished(const ::ed3k::Root& root) const { return m_published.contains(root); }

    using Fetched = std::function<void(std::optional<QByteArray> file)>;
    /// Fetches the channel file @p root (@p size) into memory; @p done on this thread with
    /// it, empty when it is no channel file (magic), nullopt after @p timeoutMs or when it
    /// can't be fetched. At once when kept.
    void fetch(const ::ed3k::Root& root, std::uint64_t size, Fetched done, int timeoutMs = kFetchTimeoutMs);
    /// A file fetched before (or published here).
    [[nodiscard]] std::optional<QByteArray> cached(const ::ed3k::Root& root) const;

protected:
    [[nodiscard]] std::vector<std::uint64_t> wantedBlocks(const Download& d) const override;
    void onFileBlock(const Download& d, const ::ed3k::event::Block& b) override;
    void onBlobFetched(const QString& owner, int member, const ::ed3k::Root& root, const QByteArray& data) override;
    [[nodiscard]] std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override;
    [[nodiscard]] bool hasOwner(const QString& owner) const override;
    void saveOwner(const QString& owner) override;
    /// Fetches and own files follow the hiding switch (hops.md §1).
    [[nodiscard]] bool anonCapable() const override { return true; }
    /// Hiding started: the anonymous key's own files are shared again.
    void onHideChanged(bool hiding) override;

private:
    struct Pending {
        std::vector<Fetched> waiting;
        std::unique_ptr<QTimer> timeout;
    };
    struct Published {
        QByteArray file;
        QString name;
        ::ed3k::KeyKind key = ::ed3k::KeyKind::Node;
    };

    /// Gives @p file (nullopt: failed) to everyone waiting for @p root.
    void deliver(const ::ed3k::Root& root, std::optional<QByteArray> file);
    void keep(const ::ed3k::Root& root, const QByteArray& file);
    /// Shares and publishes one file on the engine.
    std::optional<::ed3k::Root> shareOne(const Published& p);

    std::map<::ed3k::Root, Published> m_published;
    std::map<::ed3k::Root, Pending> m_pending;
    std::deque<std::pair<::ed3k::Root, QByteArray>> m_cache;
    int m_nextMember = 0;
};

} // namespace eMule::ed3k
