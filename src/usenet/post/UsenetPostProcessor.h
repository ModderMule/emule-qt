#pragma once

/// @file UsenetPostProcessor.h
/// @brief The verify → repair → rename → unpack → stage pipeline, on its own thread.
///
/// A `QObject` moved to a `QThread`, exactly like `UsenetWorker`, and owned the
/// same way: the queue creates the thread, connects queued signals, and joins in
/// `stop()`. Nothing crosses a thread boundary except the job and the result.
///
/// **Serial by design.** One job runs at a time even when several items finish
/// together. Repair and extraction are both disk- and CPU-bound, and two of them
/// interleaved on one spindle is slower than the same two in sequence — plus a
/// repair holds the whole release open, so the memory does not overlap kindly
/// either.
///
/// The pipeline stops at *staging*, not publishing. It copies the payload to
/// `<incoming>/<name>.usenetpart` — which no share scan will look at — and hands
/// the queue a list of renames. The slow IO therefore happens here, off the
/// daemon thread, while the only thing left on the daemon thread is an atomic
/// in-place rename and the call that offers the file to ED2K.

#include "post/Par2Verifier.h"
#include "post/UsenetDirectUnpack.h"

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>

namespace eMule::usenet {

/// Where a job currently is. Maps onto the item statuses the GUI shows.
enum class PostStage : quint8 {
    Idle = 0,
    Verifying,
    Repairing,
    Unpacking,
    Staging,
};

[[nodiscard]] QString describePostStage(PostStage stage);

/// What the unpack stage came to, for statistics.
enum class UsenetUnpackOutcome : quint8 {
    NotRun = 0,
    NothingToUnpack,   ///< no archives; the payload was published as downloaded
    Unpacked,
    Failed,
    PasswordRequired,
};

/// A path in @p dir carrying @p name, suffixed " (n)" until it names nothing that
/// already exists. Shared with UsenetQueue, which does the final rename.
[[nodiscard]] QString uniqueDestination(const QString& dir, const QString& name);

/// Subdirectory of an item's work folder that extracted members land in. Shared
/// because direct unpack writes there too, and post-processing has to find the
/// same files it would have produced itself.
inline constexpr QLatin1StringView kUnpackDirName{"_unpacked"};

/// Everything the pipeline needs. Passed by value across the thread boundary, so
/// it holds no pointers into queue state.
struct UsenetPostJob {
    QString itemId;

    /// Directory holding the assembled files — the item's scratch folder.
    QString workDir;

    /// Where the payload is staged. The incoming directory.
    QString destDir;

    /// From NzbInfo::password. libarchive decrypts **ZIP only**; every other
    /// encrypted format goes to ExternalUnpacker, which is what makes a RAR
    /// password mean anything.
    QString password;

    /// The usenetExternalUnpacker preference — an explicit 7-Zip or unrar path,
    /// empty to search. Carried on the job rather than read in the pipeline so
    /// this class keeps holding no pointers into anything.
    QString externalUnpacker;

    bool par2Enabled    = true;
    bool renameEnabled  = true;
    bool unpackEnabled  = true;

    /// Publish the payload alone. With this off the archive volumes and the
    /// recovery set are published beside it, which roughly doubles the disk cost
    /// and offers ED2K peers files no ED2K peer wants.
    bool cleanupEnabled = true;

    /// Whether the download already knows it is short. A release with no missing
    /// articles still gets verified — a corrupt article passes its own CRC far
    /// too rarely to matter, but a *truncated* one does not, and par2 is the
    /// only thing that would notice.
    bool hasMissingSegments = false;

    /// Check the release against its .sfv when PAR2 did not run.
    bool sfvEnabled = true;

    /// What the download sealed, by name. An .sfv entry outside this list is a
    /// sample nobody posted; one inside it and absent is damage.
    QStringList expectedNames;

    /// Lower case, no dots. Empty when the unwanted check is off, overridden
    /// for this item, or set to keep going.
    QStringList unwantedExtensions;

