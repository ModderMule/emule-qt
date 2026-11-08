#pragma once

/// @file Ed3kLinkGrabber.h
/// @brief The Link Grabber (hoster.md "Link Grabber"): links are crawled into a list of
/// packages, checked and edited before they become hoster downloads, as in JDownloader.
///
/// Links come in as text (grab(): pasted, the clipboard, Add Links) or already expanded
/// (add(): a folder link, Click'n'Load, a link container). Text runs through the resolver's
/// expand (JD's LinkCrawler; deep: every hoster link on a page); each link found is checked
/// (online, name, size) through the hoster join. Confirmed packages become hoster downloads.
/// The list is saved in Config/ed3k/hoster/linkgrabber.json. Daemon thread only.

#include "Ed3kResolver.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>

namespace eMule::ed3k {

class Ed3kLinkGrabber : public QObject {
    Q_OBJECT

public:
    /// A link's check state; the values are Ed3kHosterJoin::LinkStatus (sent over IPC).
    enum class Status : int {
        Checking = 0,
        Online,
        Offline,
        Unknown,
    };

    struct Package {
        quint32 id = 0;
        QString name;
        QStringList passwords;   ///< archive passwords for the unpack
        QString source;          ///< where its links came from (a page, a container, the clipboard)
        qint64 added = 0;        ///< ms since the epoch
    };

    struct Link {
        quint32 id = 0;
        quint32 packageId = 0;
        QString url;
        QString name;
        QString host;
        std::uint64_t size = 0;  ///< 0 = unknown
        Status status = Status::Unknown;
        bool enabled = true;
        bool dupe = false;       ///< already a hoster download
        bool autoNamed = false;  ///< no name when added: its package follows the name the check finds
    };

    /// What the grabber needs of the hoster join; the daemon wires these, tests fake them.
    struct Backend {
        /// checks @p urls; the results come to onLinkChecked(). False: no check possible
        std::function<bool(const QStringList& urls)> check;
        /// one download per entry of @p files in a new package; the ids
        std::function<QList<quint32>(const QList<QStringList>& files, const QString& package,
                                     const QStringList& passwords)> add;
        /// whether @p url is a hoster download already
        std::function<bool(const QString& url)> isItem;
    };

    /// How grab() delivers what it found.
    enum class Mode {
        List,   ///< into the list
        Ask,    ///< back to the caller: grabbed() with the token, the GUI asks which to add
    };

    /// What changed since the last changed(); ids only, the bridge reads the rows.
    struct Delta {
        QSet<quint32> packages;
        QSet<quint32> links;
        QSet<quint32> removedPackages;
        QSet<quint32> removedLinks;
        bool all = false;   ///< everything (clear, load): read the whole list again
        [[nodiscard]] bool isEmpty() const
        {
            return !all && packages.isEmpty() && links.isEmpty() && removedPackages.isEmpty() && removedLinks.isEmpty();
        }
    };

    static constexpr int kMaxLinks = 10000;
    static constexpr int kMaxCrawls = 3;          ///< expand tasks at once
    static constexpr int kMaxQueuedCrawls = 1000;
    static constexpr qsizetype kMaxTextBytes = 1024 * 1024;

    /// @p configDir: the list goes to <configDir>/ed3k/hoster/linkgrabber.json.
    explicit Ed3kLinkGrabber(const QString& configDir, QObject* parent = nullptr);
    ~Ed3kLinkGrabber() override;

    void setResolver(Ed3kResolver* resolver);
    void setBackend(Backend backend) { m_backend = std::move(backend); }
    /// The prefs: @p on the Link Grabber, @p all also Click'n'Load, Add Links and containers.
    void setEnabled(bool on, bool all);
    [[nodiscard]] bool isEnabled() const { return m_on; }
    /// Click'n'Load, Add Links and containers go to the list too.
    [[nodiscard]] bool takesAll() const { return m_on && m_all; }
    /// Whether the resolver crawls pages (expand with deep).
    [[nodiscard]] bool canDeepCrawl() const;

    /// Reads the saved list (once, at start).
    void load();
    /// Writes the list now (shutdown).
    void save();

    /// Crawls the http(s) links in @p text. @p clipboard: links no plugin takes are dropped
    /// first (no network). @p deep: every hoster link on the pages. @p package, @p passwords:
    /// given to what is found. A token; 0: nothing to do (no links, too much, no resolver).
    quint64 grab(const QString& text, const QString& source, bool deep, const QString& package,
                 const QStringList& passwords, Mode mode, bool clipboard = false);
    /// Adds links found elsewhere (their own package and passwords win over @p package).
    /// The number added (dupes of the list skipped).
    int add(const QList<ResolverLink>& links, const QString& source, const QString& package = {},
            const QStringList& passwords = {});

