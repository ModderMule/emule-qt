#pragma once

/// @file Ed3kSets.h
/// @brief Followed file sets (file-set.md §2.3, §5.3; hidden-set.md; multi-author-set.md):
/// the user's subscriptions to sets that change, their members downloaded through
/// Ed3kDownloads into <incoming>/<set name>/<member path>.
///
/// - follow(link): an `ed3k:set` link with `id` (setFollow, the version held handed in after a
///   restart), with `s`/`paid=1` (setFollowHidden; the link is a credential: kept owner-only,
///   never logged), with `b3`+`sz` (setFollowPinned, chaintube.md §3.2: no newer head).
/// - The engine follows with SetPolicy::Ask: the manifest events only. Which members and
///   when is decided here (§2.3: the user chooses members before downloads start; §5.3 "ask,
///   or download" per set): Policy::Download takes every new version at once, Policy::Ask
///   keeps it pending and tells the GUI (newVersion) until applyVersion().
/// - Members download one at a time per set, in the manifest's order (file-set.md §4.2; the
///   core keeps one file stream per uploader): added paused, resumed in turn.
/// - The selection is the members left out (skipped paths); anything else, new members too,
///   is downloaded. Nothing is deleted or overwritten (§5.3): a member whose file is there
///   with its size is not downloaded again; a dropped member's file stays.
/// - Inner sets (SetInner) and mounts (SetMount, then the mounted set's SetManifest) are
///   shown and downloaded as subfolders; open parts (SetPart) are listed and downloaded on
///   the user's word (setPartDownload → SetMember → a download at `local`).
/// - Only the Set* events of its own topics are taken (Ed3kNames follows website sets, the
///   cores of name and filter lists follow theirs).
/// - The engine follows once the node has contacts (a HEAD search without any waits 5 min).
/// - Persisted: Config/ed3k/sets.json (links, policy, selection, versions; owner-only) and
///   the manifests in Config/ed3k/sets/<root hex>.manifest; followed again at each start.
///
/// ToDo: paid sets (`paid=1`) need a pass (subscription.md), out of scope; followed like a
/// hidden set, nothing opens without the pass.
/// ToDo: a hidden set's version held is not handed in after a restart (the core wants the
/// sealed manifest, SetManifest gives the plain one): the newest is fetched again, members
/// already there are not downloaded again.

#include "Ed3kFfi.h"
#include "Ed3kSetManifest.h"

#include <QByteArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <vector>

namespace eMule::ed3k {

class Ed3kSession;
class Ed3kDownloads;

class Ed3kSets : public QObject {
    Q_OBJECT

public:
    enum class Policy : int { Ask = 0, Download = 1 };
    /// What a member of the shown version is here.
    enum class MemberState : int { Skipped = 0, Waiting = 1, Downloading = 2, Complete = 3, Failed = 4, Paused = 5 };

    struct Member {
        QString path;             ///< in the set, "/" between components
        ::ed3k::Root root;
        std::uint64_t size = 0;
        bool skipped = false;
        MemberState state = MemberState::Waiting;
        std::uint64_t done = 0;
        QString local;            ///< the file here
    };
    struct Part {
        ::ed3k::Key32 author{};
        std::uint64_t seq = 0;
        QString dir;
        QString name;
        int files = 0;
        std::uint64_t size = 0;
    };
    /// A set as the GUI shows it.
    struct Info {
        std::uint32_t id = 0;
        QString link;             ///< empty for a hidden set (a credential)
        QString title;
        QString authorKey;        ///< 64 hex digits; empty: none (hidden)
        bool hidden = false;
        bool pinned = false;
        bool followed = false;    ///< the engine follows it now
        Policy policy = Policy::Ask;
        std::optional<std::uint64_t> seq;          ///< the version applied
        std::optional<std::uint64_t> pendingSeq;   ///< a newer one waiting for the user
        QStringList added, changed, removed;       ///< of the pending version against the applied
        QString dir;
        QString error;            ///< not localized: a log line
        std::vector<Member> members;               ///< of the pending version when there is one
        std::vector<Part> parts;
        std::uint64_t totalSize = 0;
    };

