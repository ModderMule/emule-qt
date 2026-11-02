#pragma once

/// @file Ed3kPaidSeller.h
/// @brief Paid subscriptions, the seller's side (subscription.md §3, §4, §5, §7, §9.3): the
/// scopes this node sells (a live channel it broadcasts, a set), their offers and seasons, the
/// node's own pass and the passes issued by hand.
///
/// - sell(): scope (paidScope) → first season (paidChainNew, base = the epoch of now) → offer
///   (paidOfferSign; issuers: the seller key) → on the engine paidFollow, paidSetOffer,
///   paidPublish, and a pass of the node's own (§5: every publisher node holds one; signed by
///   the publisher for a fresh holder key) → paidSetPass. Again at each start (attach()).
/// - A channel: its channel key is the publisher (the seed comes from Ed3kLiveBroadcast);
///   offerOf() hands the offer to the broadcast (ed3k_live_broadcast paid + offer).
/// - A set (scope kind 2, a catalogue): a publisher key and FileSet.id of its own (newSet) or
///   given; offerOf() is what goes into FileSet.offer of each manifest, sealKey() seals the
///   members of a new version (§8.1).
/// - Seasons (§4.2): channel 720 one-hour epochs, set 30 one-day epochs. In a season's last
///   epoch the next is published early (planned, §4.4: travels with its keys, not in an
///   offer); once it begins a new offer names it (§3). After a leak: newSeasonAfterLeak()
///   (ed3k_paid_new_season: base = the next epoch, the offer pushed at once, §4.5).
/// - issuePass(): a pass for a buyer's holder key (an `ed3k:pass` link, §9.3) or a voucher
///   (a fresh holder seed in the link: a credential, never logged, not kept here). Signed by
///   the publisher when every scope has the same one, else by the seller key (an issuer of
///   every offer, §5 "one purchase, several scopes").
/// - Persisted: Config/ed3k/sell/sell.json (defaults, scopes, offers, seasons' chains, the own
///   pass, passes issued; owner-only) and Secret files (0600, never logged): seller.key (the
///   issuer key), <scope>.publisher, <scope>.holder (the own pass), <scope>-<base>.season.
///
/// ToDo: the automatic pass server (pass_url answering §9.1 polls after payment) is not
/// offered; that belongs to the seller's web shop. Passes are issued by hand here.
/// ToDo: §4.4 the season seeds stay on this online node (paidPublish); an offline key store
/// handing over horizon keys (paidPublishHorizon) is not offered.
/// was: Not offered: stopping a sale (t0 and period of a scope never change, §3: the scope
/// stays). (done: stopSale(), batch 15 #15: a newer offer without pay_url, pass_url and price,
/// t0 and period kept; resumeSale() / sell() again: a later offer, same t0 and period, a new
/// season by tick() if the old one ran out. Seasons and the own pass go on while stopped: the
/// passes held still get their keys)
/// was: ToDo: a private paid channel (§7, live.md §10: paidFollowPrivate with `s`) is not sold.
/// (done: sellBroadcast takes it; its secret stays in Config/ed3k/live/<id>.secret
/// (Ed3kLiveBroadcast::channelSecretOf), attach() follows with paidFollowPrivate, linkOf()
/// carries `s`: a credential, never logged nor in sell.json. No channel file: the shop's
/// pass_url must send the offer in Ed3k-Offer, §9.1)
/// was: ToDo: eMuleQt publishes no sets of the user's own yet: a set is sold through IPC or code
/// (newSet, the offer for FileSet.offer, sealKey); no GUI and no manifest written here.
/// (done, batch 15: Ed3kOwnSets ("My ed3k Sets") sells an own set with its key as the
/// publisher, writes each version's manifest with offerOf() and seals new members with
/// sealKey(); SellEd3k kind 2 target = the own set's id)

#include "Ed3kFfi.h"
#include "Ed3kStore.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <vector>

namespace eMule::ed3k {

class Ed3kSession;
class Ed3kLiveBroadcast;

class Ed3kPaidSeller : public QObject {
    Q_OBJECT

public:
    /// subscription.md §2 (kind 3, a hidden set, is not sold here)
    enum class Kind : int { Channel = 1, Set = 2 };

