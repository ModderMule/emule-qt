#pragma once

/// @file Ed3kResolver.h
/// @brief What resolves links at file hosters (hoster.md §3): check, open, expand. One
/// interface, so that the way it is built can change (emuleqt-implementation §12.4); the
/// JDownloader sidecar (Ed3kJdResolver) is the one there is.
///
/// Every call returns a task id at once; what it found comes as taskEvent() with that id, last
/// a final one (isFinal()). Account data passed in never comes back out and is never logged.

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace eMule::ed3k {

/// A login for one hoster, or for a multi-hoster that downloads another hoster's links (via).
/// Printed as "[redacted]".
struct ResolverAccount {
    QString user;
    QString password;
    QString via{};       ///< open only: the multi-hoster's domain; its plugin downloads the link with this login
    QByteArray state{};  ///< the plugin's session (JSON object) an Account event gave; empty: none
    bool askLogin = false;  ///< accountCheck only: a browser login the plugin needs goes to the user (Login event)
};

/// One hoster a multi-hoster account downloads (accountCheck of a multi-hoster).
struct DebridHost {
    QString domain;             ///< the resolver's plugin host, e.g. "rapidgator.net"
    QStringList domains;        ///< all its domains, lower case
    QString status;             ///< "working", else why not (JD's MultihosterHostStatus, lower case)
    qint64 linksLeft = -1;      ///< links left today; -1 = unlimited / unknown
    qint64 linksMax = -1;
    qint64 trafficLeft = -1;    ///< bytes; -1 = unlimited / unknown
    qint64 trafficMax = -1;
    int factor = 100;           ///< traffic charged per byte, percent
    int maxDownloads = -1;      ///< downloads at once; -1 = unlimited
    qint64 unavailableS = 0;    ///< the multi-hoster blocked it for this long
    bool operator==(const DebridHost&) const = default;
};

/// A plugin that takes a premium account (accountHosts), as JD's Add account dialog lists it.
struct AccountHost {
    QString host;    ///< lower case domain
    QString name;    ///< the plugin's name when not its host
    bool multi = false;    ///< a multi-hoster (debrid service)
    bool solver = false;   ///< a captcha solver service
    QStringList subs;      ///< other names of its sites (aliases, subdomains): a server country behind a CDN
    bool operator==(const AccountHost&) const = default;
};

/// A captcha solver service login (2captcha.com, anti-captcha.com, ...). Key-only services
/// take the key as password. Printed as "[redacted]".
struct SolverAccount {
    QString host;
    QString user;
    QString password;
    bool operator==(const SolverAccount&) const = default;
};

/// What setSolverAccounts() found for one account.
struct SolverAccountState {
    QString host;       ///< the solver's main domain; as given when no plugin took it
    bool valid = false;
    double balance = 0;
    QString currency;   ///< ISO 4217; empty: unknown
    QString code;       ///< not valid: "noPlugin", "login", "io", "failed"
    QString text;
};

/// One link an expand found.
struct ResolverLink {
    QString url;
    QString name;
    QString host;
    std::uint64_t size = 0;   ///< 0 = unknown
    QString package{};        ///< a container's package name; empty: none
    QStringList passwords{};  ///< a container's archive passwords
};

struct ResolverEvent {
    enum class Kind {
        Matched,      ///< match: host (empty: no plugin takes the link); a hoster's sites; generic
        Sites,        ///< sites: host (empty: no hoster plugin), sites
        Found,        ///< check: name, size, hash
        Folder,       ///< check: the link names count links; ask expand
        Links,        ///< expand, openContainer: links
        Progress,     ///< open: bytes on disk from the front of path; total if known
        Wait,         ///< a timer or a quota: seconds; open goes on by itself while reason is "timer"
        Puzzle,       ///< an image puzzle: puzzle id, image, host, explain, timeoutS
        Done,         ///< open: the whole file is at path (size, name, hash)
        Gone,         ///< the hoster says the file was removed
        NeedsUser,    ///< text: "puzzle" or "login"
        Failed,       ///< code: "io", "defect", "failed", "noPlugin", "folder", "unavailable", "private"
                      ///< (a request to a non-public address refused, text: the host); openContainer
                      ///< also "limit" (the DLC service refuses for a while), "invalid", "password",
                      ///< "bad" (a resolver without containers); text
        Cancelled,
        Account,      ///< accountCheck: valid, premium, expires, trafficLeft, maxDownloads; multi, hosts, state
        Status,       ///< open: what the plugin does, text: "link", "hoster", "hosterAccount", "puzzle", "connect", "transfer"
        Solvers,      ///< setSolverAccounts: solvers
        Solved,       ///< solveTest: text, host (the solver), name (the expected text, if known)
        AccountHosts, ///< accountHosts: accountHosts
        Icon,         ///< favicon: host, image (PNG); Failed "none" when the site has none
        Login,        ///< accountCheck: a browser login (OAuth device flow) waits; text: its URL, host, timeoutS
        Code,         ///< accountCheck, open: a login's confirmation code (2FA, mail) waits; host, user (the login),
                      ///< text: the site's words, name: title, timeoutS
    };
    Kind kind = Kind::Failed;
    QString host;
    QString user;                 ///< code: the login's user
    QString name;
    QString path;
    QString code;
    QString text;
    QString hashType;    ///< "md5", "sha1", ... of hash: checked, not verified (alias.md §5 rule 5)
    QString hash;
    std::uint64_t size = 0;
    std::uint64_t bytes = 0;
    std::uint64_t total = 0;
    /// open with have > 0: where the new bytes start (have: resumed, 0: from the start);
    /// sent once, with a progress or the done
    std::optional<std::uint64_t> from;
    std::uint64_t seconds = 0;
    std::uint64_t puzzle = 0;
    int count = 0;
    int timeoutS = 0;
    QByteArray image;
    QList<ResolverLink> links;
    QStringList sites;            ///< a hoster's domains, lower case: its host, then its aliases
    /// match: only the generic http plugin takes the link (setGenericHttp): "file" (the path
    /// names a file), "any"; empty: a site plugin, or none
    QString generic;
    bool valid = false;
    bool premium = false;
    qint64 expires = 0;           ///< unix seconds; 0 = unknown
    qint64 trafficLeft = -1;      ///< bytes; -1 = unlimited or unknown
    qint64 trafficMax = -1;       ///< accountCheck: the account's traffic in all; -1 = unknown
    int maxDownloads = 0;         ///< accountCheck: JD's downloads at once; -1 = unlimited, 0 = unknown
    double balance = -1;          ///< accountCheck of a solver service: its credit; -1 = none given
    QString currency;             ///< of balance, ISO 4217; empty: unknown
    QList<SolverAccountState> solvers;
    bool multi = false;           ///< accountCheck: a multi-hoster account; hosts what it downloads
    QList<DebridHost> hosts;
    QByteArray state;             ///< accountCheck: the plugin's session (JSON object), for ResolverAccount::state
    QList<AccountHost> accountHosts;

    /// The last event of a task.
    [[nodiscard]] bool isFinal() const
    {
        return kind != Kind::Progress && kind != Kind::Wait && kind != Kind::Puzzle && kind != Kind::Status
               && kind != Kind::Login && kind != Kind::Code;
    }
};

class Ed3kResolver : public QObject {
    Q_OBJECT

public:
    using TaskId = std::uint64_t;
    using QObject::QObject;
    ~Ed3kResolver() override = default;

    /// Whether tasks can run now (a runtime and the plugins are there).
    [[nodiscard]] virtual bool available() const = 0;
    /// Why not, for the GUI; empty when available.
    [[nodiscard]] virtual QString unavailableReason() const = 0;
    /// The plugin set, e.g. "JDownloader r53779, 3528 hosters"; empty before the first start.
    [[nodiscard]] virtual QString description() const = 0;
    /// The plugin engine's version, e.g. "r53779", known without a start; empty: none installed
    [[nodiscard]] virtual QString engineVersion() const { return {}; }
    /// A newer engine version fetched and waiting to be swapped in; empty: none
    [[nodiscard]] virtual QString pendingVersion() const { return {}; }
    /// A link waits for plugins that are not there: fetch them (none shipped). Default: nothing
    virtual void wantPlugins() {}
    /// What the last solverAccounts found per account; empty: none sent yet.
    [[nodiscard]] virtual QList<SolverAccountState> solverStates() const { return {}; }

    /// Which plugin takes @p url, without network access.
    virtual TaskId match(const QString& url) = 0;
    virtual TaskId check(const QString& url, const std::optional<ResolverAccount>& account) = 0;
    virtual TaskId expand(const QString& url) = 0;
    /// expand, and every hoster link on the page @p url names (JD's deep crawl).
    virtual TaskId expandDeep(const QString& url) { return expand(url); }
    /// Whether expandDeep() crawls pages (a resolver that knows it).
    [[nodiscard]] virtual bool canDeepCrawl() const { return false; }
    /// Whether open() takes a multi-hoster login (ResolverAccount::via).
    [[nodiscard]] virtual bool canDebrid() const { return false; }
    /// The links of a container file (DLC, RSDF, CCF) at @p path, as written; DLC asks JD's
    /// key service (network). Links or Failed follows.
    virtual TaskId openContainer(const QString& path) = 0;
    /// The file into @p dir (empty, one per task). @p have: bytes already at the front of its
    /// part file, written by the caller; 0 = from the start.
    virtual TaskId open(const QString& url, const std::optional<ResolverAccount>& account, const QString& dir,
                        std::uint64_t have) = 0;
    virtual TaskId accountCheck(const QString& host, const ResolverAccount& account) = 0;
    /// The hoster plugin for a domain or one of its aliases, and all its domains. No network.
    virtual TaskId sites(const QString& host) = 0;
    /// Replaces the captcha solver accounts; each is checked, valid ones solve puzzles before
    /// the user is asked. Solvers follows. Empty: none.
    virtual TaskId setSolverAccounts(const QList<SolverAccount>& accounts) = 0;
    /// One test puzzle of @p type ("IMAGE", "RECAPTCHA_V2", ...; JD's CAPTCHA_TYPE) through the
    /// solver of domain @p solver (empty: any). IMAGE uses a built-in image unless @p image.
    /// Solved or Failed ("noSolver", "unsolved") follows. Costs the solver's price.
    virtual TaskId solveTest(const QString& solver, const QString& type, const QByteArray& image = {}) = 0;
    /// Every plugin that takes a premium account. AccountHosts follows. Default: none (0)
    virtual TaskId accountHosts() { return 0; }
    /// The favicon of @p host (network). Icon or Failed follows. Default: none (0)
    virtual TaskId favicon(const QString& host)
    {
        (void)host;
        return 0;
    }
    /// The user's text for a puzzle; empty = skip it.
    virtual void answerPuzzle(std::uint64_t puzzle, const QString& text) = 0;
    /// The user's confirmation code for an accountCheck's Code event; empty = cancel. Default: nothing
    virtual void answerCode(TaskId task, const QString& code)
    {
        (void)task;
        (void)code;
    }
    virtual void cancel(TaskId task) = 0;
    /// Image puzzles are asked (true) or skipped.
    virtual void setAskPuzzles(bool on) = 0;
    /// Proxy for every request ("http://h:p", "socks5://user:pass@h:p"); empty = direct.
    virtual void setProxy(const QString& url) = 0;
    /// A public http(s) link no site plugin takes is downloaded as a plain file (#202). Default: nothing
    virtual void setGenericHttp(bool on) { (void)on; }

signals:
    void taskEvent(eMule::ed3k::Ed3kResolver::TaskId task, const eMule::ed3k::ResolverEvent& ev);
    /// available() or description() changed.
    void stateChanged();
};

} // namespace eMule::ed3k