    /// @p configDir: Config/ (ed3k/sets.json and ed3k/sets/ below it), loaded here.
    Ed3kSets(Ed3kSession& session, Ed3kDownloads& downloads, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kSets() override;

    /// The directory below which every set gets its own folder.
    void setIncomingDir(const QString& dir) { m_incomingDir = dir; }

    /// Follows the set of an `ed3k:set` link. The error is user-visible (localized).
    std::expected<std::uint32_t, QString> follow(const QString& link, Policy policy);
    bool unfollow(std::uint32_t id);
    bool setPolicy(std::uint32_t id, Policy policy);
    /// The members left out (paths); the others of the version applied are downloaded.
    bool setSkipped(std::uint32_t id, const QStringList& paths);
    /// Takes the pending version (Ask), or downloads what the selection wants of the applied one.
    bool applyVersion(std::uint32_t id);
    /// Downloads the files of the open parts of @p author (nullopt: of every author).
    bool downloadParts(std::uint32_t id, const std::optional<::ed3k::Key32>& author);

    [[nodiscard]] std::vector<Info> list() const;
    [[nodiscard]] std::optional<Info> info(std::uint32_t id) const;
    [[nodiscard]] bool isRunning() const;

    /// The owner tag of a set's member downloads.
    [[nodiscard]] static QString ownerOf(std::uint32_t id);
    /// `ed3k:set?key=…&id=…` of a set (base32, file-set.md §5.4).
    [[nodiscard]] static QString setLink(const ::ed3k::Key32& key, const QByteArray& id, const QString& name = {});
    [[nodiscard]] QString statePath() const;

signals:
    /// Something of the list changed (a version, a member's state); coalesced.
    void changed();
    /// A newer version of a set with Policy::Ask waits for the user.
    void newVersion(std::uint32_t id, const QString& title, quint64 seq, const QStringList& added, const QStringList& changedPaths,
                    const QStringList& removed);

private:
    struct Link {
        ::ed3k::Key32 key{};
        QByteArray id;
        bool hidden = false;
        std::optional<::ed3k::Root> b3;
        std::uint64_t sz = 0;
        QString name;
    };
    struct Version {
        ::ed3k::Root root;
        std::uint64_t seq = 0;
        QByteArray file;
    };
    struct Set {
        std::uint32_t id = 0;
        QString link;
        Link parsed;
        Policy policy = Policy::Ask;
        QString title;
        QString dir;
        QSet<QString> skipped;
        std::optional<Version> applied;
        std::optional<Version> pending;
        /// inner manifests by root; mounts: path → the mounted version's root
        std::map<::ed3k::Root, QByteArray> inner;
        std::map<QString, ::ed3k::Root> mounts;
        /// members added paused, waiting for their turn (one download per set at a time; only
        /// from a sets.json of before now: members of one source take turns in Ed3kJoinBase)
        std::vector<::ed3k::Root> queued;
        std::vector<Part> parts;
        std::optional<::ed3k::Topic> topic;
        QString error;
    };

    void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onSessionStatus();
    void attach();
    bool followOnEngine(Set& s);
    Set* setOfTopic(const ::ed3k::Topic& topic, QString* mountPath = nullptr);
    Set* find(std::uint32_t id);
    const Set* find(std::uint32_t id) const;
    void onManifest(Set& s, const ::ed3k::Root& root, const ::ed3k::Bytes& file, std::uint64_t seq);
    void onMountManifest(Set& s, const QString& path, const ::ed3k::Root& root, const ::ed3k::Bytes& file);
    void onInner(Set& s, const ::ed3k::Root& root, const ::ed3k::Bytes& file);
    void onMember(Set& s, const ::ed3k::event::SetMember& m);
    /// Downloads every wanted member of the applied version that is not here or on its way.
    void download(Set& s);
    /// A member's download at @p local, started (was: queued paused); false: none needed or refused.
    bool addMember(Set& s, const ::ed3k::Root& root, std::uint64_t size, const QString& local);
    /// The next queued member of @p s when none of its downloads runs.
    void pumpQueue(Set& s);
    void pumpOwnerOf(const ::ed3k::Root& root);
    /// The files of a version, inner sets and mounts flattened (depth-limited).
    [[nodiscard]] std::vector<Ed3kSetEntry> flatten(const Set& s, const QByteArray& file) const;
    void flattenInto(const Set& s, const QByteArray& file, const QString& prefix, int depth, std::vector<Ed3kSetEntry>& out) const;
    [[nodiscard]] Info infoOf(const Set& s) const;
    [[nodiscard]] QString setDir(const Set& s) const;
    [[nodiscard]] static std::optional<Link> parseLink(const QString& link);
    void markChanged();
    void load();
    void save() const;
    void storeManifest(const ::ed3k::Root& root, const QByteArray& file) const;
    [[nodiscard]] QByteArray loadManifest(const ::ed3k::Root& root) const;
    [[nodiscard]] QString setsDir() const;

    Ed3kSession& m_session;
    Ed3kDownloads& m_downloads;
    QString m_configDir;
    QString m_incomingDir;
    std::map<std::uint32_t, Set> m_sets;
    std::uint32_t m_nextId = 1;
    /// mounted topics → (set id, path)
    std::map<::ed3k::Topic, std::pair<std::uint32_t, QString>> m_mountTopics;
    bool m_attached = false;
    bool m_changePending = false;
};

} // namespace eMule::ed3k
