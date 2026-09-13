/// @file Par2Verifier.cpp
/// @brief libpar2-turbo behind a small value-returning interface.

#include "post/Par2Verifier.h"

#include "utils/Log.h"

#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QThread>

#include <vector>

#ifdef EMULE_HAVE_PAR2
#  include <par2/commandline.h>
#  include <par2/descriptionpacket.h>
#  include <par2/par2repairer.h>

#  include <exception>
#  include <iterator>
#  include <sstream>
#endif

namespace eMule::usenet {

QString describePar2Outcome(Par2Outcome outcome)
{
    switch (outcome) {
    case Par2Outcome::Unavailable:    return QObject::tr("PAR2 support is not built in");
    case Par2Outcome::NoPar2Files:    return QObject::tr("No PAR2 files");
    case Par2Outcome::Listed:         return QObject::tr("File list read");
    case Par2Outcome::Clean:          return QObject::tr("Verified");
    case Par2Outcome::RepairPossible: return QObject::tr("Repairable");
    case Par2Outcome::NeedMoreBlocks: return QObject::tr("More recovery blocks needed");
    case Par2Outcome::Repaired:       return QObject::tr("Repaired");
    case Par2Outcome::RepairFailed:   return QObject::tr("Repair failed");
    case Par2Outcome::Cancelled:      return QObject::tr("Cancelled");
    case Par2Outcome::Error:          return QObject::tr("PAR2 error");
    }
    return QObject::tr("Unknown");
}

bool Par2Verifier::available()
{
#ifdef EMULE_HAVE_PAR2
    return true;
#else
    return false;
#endif
}

QString Par2Verifier::chooseIndexFile(const QStringList& par2Paths)
{
    QString best;
    for (const QString& path : par2Paths) {
        const QString name = QFileInfo(path).fileName();

        // The index file is the one without a .volNNN+NN chunk. It carries the
        // whole file list and no recovery data, which is exactly what a first
        // verify wants: par2 loads its siblings itself from this name.
        if (!name.contains(QLatin1String(".vol"), Qt::CaseInsensitive))
            return path;

        if (best.isEmpty() || name.size() < QFileInfo(best).fileName().size())
            best = path;
    }
    return best;
}

Par2Result Par2Verifier::rename(const QString& par2Path, const QString& basePath)
{
    // doRepair must be true even though nothing is being repaired.
    // RenameTargetFiles() lives inside `if (dorepair)` (par2repairer.cpp:198),
    // so a rename pass with doRepair=false verifies, finds the misnamed files,
    // reports them, and then leaves every one of them exactly where it was.
    // renameOnly is what keeps it to perfect matches and off the recovery data.
    return run(par2Path, basePath, /*doRepair=*/true, /*renameOnly=*/true);
}

Par2Result Par2Verifier::verify(const QString& par2Path, const QString& basePath)
{
    return run(par2Path, basePath, /*doRepair=*/false, /*renameOnly=*/false);
}

Par2Result Par2Verifier::repair(const QString& par2Path, const QString& basePath)
{
    return run(par2Path, basePath, /*doRepair=*/true, /*renameOnly=*/false);
}

// ---------------------------------------------------------------------------
// Everything below here talks to libpar2 directly
// ---------------------------------------------------------------------------

#ifdef EMULE_HAVE_PAR2

namespace {

/// The 16 raw bytes, in natural order.
///
/// Not MD5Hash::print(): it prints hash[15] first (md5.cpp:53), so anything
/// built from it compares unequal to every QCryptographicHash digest and no
/// file is ever matched to its name.
QByteArray md5Bytes(const Par2::MD5Hash& hash)
{
    return QByteArray(reinterpret_cast<const char*>(hash.hash), 16);
}

/// Every non-par2 file in @p dir, as candidates for par2 to scan.
///
/// par2 never walks a directory by itself: VerifyExtraFiles() iterates exactly
/// the list it was handed. An empty list is why a rename pass reports success
/// having renamed nothing — and why a *verify* reads a damaged obfuscated file
/// as entirely missing and inflates missingBlocks to its whole block count.
/// The queue buys recovery volumes with that number.
QList<QByteArray> siblingFiles(const QString& basePath)
{
    QList<QByteArray> out;
    const QFileInfoList siblings =
        QDir(basePath).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : siblings) {
        if (fi.suffix().compare(QLatin1String("par2"), Qt::CaseInsensitive) == 0)
            continue;
        out.append(fi.absoluteFilePath().toUtf8());
    }
    return out;
}

/// argv for one par2 run.
///
/// -B is not optional. Process()'s basepath parameter is dead — grep the .cpp,
/// `basepath` is assigned in exactly one place, PreProcess(), from the
/// CommandLine — so the base directory has to travel as -B or par2 resolves
/// every source file against the wrong directory and reports the whole release
/// missing, which is indistinguishable from a genuinely dead download.
QList<QByteArray> par2ArgV(const QByteArray& baseArg, const QByteArray& parArg,
                           const QList<QByteArray>& extras)
{
    QList<QByteArray> argv{
        QByteArrayLiteral("par2"),
        QByteArrayLiteral("r"),                 // any operation; Process() takes the real flags
        QByteArrayLiteral("-B"),
        baseArg,
        parArg,
    };
    argv.append(extras);
    return argv;
}

/// Par2Repairer reports through virtuals and keeps its counters protected, so a
/// subclass is the only way to get either out.
class RepairerBridge final : public Par2::Par2Repairer {
public:
    RepairerBridge(std::ostream& sout, std::ostream& serr,
                   Par2Verifier::ProgressFn progress, Par2Verifier::CancelFn cancelCheck)
        : Par2::Par2Repairer(sout, serr, Par2::nlQuiet)
        , m_progress(std::move(progress))
        , m_cancelCheck(std::move(cancelCheck))
    {
    }

