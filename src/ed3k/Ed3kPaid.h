#pragma once

/// @file Ed3kPaid.h
/// @brief Paid subscriptions, the buyer's side (subscription.md §5, §6, §9): the paid scopes
/// followed (a `paid=1` set link, a paid catalogue's manifest), their offers (price, terms),
/// buying a pass and its renewal, the anonymous warning, share reports.
///
/// - follow(): paidFollow (or paidAnonFollow); a scope without an offer asks pass holders for
///   one (paidWantKeys: KeyRequest without a pass, §6.2 `KEY_NO_PASS` gives the offer).
/// - buy(): a new holder key per subscription (§5.1; kept for renewals), the pay link
///   `<pay_url>?scope=…&holder=…` (§9.1 step 2) handed back for the browser, `pass_url`
///   polled by the core (ed3k_pass_poll / _anon; every 15 s for 30 min, then at each start
///   until 7 days passed). PASS: the pass is set (paidSetPass), keys are asked for, the
///   renewal is polled in the last 48 h (§9.2). PASS_ENDED: refused (410) or given up.
/// - §5.1: before buying for the anonymous key (and on a resumed poll or renewal of one), the
///   core says ANON_WARN / DENIED: anonWarning() → the user is told the seller sees an
///   address → confirmAnon() (paidAnonConfirm, ED3K_STATE_PAID saved) → the poll goes ahead.
/// - §5.3: share reports on by default, the user's switch (setReports, ed3k_pass_set_reports).
/// - Key (hops.md §1): a scope bought for the anonymous key runs on the anonymous node
///   (paidAnon*: follow, unfollow, want/request keys), its Paid* / PaidAnonWarn events come
///   keyed (keyedEventPolled); it waits while hiding is off and is followed again at its start.
/// - Persisted: Config/ed3k/paid.json (scopes, offers, passes, polls; owner-only) and the
///   holder seeds in Config/ed3k/paid/<scope hex>.holder (Secret files, 0600, never logged).
///
/// was: ToDo: selling (offers, seasons, signing passes: ed3k_paid_publish*, _new_season) is not
/// offered; only buying. (done: Ed3kPaidSeller, the seller's side)
/// was: ToDo: `ed3k:pass` links (§9.3 by hand, vouchers) are not taken. (done: addPassLink;
/// a voucher's key is a holder file like a bought one, its pass taken once: `vouchers` in
/// paid.json; a pass for a scope not followed yet waits there until it is)
/// - §5.1 (ruling #10): a pass of more than one scope is not shown before the user's word
///   (State::ScopesWarn, scopesWarning → answerScopes); declined: kept unused, asked again
///   from the UI; the scopes taken are kept, a renewal of the same scopes asks no more.
///   Batch 15 #9: every set of scopes accepted is kept (paid.json `scopeSets`, for all
///   subscriptions); a pass whose scopes are a subset of one asks nothing, a new combination asks.
/// - Batch 15 #10 (§9.3): a pass is checked per scope once its publisher (and, issuer-signed,
///   its offer) is known: paidSetPass NOT_FOUND = not checked yet (State::PassUnchecked, set
///   again when an offer comes: setOffer, PAID_OFFER), another error = fails there
///   (State::PassFailed, not used). No keys asked for, no renewal, until checked.
/// - Batch 15 #11 (§9.2): a pass of an `ed3k:pass` link or voucher is by hand: not renewed
///   over pass_url by itself; renewNow() polls pass_url on the user's word.
/// - Batch 15 #15: an offer without price is a sale stopped: buy() says no longer sold.
/// - Offers: from a catalogue's manifest, the pass answer, and the core (paidOffer on
///   PAID_OFFER: a hidden set's from a pass holder's KEY_NO_PASS answer, §6.2).
/// was: ToDo: a paid live channel (subscription.md §7) is not followed here. (done:
/// followChannel from Ed3kLive while watched; the live mesh asks for keys itself)

#include "Ed3kFfi.h"

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

