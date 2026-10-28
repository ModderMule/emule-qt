#pragma once

/// @file Ed3kNames.h
/// @brief Readable names (name.md) in the daemon: the host duties of the core's names and
/// what the local website server (Ed3kSiteServer) needs of the node.
///
/// - DNS (§5.1): `NameDns` → a TXT lookup (QDnsLookup, 10 s; injectable for tests) →
///   nameDnsAnswer / nameDnsFailed. `secure` is always false: QDnsLookup's AD bit comes from
///   a resolver this host does not validate, so it is no DNSSEC proof (§5.3).
/// - `ed3k.json` (§5.2) goes out as an ordinary HttpRequest (Ed3kHttp), nothing here.
/// - Name lists (§4.5): `NameListFetch` → the list file fetched into memory (the base's
///   fetchBlob, at most 4 MiB) → nameListFile; list 0 (a friend's list) the same, silently.
/// - Resolving what the user opened (resolve, open); `NameResolved` / `NameFailed` → signals.
/// - Pins, petnames (mirrored here: the core lists none), followed lists (kept here with
///   their links and labels, the core keeps the order).
/// - Websites (§6): sets followed for the site server (setFollow, its SetManifest kept in
///   memory) and their members fetched into memory (cap per member and in all), shared
///   while kept (§6.4). Manifests and members are also kept on disk across restarts
///   (Ed3kSiteCache, 2 GiB, LRU); one read back from disk is shared again from memory.
///
/// Petnames and pins never leave the node but as §4.3 says; nothing here is a secret.

#include "Ed3kJoinBase.h"
#include "Ed3kSiteCache.h"

#include <QByteArray>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace eMule::ed3k {

class Ed3kSession;

class Ed3kNames : public Ed3kJoinBase {
    Q_OBJECT

public:
    /// What a TXT question gave: one record per entry, its strings joined (§5.1).
    struct DnsAnswer {
        std::vector<std::string> records;
        std::uint32_t ttl = 0;
        bool secure = false;
    };
    /// done(nullopt): no answer. Called at most once, on this thread.
    using DnsDone = std::function<void(std::optional<DnsAnswer>)>;
    using DnsResolver = std::function<void(const QString& qname, DnsDone done)>;

    /// How a name was resolved (ED3K_NAME_HOW_*, §4.4).
    struct How {
        int how = 0;
        std::uint64_t a = 0;
        std::uint32_t b = 0;
        QString title;
    };
    struct Seen {
        ::ed3k::Key32 zone{};
        How how;
    };
    struct Resolved {
        std::uint64_t id = 0;
        QString name;
        ::ed3k::Key32 zone{};
        QString label;
        QStringList links;
        How how;
        std::vector<Seen> warnings;
        std::vector<::ed3k::Key32> ties;
        bool confirm = false;
        bool webDisagree = false;
    };
    struct Pin {
        QString name;
        ::ed3k::Key32 zone{};
        std::uint64_t since = 0;
        How how;
        bool vouched = false;
        QString link;   ///< of a web name
    };
    struct Petname {
        QString text;
        ::ed3k::Key32 zone{};
        bool shareable = false;
    };
    struct List {
        std::uint32_t id = 0;
        QString link;
        QString label;
        std::uint64_t entries = 0;   ///< names of the version in effect; 0 none yet
        bool refused = false;        ///< a list file of it was refused
    };
    /// A set link's key and id (file-set.md §5.4), and its `path`.
    struct SetRef {
        ::ed3k::Key32 key{};
        QByteArray id;
        QString path;
    };

    /// A member fetched for a website at most (§6.3: members are fetched whole here).
    static constexpr std::uint64_t kSiteMemberMax = 64ULL * 1024 * 1024;
    /// What the website members kept in memory take at most; the oldest go first.
    static constexpr std::uint64_t kSiteCacheMax = 256ULL * 1024 * 1024;
    /// What the manifests and members kept on disk take at most (§6.4), LRU.
    static constexpr std::uint64_t kSiteDiskMax = Ed3kSiteCache::kDefaultMax;
    /// A name list file at most (§4.5).
    static constexpr std::uint64_t kListFileMax = 4ULL * 1024 * 1024;