    void SigFilename(std::string filename) override
    {
        m_currentFile = QString::fromStdString(filename);
    }

    void BeginRepair() override
    {
        // The only honest signal that a repair actually ran. After a successful
        // one damagedfilecount is back to zero, so deciding "was it repaired?"
        // from the counters afterwards always answers "it was clean".
        m_repairRan = true;
        m_blocksAtRepair = int(missingblockcount);
    }

    void SigProgress(int progress) override
    {
        // Per mille, not per cent: par2repairer.cpp prints newfraction/10 with
        // one decimal. Reporting it raw would peg every progress bar at 100%.
        if (m_progress)
            m_progress(qBound(0, progress / 10, 100), m_currentFile);

        if (m_cancelCheck && m_cancelCheck())
            cancelled = true;   // honoured at the next checkpoint in the scan
    }

    [[nodiscard]] Par2Result snapshot() const
    {
        Par2Result r;
        r.missingBlocks   = int(missingblockcount);
        r.recoveryBlocks  = int(recoverypacketmap.size());
        r.availableBlocks = int(availableblockcount);
        r.sourceBlocks    = int(sourceblockcount);
        r.completeFiles   = int(completefilecount);
        r.damagedFiles    = int(damagedfilecount);
        r.missingFiles    = int(missingfilecount);
        r.renamedFiles    = int(renamedfilecount);
        r.repairedBlocks  = m_repairRan ? m_blocksAtRepair : 0;
        r.backupFiles     = backupFiles();
        return r;
    }

    /// What the set says it covers. Valid after PreProcess() alone.
    [[nodiscard]] QList<Par2SetFile> fileList() const
    {
        QList<Par2SetFile> out;
        const size_t recoverable = mainpacket ? size_t(mainpacket->RecoverableFileCount()) : 0;
        out.reserve(qsizetype(sourcefiles.size()));

        for (size_t i = 0; i < sourcefiles.size(); ++i) {
            // CreateSourceFileList() pushes its map lookup unconditionally
            // (par2repairer.cpp:1060), so a set that lost a description packet
            // leaves a null here rather than a short vector.
            const Par2::Par2RepairerSourceFile* sf = sourcefiles[i];
            const Par2::DescriptionPacket* dp = sf ? sf->GetDescriptionPacket() : nullptr;
            if (!dp)
                continue;

            Par2SetFile f;
            f.fileName    = QString::fromStdString(dp->FileName());
            f.size        = qint64(dp->FileSize());
            f.hash16k     = md5Bytes(dp->Hash16k());
            f.hashFull    = md5Bytes(dp->HashFull());
            f.recoverable = i < recoverable;
            out.append(std::move(f));
        }
        return out;
    }

    [[nodiscard]] qint64 blockSize() const
    {
        return mainpacket ? qint64(mainpacket->BlockSize()) : 0;
    }

    /// What RenameTargetFiles() moved aside instead of deleting. See
    /// Par2Result::backupFiles.
    [[nodiscard]] QStringList backupFiles() const
    {
        QStringList out;
        out.reserve(qsizetype(backuplist.size()));
        for (const Par2::DiskFile* f : backuplist) {
            if (f)
                out.append(QString::fromStdString(f->FileName()));
        }
        return out;
    }

    [[nodiscard]] bool wasCancelled() const { return cancelled; }
    [[nodiscard]] bool repairRan() const { return m_repairRan; }

private:
    Par2Verifier::ProgressFn m_progress;
    Par2Verifier::CancelFn   m_cancelCheck;
    QString                  m_currentFile;
    bool                     m_repairRan = false;
    int                      m_blocksAtRepair = 0;
};

} // namespace

