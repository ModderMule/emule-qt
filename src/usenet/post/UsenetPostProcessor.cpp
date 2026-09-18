/// @file UsenetPostProcessor.cpp
/// @brief verify → repair → rename → unpack → stage, in that order and on one thread.

#include "post/UsenetPostProcessor.h"

#include "post/Par2NameIndex.h"
#include "post/UsenetReleaseChecks.h"
#include "post/UsenetUnpacker.h"
#include "prefs/Preferences.h"
#include "queue/UsenetQueueItem.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>

namespace eMule::usenet {

namespace {

/// Where the unpacker writes, inside the item's own scratch folder. Underscored
/// so it cannot collide with a release that happens to contain a directory of
/// its own, and inside workDir so a failed job leaves nothing behind after the
/// queue removes the item directory.

/// Every PAR2 packet begins with this (par2fileformat.cpp:25), so a file that
/// starts with it is a par2 file whatever it is called.
constexpr char kPar2Magic[] = "PAR2\0PKT";
constexpr int kPar2MagicLen = 8;

[[nodiscard]] bool looksLikePar2(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.read(kPar2MagicLen) == QByteArray(kPar2Magic, kPar2MagicLen);
}

/// The par2 files of a release, by extension and then by content.
///
/// The content check is not redundant. A post that obfuscates the .par2
/// *subjects* as well leaves nothing with the extension: NzbFileInfo::isPar2()
/// is a substring test over the filename or the subject, so the queue never
/// recognises those files either, and such a release is today never verified,
/// never repaired and never unpacked — in silence. Nothing can fix that before
/// the bytes land, because you cannot fetch first a file you cannot identify;
/// once they have, the file says what it is.
[[nodiscard]] QStringList par2FilesIn(const QString& dir)
{
    QStringList result;
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                          QDir::Name);
    for (const QFileInfo& fi : entries) {
        if (fi.suffix().compare(QLatin1String("par2"), Qt::CaseInsensitive) == 0) {
            result.append(fi.absoluteFilePath());
            continue;
        }
        if (fi.size() >= kPar2MagicLen && looksLikePar2(fi.absoluteFilePath())) {
            logInfo(QStringLiteral("Usenet: \"%1\" is a PAR2 file under another name")
                        .arg(fi.fileName()));
            result.append(fi.absoluteFilePath());
        }
    }
    return result;
}

/// Whether what a direct unpack produced is still on disk and the right size.
/// Cheap insurance: a half-deleted or truncated result must fall back to a real
/// unpack rather than be published as a finished release.
[[nodiscard]] bool directUnpackStillValid(const UsenetDirectUnpackResult& done)
{
    if (!done.ok || done.extracted.isEmpty())
        return false;
    for (const QString& path : done.extracted) {
        if (!QFileInfo::exists(path))
            return false;
    }
    return true;
}

/// Delete what par2 moved aside rather than deleted.
///
/// RenameTargetFiles() renames a damaged target to "<name>.1" and keeps it,
/// because we pass purgefiles=false -- and we pass false on purpose, since
/// par2's purge deletes RemoveParFiles() too and the queue's on-demand recovery
/// round would then reload a set that is gone. So the backups are ours to clear,
/// and they have to go before anything walks this folder: they are a
/// known-damaged copy of a payload file, and publishing one offers it to ED2K
/// peers as if it were the release.
///
/// Not routed through `consumed`: that list is "the originals", which the
/// cleanup preference is allowed to keep. These are par2's scratch, and
/// VerifyTargetFiles() has already proved the repaired file correct.
void removePar2Backups(const QStringList& backups)
{
    for (const QString& path : backups) {
        if (!QFile::exists(path))
            continue;
        if (QFile::remove(path))
            logInfo(QStringLiteral("Usenet: removed PAR2 backup \"%1\"")
                        .arg(QFileInfo(path).fileName()));
        else
            logWarning(QStringLiteral("Usenet: cannot remove PAR2 backup \"%1\"").arg(path));
    }
}

/// Delete copies of a covered file that are sitting under some other name.
///
/// A repair does not overwrite what it repaired. When the damaged file carried
/// the *right* name par2 moves it aside as "<name>.1" and reports it, which
/// removePar2Backups() clears. When it carried an obfuscated one, par2 never
/// matched a target to it at all: it built the correct file from the blocks it
/// could read and left the original exactly where it was, unreported. The work
/// folder then holds the release twice, once good and once damaged, and
/// payloadFilesIn() would offer both to ED2K peers.
///
/// The rule is that a covered file is published under its covered name and
/// nowhere else. hash16k identifies an undamaged copy exactly; a damaged one
/// cannot be hashed into place — the damage may be *inside* the identity window,
/// which is the very reason it was never named — so a file of exactly a covered
/// file's length, under a name the set does not use, while the properly named
/// file is right there, counts too.
void removeStaleCopiesOfCoveredFiles(const QString& par2Path, const QString& dir)
{
    Par2Verifier verifier;
    const Par2FileList list = verifier.listFiles(par2Path, dir);
    if (!list.ok())
        return;

    QSet<QString> coveredNames;
    QHash<qint64, QString> coveredBySize;
    Par2NameIndex names;
    names.setFiles(list.files);

    for (const Par2SetFile& f : list.files) {
        if (!f.recoverable)
            continue;
        const QString bare = QFileInfo(f.fileName).fileName();
        coveredNames.insert(bare);
        if (QFileInfo::exists(QDir(dir).filePath(bare)))
            coveredBySize.insert(f.size, bare);
    }
    if (coveredNames.isEmpty())
        return;

    for (const QFileInfo& fi : QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        if (coveredNames.contains(fi.fileName()))
            continue;
        if (fi.suffix().compare(QLatin1String("par2"), Qt::CaseInsensitive) == 0)
            continue;
        if (fi.size() >= kPar2MagicLen && looksLikePar2(fi.absoluteFilePath()))
            continue;

        QString of = names.matchFile(fi.absoluteFilePath(), fi.size());
        if (of.isEmpty())
            of = coveredBySize.value(fi.size());
        if (of.isEmpty())
            continue;

        // Only when the good one is actually there. Otherwise this *is* the
        // file, merely misnamed, and deleting it would destroy the release.
        const QString good = QFileInfo(of).fileName();
        if (!QFileInfo::exists(QDir(dir).filePath(good)))
            continue;

        if (QFile::remove(fi.absoluteFilePath())) {
            logInfo(QStringLiteral("Usenet: removed \"%1\", a stale copy of \"%2\"")
                        .arg(fi.fileName(), good));
        }
    }
}

/// What is left in the work folder that could be the release itself.
///
/// The two exclusions are the same ones par2FilesIn() includes, and they have to
/// stay in step: recovery data is of no use to an ED2K peer, and a par2 file
/// posted under a name that hides it would otherwise be published as if it were
/// the movie.
[[nodiscard]] QStringList payloadFilesIn(const QString& dir)
{
    QStringList result;
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                          QDir::Name);
    for (const QFileInfo& fi : entries) {
        if (fi.suffix().compare(QLatin1String("par2"), Qt::CaseInsensitive) == 0)
            continue;
        if (fi.size() >= kPar2MagicLen && looksLikePar2(fi.absoluteFilePath()))
            continue;
        result.append(fi.absoluteFilePath());
    }
    return result;
}