    /// What the queue already knows about the release: a playable name in the
    /// NZB, or a video tag in its title.
    bool mediaRelease = false;

    /// Sets already extracted during the download. Their members are on disk in
    /// the same place this would have put them, so the unpack stage skips those
    /// sets — unless a repair ran, which invalidates them by definition.
    QList<UsenetDirectUnpackResult> directUnpacked;
};

/// One payload file on its way into the incoming directory.
///
/// `source` is what makes the queue able to say which NZB file a published file
/// came *from*. Positional correspondence cannot: the payload list is sorted by
/// name, drops every .par2, and for an unpacked release holds extracted members
/// that are not NZB files at all.
struct UsenetStagedFile {
    /// Where it was staged from — a sealed UsenetFileState::tempPath for a raw
    /// release, an extracted member for an unpacked one. Empty matches nothing.
    QString source;

    /// `<incoming>/<name>.usenetpart`. The share scan skips it by name.
    QString stagedPath;

    /// The name it takes once the queue renames it in place, atomically.
    QString finalPath;
};

struct UsenetPostResult {
    QString itemId;
    bool success = false;

    /// Verification came up short and fetching recovery volumes would fix it.
    /// The queue turns this into another download round rather than a failure.
    bool needsMoreBlocks = false;
    int  blocksNeeded = 0;

    /// Staged payload, each waiting for the queue to rename it into place.
    QList<UsenetStagedFile> staged;

    /// Archive volumes and par2 files that are safe to delete. Only ever
    /// populated after a *successful* extraction.
    QStringList consumed;

    Par2Outcome par2Outcome = Par2Outcome::NoPar2Files;

    /// The release is password-protected and no password we have opens it. The
    /// GUI turns this into "Set Password…", so an ordinary failure must never
    /// set it — see UsenetUnpacker::Result::passwordRequired.
    bool passwordRequired = false;

    /// A password was supplied and refused, as opposed to missing.
    bool wrongPassword = false;

    /// PAR2 rewrote volumes — by the repair, or by the rename pass, after which
    /// par2Outcome reads Clean. Statistics only.
    bool repaired = false;

    /// Source blocks the repair restored. Statistics only.
    int blocksRepaired = 0;

    UsenetUnpackOutcome unpackOutcome = UsenetUnpackOutcome::NotRun;

    /// A media release carrying files it must not, bare names. Not a failure:
    /// the queue decides, from the user's setting, whether that pauses or fails
    /// the item, and nothing was published.
    QStringList unwantedFiles;

    QString message;
};

class UsenetPostProcessor : public QObject {
    Q_OBJECT

public:
    explicit UsenetPostProcessor(QObject* parent = nullptr);
    ~UsenetPostProcessor() override;

    /// Ask the running job to stop at its next checkpoint. Safe from any thread —
    /// this is the one piece of shared state in the module, and it is atomic
    /// precisely so the queue can set it while the pipeline is mid-scan.
    void requestStop();

public slots:
    /// Run one job. Queued from the daemon thread; the pipeline is synchronous
    /// inside it, and the event loop is only re-entered between jobs.
    void process(const eMule::usenet::UsenetPostJob& job);

signals:
    void stageChanged(const QString& itemId, int stage, int percent, const QString& detail);
    void finished(const eMule::usenet::UsenetPostResult& result);

private:
    void emitStage(const QString& itemId, PostStage stage, int percent, const QString& detail);

    /// Copy @p files into destDir under the `.usenetpart` suffix and record the
    /// rename each one is waiting for.
    bool stageForPublish(const QStringList& files, const QString& destDir,
                         UsenetPostResult& result);

    std::atomic<bool> m_stopRequested{false};
};

} // namespace eMule::usenet

Q_DECLARE_METATYPE(eMule::usenet::UsenetPostJob)
Q_DECLARE_METATYPE(eMule::usenet::UsenetPostResult)
