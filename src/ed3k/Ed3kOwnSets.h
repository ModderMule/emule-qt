#pragma once

/// @file Ed3kOwnSets.h
/// @brief The user's own ed3k sets ("My ed3k Sets", file-set.md §1-§5, hidden-set.md,
/// subscription.md §8): a folder or a list of files published as a set that changes, signed
/// by a key of its own.
///
/// - Kinds: open (an ordinary signed set, `ed3k:set?key&id`), hidden (hidden-set.md: the link
///   with `s` is a credential, never logged; members sealed with its k_data, §4.2), paid (a
///   catalogue, subscription.md §8.2: the offer of Ed3kPaidSeller in FileSet.offer, every
///   member sealed with the k_data of the epoch it is published in, §8.1).
/// - A version: members hashed on a worker (hashInBackground), sealed ones sealed first
///   (Ed3kSealedSealer, a random 12-byte nonce each) into <set dir>/.sealed/<wire name>
///   (hidden-set.md §4.3), the manifest written (setManifestWrite: checked as a reader checks
///   it), shared (shareBlob) and published (setPublish with the set key; hidden:
///   hiddenSealManifest → setPublishHidden). Open members: SOURCE and keywords; sealed members
///   and a hidden manifest: SOURCE only, named by their wire name (packmule publishes no MEMBER
///   record for them).
/// - A member carried into a later version keeps its root (and epoch, its sealed file: §8.1
///   "not resealed") while its file is unchanged (size, modification time).
/// - New version (seq + 1): on request (publishNewVersion, retitle), a sold set's newer offer
///   (a stop, new terms, a new season). At start every published set is shared and published
///   again: the version kept when nothing changed (its head re-signed), else a new one.
/// - unpublish(): ed3k_set_unpublish (no keywords, MEMBER records nor 24 h head any more), the
///   files no longer shared; the set stays listed, "Publish New Version" brings it back.
/// - Key (hops.md §1): a set is under the key of its member files (Ed3kHide::keyOf); files of
///   neither key take the set's, a new set's is Ed3kHide::keyForUse(). Members under both
///   keys, or under the other key than the set's, are refused (no version); for the latter the
///   set is offered under the files' key (offerKey, shareUnderKey). State from the core's set
///   calls is the same refusal. Manifest, members and records go under the set's key; one of
///   the anonymous key is published only while hiding runs.
/// - Persisted: Config/ed3k/own-sets.json (owner-only) and Config/ed3k/own-sets/: <id>.key
///   (the set key, a secret), <id>.secret (a hidden set's link secret), <id>.manifest (the
///   version published), <id>/.sealed/ (sealed members).
///
/// ToDo: kind 3 of subscription.md §2 (a paid hidden set, `paid=1`) is not offered.
/// ToDo: members are not watched: a changed folder needs "Publish New Version" (or a restart).