/// An Offer read for showing (subscription.md §3); the core checked its signature.
struct Ed3kPaidOffer {
    QByteArray scope;
    std::uint64_t issued = 0;
    std::uint64_t t0 = 0;
    std::uint32_t period = 0;
    QString price, payUrl, passUrl, terms, reportUrl;
    std::uint32_t freeAfter = 0;
    std::uint32_t devices = 0;   ///< 0 = 3
    int issuers = 0;

    /// A serialised SignedOffer; nullopt: not one.
    [[nodiscard]] static std::optional<Ed3kPaidOffer> parse(const QByteArray& signedOffer);
};

/// A Pass read for showing (§5).
struct Ed3kPaidPass {
    std::vector<QByteArray> scopes;
    QByteArray holder;
    std::uint64_t until = 0;
    std::uint64_t issued = 0;
    bool relay = false;

    [[nodiscard]] static std::optional<Ed3kPaidPass> parse(const QByteArray& signedPass);
};

class Ed3kPaid : public QObject {
    Q_OBJECT

public:
    /// subscription.md §2
    enum class Kind : int { Channel = 1, Set = 2, HiddenSet = 3 };
    enum class State : int {
        NoPass = 0,       ///< none bought (or an offer only)
        Buying = 1,       ///< pay_url opened, pass_url polled
        Held = 2,         ///< a pass held (until)
        Refused = 3,      ///< 410: no pass will come for this holder key
        GaveUp = 4,       ///< no answer other than 404 within 7 days, or a URL refused
        AnonWarning = 5,  ///< waits for the user's word on the anonymous key (§5.1)
        ScopesWarn = 6,   ///< a pass of several scopes waits for the user's word (§5.1); declined: unused
        PassUnchecked = 7,   ///< a pass held, not checked for this scope yet (§9.3: publisher/offer unknown)
        PassFailed = 8,   ///< the pass fails for this scope (§9.3): not used here
    };

    struct Info {
        QString scope;            ///< 32 hex digits
        Kind kind = Kind::HiddenSet;
        QString title;
        bool anon = false;        ///< the anonymous key holds (or buys) the pass
        std::optional<Ed3kPaidOffer> offer;
        State state = State::NoPass;
        std::uint64_t until = 0;  ///< of the pass held, Unix s; 0 none
        bool renewing = false;    ///< §9.2 polled in the last 48 h
        bool renewalEnded = false;   ///< the seller answered 410 to a renewal
        std::uint64_t newestEpoch = 0;   ///< of the keys held; 0 none
        bool keys = false;
        int passScopes = 0;       ///< scopes of the pass waiting (ScopesWarn), else of the one held
        bool scopesDeclined = false;   ///< ScopesWarn: the user said no (asked again from the UI)
        bool byHand = false;      ///< §9.2: a link/voucher pass, renewed only by renewNow()
        bool renewable = false;   ///< a pass held and a pass_url in the offer: renewNow() can poll
    };
    /// What addPassLink took.
    struct PassTaken {
        QString scope;            ///< of a followed subscription when the pass covers one
        QString title;
        int scopes = 1;
        bool voucher = false;
        bool waits = false;       ///< ScopesWarn: ask the user, then answerScopes
    };
    /// The publisher and scope of a paid `ed3k:live` link (§7: `paid=1`).
    struct ChannelLink {
        ::ed3k::Key32 key{};
        ::ed3k::Scope scope;
    };