    Ed3kNames(Ed3kSession& session, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kNames() override;

    void start() override;
    void stop() override;

    /// The TXT lookup; null restores QDnsLookup (tests inject a fake).
    void setDnsResolver(DnsResolver resolver);
    /// QDnsLookup TXT with a 10 s timeout.
    [[nodiscard]] static DnsResolver systemResolver();

    [[nodiscard]] bool isRunning() const;

    /// nameResolve: an ed3k:name link, a petname, alice, shop.alice.ed3k, a domain or a
    /// handle (only what the user opened, §5.3). resolved() or failed() follows with the id.
    ::ed3k::Result<std::uint64_t> resolve(const QString& text);
    /// The user opens @p id: pinned (§4.4). The resolution, kept until then.
    ::ed3k::Result<Resolved> open(std::uint64_t id);
    /// A resolution not opened yet; nullopt: none.
    [[nodiscard]] std::optional<Resolved> resolution(std::uint64_t id) const;

    [[nodiscard]] std::vector<Pin> pins();
    ::ed3k::Result<> unpin(const QString& name);
    ::ed3k::Result<> vouch(const QString& name, bool on);
    [[nodiscard]] std::vector<Petname> petnames() const { return m_petnames; }
    ::ed3k::Result<> setPetname(const QString& text, const ::ed3k::Key32& zone, bool shareable);
    ::ed3k::Result<> removePetname(const QString& text);
    /// Followed lists in the user's order.
    [[nodiscard]] std::vector<List> lists() const { return m_lists; }
    ::ed3k::Result<std::uint32_t> followList(const QString& link, const QString& label);
    ::ed3k::Result<> unfollowList(std::uint32_t id);
    ::ed3k::Result<> moveList(std::uint32_t id, std::uint32_t place);

    // -- websites (Ed3kSiteServer) ---------------------------------------

    /// key, id and path of an `ed3k:set` link with `id`; nullopt for anything else.
    [[nodiscard]] static std::optional<SetRef> parseSetLink(const QString& link);
    /// The first `ed3k:set` link of @p links that parses.
    [[nodiscard]] static std::optional<SetRef> firstSet(const QStringList& links);
    /// The newest manifest held of a set; empty: none yet.
    [[nodiscard]] QByteArray siteManifest(const ::ed3k::Key32& key, const QByteArray& id) const;
    /// Whether the set is followed (its manifest is coming).
    [[nodiscard]] bool siteFollowed(const ::ed3k::Key32& key, const QByteArray& id) const;
    /// Follows the set for its website (only for the user, §6.2). false: refused.
    bool followSite(const ::ed3k::Key32& key, const QByteArray& id);
    /// A manifest held of a set (tests; a version kept): siteChanged() follows.
    void setSiteManifest(const ::ed3k::Key32& key, const QByteArray& id, const QByteArray& manifest);
    /// The bytes of a website member when kept (in memory, else on disk); nullopt: not here.
    [[nodiscard]] std::optional<QByteArray> siteMember(const ::ed3k::Root& root);
    /// @p len bytes of a member at @p offset from verified blocks only (§6.3 ranges): the member
    /// kept, or the blocks of its fetch in memory there so far; nullopt: not (all) there yet.
    [[nodiscard]] std::optional<QByteArray> siteMemberRange(const ::ed3k::Root& root, std::uint64_t offset, std::uint64_t len);
    /// The disk cache of the websites (tests set a small cap).
    [[nodiscard]] Ed3kSiteCache& siteCache() { return *m_cache; }
    /// Fetches a member into memory (siteMemberReady() when it is in). false: above the cap,
    /// not running, or refused.
    bool fetchSiteMember(const ::ed3k::Root& root, std::uint64_t size);
    /// A source of a member being fetched (tests: no Kad search).
    void addSiteSource(const ::ed3k::Root& root, const ::ed3k::Source& source) { addSource(root, source); }

    [[nodiscard]] static QByteArray toBytes(std::span<const std::uint8_t> bytes);
    [[nodiscard]] static std::optional<::ed3k::Key32> keyFrom(const QByteArray& bytes);
    /// A zone or key as 64 hex digits or 52 base32 characters.
    [[nodiscard]] static std::optional<::ed3k::Key32> keyFromText(const QString& text);

signals:
    void resolved(const eMule::ed3k::Ed3kNames::Resolved& r);
    /// @p why ED3K_NAME_WHY_*.
    void failed(quint64 id, const QString& name, int why, const QList<QByteArray>& ties);
    /// Pins, petnames or lists changed.
    void namesChanged();
    /// A friend names the list it publishes (§4.5): offer to follow it.
    void friendListOffered(const QByteArray& node, const QString& link);
    /// A manifest of a website set arrived.
    void siteChanged(const QByteArray& key, const QByteArray& id);
    /// A website member is in (siteMember()).
    void siteMemberReady(const QByteArray& root);

protected:
    [[nodiscard]] std::vector<std::uint64_t> wantedBlocks(const Download& d) const override;
    void onFileBlock(const Download& d, const ::ed3k::event::Block& b) override;
    void onBlobFetched(const QString& owner, int member, const ::ed3k::Root& root, const QByteArray& data) override;
    [[nodiscard]] std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override;
    [[nodiscard]] bool hasOwner(const QString& owner) const override;
    void saveOwner(const QString& owner) override;

private:
    struct Site {
        ::ed3k::Key32 key{};
        QByteArray id;
        std::optional<::ed3k::Topic> topic;
        QByteArray manifest;
    };

    void onNameEvent(const ::ed3k::Event& event, bool* handled);
    void onDns(const ::ed3k::event::NameDns& q);
    void onResolved(const ::ed3k::event::NameResolved& r);
    void onListFetch(const ::ed3k::event::NameListFetch& f);
    void onSiteManifest(const ::ed3k::Topic& topic, const ::ed3k::Bytes& file);
    /// Keeps a member in memory (and on disk unless @p fromDisk) and shares it.
    void keepMember(const ::ed3k::Root& root, const QByteArray& data, bool fromDisk = false);
    [[nodiscard]] static QString siteKey(const ::ed3k::Key32& key, const QByteArray& id);
    void loadLocal();
    void saveLists() const;
    void savePetnames() const;
    [[nodiscard]] QString localPath(const QString& name) const;

    DnsResolver m_dns;
    /// resolutions not opened yet, by id (dropped when opened or at stop)
    std::map<std::uint64_t, Resolved> m_resolved;
    std::vector<Petname> m_petnames;
    std::vector<List> m_lists;
    /// websites by siteKey(); followed topics → that key
    std::map<QString, Site> m_sites;
    std::map<::ed3k::Topic, QString> m_siteTopics;
    /// website members in memory, oldest first
    std::list<std::pair<::ed3k::Root, QByteArray>> m_members;
    std::uint64_t m_memberBytes = 0;
    /// manifests and members across restarts (§6.4)
    std::unique_ptr<Ed3kSiteCache> m_cache;
    /// lookups of this run (the guard of their late answers)
    std::shared_ptr<int> m_dnsGen;
};

} // namespace eMule::ed3k
