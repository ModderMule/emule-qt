/// @file UsenetUnpacker.cpp
/// @brief Volume-set detection, then libarchive.

#include "post/UsenetUnpacker.h"

#include "archive/ArchiveReader.h"
#include "archive/ExternalUnpacker.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>
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
                                              const QSet<QString>& skipFirstVolumes,
                                              const QString& externalTool)
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
        const bool opened = reader.open(set.volumes);

        // Encryption is not a corrupt archive, and it must not be reported as
        // one. libarchive decrypts ZIP and nothing else, so every other
        // encrypted format arrives here having failed for a reason the user can
        // actually do something about.
        //
        // A header-encrypted set fails at open() with no entries at all; a
        // data-encrypted one opens, lists, and fails at the first read. Both
        // land in encryptionBlocked(), which is why this is one branch and not
        // two.
        if (!opened && !reader.encryptionBlocked()) {
            result.error = QObject::tr("Cannot open %1")
                               .arg(QFileInfo(set.firstVolume).fileName());
            allOk = false;
            continue;
        }

        if (opened && !reader.encryptionBlocked()) {
            // Collect the destinations before extracting: the sanitiser decides
            // them, and asking it again afterwards is how the two lists drift
            // apart.
            QStringList produced;
            QStringList memberNames;
            for (int i = 0; i < reader.entryCount(); ++i) {
                if (reader.entryIsDir(i))
                    continue;
                memberNames.append(reader.entryName(i));
                const QString out = ArchiveReader::safeEntryPath(destDir, reader.entryName(i));
                if (!out.isEmpty())
                    produced.append(out);
            }

            // Before a byte is written: a set carrying what the caller refuses is
            // not extracted, and neither is anything after it.
            if (m_veto) {
                const QStringList refused = m_veto(memberNames);
                if (!refused.isEmpty()) {
                    result.vetoed += refused;
                    result.error = QObject::tr("%1 contains unwanted files")
                                       .arg(QFileInfo(set.firstVolume).fileName());
                    allOk = false;
                    break;
                }
            }

            if (reader.extractAll(destDir)) {
                for (const QString& rejected : reader.rejectedEntries()) {
                    logWarning(
                        QStringLiteral("Usenet: skipped unsafe archive member \"%1\" in \"%2\"")
                            .arg(rejected, QFileInfo(set.firstVolume).fileName()));
                }

                result.extractedFiles += produced;
                result.consumedArchives += set.volumes;

                logInfo(QStringLiteral("Usenet: unpacked \"%1\" (%2 file(s))")
                            .arg(QFileInfo(set.firstVolume).fileName())
                            .arg(produced.size()));
                continue;
            }

            if (!reader.encryptionBlocked()) {
                result.error = QObject::tr("Extraction of %1 failed")
                                   .arg(QFileInfo(set.firstVolume).fileName());
                allOk = false;
                continue;
            }
            // Fell through: the read got as far as encrypted data. Nothing
            // libarchive wrote is usable.
            for (const QString& path : reader.extractedFiles())
                QFile::remove(path);
        }

        if (!unpackEncrypted(set, destDir, password, externalTool, reader, result)) {
            allOk = false;
            if (!result.vetoed.isEmpty())
                break;
        }
    }

    if (m_progress)
        m_progress(100, QString());

    // A set that was found and produced nothing is a failure, never a success.
    //
    // This is the second half of the header-encrypted data-loss fix. Before it,
    // an archive that listed no members extracted no files, reported ok, and
    // handed the caller a consumedArchives list holding every volume — which
    // UsenetQueue then deleted, leaving a "Complete" download with nothing in
    // it. Even with ArchiveReader fixed, nothing downstream should depend on
    // that fix to stay safe.
    if (allOk && result.extractedFiles.isEmpty() && !result.consumedArchives.isEmpty()) {
        logWarning(QStringLiteral("Usenet: archive sets in \"%1\" produced no files")
                       .arg(sourceDir));
        result.consumedArchives.clear();
        result.error = QObject::tr("The archives produced no files");
        allOk = false;
    }

    result.ok = allOk;
    return result;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