/// What a verify says about the files the user skipped.
///
/// Judged on whole files, never on per-file block counts: par2 credits a block
/// to wherever its content turns up, so a damaged file whose lost block exists
/// elsewhere could read as short of nothing and be published unrepaired.
struct SkippedJudgement {
    bool onlySkipped = false;   ///< every incomplete file is a skipped one
    QList<int> needed;          ///< skipped files a repair of the rest needs
};

[[nodiscard]] SkippedJudgement judgeSkippedFiles(const Par2Result& check,
                                                 const UsenetPostJob& job)
{
    SkippedJudgement out;
    if (check.files.isEmpty())
        return out;   // no per-file detail, no opinion

    const auto key = [](const QString& name) {
        return sanitizeName(QFileInfo(name).fileName()).toCaseFolded();
    };
    QHash<QString, int> owner;
    for (const UsenetPostJob::SkippedFile& s : job.skippedFiles) {
        for (const QString& n : s.names)
            owner.insert(key(n), s.fileIndex);
    }

    QSet<int> matched;
    bool unexplained = false;
    bool absentUnexplained = false;
    for (const Par2FileStatus& f : check.files) {
        if (f.complete)
            continue;
        if (const auto it = owner.constFind(key(f.fileName)); it != owner.cend()) {
            matched.insert(*it);
            continue;
        }
        unexplained = true;
        if (!f.targetExists)
            absentUnexplained = true;
    }

    if (!unexplained) {
        out.onlySkipped = !matched.isEmpty();
        return out;
    }

    // Real damage. A skipped file posted under a name the set does not use
    // cannot be told from a lost one, so an absent file nothing names brings
    // every skipped file back: bytes are cheaper than a wrong verdict.
    for (const UsenetPostJob::SkippedFile& s : job.skippedFiles) {
        if (absentUnexplained || matched.contains(s.fileIndex))
            out.needed.append(s.fileIndex);
    }
    return out;
}

} // namespace

