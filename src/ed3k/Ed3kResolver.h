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

#include <cstdint>
#include <optional>

namespace eMule::ed3k {

/// A login for one hoster. Printed as "[redacted]".
struct ResolverAccount {
    QString user;
    QString password;
};

/// One link an expand found.
struct ResolverLink {
    QString url;
    QString name;
    QString host;
    std::uint64_t size = 0;   ///< 0 = unknown
};

struct ResolverEvent {
    enum class Kind {
        Matched,      ///< match: host (empty: no plugin takes the link)
        Found,        ///< check: name, size, hash
        Folder,       ///< check: the link names count links; ask expand
        Links,        ///< expand: links
        Progress,     ///< open: bytes on disk from the front of path; total if known
        Wait,         ///< a timer or a quota: seconds; open goes on by itself while reason is "timer"
        Puzzle,       ///< an image puzzle: puzzle id, image, host, explain, timeoutS
        Done,         ///< open: the whole file is at path (size, name, hash)
        Gone,         ///< the hoster says the file was removed
        NeedsUser,    ///< text: "puzzle" or "login"
        Failed,       ///< code: "io", "defect", "failed", "noPlugin", "folder", "unavailable"; text
        Cancelled,
        Account,      ///< accountCheck: valid, premium, expires, trafficLeft
    };
    Kind kind = Kind::Failed;
    QString host;
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
    bool valid = false;
    bool premium = false;
    qint64 expires = 0;           ///< unix seconds; 0 = unknown
    qint64 trafficLeft = -1;      ///< bytes; -1 = unlimited or unknown

    /// The last event of a task.
    [[nodiscard]] bool isFinal() const
    {
        return kind != Kind::Progress && kind != Kind::Wait && kind != Kind::Puzzle;
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

    /// Which plugin takes @p url, without network access.
    virtual TaskId match(const QString& url) = 0;
    virtual TaskId check(const QString& url, const std::optional<ResolverAccount>& account) = 0;
    virtual TaskId expand(const QString& url) = 0;
    /// The file into @p dir (empty, one per task). @p have: bytes already at the front of its
    /// part file, written by the caller; 0 = from the start.
    virtual TaskId open(const QString& url, const std::optional<ResolverAccount>& account, const QString& dir,
                        std::uint64_t have) = 0;
    virtual TaskId accountCheck(const QString& host, const ResolverAccount& account) = 0;
    /// The user's text for a puzzle; empty = skip it.
    virtual void answerPuzzle(std::uint64_t puzzle, const QString& text) = 0;
    virtual void cancel(TaskId task) = 0;
    /// Image puzzles are asked (true) or skipped.
    virtual void setAskPuzzles(bool on) = 0;
    /// Proxy for every request ("http://h:p", "socks5://user:pass@h:p"); empty = direct.
    virtual void setProxy(const QString& url) = 0;

signals:
    void taskEvent(eMule::ed3k::Ed3kResolver::TaskId task, const eMule::ed3k::ResolverEvent& ev);
    /// available() or description() changed.
    void stateChanged();
};

} // namespace eMule::ed3k
