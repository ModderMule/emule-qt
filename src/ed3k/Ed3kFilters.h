#pragma once

/// @file Ed3kFilters.h
/// @brief Shared filter lists (filter-list.md) in the daemon: the host side of the core's
/// lists and the user's own verdicts.
///
/// - Following (§2): only lists the user chose (filterFollow of an ed3k:set link, §3); the
///   core keeps them in filter.dat (Ed3kSession). `FilterFetch` → the list file fetched into
///   memory (the base's fetchBlob, at most kListFileMax, §2) → filterFile.
///   `FilterUpdated`/`TooLarge`/`Ignored`/`Changed` are logged and change the list.
/// - Mode per list (§3): mark (default), hide, refuse; the own list too (ED3K_FILTER_OWN).
/// - Own verdicts (§2, §3): filterSetOwn; mirrored here (the core lists none) in
///   Config/ed3k/filter-own.dat, not a secret.
/// - judge(): what a search row, a download or a set shows (§3). Nothing is deleted, no
///   download stopped, no connection refused because of a list. A row is judged by its
///   record's publisher and, for a signed manifest known here, by the set's author (§1).
/// - Publishing (§2): the own list as a list file in a signed set (filterOwnWrite →
///   filterOwnManifest → setPublish) under a key of its own (filter-own.key, a secret, not
///   the node key); published again on every change of the own verdicts and at start.

#include "Ed3kJoinBase.h"

