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

#include <QString>
#include <QStringList>

#include <functional>

namespace eMule::usenet {

enum class Par2Outcome {
    Unavailable,     ///< Built without libpar2-turbo.
    NoPar2Files,     ///< No usable par2 set; nothing could be checked.
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
    /// different name. Non-zero after a rename pass means an obfuscated release
    /// just got its real filenames back.
    int renamedFiles = 0;

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
    /// Every non-par2 file in @p basePath is offered as a rename candidate.
    /// par2 only ever scans files handed to it — it does not walk the directory
    /// on its own — so a rename pass with nothing to scan silently does nothing.
    Par2Result rename(const QString& par2Path, const QString& basePath);

    /// Check without changing anything.
    Par2Result verify(const QString& par2Path, const QString& basePath);

    /// Check and rewrite damaged files from the recovery blocks.
    Par2Result repair(const QString& par2Path, const QString& basePath);

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