// ---------------------------------------------------------------------------
// listFiles — PreProcess() and stop
// ---------------------------------------------------------------------------

Par2FileList Par2Verifier::listFiles(const QString& par2Path, const QString& basePath)
{
    Par2FileList result;

    if (par2Path.isEmpty() || !QFileInfo::exists(par2Path)) {
        result.outcome = Par2Outcome::NoPar2Files;
        result.message = QStringLiteral("no par2 file at %1").arg(par2Path);
        return result;
    }

    QString base = QDir::toNativeSeparators(QDir(basePath).absolutePath());
    if (!base.endsWith(QDir::separator()))
        base += QDir::separator();

    std::ostringstream sout;
    std::ostringstream serr;

    // No extra files: nothing is scanned, only the packets read. That is the
    // whole point — PreProcess() ends in CreateSourceFileList(), so the names
    // are known before a single source file has been opened.
    const QList<QByteArray> argStorage =
        par2ArgV(base.toUtf8(), par2Path.toUtf8(), {});

    std::vector<const char*> argv;
    argv.reserve(size_t(argStorage.size()));
    for (const QByteArray& arg : argStorage)
        argv.push_back(arg.constData());

    try {
        Par2::CommandLine commandline;
        if (!commandline.Parse(int(argv.size()), argv.data())) {
            result.outcome = Par2Outcome::Error;
            result.message = QStringLiteral("par2 rejected its own arguments");
            return result;
        }

        RepairerBridge repairer(sout, serr, {}, {});

        if (const Par2::Result pre = repairer.PreProcess(commandline); pre != Par2::eSuccess) {
            result.outcome = pre == Par2::eInsufficientCriticalData ? Par2Outcome::NoPar2Files
                                                                   : Par2Outcome::Error;
            result.message = QString::fromStdString(serr.str()).trimmed();
            return result;
        }

        result.files     = repairer.fileList();
        result.blockSize = repairer.blockSize();
        result.outcome   = result.files.isEmpty() ? Par2Outcome::NoPar2Files : Par2Outcome::Listed;
        result.message   = QString::fromStdString(serr.str()).trimmed();
    } catch (const std::exception& e) {
        result.outcome = Par2Outcome::Error;
        result.message = QString::fromUtf8(e.what());
    } catch (...) {
        result.outcome = Par2Outcome::Error;
        result.message = QStringLiteral("unknown par2 failure");
    }

    return result;
}

// ---------------------------------------------------------------------------
// run — the one call the three presets above share
// ---------------------------------------------------------------------------

