#pragma once

/// @file UsenetWebBackend.h
/// @brief What the web server may ask of the Usenet engine.
///
/// WebServer lives in eMule::Core, and `core -> usenet` must never happen, so
/// the page and the REST routes talk to this interface and DaemonApp installs
/// the implementation — the same seam setUsenetStreamResolver() uses.
///
/// Rows travel as CBOR maps in exactly the shape the IPC sends
/// (GetUsenetQueue, GetUsenetItemDetails, ListUsenetArchiveEntries), so the web
/// page, the REST API and the GUI read one field vocabulary from one serializer.

#include <QCborArray>
#include <QCborMap>
#include <QString>

#include <functional>

namespace eMule {

/// Mirrors usenet::UsenetAddOutcome. A plain int on this side for the same
/// reason the IPC sends one: core cannot name the Usenet enum.
enum class UsenetWebAddOutcome : int {
    Added = 0,
    Duplicate = 1,
    Invalid = 2,
    Failed = 3,
    AlreadyDownloaded = 4,
};

struct UsenetWebAddOptions {
    bool force = false;     ///< the user was asked about AlreadyDownloaded and said yes
    QString password;
    int category = 0;
    int priority = 0;
    bool paused = false;
};

struct UsenetWebAddResult {
    QString itemId;         ///< empty when refused
    QString error;          ///< the refusal as a sentence
    UsenetWebAddOutcome outcome = UsenetWebAddOutcome::Failed;

    /// Too many URL fetches already running; nothing was attempted.
    bool busy = false;

    [[nodiscard]] bool ok() const { return !itemId.isEmpty(); }
};

enum class UsenetWebCategoryAction { Pause, Resume, Cancel };

class UsenetWebBackend {
public:
    virtual ~UsenetWebBackend() = default;

    /// False before the daemon constructed the engine, or after it tore it down.
    [[nodiscard]] virtual bool available() const = 0;

    [[nodiscard]] virtual QCborArray queue() const = 0;

    /// Whether @p id is in the queue. Cheap, unlike details().
    [[nodiscard]] virtual bool contains(const QString& id) const = 0;

    /// The GetUsenetItemDetails map, or an empty map when there is no such item.
    [[nodiscard]] virtual QCborMap details(const QString& id) const = 0;

    /// The ListUsenetArchiveEntries map. Not const: listing asks for bytes.
    [[nodiscard]] virtual QCborMap archiveEntries(const QString& id, int fileIndex) = 0;

    /// `maxDownloadKb`, `usenetLimitKb`, `ed2kBudgetKb`, the engine's `rate` in
    /// bytes/s and whether it is `paused` — what the Qt window's summary line and
    /// toolbar are built from.
    [[nodiscard]] virtual QCborMap downloadSplit() const = 0;

    /// Pause or resume the whole engine; persisted.
    virtual void setEnginePaused(bool paused) = 0;

    /// Leave files of one release out, or bring them back. Empty when applied,
    /// else the refusal as a sentence.
    virtual QString setFilesSkipped(const QString& id, const QList<int>& files, bool skipped) = 0;

    [[nodiscard]] virtual bool categoryExists(int category) const = 0;

    virtual bool pause(const QString& id) = 0;
    /// A person pressed Resume: overrules a check that stopped the release.
    virtual bool resume(const QString& id) = 0;
    virtual bool remove(const QString& id, bool deleteFiles) = 0;
    virtual bool setPriority(const QString& id, int priority) = 0;
    virtual bool setCategory(const QString& id, int category) = 0;
    virtual bool setPassword(const QString& id, const QString& password) = 0;

    /// Re-run the availability probe. Empty when it started, else why not.
    virtual QString recheck(const QString& id) = 0;

    /// Returns how many releases it acted on, or -1 for an unknown category.
    virtual int applyCategoryAction(int category, UsenetWebCategoryAction action) = 0;

    virtual UsenetWebAddResult addNzb(const QByteArray& data, const QString& name,
                                      const UsenetWebAddOptions& options) = 0;

    /// Fetch then add. @p done runs exactly once, possibly before this returns.
    virtual void addNzbUrl(const QString& url, const UsenetWebAddOptions& options,
                           std::function<void(const UsenetWebAddResult&)> done) = 0;
};

} // namespace eMule