#include <QByteArray>
#include <QString>
#include <QTimer>

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace eMule::ed3k {

class Ed3kSession;

class Ed3kFilters : public Ed3kJoinBase {
    Q_OBJECT

public:
    using EntryId = std::array<std::uint8_t, 16>;

    /// A followed list or one opened by hand (filterLists, filterLabel).
    struct List {
        std::uint32_t id = 0;
        QString label;
        bool hide = false;
        bool refuse = false;
        bool byHand = false;
        std::optional<EntryId> set;        ///< the head target of a followed one
        std::optional<std::uint64_t> seq;  ///< the version in effect; nullopt: none yet
        std::uint32_t files = 0;
        std::uint64_t entries = 0;
        std::uint32_t waiting = 0;         ///< list files still wanted
    };
    /// A verdict of the user's own list.
    struct Own {
        ::ed3k::FilterSubject subject = ::ed3k::FilterSubject::File;
        EntryId id{};
        ::ed3k::FilterVerdict verdict = ::ed3k::FilterVerdict::None;
        QString reason;
    };
    /// What a row shows (§3): the core's hide/refuse over every subject asked, the verdict
    /// marked (the own one first, else the first list's), who said it.
    struct Judgement {
        bool hide = false;
        bool refuse = false;
        bool shown = false;   ///< the user chose to show it anyway
        ::ed3k::FilterVerdict verdict = ::ed3k::FilterVerdict::None;
        bool own = false;     ///< the verdict is the user's own
        QString by;           ///< label / title of the list that said it; empty for the own
        QString reason;
        bool disagree = false;   ///< lists disagree (GOOD against the rest): marked, not hidden
    };

    /// A list file at most (§2: 4 MiB initial).
    static constexpr std::uint64_t kListFileMax = 4ULL * 1024 * 1024;

    Ed3kFilters(Ed3kSession& session, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kFilters() override;

    void start() override;
    void stop() override;
    [[nodiscard]] bool isRunning() const;

    [[nodiscard]] std::vector<List> lists();
    /// filterFollow of an ed3k:set link the user chose.
    ::ed3k::Result<std::uint32_t> follow(const QString& link, const QString& label);
    ::ed3k::Result<> unfollow(std::uint32_t id);
    /// A list file opened by hand (§2: no author, no updates; the GUI says so).
    ::ed3k::Result<std::uint32_t> openFile(const QString& label, const QByteArray& file);
    /// id ED3K_FILTER_OWN: the own list.
    ::ed3k::Result<> setMode(std::uint32_t id, bool hide, bool refuse);
    [[nodiscard]] bool ownHide() const { return m_ownHide; }
    [[nodiscard]] bool ownRefuse() const { return m_ownRefuse; }

    [[nodiscard]] std::vector<Own> own() const { return m_own; }
    /// The user's verdict; None takes it back.
    ::ed3k::Result<> setOwn(::ed3k::FilterSubject subject, const EntryId& id, ::ed3k::FilterVerdict verdict,
                            const QString& reason);

    /// The core's judgement of one entry.
    ::ed3k::Result<::ed3k::Engine::FilterJudgement> judgeOne(::ed3k::FilterSubject subject, const EntryId& id);
    /// A search row / download (§3): its root, ed2k MD4 and author (node ID), each optional;
    /// @p setAuthor the author key of the set when the row is a signed manifest known here
    /// (filter-list.md §1, batch 12 ruling #18: either one marks it).
    /// nullopt: nothing judges it (or the node is off).
    [[nodiscard]] std::optional<Judgement> judge(const std::optional<::ed3k::Root>& root,
                                                 const std::optional<EntryId>& ed2k,
                                                 const std::optional<::ed3k::NodeId>& author,
                                                 const std::optional<::ed3k::Key32>& setAuthor = std::nullopt);
    /// "Show anyway" (§3): a local, unpublished choice for the file of @p root, over every
    /// list and the own verdict; @p on false takes it back.
    ::ed3k::Result<> showAnyway(const ::ed3k::Root& root, bool on);

    /// The own list published (§2): its link, title; empty link: not published.
    struct Published {
        QString link;
        QString title;
    };
    [[nodiscard]] Published published() const;
    /// Publishes the own list as a set titled @p title (again on every change); its link.
    std::expected<QString, ::ed3k::Error> publishOwn(const QString& title);
    /// Stops publishing it again (what is out stays until it expires).
    void unpublishOwn();

    /// A source of a list file being fetched (tests: no Kad search).
    void addListSource(const ::ed3k::Root& root, const ::ed3k::Source& source) { addSource(root, source); }
    /// The own verdicts' file in Config/ed3k.
    [[nodiscard]] static QString ownFileName();

signals:
    /// Lists, their versions, modes or the own verdicts changed.
    void filtersChanged();
    /// A list file of a FilterFetch is in and was handed to the core (@p taken: filterFile ok).
    void listFileFetched(quint32 filter, bool taken);

protected:
    [[nodiscard]] std::vector<std::uint64_t> wantedBlocks(const Download& d) const override;
    void onFileBlock(const Download& d, const ::ed3k::event::Block& b) override;
    void onBlobFetched(const QString& owner, int member, const ::ed3k::Root& root, const QByteArray& data) override;
    [[nodiscard]] std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override;
    [[nodiscard]] bool hasOwner(const QString& owner) const override;
    void saveOwner(const QString& owner) override;

private:
    void onFilterEvent(const ::ed3k::Event& event, bool* handled);
    void onFetch(const ::ed3k::event::FilterFetch& f);
    void loadOwn();
    void saveOwn() const;
    [[nodiscard]] QString localPath(const QString& name) const;
    /// Writes, shares and publishes a new version of the own list (m_pubId); false: failed (logged).
    bool republish();
    /// At start: the version kept shared and published again; else a new one.
    void restorePublished();
    /// Shares both files and publishes the manifest under the list key.
    ::ed3k::Result<> shareAndPublish(const QByteArray& list, const QByteArray& manifest);
    void schedulePublish();

    std::vector<Own> m_own;
    bool m_ownHide = false;
    bool m_ownRefuse = false;
    // the own list published (§2): set id, title, the roots shared now
    QByteArray m_pubId;
    QString m_pubTitle;
    std::optional<::ed3k::Root> m_pubList;
    std::optional<::ed3k::Root> m_pubManifest;
    QTimer m_pubTimer;
};

} // namespace eMule::ed3k