    /// A link check's result (Ed3kHosterJoin::linkChecked); @p status a LinkStatus value.
    void onLinkChecked(const QString& url, int status, const QString& host, const QString& name, std::uint64_t size);

    /// Renames packages (@p name not empty) and replaces their passwords (@p passwords set).
    bool editPackages(const QList<quint32>& ids, const QString& name, const std::optional<QStringList>& passwords);
    void setLinksEnabled(const QList<quint32>& linkIds, bool on);
    /// Moves links into package @p packageId; 0: a new package @p newName. The package id.
    quint32 moveLinks(const QList<quint32>& linkIds, quint32 packageId, const QString& newName);
    /// One package per hoster for each of @p packageIds that holds links of several.
    void splitByHost(const QList<quint32>& packageIds);
    void remove(const QList<quint32>& linkIds, const QList<quint32>& packageIds);
    /// "offline", "disabled", "dupes" or "all": removes those links. False: unknown mode.
    bool cleanup(const QString& mode);
    /// Checks the links again (all of the packages given too).
    void recheck(const QList<quint32>& linkIds, const QList<quint32>& packageIds = {});
    /// Stops the crawls that run or wait.
    void abortCrawls();
    /// Makes downloads of the enabled links of @p packageIds and of @p linkIds (offline ones
    /// only with @p includeOffline), one package each; they leave the list. The download ids.
    QList<quint32> confirm(const QList<quint32>& packageIds, const QList<quint32>& linkIds, bool includeOffline);

    [[nodiscard]] QList<Package> packages() const;
    [[nodiscard]] QList<Link> links() const;
    [[nodiscard]] const Package* package(quint32 id) const;
    [[nodiscard]] const Link* link(quint32 id) const;
    /// Links being crawled or waiting for it.
    [[nodiscard]] int crawling() const { return int(m_crawls.size()) + int(m_queue.size()); }

    /// The package name for a file: archive volumes of one set share it (".part2.rar", ".r01",
    /// ".7z.001", ...), else the name without its extension.
    [[nodiscard]] static QString packageNameFor(const QString& fileName);

signals:
    /// The list changed (coalesced, at most every 200 ms).
    void changed(const eMule::ed3k::Ed3kLinkGrabber::Delta& delta);
    /// A grab() in Mode::Ask finished: its links (empty: none found; @p code why).
    void grabbed(quint64 token, const QString& source, const QList<eMule::ed3k::ResolverLink>& links,
                 const QString& code);

private:
    /// One link of a grab() to crawl.
    struct Crawl {
        quint64 token = 0;
        QString url;
        bool deep = false;
        bool clipboard = false;   ///< match first
    };
    /// A grab() whose crawls run.
    struct Grab {
        QString source;
        QString package;
        QStringList passwords;
        Mode mode = Mode::List;
        int open = 0;                  ///< its crawls not answered yet
        QList<ResolverLink> found;     ///< Mode::Ask: collected
        QString code;                  ///< the last failure
    };
    enum class TaskKind { Match, Expand };
    struct Task {
        TaskKind kind = TaskKind::Expand;
        Crawl crawl;
    };

    void onTaskEvent(Ed3kResolver::TaskId task, const ResolverEvent& ev);
    void pump();
    void crawlDone(quint64 token, const QList<ResolverLink>& links, const QString& code);
    /// The package for a link of @p source: @p name, or one of that name made now.
    quint32 packageFor(const QString& name, const QString& source, const QStringList& passwords);
    void dropEmptyPackages();
    void touchLink(quint32 id);
    void touchPackage(quint32 id);
    void touchAll();
    void startChecks(const QStringList& urls);
    [[nodiscard]] QString file() const;

    QString m_configDir;
    Ed3kResolver* m_resolver = nullptr;
    Backend m_backend;
    bool m_on = false;
    bool m_all = false;
    bool m_loaded = false;
    std::map<quint32, Package> m_packages;
    std::map<quint32, Link> m_links;
    QHash<QString, quint32> m_byUrl;   ///< url -> link id
    quint32 m_nextPackage = 1;
    quint32 m_nextLink = 1;
    quint64 m_nextToken = 1;
    std::map<quint64, Grab> m_grabs;
    QList<Crawl> m_queue;
    std::map<Ed3kResolver::TaskId, Task> m_crawls;   ///< running match/expand tasks
    Delta m_delta;
    QTimer m_emit;
    QTimer m_save;
};

} // namespace eMule::ed3k
