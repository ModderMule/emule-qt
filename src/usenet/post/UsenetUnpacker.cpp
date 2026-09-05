/// @file UsenetUnpacker.cpp
/// @brief Volume-set detection, then libarchive.

#include "post/UsenetUnpacker.h"

#include "archive/ArchiveReader.h"
#include "utils/Log.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

namespace eMule::usenet {

namespace {

/// `name.part07.rar` — the modern RAR scheme. The part number is 1-based, and
/// its width varies with the volume count (part01 / part001), so compare the
/// parsed number and never the string.
const QRegularExpression& rePartRar()
{
    static const QRegularExpression re(QStringLiteral(R"(^(.*)\.part(\d+)\.rar$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

/// `name.r00`, `name.r01` — the old scheme, where the *first* volume is
/// `name.rar` and carries no number at all. That asymmetry is the trap: sorting
/// the set by name puts `name.r00` first and `name.rar` last.
const QRegularExpression& reOldRar()
{
    static const QRegularExpression re(QStringLiteral(R"(^(.*)\.r(\d{2,3})$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

/// `name.7z.001`, `name.zip.001` — a container split by number.
const QRegularExpression& reNumbered()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(.*\.(?:7z|zip|rar|tar|tar\.gz|tar\.bz2|tar\.xz))\.(\d{3,})$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

/// Single-file archives.
const QRegularExpression& reSingle()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(.*)\.(?:rar|zip|7z|tar|tgz|tar\.gz|tar\.bz2|tar\.xz)$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

struct Volume {
    QString path;
    int index = 0;      ///< position within the set; lowest opens it
};

} // namespace

bool UsenetUnpacker::isArchiveVolume(const QString& fileName)
{
    return volumePositionOf(fileName).index >= 0;
}

UsenetUnpacker::VolumePosition UsenetUnpacker::volumePositionOf(const QString& fileName)
{
    if (auto m = rePartRar().match(fileName); m.hasMatch())
        return {m.captured(1).toLower(), m.captured(2).toInt()};

    if (auto m = reOldRar().match(fileName); m.hasMatch()) {
        // Offset by one so this can never tie with the bare .rar below, which is
        // volume zero of the same set.
        return {m.captured(1).toLower(), m.captured(2).toInt() + 1};
    }

    if (auto m = reNumbered().match(fileName); m.hasMatch())
        return {m.captured(1).toLower(), m.captured(2).toInt()};

    if (auto m = reSingle().match(fileName); m.hasMatch()) {
        // Volume zero: for the .r00 scheme this is the real first volume, and
        // for a lone .zip it is the only one.
        return {m.captured(1).toLower(), 0};
    }

    return {};
}

QList<ArchiveSet> UsenetUnpacker::findArchiveSets(const QString& dir)
{
    QHash<QString, QList<Volume>> sets;   // set key -> volumes

    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                          QDir::Name);
    for (const QFileInfo& fi : entries) {
        const VolumePosition pos = volumePositionOf(fi.fileName());
        if (pos.index >= 0)
            sets[pos.baseName].append({fi.absoluteFilePath(), pos.index});
    }

    QList<ArchiveSet> result;
    for (auto it = sets.constBegin(); it != sets.constEnd(); ++it) {
        QList<Volume> volumes = it.value();
        std::sort(volumes.begin(), volumes.end(),
                  [](const Volume& a, const Volume& b) { return a.index < b.index; });

        ArchiveSet set;
        set.baseName = it.key();
        set.firstVolume = volumes.first().path;
        for (const Volume& v : volumes)
            set.volumes.append(v.path);
        result.append(std::move(set));
    }

    // Stable order so a log or a test reads the same way twice.
    std::sort(result.begin(), result.end(),
              [](const ArchiveSet& a, const ArchiveSet& b) { return a.baseName < b.baseName; });
    return result;
}

UsenetUnpacker::Result UsenetUnpacker::unpack(const QString& sourceDir, const QString& destDir,
                                              const QString& password,
                                              const QSet<QString>& skipFirstVolumes)
{
    Result result;

    const QList<ArchiveSet> sets = findArchiveSets(sourceDir);
    if (sets.isEmpty()) {
        result.ok = true;
        result.nothingToDo = true;
        return result;
    }

    QDir().mkpath(destDir);
    bool allOk = true;
    int setIndex = 0;

    for (const ArchiveSet& set : sets) {
        if (skipFirstVolumes.contains(set.firstVolume)) {
            ++setIndex;
            continue;   // already extracted while the download ran
        }
        if (m_progress) {
            m_progress(sets.size() > 1 ? setIndex * 100 / sets.size() : 0,
                       QFileInfo(set.firstVolume).fileName());
        }
        ++setIndex;

        ArchiveReader reader;
        if (!password.isEmpty())
            reader.setPassphrase(password);

        // The whole list, not just volume one. libarchive reads a set as one
        // stream over the files the caller hands it and never opens a sibling
        // volume by name, so opening on volume one stops at its end.
        if (!reader.open(set.volumes)) {
            result.error = QObject::tr("Cannot open %1")
                               .arg(QFileInfo(set.firstVolume).fileName());
            allOk = false;
            continue;
        }

        if (reader.hasEncryptedEntries()
            && reader.formatName().contains(QLatin1String("RAR"), Qt::CaseInsensitive)) {
            // libarchive flags RAR encryption and stops there — there is no
            // passphrase path for it at all. Say so, because the alternative is
            // a read error the user cannot act on.
            result.encryptedUnsupported = true;
            result.error = QObject::tr("%1 is a password-protected RAR, which cannot be "
                                       "unpacked").arg(QFileInfo(set.firstVolume).fileName());
            allOk = false;
            continue;
        }

        // Collect the destinations before extracting: the sanitiser decides them,
        // and asking it again afterwards is how the two lists drift apart.
        QStringList produced;
        for (int i = 0; i < reader.entryCount(); ++i) {
            if (reader.entryIsDir(i))
                continue;
            const QString out = ArchiveReader::safeEntryPath(destDir, reader.entryName(i));
            if (!out.isEmpty())
                produced.append(out);
        }

        if (!reader.extractAll(destDir)) {
            result.error = QObject::tr("Extraction of %1 failed")
                               .arg(QFileInfo(set.firstVolume).fileName());
            allOk = false;
            continue;
        }

        for (const QString& rejected : reader.rejectedEntries()) {
            logWarning(QStringLiteral("Usenet: skipped unsafe archive member \"%1\" in \"%2\"")
                           .arg(rejected, QFileInfo(set.firstVolume).fileName()));
        }

        result.extractedFiles += produced;
        result.consumedArchives += set.volumes;

        logInfo(QStringLiteral("Usenet: unpacked \"%1\" (%2 file(s))")
                    .arg(QFileInfo(set.firstVolume).fileName())
                    .arg(produced.size()));
    }

    if (m_progress)
        m_progress(100, QString());

    result.ok = allOk;
    return result;
}

} // namespace eMule::usenet
