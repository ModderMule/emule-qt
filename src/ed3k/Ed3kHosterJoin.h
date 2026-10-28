#pragma once

/// @file Ed3kHosterJoin.h
/// @brief Downloads from links at file hosters (hoster.md): the host side of packmule's
/// hoster service. Contract: libs/packmule/docs/host-io.md "Links at file hosters".
///
/// A pasted link goes through these steps:
/// 1. match: does a resolver plugin take it (none: NoResolver)
/// 2. §4 lookup: the ALIAS record of the link (scheme url). A root found: the link joins
///    an ed3k download of that root, with Kad sources, and the first block from the link
///    tests the claim
/// 3. none found (§4.1): the link is downloaded whole and its root computed at the end
/// 4. the core asks the resolver (event::LinkResolve: check, open, expand); this class
///    answers it, reads the part file the resolver writes and hands the bytes in in order
/// 5. verified blocks (LinkBlock, peers' Block) and checked bytes of a rootless link
///    (LinkUnverified) are written into the item's file; when it is complete it moves to
///    the incoming folder, where eMule's shared files take it
///
/// Links peers name (event::Links) are used for roots this join downloads when the user
/// allows it. Everything runs on the daemon thread; the file reads on the session's pool.
/// The items are saved in Config/ed3k/hoster/items.json; account data never is.

#include "Ed3kJoinBase.h"
#include "Ed3kResolver.h"
#include "prefs/Preferences.h"

#include <QElapsedTimer>
#include <QList>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace eMule::ed3k {

class Ed3kHosterJoin : public Ed3kJoinBase {
    Q_OBJECT

public:
    enum class State : int {
        Matching = 0,   ///< asking which plugin takes the link
        Looking,        ///< §4 lookup of the link's ALIAS record
        Checking,       ///< the resolver checks name and size
        Queued,         ///< waits for a stream (one per hoster, maxStreams)
        Waiting,        ///< a hoster timer or quota; until waitUntil
        Downloading,
        Puzzle,         ///< an image puzzle waits for the user
        Paused,
        Complete,
        Gone,           ///< the hoster removed the file
        NeedsUser,      ///< a login or a puzzle the resolver cannot show
        NoResolver,     ///< no plugin for this hoster, or no resolver at all
        Failed,
    };

    /// Why an item is in its state; the GUI words it (IPC carries no localized text).
    /// Values are sent over IPC and saved: append only.
    enum class Reason : int {
        None = 0,
        WaitingForNode,      ///< the ed3k node is not running
        NoResolverBuilt,     ///< built without a resolver
        ResolverNotReady,    ///< arg: why (Ed3kResolver::unavailableReason, technical)
        NoPlugin,            ///< arg: host
        HostersOff,          ///< the user's switch is off (§3)
        LinkRefused,         ///< arg: the core's error
        Busy,
        HosterTimer,         ///< waitUntil
        HosterQuota,
        HosterBusy,
        NeedsLogin,
        NeedsBrowserPuzzle,  ///< reCAPTCHA and the like
        Blocked,             ///< a puzzle, a login or a quota (the core's NEEDS_USER)
        EndedEarly,
        Removed,
        NotTheFile,          ///< CORRUPT: not the file ed3k knows
        OtherSize,           ///< SIZE_MISMATCH
        TransferFailed,
        ResolverError,       ///< arg: the resolver's code or text
        WriteFailed,         ///< arg: the file system's error
        MoveFailed,          ///< arg: the incoming folder
        Folder,              ///< arg: the number of links found
        PeerLink,            ///< downloading from a link a peer named; arg: its host
    };

    /// One download from a hoster link, as the GUI shows it.
    struct Item {
        quint32 id = 0;
        QString url;
        QString host;        ///< the plugin's host name
        QString name;
        std::uint64_t size = 0;      ///< 0 = unknown
        std::uint64_t done = 0;      ///< bytes in the file (verified, or checked when rootless)
        std::uint64_t speed = 0;     ///< bytes/s, from the hoster
        State state = State::Matching;
        Reason reason = Reason::None;
        QString reasonArg;           ///< technical detail for some reasons, not localized
        qint64 waitUntil = 0;        ///< ms since the epoch, while Waiting
        bool verified = false;       ///< downloaded under a root (§4) rather than whole (§4.1)
        int peerSources = 0;         ///< ed3k streams of the root
        QString file;                ///< where the bytes go; the incoming file once Complete
        QString rootHex;
        qint64 added = 0;
    };

