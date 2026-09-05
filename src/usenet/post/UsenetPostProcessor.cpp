/// @file UsenetPostProcessor.cpp
/// @brief verify → repair → rename → unpack → stage, in that order and on one thread.

#include "post/UsenetPostProcessor.h"

#include "post/UsenetUnpacker.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace eMule::usenet {

namespace {

/// Where the unpacker writes, inside the item's own scratch folder. Underscored
/// so it cannot collide with a release that happens to contain a directory of
/// its own, and inside workDir so a failed job leaves nothing behind after the
/// queue removes the item directory.

[[nodiscard]] QStringList par2FilesIn(const QString& dir)
{
    QStringList result;
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                          QDir::Name);
    for (const QFileInfo& fi : entries) {
        if (fi.suffix().compare(QLatin1String("par2"), Qt::CaseInsensitive) == 0)
            result.append(fi.absoluteFilePath());
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

[[nodiscard]] QStringList payloadFilesIn(const QString& dir)
{
    QStringList result;
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                          QDir::Name);
    for (const QFileInfo& fi : entries) {
        if (fi.suffix().compare(QLatin1String("par2"), Qt::CaseInsensitive) == 0)
            continue;
        result.append(fi.absoluteFilePath());
    }
    return result;
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
            // par2's rename pass repairs as a side effect: RenameTargetFiles()
            // lives inside `if (dorepair)`, so a renameonly run still rewrites
            // damaged data. The verify that follows then reports Clean, which
            // makes the *later* outcome useless as a "were the volumes touched"
            // signal — so record it here.
            par2Repaired = par2Repaired || renamed.outcome == Par2Outcome::Repaired;
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

        if (check.outcome == Par2Outcome::NeedMoreBlocks) {
            // Not a failure. The queue skipped the recovery volumes on purpose;
            // this is the moment it finds out how many of them it actually needs.
            result.needsMoreBlocks = true;
            result.blocksNeeded = check.blocksStillNeeded();
            result.message = QObject::tr("Needs %n more recovery block(s)",
                                         nullptr, result.blocksNeeded);
            emit finished(result);
            return;
        }

        if (check.outcome == Par2Outcome::RepairPossible) {
            emitStage(job.itemId, PostStage::Repairing, 0, QString());
            verifier.setProgressCallback([&](int percent, const QString& file) {
                emitStage(job.itemId, PostStage::Repairing, percent, file);
            });

            const Par2Result repaired = verifier.repair(index, job.workDir);
            result.par2Outcome = repaired.outcome;
            par2Repaired = par2Repaired || repaired.outcome == Par2Outcome::Repaired;

            if (!repaired.ok()) {
                result.message = repaired.outcome == Par2Outcome::Cancelled
                    ? QObject::tr("Cancelled")
                    : QObject::tr("Repair failed: %1").arg(describePar2Outcome(repaired.outcome));
                emit finished(result);
                return;
            }
            logInfo(QStringLiteral("Usenet: repaired \"%1\"").arg(job.itemId));
        } else if (!check.ok()) {
            result.message = QObject::tr("Verification failed: %1")
                                 .arg(describePar2Outcome(check.outcome));
            emit finished(result);
            return;
        }
    } else if (job.hasMissingSegments) {
        // The one case phase 3 got wrong: a short file used to be published and
        // offered to peers regardless. Without a usable recovery set there is
        // nothing to repair it with, so it is a failure and it is not shared.
        result.message = Par2Verifier::available()
            ? QObject::tr("Articles are missing and the release has no PAR2 files")
            : QObject::tr("Articles are missing and this build has no PAR2 support");
        emit finished(result);
        return;
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

        const auto unpacked = unpacker.unpack(job.workDir, unpackDir, job.password, skip);

        if (!unpacked.ok) {
            result.message = unpacked.error;
            emit finished(result);
            return;
        }

        if (unpacked.nothingToDo && directPayload.isEmpty()) {
            payload = payloadFilesIn(job.workDir);
        } else {
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

    // Recovery volumes are never payload. They exist to repair the release, and
    // that job is finished by the time we get here.
    consumed += par2Files;

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

        result.staged.append({stagedPath, finalPath});

        if (!files.isEmpty()) {
            emitStage(result.itemId, PostStage::Staging,
                      ++index * 100 / files.size(), name);
        }
    }
    return true;
}

} // namespace eMule::usenet