QString describePostStage(PostStage stage)
{
    switch (stage) {
    case PostStage::Idle:      return QObject::tr("Idle");
    case PostStage::Verifying: return QObject::tr("Verifying");
    case PostStage::Repairing: return QObject::tr("Repairing");
    case PostStage::Unpacking: return QObject::tr("Unpacking");
    case PostStage::Staging:   return QObject::tr("Finishing");
    }
    return QObject::tr("Unknown");
}

QString uniqueDestination(const QString& dir, const QString& name)
{
    QDir d(dir);
    QString candidate = d.filePath(name);
    if (!QFile::exists(candidate))
        return candidate;

    const QFileInfo fi(name);
    const QString base = fi.completeBaseName();
    const QString suffix = fi.suffix().isEmpty() ? QString()
                                                 : QLatin1Char('.') + fi.suffix();
    for (int n = 1; n < 10000; ++n) {
        candidate = d.filePath(QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix));
        if (!QFile::exists(candidate))
            return candidate;
    }
    return d.filePath(name + QStringLiteral(".dup"));
}

// ---------------------------------------------------------------------------

UsenetPostProcessor::UsenetPostProcessor(QObject* parent)
    : QObject(parent)
{
}

UsenetPostProcessor::~UsenetPostProcessor() = default;

void UsenetPostProcessor::requestStop()
{
    m_stopRequested = true;
}

