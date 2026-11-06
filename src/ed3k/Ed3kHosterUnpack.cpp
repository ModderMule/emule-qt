#include "Ed3kHosterUnpack.h"

#include "post/UsenetPostProcessor.h"
#include "post/UsenetUnpacker.h"
#include "prefs/Preferences.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>

namespace eMule::ed3k {

namespace {

/// Moves @p from to @p to through the share scan's staging suffix: a copy across
/// volumes is never visible under a shareable name.
bool publishFile(const QString& from, const QString& to)
{
    const QString staged = to + QString(Preferences::kUsenetPartSuffix);
    QFile::remove(staged);
    if (!QFile::rename(from, staged)) {
        if (!QFile::copy(from, staged)) {
            QFile::remove(staged);
            return false;
        }
        QFile::remove(from);
    }
    return QFile::rename(staged, to);
}

} // namespace

bool Ed3kHosterUnpack::isArchive(const QString& fileName)
{
    return usenet::UsenetUnpacker::isArchiveVolume(fileName);
}

QString Ed3kHosterUnpack::setKey(const QString& fileName)
{
    const auto pos = usenet::UsenetUnpacker::volumePositionOf(fileName);
    return pos.index >= 0 ? pos.baseName : QString();
}

QList<Ed3kHosterUnpack::Set> Ed3kHosterUnpack::setsIn(const QString& dir)
{
    QList<Set> out;
    for (const usenet::ArchiveSet& s : usenet::UsenetUnpacker::findArchiveSets(dir))
        out.append({s.firstVolume, s.volumes, s.headless});
    return out;
}

bool Ed3kHosterUnpack::start(const HosterUnpackJob& job)
{
    if (busy() || job.firstVolume.isEmpty())
        return false;
    m_running = job.firstVolume;
    QPointer<Ed3kHosterUnpack> self(this);
    QThreadPool::globalInstance()->start([self, job] {
        const auto report = [self, key = job.firstVolume](int percent) {
            QMetaObject::invokeMethod(self, [self, key, percent] {
                if (self)
                    emit self->progress(key, percent);
            }, Qt::QueuedConnection);
        };
        HosterUnpackResult r = run(job, report);
        QMetaObject::invokeMethod(self, [self, r = std::move(r)] {
            if (!self)
                return;
            self->m_running.clear();
            emit self->finished(r);
        }, Qt::QueuedConnection);
    });
    return true;
}

HosterUnpackResult Ed3kHosterUnpack::run(const HosterUnpackJob& job, const std::function<void(int)>& progress)
{
    HosterUnpackResult r;
    r.firstVolume = job.firstVolume;
    QDir(job.scratchDir).removeRecursively();
    if (!QDir().mkpath(job.scratchDir)) {
        r.error = QStringLiteral("cannot create %1").arg(job.scratchDir);
        return r;
    }

    usenet::UsenetUnpacker unpacker;
    if (progress)
        unpacker.setProgressCallback([&progress](int percent, const QString&) { progress(percent); });
    // the unpacker tries every password in turn, per set
    usenet::UsenetUnpacker::Options options;
    options.passwords = QStringList{job.password} + job.passwords;
    options.skipFirstVolumes = job.skip;
    options.externalTool = job.externalTool;
    options.nestedDepth = job.nestedDepth;
    const auto u = unpacker.unpackAll(job.archiveDir, job.scratchDir, options);
    if (!u.ok || u.nothingToDo) {
        r.passwordRequired = u.passwordRequired;
        r.error = u.nothingToDo ? QStringLiteral("the first volume is missing") : u.error;
        QDir(job.scratchDir).removeRecursively();
        return r;
    }

    // inner archives that were opened live in scratch: drop them, or keep them
    // beside their contents when the user keeps archives
    const QString scratchRoot = QDir(job.scratchDir).absolutePath() + QLatin1Char('/');
    QStringList consumedOutside;
    for (const QString& v : u.consumedArchives) {
        if (!QFileInfo(v).absoluteFilePath().startsWith(scratchRoot))
            consumedOutside << v;
        else if (job.deleteNestedArchives)
            QFile::remove(v);
    }

    // what came out, moved into incoming with its folders; a taken name gets " (n)"
    const QDir scratch(job.scratchDir);
    QDirIterator it(job.scratchDir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    QStringList found;
    while (it.hasNext())
        found << it.next();
    found.sort();
    for (const QString& f : std::as_const(found)) {
        const QString rel = scratch.relativeFilePath(f);
        const QString dir = QFileInfo(QDir(job.destDir).filePath(rel)).absolutePath();
        QDir().mkpath(dir);
        const QString to = usenet::uniqueDestination(dir, QFileInfo(f).fileName());
        if (!publishFile(f, to)) {
            r.error = QStringLiteral("cannot move %1 to %2").arg(rel, dir);
            continue;
        }
        r.files << to;
    }
    QDir(job.scratchDir).removeRecursively();
    r.volumes = consumedOutside;
    r.ok = r.error.isEmpty();
    if (progress)
        progress(100);
    return r;
}

} // namespace eMule::ed3k