Par2Result Par2Verifier::run(const QString& par2Path, const QString& basePath,
                             bool doRepair, bool renameOnly)
{
    Par2Result result;

    if (par2Path.isEmpty() || !QFileInfo::exists(par2Path)) {
        result.outcome = Par2Outcome::NoPar2Files;
        result.message = QStringLiteral("no par2 file at %1").arg(par2Path);
        return result;
    }

    QString base = QDir::toNativeSeparators(QDir(basePath).absolutePath());
    if (!base.endsWith(QDir::separator()))
        base += QDir::separator();

    std::ostringstream sout;
    std::ostringstream serr;

    // PreProcess() is not optional. It is what loads the packets and builds
    // mainpacket; Process() walks straight into mainpacket->
    // RecoverableFileCount() and segfaults on a null pointer without it. The
    // library's own par2repair() convenience function omits it and is therefore
    // just as broken — do not "simplify" to that. For why the base directory
    // travels as -B rather than as Process()'s parameter, see par2ArgV().
    const QByteArray baseArg = base.toUtf8();
    const QByteArray parArg  = par2Path.toUtf8();

    // Every pass gets the siblings, not just the rename one.
    //
    // A rename with an empty list renames nothing, which is the obvious half. The
    // expensive half is a *verify*: renameonly stops at the first partial match,
    // so a damaged obfuscated file is never renamed, and with no extra files the
    // verify never sees it either. It is then booked as an entirely missing file
    // and missingBlocks absorbs its whole block count -- the number
    // requestPar2Volumes() spends the user's allowance on, and the reason a
    // repairable release could be declared dead.
    //
    // Rebuilt on every call: the rename pass renames files on disk between them,
    // and par2 just skips a path it cannot open, so a reused list degrades in
    // silence.
    const QList<QByteArray> argStorage = par2ArgV(baseArg, parArg, siblingFiles(basePath));

    std::vector<const char*> argv;
    argv.reserve(size_t(argStorage.size()));
    for (const QByteArray& arg : argStorage)
        argv.push_back(arg.constData());

    try {
        Par2::CommandLine commandline;
        if (!commandline.Parse(int(argv.size()), argv.data())) {
            result.outcome = Par2Outcome::Error;
            result.message = QStringLiteral("par2 rejected its own arguments");
            return result;
        }

        RepairerBridge repairer(sout, serr, m_progress, m_cancel);

        if (const Par2::Result pre = repairer.PreProcess(commandline); pre != Par2::eSuccess) {
            result = repairer.snapshot();
            result.outcome = pre == Par2::eInsufficientCriticalData ? Par2Outcome::NoPar2Files
                                                                   : Par2Outcome::Error;
            result.message = QString::fromStdString(serr.str()).trimmed();
            return result;
        }

        // The extra files have to be handed over twice. PreProcess() reads them
        // off the CommandLine; Process() ignores that and uses its own
        // parameter, so passing {} here leaves VerifyExtraFiles with nothing to
        // scan and a rename pass reports success having renamed nothing.
        //
        // Recovery volumes need no such help: LoadPacketsFromOtherFiles() finds
        // the sibling .volNNN+NN.par2 files from the index name on its own,
        // which is what lets a re-run pick up volumes fetched after the first
        // pass without being told about them.
        const Par2::Result rc = repairer.Process(
            /*memorylimit*/ 256,                       // megabytes; par2cmdline's own floor
            base.toStdString(),                        // ignored; see the note above
            /*nthreads*/ quint32(qMax(1, QThread::idealThreadCount())),
            /*filethreads*/ 2,
            par2Path.toStdString(),
            commandline.GetExtraFiles(),
            doRepair,
            /*purgefiles*/ false,                      // we own cleanup; see UsenetQueue
            renameOnly,
            /*skipdata*/ false,
            /*skipleaway*/ 0);

        result = repairer.snapshot();
        result.message = QString::fromStdString(serr.str()).trimmed();

        if (repairer.wasCancelled()) {
            result.outcome = Par2Outcome::Cancelled;
            return result;
        }

        switch (rc) {
        case Par2::eSuccess:
            result.outcome = repairer.repairRan() ? Par2Outcome::Repaired : Par2Outcome::Clean;
            break;
        case Par2::eRepairPossible:
            // Only reachable with dorepair=false; a repair run that could repair
            // does so and returns eSuccess.
            result.outcome = Par2Outcome::RepairPossible;
            break;
        case Par2::eRepairNotPossible:
            // Two different situations share this code: short of recovery
            // blocks, and enough blocks but the repair could not be attempted.
            // blocksStillNeeded() is what tells the queue whether fetching more
            // volumes would help, so distinguish them rather than always asking
            // for blocks that would not change the answer.
            result.outcome = result.blocksStillNeeded() > 0 ? Par2Outcome::NeedMoreBlocks
                                                            : Par2Outcome::RepairFailed;
            break;
        case Par2::eRepairFailed:
            result.outcome = Par2Outcome::RepairFailed;
            break;
        case Par2::eInsufficientCriticalData:
            result.outcome = Par2Outcome::NoPar2Files;
            break;
        default:
            result.outcome = Par2Outcome::Error;
            break;
        }
    } catch (const std::exception& e) {
        // The library throws for out-of-memory and a few IO paths. A repair
        // failing must not take the daemon with it.
        result.outcome = Par2Outcome::Error;
        result.message = QString::fromUtf8(e.what());
    } catch (...) {
        result.outcome = Par2Outcome::Error;
        result.message = QStringLiteral("unknown par2 failure");
    }

    if (result.outcome == Par2Outcome::Error || result.outcome == Par2Outcome::RepairFailed) {
        logWarning(QStringLiteral("PAR2: %1 on \"%2\": %3")
                       .arg(describePar2Outcome(result.outcome),
                            QFileInfo(par2Path).fileName(),
                            result.message.isEmpty() ? QStringLiteral("no detail")
                                                     : result.message));
    }
    return result;
}

#else // !EMULE_HAVE_PAR2

Par2FileList Par2Verifier::listFiles(const QString& par2Path, const QString& basePath)
{
    Q_UNUSED(par2Path)
    Q_UNUSED(basePath)

    return {};   // outcome defaults to Unavailable
}

Par2Result Par2Verifier::run(const QString& par2Path, const QString& basePath,
                             bool doRepair, bool renameOnly)
{
    Q_UNUSED(par2Path)
    Q_UNUSED(basePath)
    Q_UNUSED(doRepair)
    Q_UNUSED(renameOnly)

    Par2Result result;
    result.outcome = Par2Outcome::Unavailable;
    return result;
}

#endif // EMULE_HAVE_PAR2

} // namespace eMule::usenet