#include "Ed3kJoinBase.h"
#include "Ed3kPaidSeller.h"

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace eMule::ed3k {

class Ed3kSession;
class Ed3kSealedSealer;

class Ed3kOwnSets : public Ed3kJoinBase {
    Q_OBJECT

public:
    /// IPC kinds (1145-1147)
    enum class Kind : int { Open = 1, Hidden = 2, Paid = 3 };

    /// A set as the GUI lists it. `link` is empty for a hidden set (a credential: linkOf()).
    struct Info {
        QString id;               ///< FileSet.id, 32 hex digits
        QString title;
        QString folder;           ///< empty: a list of files
        int files = 0;
        std::uint64_t size = 0;   ///< of the members' plain files
        std::uint64_t seq = 0;    ///< the version published; 0: none yet
        Kind kind = Kind::Open;
        bool published = false;
        bool building = false;    ///< a version is being made
        QString scope;            ///< hex of a paid set's scope; empty: not sold
        bool sold = false;        ///< sold now (not stopped)
        bool stopped = false;
        QString link;             ///< open only
        QString error;            ///< of the last version made, localized
        /// "Node" / "Anonymous" (Ed3kHide::keyName); empty: none chosen yet
        QString key;
        /// a version was refused for files under this key: the set may move there (shareUnderKey)
        QString offerKey;
    };

    /// @p configDir: Config/ (ed3k/own-sets.json and ed3k/own-sets/ below it), loaded here.
    Ed3kOwnSets(Ed3kSession& session, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kOwnSets() override;

    void start() override;
    void stop() override;

    /// Paid sets are sold there (null: none can be sold).
    void setSeller(Ed3kPaidSeller* seller);
    /// Roots another sharer serves (eMule's share list over ed3k): not opened here again.
    void setSharedElsewhere(std::function<bool(const ::ed3k::Root&)> f) { m_sharedElsewhere = std::move(f); }

    /// A new set of @p folder (every file below it) or @p files, published at once (a paid
    /// one once sold: sell()). The id. The error is user-visible.
    std::expected<QString, QString> create(const QString& title, const QString& folder, const QStringList& files, Kind kind);
    /// A new version: the folder scanned again (or the files), seq + 1. A set unpublished is
    /// published again.
    std::expected<void, QString> publishNewVersion(const QString& id);
    std::expected<void, QString> retitle(const QString& id, const QString& title);
    /// Stops publishing (ed3k_set_unpublish) and sharing the files; the set stays listed.
    std::expected<void, QString> unpublish(const QString& id);
    /// Sells the set (Ed3kPaidSeller, kind 2, its key the publisher) with @p terms; a set sold
    /// already: new terms. Its members are sealed in the next version. The scope.
    std::expected<QString, QString> sell(const QString& id, const Ed3kPaidSeller::Terms& terms);
    /// The user agreed to share the set under @p key (hops.md §1, the key its files are under):
    /// taken back under the old key, a new version under @p key.
    std::expected<void, QString> shareUnderKey(const QString& id, ::ed3k::KeyKind key);

    [[nodiscard]] bool has(const QString& id) const;
    [[nodiscard]] std::vector<Info> list() const;
    [[nodiscard]] std::optional<Info> info(const QString& id) const;
    /// The set's link; a hidden set's carries `s` (a credential: never log it). Empty: none.
    [[nodiscard]] QString linkOf(const QString& id) const;
    /// The head target of a published open or paid set (tests).
    [[nodiscard]] std::optional<::ed3k::Topic> headOf(const QString& id) const;
    /// The manifest root of the version published (sealed for a hidden set).
    [[nodiscard]] std::optional<::ed3k::Root> manifestOf(const QString& id) const;
    [[nodiscard]] QString statePath() const;
    /// Config/ed3k/own-sets/<id>: the set's own files (.sealed/ below it).
    [[nodiscard]] QString setDir(const QString& id) const;

signals:
    /// The list, a version or a sale state changed; coalesced.
    void changed();
    /// A version was published (@p ok) or failed (the error in info()).
    void versionDone(const QString& id, quint64 seq, bool ok);

protected:
    [[nodiscard]] std::vector<std::uint64_t> wantedBlocks(const Download& d) const override;
    void onFileBlock(const Download& d, const ::ed3k::event::Block& b) override;
    void onBlobFetched(const QString& owner, int member, const ::ed3k::Root& root, const QByteArray& data) override;
    [[nodiscard]] std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override;
    [[nodiscard]] bool sharedGone(const Shared& s) const override;
    [[nodiscard]] bool hasOwner(const QString& owner) const override;
    void saveOwner(const QString& owner) override;
    /// members and manifests go under the set's key (hops.md §1)
    [[nodiscard]] bool anonCapable() const override { return true; }
    void onHideChanged(bool hiding) override;

private:
    struct Member {
        QString path;             ///< in the set, "/" between components
        QString file;             ///< the plain file here
        qint64 fileSize = -1;
        qint64 mtimeMs = -1;
        ::ed3k::Root root{};      ///< of the shared file (the sealed one when sealed)
        std::uint64_t size = 0;
        std::optional<std::uint64_t> epoch;   ///< paid: sealed with this epoch's key
        bool sealed = false;      ///< hidden: sealed with the link's k_data
        QString sealedPath;       ///< the sealed copy; empty: open
    };
    struct Set {
        QByteArray id;
        QString title;
        QString folder;
        QStringList files;
        Kind kind = Kind::Open;
        std::uint64_t seq = 0;
        std::uint64_t created = 0;
        QString scope;            ///< hex, sold
        QByteArray offer;         ///< in the version published
        bool published = false;
        std::vector<Member> members;
        std::optional<::ed3k::Root> manifest;   ///< shared now (sealed for hidden)
        std::optional<::ed3k::Topic> head;
        QString error;
        /// the key it is published under (hops.md §1); nullopt: none chosen yet
        std::optional<::ed3k::KeyKind> key;
        /// offered after a refusal: the key its files are under (not kept)
        std::optional<::ed3k::KeyKind> offerKey;
    };
    struct Build;

    void onSellerChanged();
    void checkOffers();
    /// Starts a version of @p s: @p restore: the one kept when nothing changed.
    void startBuild(Set& s, bool restore);
    void buildStep(const QString& owner);
    void buildFailed(const QString& owner, const QString& error);
    void finishBuild(const QString& owner);
    /// The manifest of @p b written, shared and published; false: @p error.
    bool publishVersion(Set& s, Build& b, QString* error);
    /// The members of @p b shared (open: SOURCE and keywords; sealed: SOURCE only).
    void shareMembers(Set& s, Build& b);
    /// The key of the version @p b (hops.md §1): that of its files; nullopt: refused, why in
    /// @p error, the key offered in s.offerKey.
    [[nodiscard]] std::optional<::ed3k::KeyKind> versionKey(Set& s, const Build& b, QString* error);
    /// "the files are under @p files, the set under @p set: share the set under @p files?"
    [[nodiscard]] static QString keyRefusal(::ed3k::KeyKind files, ::ed3k::KeyKind set);
    void unshareSet(Set& s);
    /// the files of @p s now: (path in the set, file)
    [[nodiscard]] std::vector<std::pair<QString, QString>> scan(const Set& s) const;
    [[nodiscard]] std::optional<Seed> keyOf(const Set& s) const;
    [[nodiscard]] QString hiddenLink(const Set& s) const;
    [[nodiscard]] Info infoOf(const Set& s) const;
    [[nodiscard]] static QString ownerOf(const QByteArray& id);
    Set* find(const QString& id);
    const Set* find(const QString& id) const;
    Set* findOwner(const QString& owner);
    /// Serves @p path under @p root (sharedRead): a member file
    bool serveFile(const QString& owner, const ::ed3k::Root& root, std::uint64_t size, const QString& name, const QString& path,
                   std::span<const std::byte> hashes);
    void markChanged();
    void load();
    void save() const;
    [[nodiscard]] QString ownDir() const;

    std::map<QByteArray, Set> m_sets;
    std::map<QString, std::unique_ptr<Build>> m_builds;   ///< by owner
    /// shared member files: serve id → (path, size)
    std::map<int, std::pair<QString, std::uint64_t>> m_serve;
    std::map<::ed3k::Root, int> m_serveOf;
    int m_nextServe = 1;
    QPointer<Ed3kPaidSeller> m_seller;
    std::function<bool(const ::ed3k::Root&)> m_sharedElsewhere;
    std::unique_ptr<Ed3kSealedSealer> m_sealer;
    QTimer m_offerTimer;
    bool m_changePending = false;
};

} // namespace eMule::ed3k