    /// What the buyer is shown (§3) and the season shape; also the defaults of a new offer.
    struct Terms {
        QString price;            ///< for display, ≤ 128 B UTF-8, e.g. "4.99 EUR / month"
        QString payUrl;           ///< https; empty = sold by hand only
        QString passUrl;          ///< https; empty = sold by hand only
        QString reportUrl;        ///< https; empty = no share reports (§5.3)
        QString terms;            ///< for display, ≤ 1024 B UTF-8
        std::uint32_t freeAfter = 0;   ///< epochs; 0 = never released (§4.3)
        std::uint32_t devices = 0;     ///< 1..63; 0 = 3 (§5.3)
        std::uint32_t period = 0;      ///< s, 60..2678400; 0 = the kind's (1 h channel, 1 day set)
        std::uint32_t seasonLength = 0;   ///< epochs; 0 = the kind's (720 / 30)
        std::uint32_t passDays = 30;   ///< the default end of a pass issued by hand
    };

    struct Issued {
        QStringList scopes;       ///< hex
        QString holder;           ///< hex, public
        std::uint64_t issued = 0;
        std::uint64_t until = 0;
        bool voucher = false;     ///< the link carried the holder key (not kept)
        QString note;
        QString link;             ///< `ed3k:pass?p=…` without k; empty for a voucher
    };

    struct Info {
        QString scope;            ///< 32 hex digits
        Kind kind = Kind::Set;
        QString title;
        QString publisher;        ///< 64 hex digits
        QString target;           ///< a channel: its `id` hex; a set: FileSet.id hex
        QString link;             ///< `ed3k:live?…&paid=1` or `ed3k:set?key&id`
        Terms terms;
        std::uint64_t t0 = 0;
        std::uint64_t epoch = 0;  ///< of now
        std::uint64_t seasonBase = 0;   ///< the season the offer names
        std::uint32_t seasonLength = 0;
        std::uint64_t offerIssued = 0;
        bool planned = false;     ///< the next season is published already
        std::uint64_t ownUntil = 0;   ///< the node's own pass
        bool onEngine = false;
        std::vector<Issued> issued;
        bool isPrivate = false;   ///< a private channel (live.md §10): `link` carries `s`
        bool stopped = false;     ///< §3: the sale stopped (the offer has no price nor URLs)
    };

