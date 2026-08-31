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
constexpr QLatin1StringView kUnpackDirName{"_unpacked"};

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

        UsenetUnpacker unpacker;
        unpacker.setProgressCallback([&](int percent, const QString& file) {
            emitStage(job.itemId, PostStage::Unpacking, percent, file);
        });

        const auto unpacked = unpacker.unpack(job.workDir, unpackDir, job.password);

        if (!unpacked.ok) {
            result.message = unpacked.error;
            emit finished(result);
            return;
        }

        if (unpacked.nothingToDo) {
            payload = payloadFilesIn(job.workDir);
        } else {
            payload = unpacked.extractedFiles;
            consumed = unpacked.consumedArchives;
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
