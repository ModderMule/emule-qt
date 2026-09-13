#pragma once

/// @file Par2Verifier.h
/// @brief PAR2 verify, repair and rename, wrapped around libpar2-turbo.
///
/// Three operations over one library call. `Par2Repairer::Process()` takes
/// `dorepair` and `renameonly` flags and everything else is identical, so this
/// exists to give those three combinations names, to turn the library's
/// protected block counters into a value type, and to keep every par2 header out
/// of the rest of the module.
///
/// `listFiles()` is the fourth, and it stops earlier than the other three: the
/// recovery set's filenames are known after PreProcess(), before any data is
/// scanned. That is what lets an obfuscated release be named while it is still
/// downloading rather than after.
///
/// The counters are the point. `missingBlocks` is what decides how many recovery
/// volumes the queue still has to fetch — see UsenetQueue's on-demand par2 pass.
/// Verify tells you the number; nothing else in the pipeline can.
///
/// Optional at build time. Without EMULE_HAVE_PAR2 every call returns
/// Par2Outcome::Unavailable and the pipeline says so rather than quietly
/// treating a damaged release as fine.
///
/// Blocking and synchronous on purpose: a repair is minutes of CPU and disk, and
/// UsenetPostProcessor already owns a thread to run it on.

#include <QtGlobal>

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace eMule::usenet {

enum class Par2Outcome {
    Unavailable,     ///< Built without libpar2-turbo.
    NoPar2Files,     ///< No usable par2 set; nothing could be checked.
    Listed,          ///< The set's file list was read; nothing was verified.
    Clean,           ///< Every source file verified intact.
    RepairPossible,  ///< Damaged, and enough recovery blocks are already present.
    NeedMoreBlocks,  ///< Damaged, and short of recovery blocks. Fetch more.
    Repaired,        ///< Damaged, repaired, verified.
    RepairFailed,    ///< Repair ran and the files are still wrong.
    Cancelled,
    Error,
};

[[nodiscard]] QString describePar2Outcome(Par2Outcome outcome);

struct Par2Result {
    Par2Outcome outcome = Par2Outcome::Unavailable;

    /// Source blocks that are damaged or missing — the number of recovery
    /// blocks a repair would consume.
    int missingBlocks = 0;

    /// Recovery blocks the par2 files currently on disk actually supply.
    /// par2cmdline's own rule, verbatim, is that repair is possible exactly when
    /// `recoveryBlocks >= missingBlocks` (par2repairer.cpp:2223).
    int recoveryBlocks = 0;

    /// Source blocks that verified intact. Informational — it is *not* the
    /// recovery-block figure, and reading it as one makes a short release look
    /// repairable.
    int availableBlocks = 0;

    int sourceBlocks   = 0;
    int completeFiles  = 0;
    int damagedFiles   = 0;
    int missingFiles   = 0;

    /// Files that verified against the par2 set but were stored under a
    /// different name. Non-zero after rename() means an obfuscated release just
    /// got its real filenames back.
    ///
    /// verify() can report it too, and there it means only *found under the
    /// wrong name* — RenameTargetFiles() lives inside `if (dorepair)`, so
    /// nothing was moved. Read it from rename(), not from a check.
    int renamedFiles = 0;

    /// Source blocks a repair restored; 0 when none ran. Captured as the repair
    /// begins, because afterwards the counters above read clean.
    int repairedBlocks = 0;

    /// Files par2 moved aside rather than deleted.
    ///
    /// RenameTargetFiles() renames a *damaged* target with DiskFile::Rename(void)
    /// — no argument — which appends ".1" and pushes it on par2's backup list;
    /// CreateTargetFiles() then builds the correct name and repairs into it. We
    /// pass purgefiles=false, so they stay. They are byte-for-byte a damaged copy
    /// of a payload file sitting next to the repaired one, and the post-processor
    /// publishes every non-par2 file in the work directory — so somebody has to
    /// delete them, which means somebody has to be told what they are.
    QStringList backupFiles;

    /// The library's own stdout/stderr, kept for the log when something failed.
    QString message;

    [[nodiscard]] bool ok() const
    {
        return outcome == Par2Outcome::Clean || outcome == Par2Outcome::Repaired;
    }

    /// How many further recovery blocks the queue has to fetch before a repair
    /// can succeed. Zero when the set is already repairable — which includes the
    /// clean case.
    [[nodiscard]] int blocksStillNeeded() const
    {
        return qMax(0, missingBlocks - recoveryBlocks);
    }
};

/// PAR2's fixed identity window: a file is identified by its length plus the
/// MD5 of its first this-many bytes. A file shorter than this hashes whole, so
/// there hash16k and hashFull are the same 16 bytes.
inline constexpr qint64 kPar2Hash16kBytes = 16384;

/// One file a recovery set names, straight out of its description packet.
struct Par2SetFile {
    /// As posted. PAR2 stores *paths*, so this can carry directory components
    /// and comes off Usenet as untrusted as an NZB subject — run it through
    /// QFileInfo::fileName() and sanitizeName() before touching the disk.
    QString fileName;

    qint64 size = 0;

    /// Raw 16 bytes, natural MD5 order.
    ///
    /// Never MD5Hash::print(): that prints hash[15] first (md5.cpp:53), so a
    /// hex round trip through it compares unequal to every QCryptographicHash
    /// digest and nothing is ever matched.
    QByteArray hash16k;
    QByteArray hashFull;

    /// In the recovery set, as opposed to a file the set merely lists.
    bool recoverable = true;
};

/// What a recovery set says it covers, read without verifying anything.
struct Par2FileList {
    Par2Outcome outcome = Par2Outcome::Unavailable;

    /// Main-packet order, recoverable files first. Entries par2 could not build
    /// a description for are simply absent — a partial list is normal.
    QList<Par2SetFile> files;

    qint64 blockSize = 0;
    QString message;

    [[nodiscard]] bool ok() const { return outcome == Par2Outcome::Listed; }
};

class Par2Verifier {
public:
    /// Whether this build can verify at all. False makes every call below return
    /// Par2Outcome::Unavailable.
    [[nodiscard]] static bool available();

    /// Called with 0-100 and the file being worked on. May be called often; it
    /// runs on the calling thread, so it must not block.
    using ProgressFn = std::function<void(int percent, const QString& fileName)>;

    /// Polled during the scan. Returning true aborts at the next checkpoint and
    /// yields Par2Outcome::Cancelled.
    using CancelFn = std::function<bool()>;

    Par2Verifier() = default;

    void setProgressCallback(ProgressFn fn) { m_progress = std::move(fn); }
    void setCancelCheck(CancelFn fn) { m_cancel = std::move(fn); }

    /// Restore real filenames from the par2 metadata, touching nothing else.
    /// Run this first: an obfuscated release has names the verifier cannot match
    /// and the unpacker cannot recognise as volume 1.
    ///
    /// Only *perfect* matches are renamed — par2's renameonly stops at the first
    /// partial one — so a damaged file keeps its meaningless name and it is
    /// verify() below that has to cope with it.
    Par2Result rename(const QString& par2Path, const QString& basePath);

    /// Check without changing anything.
    ///
    /// Every non-par2 file in @p basePath is offered for scanning, here as much
    /// as in rename(): par2 never walks a directory on its own, and a verify
    /// with nothing to scan reads a *damaged obfuscated* file as an entirely
    /// missing one — inflating missingBlocks to its whole block count, which is
    /// the figure the queue buys recovery volumes with.
    Par2Result verify(const QString& par2Path, const QString& basePath);

    /// Check and rewrite damaged files from the recovery blocks.
    Par2Result repair(const QString& par2Path, const QString& basePath);

    /// The names the recovery set expects, from PreProcess() alone.
    ///
    /// No data is scanned and no source file has to exist: PreProcess() ends in
    /// CreateSourceFileList(), which is where the description packets become
    /// filenames. Roughly the cost of reading the .par2 once, which is what
    /// makes it affordable while a release is still downloading — and that is
    /// the only time the answer can still change what gets fetched.
    ///
    /// It also loads the `name.*.par2` siblings, so it is cheap during a
    /// download (only the index is on disk) and not cheap afterwards.
    [[nodiscard]] Par2FileList listFiles(const QString& par2Path, const QString& basePath);

    /// The `.par2` file to drive a set from: the index file — the one with no
    /// `.volNNN+NN` — if the list has one, else the shortest name, which is the
    /// same thing by another route. Returns empty for an empty list.
    [[nodiscard]] static QString chooseIndexFile(const QStringList& par2Paths);

private:
    Par2Result run(const QString& par2Path, const QString& basePath,
                   bool doRepair, bool renameOnly);

    ProgressFn m_progress;
    CancelFn   m_cancel;
};

} // namespace eMule::usenet