void UsenetPostProcessor::process(const UsenetPostJob& job)
{
    m_stopRequested = false;

    UsenetPostResult result;
    result.itemId = job.itemId;

    // Whether PAR2 rewrote any of the volumes, at any stage. Anything unpacked
    // from them while they were downloading came from bytes that no longer
    // exist, so it cannot be trusted.
    bool par2Repaired = false;

    const QStringList par2Files = par2FilesIn(job.workDir);
    const bool canVerify = job.par2Enabled && Par2Verifier::available() && !par2Files.isEmpty();

    // .sfv files a clean check has read; cleanup treats them like the par2 set.
    QStringList verifiedSfvFiles;

    // -- verify, and repair if we can -------------------------------------
    if (canVerify) {
        const QString index = Par2Verifier::chooseIndexFile(par2Files);

        Par2Verifier verifier;
        verifier.setCancelCheck([this] { return m_stopRequested.load(); });

        if (job.renameEnabled) {
            // Before verify, not after. An obfuscated release verifies as
            // entirely missing until its real names are back, and the unpacker
            // cannot pick a first volume out of hex strings either.
            emitStage(job.itemId, PostStage::Verifying, 0, QObject::tr("Checking names"));
            verifier.setProgressCallback([&](int percent, const QString& file) {
                emitStage(job.itemId, PostStage::Verifying, percent, file);
            });
            const Par2Result renamed = verifier.rename(index, job.workDir);
            removePar2Backups(renamed.backupFiles);
            // par2's rename pass repairs as a side effect: RenameTargetFiles()
            // lives inside `if (dorepair)`, so a renameonly run still rewrites
            // damaged data. The verify that follows then reports Clean, which
            // makes the *later* outcome useless as a "were the volumes touched"
            // signal — so record it here.
            par2Repaired = par2Repaired || renamed.outcome == Par2Outcome::Repaired;
            result.repaired = par2Repaired;
            result.blocksRepaired += renamed.repairedBlocks;
            if (renamed.renamedFiles > 0) {
                logInfo(QStringLiteral("Usenet: recovered %1 filename(s) from PAR2")
                            .arg(renamed.renamedFiles));
            }
        }

        emitStage(job.itemId, PostStage::Verifying, 0, QString());
        verifier.setProgressCallback([&](int percent, const QString& file) {
            emitStage(job.itemId, PostStage::Verifying, percent, file);
        });

        Par2Result check = verifier.verify(index, job.workDir);
        result.par2Outcome = check.outcome;

        if (check.outcome == Par2Outcome::Cancelled) {
            result.message = QObject::tr("Cancelled");
            emit finished(result);
            return;
        }

        // A skipped file reads as missing. That alone is no damage; beside real
        // damage the repair needs it, and fetching it beats buying its blocks.
        bool onlySkippedMissing = false;
        if (!job.skippedFiles.isEmpty()
            && (check.outcome == Par2Outcome::NeedMoreBlocks
                || check.outcome == Par2Outcome::RepairPossible)) {
            const SkippedJudgement judged = judgeSkippedFiles(check, job);
            if (judged.onlySkipped) {
                onlySkippedMissing = true;
                result.par2Outcome = Par2Outcome::Clean;
                logInfo(QStringLiteral("Usenet: \"%1\" verified; only skipped files are absent")
                            .arg(job.itemId));
            } else if (check.outcome == Par2Outcome::NeedMoreBlocks && !judged.needed.isEmpty()) {
                result.needsSkippedFiles = judged.needed;
                result.message = QObject::tr("The repair needs %n skipped file(s)", nullptr,
                                             int(judged.needed.size()));
                emit finished(result);
                return;
            }
        }

        if (!onlySkippedMissing && check.outcome == Par2Outcome::NeedMoreBlocks) {
            // Not a failure. The queue skipped the recovery volumes on purpose;
            // this is the moment it finds out how many of them it actually needs.
            result.needsMoreBlocks = true;
            result.blocksNeeded = check.blocksStillNeeded();
            result.message = QObject::tr("Needs %n more recovery block(s)",
                                         nullptr, result.blocksNeeded);
            emit finished(result);
            return;
        }

        if (!onlySkippedMissing && check.outcome == Par2Outcome::RepairPossible) {
            emitStage(job.itemId, PostStage::Repairing, 0, QString());
            verifier.setProgressCallback([&](int percent, const QString& file) {
                emitStage(job.itemId, PostStage::Repairing, percent, file);
            });

            const Par2Result repaired = verifier.repair(index, job.workDir);
            removePar2Backups(repaired.backupFiles);
            result.par2Outcome = repaired.outcome;
            par2Repaired = par2Repaired || repaired.outcome == Par2Outcome::Repaired;
            result.repaired = par2Repaired;
            result.blocksRepaired += repaired.repairedBlocks;

            if (!repaired.ok()) {
                result.message = repaired.outcome == Par2Outcome::Cancelled
                    ? QObject::tr("Cancelled")
                    : QObject::tr("Repair failed: %1").arg(describePar2Outcome(repaired.outcome));
                emit finished(result);
                return;
            }
            logInfo(QStringLiteral("Usenet: repaired \"%1\"").arg(job.itemId));
        } else if (!onlySkippedMissing && !check.ok()) {
            result.message = QObject::tr("Verification failed: %1")
                                 .arg(describePar2Outcome(check.outcome));
            emit finished(result);
            return;
        }

        // The set is good now, so every file it covers is on disk under its own
        // name — and anything else that is a copy of one is a leftover of the
        // repair, not part of the release.
        removeStaleCopiesOfCoveredFiles(index, job.workDir);
    } else if (job.hasMissingSegments) {
        // The one case phase 3 got wrong: a short file used to be published and
        // offered to peers regardless. Without a usable recovery set there is
        // nothing to repair it with, so it is a failure and it is not shared.
        result.message = Par2Verifier::available()
            ? QObject::tr("Articles are missing and the release has no PAR2 files")
            : QObject::tr("Articles are missing and this build has no PAR2 support");
        emit finished(result);
        return;
    } else if (job.sfvEnabled) {
        // No PAR2 ran: none shipped, it is off, or this build has none. An .sfv
        // cannot repair anything, but it can refuse what arrived damaged.
        emitStage(job.itemId, PostStage::Verifying, 0, QObject::tr("Checking SFV"));
        const SfvCheck sfv = verifySfv(
            job.workDir, job.expectedNames,
            [&](int percent, const QString& file) {
                emitStage(job.itemId, PostStage::Verifying, percent, file);
            },
            [this] { return m_stopRequested.load(); });

        if (sfv.outcome == SfvCheck::Outcome::Cancelled) {
            result.message = QObject::tr("Cancelled");
            emit finished(result);
            return;
        }
        if (!sfv.unposted.isEmpty()) {
            logInfo(QStringLiteral("Usenet: the SFV lists file(s) this release never posted: %1")
                        .arg(describeFileList(sfv.unposted)));
        }
        if (sfv.outcome == SfvCheck::Outcome::NothingToCheck) {
            logInfo(QStringLiteral("Usenet: the SFV of \"%1\" lists none of its files; not checked")
                        .arg(job.itemId));
        }
        if (sfv.outcome == SfvCheck::Outcome::Damaged) {
            // Extracted while downloading, from volumes now known to be bad.
            for (const UsenetDirectUnpackResult& done : job.directUnpacked) {
                for (const QString& path : done.extracted)
                    QFile::remove(path);
            }
            result.message = QObject::tr("SFV check failed: %n file(s) damaged (%1)", nullptr,
                                         int(sfv.damaged.size()))
                                 .arg(describeFileList(sfv.damaged));
            emit finished(result);
            return;
        }
        if (sfv.outcome == SfvCheck::Outcome::Clean) {
            logInfo(QStringLiteral("Usenet: %1 file(s) passed the SFV check").arg(sfv.checked));
            verifiedSfvFiles = sfv.sfvFiles;
        }
    }

    // Skipped files a repair fetched back or rebuilt: checked, never published.
    for (const QString& name : job.discardAfterVerify) {
        const QString path = QDir(job.workDir).filePath(name);
        if (QFileInfo(path).isFile() && QFile::remove(path))
            logInfo(QStringLiteral("Usenet: dropped skipped file \"%1\"").arg(name));
    }

    if (m_stopRequested) {
        result.message = QObject::tr("Cancelled");
        emit finished(result);
        return;
    }

    // -- unpack -------------------------------------------------------------
    QStringList payload;
    QStringList consumed;

    if (job.unpackEnabled) {
        emitStage(job.itemId, PostStage::Unpacking, 0, QString());

        const QString unpackDir = QDir(job.workDir).filePath(QString(kUnpackDirName));

        // A repair rewrote the volumes, so anything extracted from them while
        // they were downloading came from bytes that no longer exist. Throw it
        // away and unpack the repaired set instead. A clean verify means the
        // volumes were right all along, and so is what came out of them.
        const bool volumesRewritten = par2Repaired;

        QSet<QString> skip;
        QStringList directPayload;
        QStringList directConsumed;
        for (const UsenetDirectUnpackResult& done : job.directUnpacked) {
            if (volumesRewritten || !directUnpackStillValid(done)) {
                for (const QString& path : done.extracted)
                    QFile::remove(path);
                continue;
            }
            skip.insert(done.firstVolume);
            directPayload += done.extracted;
            directConsumed += done.consumed;
        }
        if (volumesRewritten && !job.directUnpacked.isEmpty()) {
            logInfo(QStringLiteral("Usenet: discarding what was unpacked during the "
                                   "download; the release needed a repair"));
        }

        UsenetUnpacker unpacker;
        unpacker.setProgressCallback([&](int percent, const QString& file) {
            emitStage(job.itemId, PostStage::Unpacking, percent, file);
        });

        if (!job.unwantedExtensions.isEmpty()) {
            unpacker.setVeto([&job](const QStringList& names) {
                return unwantedFileNames(names, job.unwantedExtensions, job.mediaRelease);
            });
        }

        const auto unpacked =
            unpacker.unpack(job.workDir, unpackDir, job.password, skip, job.externalUnpacker);

        if (!unpacked.vetoed.isEmpty()) {
            // Nothing extracted from this release stays. The volumes do, so a
            // Resume that overrules the check unpacks it again from them.
            for (const QString& path : unpacked.extractedFiles)
                QFile::remove(path);
            for (const QString& path : std::as_const(directPayload))
                QFile::remove(path);
            result.unwantedFiles = unpacked.vetoed;
            result.message = QObject::tr("Contains unwanted files: %1")
                                 .arg(describeFileList(unpacked.vetoed));
            emit finished(result);
            return;
        }

        if (!unpacked.ok) {
            // Carried before the early return, or the GUI cannot tell a release
            // that needs a password from one that is simply broken.
            result.passwordRequired = unpacked.passwordRequired;
            result.wrongPassword = unpacked.wrongPassword;
            result.unpackOutcome = unpacked.passwordRequired ? UsenetUnpackOutcome::PasswordRequired
                                                             : UsenetUnpackOutcome::Failed;
            result.message = unpacked.error;
            emit finished(result);
            return;
        }

        if (unpacked.nothingToDo && directPayload.isEmpty()) {
            result.unpackOutcome = UsenetUnpackOutcome::NothingToUnpack;
            payload = payloadFilesIn(job.workDir);
        } else {
            result.unpackOutcome = UsenetUnpackOutcome::Unpacked;
            // A par2 rename between the download and here would stale the skip
            // key, so a set can be extracted twice over the same output. Both
            // lists then name it; de-duplicate rather than publish it twice.
            payload = directPayload;
            for (const QString& path : unpacked.extractedFiles) {
                if (!payload.contains(path))
                    payload.append(path);
            }
            consumed = directConsumed;
            for (const QString& path : unpacked.consumedArchives) {
                if (!consumed.contains(path))
                    consumed.append(path);
            }
        }
    } else {
        payload = payloadFilesIn(job.workDir);
    }

    // Publishing nothing is never a success, and `consumed` is a delete list.
    //
    // Belt and braces over the same guard in UsenetUnpacker: the two failures
    // this catches — an archive that lists no members, and a payload that got
    // filtered away — reach here by different routes, and the consequence is the
    // same either way. A release that unpacked to nothing used to be marked
    // Complete with every volume deleted after it.
    if (payload.isEmpty() && !consumed.isEmpty()) {
        result.message = QObject::tr("Nothing could be published from this release");
        emit finished(result);
        return;
    }

    // The last look before publishing, over everything together: a set judged
    // alone may not have known the release was a movie, and a release with no
    // archives had no member list for anyone to judge.
    if (!job.unwantedExtensions.isEmpty()) {
        QStringList names;
        for (const QString& path : std::as_const(payload))
            names.append(QFileInfo(path).fileName());
        QStringList unwanted = unwantedFileNames(names, job.unwantedExtensions, job.mediaRelease);
        for (const QString& path : std::as_const(payload)) {
            if (isFakeMediaFile(path))
                unwanted.append(QFileInfo(path).fileName());
        }
        unwanted.removeDuplicates();

        if (!unwanted.isEmpty()) {
            // Extracted files go; downloaded ones are the release itself and stay.
            if (result.unpackOutcome == UsenetUnpackOutcome::Unpacked) {
                for (const QString& path : std::as_const(payload))
                    QFile::remove(path);
            }
            result.unwantedFiles = unwanted;
            result.message =
                QObject::tr("Contains unwanted files: %1").arg(describeFileList(unwanted));
            emit finished(result);
            return;
        }
    }

    // Recovery volumes are never payload. They exist to repair the release, and
    // that job is finished by the time we get here.
    consumed += par2Files;

    // A checked .sfv has done its job too: published, it offers ED2K peers a
    // checksum list for files they will mostly never see.
    for (const QString& sfv : std::as_const(verifiedSfvFiles)) {
        payload.removeAll(sfv);
        if (!consumed.contains(sfv))
            consumed.append(sfv);
    }

    if (!job.cleanupEnabled) {
        // Publish everything instead of only what came out of the archives.
        for (const QString& path : consumed) {
            if (!payload.contains(path))
                payload.append(path);
        }
        consumed.clear();
    }

    if (m_stopRequested) {
        result.message = QObject::tr("Cancelled");
        emit finished(result);
        return;
    }

    // -- stage --------------------------------------------------------------
    emitStage(job.itemId, PostStage::Staging, 0, QString());

    if (!stageForPublish(payload, job.destDir, result)) {
        result.staged.clear();
        result.message = QObject::tr("Could not move the finished files into %1")
                             .arg(job.destDir);
        emit finished(result);
        return;
    }

    result.consumed = consumed;
    result.success = true;
    emit finished(result);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void UsenetPostProcessor::emitStage(const QString& itemId, PostStage stage, int percent,
                                    const QString& detail)
{
    emit stageChanged(itemId, int(stage), percent, detail);
}

bool UsenetPostProcessor::stageForPublish(const QStringList& files, const QString& destDir,
                                          UsenetPostResult& result)
{
    if (destDir.isEmpty()) {
        logError(QStringLiteral("Usenet: no incoming directory configured"));
        return false;
    }
    QDir().mkpath(destDir);

    int index = 0;
    for (const QString& source : files) {
        const QString name = QFileInfo(source).fileName();
        const QString finalPath = uniqueDestination(destDir, name);
        const QString stagedPath = finalPath + QString(Preferences::kUsenetPartSuffix);

        QFile::remove(stagedPath);

        // rename first: within one filesystem this is instant and atomic. Across
        // volumes it fails, and the copy that follows is the reason this runs on
        // the post-processing thread rather than the daemon's.
        if (!QFile::rename(source, stagedPath)) {
            if (!QFile::copy(source, stagedPath)) {
                logError(QStringLiteral("Usenet: cannot place \"%1\" in %2").arg(name, destDir));
                return false;
            }
            QFile::remove(source);
        }

        result.staged.append({source, stagedPath, finalPath});

        if (!files.isEmpty()) {
            emitStage(result.itemId, PostStage::Staging,
                      ++index * 100 / files.size(), name);
        }
    }
    return true;
}

} // namespace eMule::usenet
