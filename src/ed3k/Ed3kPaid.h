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
/// - Persisted: Config/ed3k/paid.json (scopes, offers, passes, polls; owner-only) and the
///   holder seeds in Config/ed3k/paid/<scope hex>.holder (Secret files, 0600, never logged).
///
/// ToDo: selling (offers, seasons, signing passes: ed3k_paid_publish*, _new_season) is not
/// offered; only buying.
/// ToDo: `ed3k:pass` links (§9.3 by hand, vouchers) are not taken.
/// - Offers: from a catalogue's manifest, the pass answer, and the core (paidOffer on
///   PAID_OFFER: a hidden set's from a pass holder's KEY_NO_PASS answer, §6.2).
/// ToDo: a paid live channel (subscription.md §7) is not followed here.

#include "Ed3kFfi.h"

#include <QByteArray>
#include <QObject>
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

private:
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
    };

    void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onSessionStatus();
    void attach(Sub& s);
    /// false: refused (the error), or waits for the user (anonWait)
    bool startPoll(Sub& s, bool resume, QString* error = nullptr);
    void startRenewal(Sub& s);
    void onPass(Sub& s, const ::ed3k::event::PassBought& p);
    /// the pay link of @p s (§9.1 step 2); empty without a pay_url
    [[nodiscard]] QString payLinkOf(const Sub& s) const;
    void setPassOnEngine(Sub& s);
    /// the offer the core holds, when newer
    void readOffer(Sub& s);
    Sub* find(const ::ed3k::Scope& scope);
    const Sub* find(const ::ed3k::Scope& scope) const;
    [[nodiscard]] Info infoOf(const Sub& s) const;
    [[nodiscard]] QString holderFile(const Sub& s) const;
    [[nodiscard]] QString paidDir() const;
    void markChanged();
    void load();
    void save() const;

    Ed3kSession& m_session;
    QString m_configDir;
    std::map<::ed3k::Scope, Sub> m_subs;
    bool m_reports = true;
    bool m_attached = false;
    bool m_changePending = false;
};

} // namespace eMule::ed3k