    /// @p configDir: items go to <configDir>/ed3k/hoster.
    Ed3kHosterJoin(Ed3kSession& session, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kHosterJoin() override;

    /// nullptr: no resolver (EMULE_HOSTER_JD off, or none yet); every ask is NO_RESOLVER.
    void setResolver(Ed3kResolver* resolver);
    [[nodiscard]] Ed3kResolver* resolver() const { return m_resolver; }
    /// The user's prefs; @p tempDir: parts and the resolver's scratch; @p incomingDir: done files.
    void setConfig(const Ed3kHosterPrefs& prefs, const QString& tempDir, const QString& incomingDir);

    void start() override;
    void stop() override;

    /// Every http(s) link in @p text becomes an item (duplicates skipped); their ids.
    QList<quint32> addLinks(const QString& text);
    void pause(quint32 id);
    void resume(quint32 id);
    /// Removes the item; its unfinished file always, a complete one only with @p deleteFile.
    void remove(quint32 id, bool deleteFile);
    void answerPuzzle(quint64 puzzle, const QString& text);
    /// Checks a login with the resolver; accountChecked() follows.
    void checkAccount(const QString& host, const QString& user, const QString& password);

    [[nodiscard]] QList<Item> items() const;
    [[nodiscard]] std::optional<Item> item(quint32 id) const;
    /// http(s) links in @p text, one per line or separated by spaces (tests).
    [[nodiscard]] static QStringList linksIn(const QString& text);

signals:
    void itemChanged(quint32 id);
    void itemRemoved(quint32 id);
    /// An image puzzle for @p id (0: a check no item started). Answer with answerPuzzle().
    void puzzleAsked(quint32 id, quint64 puzzle, const QByteArray& image, const QString& host,
                     const QString& explain, int timeoutS);
    /// The link of @p id names a folder: these are its links (the GUI asks which to add).
    void linksExpanded(quint32 id, const QList<eMule::ed3k::ResolverLink>& links);
    void accountChecked(const QString& host, const QString& user, const eMule::ed3k::ResolverEvent& result);

protected:
    [[nodiscard]] std::vector<std::uint64_t> wantedBlocks(const Download& d) const override;
    void onFileBlock(const Download& d, const ::ed3k::event::Block& b) override;
    void onBlobFetched(const QString& owner, int member, const ::ed3k::Root& root, const QByteArray& data) override;
    [[nodiscard]] std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override;
    void onOtherEvent(const ::ed3k::Event& event) override;
    [[nodiscard]] bool hasOwner(const QString& owner) const override;
    void saveOwner(const QString& owner) override;

private:
    struct Entry;
    /// A resolver task the core asked for (event::LinkResolve).
    struct Ask {
        std::uint64_t link = 0;
        quint32 item = 0;            ///< 0: a peer's link of no item
        QString url;                 ///< the link asked for (the item's own or a peer's)
        std::uint32_t kind = 0;      ///< ED3K_LINK_*
        QString dir;                 ///< open: the scratch folder
        QString path;                ///< open: the part file, then the done file
        std::uint64_t fed = 0;       ///< bytes handed to linkData (from where the stream opened)
        std::uint64_t have = 0;      ///< open: bytes put at the front of the part file to resume
        std::uint64_t onDisk = 0;    ///< bytes the resolver wrote
        bool opened = false;
        bool done = false;           ///< the resolver finished the file
        bool reading = false;
        bool ended = false;
        int readFailures = 0;
    };

    [[nodiscard]] Entry* find(quint32 id) const;
    [[nodiscard]] Entry* byLink(std::uint64_t link) const;
    [[nodiscard]] Entry* byRoot(const ::ed3k::Root& root) const;
    [[nodiscard]] static QString ownerOf(quint32 id);
    [[nodiscard]] static quint32 idOf(const QString& owner);
    void setState(Entry& e, State s, Reason why = Reason::None, const QString& arg = {});
    void changed(const Entry& e);
    [[nodiscard]] std::optional<ResolverAccount> accountFor(const QString& host) const;

    /// The steps of a pasted link.
    void begin(Entry& e);
    void onMatched(Entry& e, const QString& host);
    void lookUp(Entry& e);
    void onAliasFound(const ::ed3k::event::AliasFound& found);
    void startRooted(Entry& e, const ::ed3k::Root& root, std::uint64_t size, const QString& name,
                     const std::optional<::ed3k::Key32>& publisher);
    void startRootless(Entry& e);
    /// linkAsk when a stream is free; else Queued and tried by the tick.
    void askOpen(Entry& e);
    [[nodiscard]] int activeStreams() const;

    /// The core asks the resolver.
    void onResolve(const ::ed3k::event::LinkResolve& r);
    void onResolverEvent(Ed3kResolver::TaskId task, const ResolverEvent& ev);
    void answer(std::uint64_t link, std::uint32_t kind, const QString& text = {}, std::optional<std::uint64_t> value = {});
    /// Resume: the first @p offset bytes of @p from go to the task's part file in the
    /// background, then the resolver opens from there (prefilled()).
    void prefill(Ask a, const QString& from, std::uint64_t offset);
    void prefilled(std::uint64_t link, bool ok);
    void openTask(Ask a);
    /// Answers OPENED once where the bytes start is known; false: not yet.
    bool opened(Ask& a, const ResolverEvent& ev);
    /// Where an open of a rooted item starts: the first block not held from 0.
    [[nodiscard]] std::uint64_t resumeOffset(const Entry& e) const;
    /// Reads what the resolver wrote past Ask::fed and hands it in.
    void feed(Ed3kResolver::TaskId task);
    void fed(Ed3kResolver::TaskId task, std::optional<QByteArray> data);
    void endStream(Ed3kResolver::TaskId task, bool broken);

    void onLinkBlock(std::uint64_t link, const ::ed3k::Root& root, std::uint64_t offset, const ::ed3k::Bytes& data);
    void onLinkUnverified(std::uint64_t link, std::uint64_t offset, const ::ed3k::Bytes& data);
    void onLinkRootless(std::uint64_t link, const ::ed3k::Root& root, std::uint64_t size);
    void onLinkEnded(std::uint64_t link, std::uint32_t code, std::uint64_t bytes);
    void onLinkClaim(std::uint64_t link, const ::ed3k::Root& root, bool holds);
    void onLinkVouched(std::uint64_t link, const ::ed3k::Root& root);
    void onPeerLinks(const ::ed3k::event::Links& links);
    /// A peer's link of @p e not tried yet is opened when its own cannot deliver; false: none.
    bool tryPeerLink(Entry& e);
    /// The item's own link went nowhere for now: whether a peer's link could stand in.
    [[nodiscard]] static bool ownLinkStuck(const Entry& e);
    /// §4.1: the link of @p e goes with its source record and to downloaders when published.
    void shareLink(Entry& e);

    /// Writes @p data at @p offset of the item's file; false: the disk refused.
    bool writeAt(Entry& e, std::uint64_t offset, std::span<const std::byte> data);
    void checkComplete(Entry& e);
    void finish(Entry& e);
    /// Drops the link, the ed3k download and the resolver task of @p e.
    void release(Entry& e, bool keepFile);
    void retryLater(Entry& e, int seconds, Reason why, const QString& arg = {});
    void onTick();
    void load();
    void save();
    [[nodiscard]] QString itemsFile() const;
    [[nodiscard]] QString workDir(quint32 id) const;

    QPointer<Ed3kResolver> m_resolver;
    Ed3kHosterPrefs m_prefs;
    QString m_tempDir;
    QString m_incomingDir;
    std::map<quint32, std::unique_ptr<Entry>> m_items;
    quint32 m_nextId = 1;
    /// resolver tasks → what they are for
    std::map<Ed3kResolver::TaskId, quint32> m_matches;
    std::map<Ed3kResolver::TaskId, Ask> m_asks;
    std::map<Ed3kResolver::TaskId, std::pair<QString, QString>> m_accountChecks;
    /// ALIAS searches (§4 lookup) → item
    std::map<std::uint32_t, quint32> m_searches;
    /// links peers named for our roots
    struct PeerLink {
        quint32 item = 0;
        std::uint64_t offset = 0;    ///< of the range it delivers; 0: the file from its start
        QString url;
    };
    std::map<std::uint64_t, PeerLink> m_peerLinks;
    /// opens waiting for their part file to be prefilled, by link
    std::map<std::uint64_t, Ask> m_preparing;
    /// puzzle → item, to clear the state on answer
    std::map<quint64, quint32> m_puzzles;
    QTimer m_tick;
};

} // namespace eMule::ed3k