    /// @p configDir: Config/ (ed3k/paid.json and ed3k/paid/ below it), loaded here.
    Ed3kPaid(Ed3kSession& session, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kPaid() override;

    /// Follows a paid scope of @p publisher (the set or channel key); kept across restarts.
    void follow(const ::ed3k::Key32& publisher, const ::ed3k::Scope& scope, Kind kind, const QString& title);
    /// Stops following (the pass and holder key are kept: a renewal still needs them).
    void unfollow(const ::ed3k::Scope& scope);
    /// The offer of a followed scope (from a manifest, a pass answer): handed to the core.
    bool setOffer(const ::ed3k::Scope& scope, const QByteArray& signedOffer);

    /// §9.1: makes the holder key, starts polling `pass_url`; the pay link to open in the
    /// browser; empty when the user must be told first (anon, §5.1: anonWarning, then
    /// confirmAnon gives the link). The error is user-visible.
    std::expected<QString, QString> buy(const QString& scopeHex, bool anon = false);
    /// §5.1: the user was told the seller sees an address: the waiting poll goes ahead. The
    /// pay link when it was a purchase (open it now), else empty; nullopt: nothing waits.
    std::optional<QString> confirmAnon(const QString& scopeHex);
    /// §9.3: an `ed3k:pass` link (a credential: never logged). Without `k` the holder key must
    /// be one of ours; with `k` (a voucher) it is stored like a bought one and the pass taken
    /// once. The error is user-visible.
    std::expected<PassTaken, QString> addPassLink(const QString& link);
    /// §5.1: the user's word on a pass of several scopes (every subscription it came to).
    /// false: nothing waits.
    bool answerScopes(const QString& scopeHex, bool use);
    /// §9.2 (#11): polls pass_url of the offer for the pass held now (a by-hand one too: the
    /// user asked; it shows an address to the seller). The error is user-visible.
    std::expected<void, QString> renewNow(const QString& scopeHex);
    /// §7: a paid channel watched (Ed3kLive); followed while watched only, under the key the
    /// stream is watched under (@p use; hops.md §1).
    void followChannel(const ::ed3k::Key32& key, const ::ed3k::Scope& scope, const QString& title,
                       ::ed3k::KeyKind use = ::ed3k::KeyKind::Node);
    void leaveChannel(const ::ed3k::Scope& scope);
    /// nullopt: no paid live link (no `paid=1`, or not one).
    [[nodiscard]] static std::optional<ChannelLink> channelOfLink(const QString& link);
    /// §5.3: share reports on or off (kept).
    void setReports(bool on);
    [[nodiscard]] bool reports() const { return m_reports; }

    [[nodiscard]] std::vector<Info> list() const;
    [[nodiscard]] std::optional<Info> info(const QString& scopeHex) const;
    [[nodiscard]] bool holdsPass(const ::ed3k::Scope& scope) const;

    /// Asks @p node for the keys (tests: no Kad; the core asks by itself otherwise).
    bool requestKeys(const QString& scopeHex, const ::ed3k::Source& node);

    [[nodiscard]] static QString hexOf(const ::ed3k::Scope& scope);
    [[nodiscard]] static std::optional<::ed3k::Scope> scopeOf(const QString& hex);
    /// `<url>?scope=<base32>&holder=<base32>` (`&` when it has a query), §9.1.
    [[nodiscard]] static QString withQuery(const QString& url, const ::ed3k::Scope& scope, const ::ed3k::Key32& holder);
    [[nodiscard]] QString statePath() const;

signals:
    void changed();
    /// Keys of these epochs are held now (the set may open).
    void keysArrived(const ::ed3k::Scope& scope);
    /// §5.1: tell the user that buying (polling) for the anonymous key shows an address to the
    /// seller, then confirmAnon().
    void anonWarning(const QString& scopeHex, const QString& title);
    /// A pass came (or a renewal).
    void passArrived(const QString& scopeHex, const QString& title, quint64 until);
    /// §5.1: a bought pass covers @p others more scopes: ask before it is shown (answerScopes).
    void scopesWarning(const QString& scopeHex, const QString& title, int others);

private:
    /// §9.3 (#10): what paidSetPass said of the pass held, per scope
    enum class Check : int { Unchecked = 0, Ok = 1, Failed = 2 };
    struct Sub {
        ::ed3k::Scope scope;
        ::ed3k::Key32 publisher{};
        Kind kind = Kind::HiddenSet;
        QString title;
        bool anon = false;
        bool followed = false;    ///< by the user (else kept for its pass only)
        QByteArray offer;
        QByteArray pass;
        std::optional<::ed3k::Key32> holder;   ///< public; the seed is a file
        std::uint64_t until = 0;
        std::uint64_t began = 0;  ///< Unix s: pay_url opened
        State state = State::NoPass;
        bool renewing = false;
        bool renewalEnded = false;
        std::uint64_t newestEpoch = 0;
        bool keys = false;
        bool onEngine = false;
        bool anonWait = false;    ///< §5.1: the core waits for the user's word
        bool anonResume = false;  ///< the poll that waits is a resumed one
        bool known = true;        ///< publisher known (false: a pass link's scope not followed yet)
        QByteArray waitingPass;   ///< §5.1: several scopes, not shown before the user's word
        std::uint64_t waitingUntil = 0;
        bool scopesDeclined = false;
        QStringList scopesOk;     ///< was: scopes (hex) the user took a pass of (now m_scopeSets; read for old files)
        bool watching = false;    ///< a channel: watched now (Ed3kLive)
        bool byHand = false;      ///< §9.2: the pass held came by link/voucher (#11)
        bool waitingByHand = false;   ///< of waitingPass
        Check check = Check::Unchecked;   ///< §9.3: the pass held for this scope (#10)
        bool renewAsked = false;  ///< renewNow() waits for the anonymous warning
    };

    // was: void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onEventPolled(const ::ed3k::Event& event, ::ed3k::KeyKind key, bool* handled);
    void onSessionStatus();
    void attach(Sub& s);
    /// Hiding started (true) or stopped: the anonymous scopes follow it.
    void hideChanged(bool hiding);
    /// The scope off the node it is followed on (paidUnfollow / paidAnonUnfollow).
    void detachSub(Sub& s);
    /// paidWantKeys under the scope's key.
    void wantKeys(Sub& s);
    /// false: refused (the error), or waits for the user (anonWait)
    bool startPoll(Sub& s, bool resume, QString* error = nullptr);
    void startRenewal(Sub& s);
    void onPass(Sub& s, const ::ed3k::event::PassBought& p);
    /// The pass to every scope it covers (new subscriptions not followed yet for the others),
    /// the holder seed copied where needed; @p first takes it in any case. True: it waits for
    /// the user's word (§5.1).
    bool spreadPass(Sub& first, const QByteArray& pass, std::uint64_t until, const std::vector<::ed3k::Scope>& scopes,
                    const ::ed3k::Key32& holder, const ::ed3k::Key32& seed, bool byHand = false);
    /// a pass to @p s: used, or kept for the user's word; true: waits. @p byHand: a link/voucher's
    bool takePass(Sub& s, const QByteArray& pass, std::uint64_t until, const std::vector<::ed3k::Scope>& scopes, bool byHand = false);
    void usePass(Sub& s, const QByteArray& pass, std::uint64_t until);
    /// §9.3 (#10): the pass held set on the engine; checked: keys and renewal (not while
    /// attaching: attach() does those)
    void checkPass(Sub& s, bool attaching = false);
    /// §5.1 (#9): @p scopes within a set the user accepted (one scope: always)
    [[nodiscard]] bool scopesAccepted(const std::vector<::ed3k::Scope>& scopes) const;
    void acceptScopes(const std::vector<::ed3k::Scope>& scopes);
    /// the pay link of @p s (§9.1 step 2); empty without a pay_url
    [[nodiscard]] QString payLinkOf(const Sub& s) const;
    /// was: void; now what the core said of the pass (§9.3)
    Check setPassOnEngine(Sub& s);
    /// the offer the core holds, when newer (true); a pass not checked yet is set again
    bool readOffer(Sub& s);
    Sub* find(const ::ed3k::Scope& scope);
    const Sub* find(const ::ed3k::Scope& scope) const;
    /// find() when the scope runs under @p key (an event of that node); else null.
    Sub* findKeyed(const ::ed3k::Scope& scope, ::ed3k::KeyKind key);
    [[nodiscard]] Info infoOf(const Sub& s) const;
    [[nodiscard]] QString holderFile(const Sub& s) const;
    [[nodiscard]] QString paidDir() const;
    void markChanged();
    void load();
    void save() const;

    Ed3kSession& m_session;
    QString m_configDir;
    std::map<::ed3k::Scope, Sub> m_subs;
    /// §9.3: SHA-256 (hex) of the passes of vouchers taken: each once
    QSet<QString> m_vouchers;
    /// §5.1 (#9): every set of scopes (hex, sorted) the user accepted a pass of
    std::vector<QStringList> m_scopeSets;
    bool m_reports = true;
    bool m_attached = false;
    bool m_changePending = false;
};

} // namespace eMule::ed3k