bool UsenetUnpacker::unpackEncrypted(const ArchiveSet& set, const QString& destDir,
                                     const QString& password, const QString& externalTool,
                                     const ArchiveReader& reader, Result& result)
{
    const QString name = QFileInfo(set.firstVolume).fileName();
    result.encryptedUnsupported = true;

    ExternalUnpacker external(externalTool);
    if (!external.available()) {
        result.passwordRequired = true;
        result.error = QObject::tr("%1 is password-protected. Install 7-Zip or unrar so "
                                   "eMule can unpack it.").arg(name);
        return false;
    }

    if (password.isEmpty()) {
        // Running the tool with no password would only make it ask, and a
        // daemon has nobody to ask. Say what is missing instead.
        result.passwordRequired = true;
        result.error = QObject::tr("%1 is password-protected — set a password for this "
                                   "download to unpack it.").arg(name);
        return false;
    }

    const QString format = reader.formatName().isEmpty() ? QStringLiteral("archive")
                                                         : reader.formatName();
    if (reader.wrongPassphrase()) {
        // ZIP is the one format libarchive *can* decrypt, so saying it cannot
        // would be wrong here — the passphrase was refused. Worth retrying
        // through the tool anyway: it reads variants libarchive does not, and
        // its answer is what separates "wrong password" from "broken archive".
        logInfo(QStringLiteral("Usenet: the password for \"%1\" was refused; asking %2")
                    .arg(name, external.toolName()));
    } else {
        logInfo(QStringLiteral("Usenet: \"%1\" is encrypted %2, which libarchive cannot "
                               "decrypt; using %3")
                    .arg(name, format, external.toolName()));
    }

    const auto outcome = external.extract(set.volumes, destDir, password);

    for (const QString& rejected : outcome.rejectedEntries) {
        logWarning(QStringLiteral("Usenet: skipped unsafe archive member \"%1\" in \"%2\"")
                       .arg(rejected, name));
    }

    if (outcome.ok() && outcome.extractedFiles.isEmpty()) {
        // Exit 0 and nothing on disk. The unpack()-level guard would catch this
        // only when *every* set came up empty; per set, one empty set beside a
        // good one would still put its volumes on the delete list.
        result.error = QObject::tr("%1 produced no files").arg(name);
        return false;
    }

    if (outcome.ok() && m_veto) {
        // Asked afterwards: listing an encrypted set first would be a second
        // decrypting pass through the tool. What it wrote goes instead.
        QStringList names;
        for (const QString& path : outcome.extractedFiles)
            names.append(QFileInfo(path).fileName());
        const QStringList refused = m_veto(names);
        if (!refused.isEmpty()) {
            for (const QString& path : outcome.extractedFiles)
                QFile::remove(path);
            result.vetoed += refused;
            result.error = QObject::tr("%1 contains unwanted files").arg(name);
            return false;
        }
    }

    if (outcome.ok()) {
        result.extractedFiles += outcome.extractedFiles;
        result.consumedArchives += set.volumes;
        logInfo(QStringLiteral("Usenet: unpacked \"%1\" with %2 (%3 file(s))")
                    .arg(name, external.toolName())
                    .arg(outcome.extractedFiles.size()));
        return true;
    }

    // Only a passphrase problem may ask the user for a password again. A tool
    // that crashed or ran out of disk is a different conversation, and offering
    // "Set Password…" for it sends the user chasing the wrong thing.
    if (outcome.outcome == ExternalUnpacker::Outcome::WrongPassword
        || reader.wrongPassphrase()) {
        result.passwordRequired = true;
        result.wrongPassword = true;
        result.error = QObject::tr("The password for %1 is wrong.").arg(name);
        return false;
    }

    result.error = outcome.error.isEmpty()
                       ? QObject::tr("Extraction of %1 failed").arg(name)
                       : outcome.error;
    return false;
}

} // namespace eMule::usenet