    /// @p configDir: Config/ (ed3k/sell/ below it), loaded here.
    Ed3kPaidSeller(Ed3kSession& session, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kPaidSeller() override;

    /// Sells the scope of @p publisherSeed (a channel key or set author key, a secret) and
    /// @p id (channel id, FileSet.id); the scope (hex). Again for a scope sold: its terms
    /// change (a new offer, same t0 and period: §3). The error is user-visible.
    /// @p isPrivate: a private channel (its secret is the broadcast's, live.md §10).
    std::expected<QString, QString> sell(Kind kind, const Seed& publisherSeed, const QByteArray& id, const QString& title,
                                         const Terms& terms, bool isPrivate = false);
    /// A set of its own: a fresh publisher key and FileSet.id (16 random bytes), then sell().
    std::expected<QString, QString> newSet(const QString& title, const Terms& terms);
    /// New terms of a scope sold (the season and t0 stay); a stopped sale is sold again.
    std::expected<QString, QString> setTerms(const QString& scopeHex, const Terms& terms);
    /// §3 (#15): stops selling: a newer offer without pay_url, pass_url and price; t0, period,
    /// the seasons and the passes held stay. The terms are kept for resumeSale().
    std::expected<void, QString> stopSale(const QString& scopeHex);
    /// Sells a stopped scope again with its terms (setTerms: a later offer, same t0, period).
    std::expected<void, QString> resumeSale(const QString& scopeHex);
    /// The link of a scope sold; a private channel's carries `s` (a credential: never log
    /// it). Empty: not sold.
    [[nodiscard]] QString saleLink(const QString& scopeHex) const;
    /// §4.5: a new season from the next epoch, its offer pushed to the neighbours at once.
    std::expected<void, QString> newSeasonAfterLeak(const QString& scopeHex);
    /// Sells the channel of the broadcast @p streamId (its channel key is the publisher; a
    /// private one with its secret, §7); a broadcast not paid yet starts again as a paid one
    /// (§7: every segment sealed). The scope.
    std::expected<QString, QString> sellBroadcast(Ed3kLiveBroadcast& broadcast, const QString& streamId, const Terms& terms);
    /// Hands the offers of the channels sold to @p broadcast (setOfferSource) for its starts;
    /// a changed offer of an open channel on air (new terms, stop, resume) is a new channel file.
    void serve(Ed3kLiveBroadcast& broadcast);

    /// §9.3: a pass for @p holder (nullopt: a voucher, a fresh holder key in the link) of
    /// @p scopesHex (each one sold here) until @p until (Unix s); the `ed3k:pass` link. A
    /// voucher's link is a credential: shown once, never logged or kept.
    std::expected<QString, QString> issuePass(const QStringList& scopesHex, const std::optional<::ed3k::Key32>& holder, std::uint64_t until,
                                              const QString& note = {});

    /// The serialised SignedOffer of a channel (channel key and id) sold; empty: not sold.
    [[nodiscard]] QByteArray offerOf(const ::ed3k::Key32& channelKey, const QByteArray& id) const;
    /// The serialised SignedOffer of a scope sold; empty: none.
    [[nodiscard]] QByteArray offerOf(const QString& scopeHex) const;
    struct SealKey {
        std::uint64_t epoch = 0;
        ::ed3k::Key32 kData{};    ///< a secret: wipe after use
    };
    /// §8.1: the epoch of now and its k_data, to seal a new member (nonce ‖ ChaCha20); nullopt:
    /// not sold or not running.
    [[nodiscard]] std::optional<SealKey> sealKey(const QString& scopeHex);

    [[nodiscard]] std::vector<Info> list() const;
    [[nodiscard]] std::optional<Info> info(const QString& scopeHex) const;
    [[nodiscard]] Terms defaults() const { return m_defaults; }
    void setDefaults(const Terms& t);
    /// The seller key (an issuer of every offer), hex; made on first use.
    [[nodiscard]] QString sellerKey();

    /// Seasons and the own pass looked after now (a timer does every minute).
    void tick();

    /// A holder key typed by the buyer: base32 (52 characters) or 64 hex digits.
    [[nodiscard]] static std::optional<::ed3k::Key32> parseHolder(const QString& text);
    [[nodiscard]] QString statePath() const;
    [[nodiscard]] QString sellDir() const;

signals:
    void changed();
    /// A season began (planned or after a leak); its offer is the one held now.
    void seasonStarted(const QString& scopeHex, const QString& title, quint64 base);

private:
    struct Season {
        std::uint64_t base = 0;
        std::uint32_t length = 0;
        QByteArray chain;         ///< SignedChain
        bool inOffer = false;     ///< the offer names it (else planned)
    };
    struct Sale {
        ::ed3k::Scope scope{};
        Kind kind = Kind::Set;
        ::ed3k::Key32 publisher{};
        QByteArray id;
        QString title;
        Terms terms;
        std::uint64_t t0 = 0;
        QByteArray offer;         ///< SignedOffer held
        std::uint64_t offerIssued = 0;
        std::vector<Season> seasons;   ///< by base
        QByteArray ownPass;
        std::uint64_t ownUntil = 0;
        std::vector<Issued> issued;
        bool onEngine = false;
        bool isPrivate = false;   ///< a private channel: the secret is the broadcast's file
        bool stopped = false;     ///< §3: offers signed without price, pay_url, pass_url
    };

    void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onSessionStatus();
    void attach(Sale& s);
    /// signs the offer naming @p season with @p s's terms; false: refused (the error)
    bool signOffer(Sale& s, const Season& season, const Secret& pub, QString* error);
    /// a new chain from @p base of @p s, its seed a file; nullopt: failed
    std::optional<Season> makeSeason(Sale& s, std::uint64_t base, std::uint32_t length, const Secret& pub);
    void publishSeason(Sale& s, const Season& season);
    /// the own pass signed again when it runs out within 7 days (or none)
    void renewOwnPass(Sale& s, const Secret& pub);
    void tickOne(Sale& s);
    [[nodiscard]] std::uint64_t epochOf(const Sale& s, std::uint64_t unixS) const;
    [[nodiscard]] const Season* offerSeason(const Sale& s) const;
    [[nodiscard]] std::uint32_t periodOf(const Sale& s) const;
    [[nodiscard]] std::uint32_t lengthOf(const Sale& s) const;
    [[nodiscard]] std::optional<Seed> sellerSeed();
    [[nodiscard]] std::optional<Seed> publisherSeed(const Sale& s) const;
    Sale* find(const QString& scopeHex);
    const Sale* find(const QString& scopeHex) const;
    Sale* find(const ::ed3k::Scope& scope);
    [[nodiscard]] Info infoOf(const Sale& s) const;
    [[nodiscard]] QString linkOf(const Sale& s) const;
    [[nodiscard]] QString fileOf(const Sale& s, const QString& suffix) const;
    void removeFiles(const Sale& s);
    /// the offer held on the engine (when attached) and in the channel file of an open
    /// channel on air
    void pushOffer(Sale& s);
    void markChanged();
    void load();
    void save() const;

    Ed3kSession& m_session;
    QString m_configDir;
    std::map<::ed3k::Scope, Sale> m_sales;
    QPointer<Ed3kLiveBroadcast> m_broadcast;   ///< of serve(): channel files of channels sold
    Terms m_defaults;
    QTimer m_timer;
    bool m_attached = false;
    bool m_changePending = false;
};

} // namespace eMule::ed3k
