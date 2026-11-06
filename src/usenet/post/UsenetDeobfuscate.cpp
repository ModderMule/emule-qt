/// @file UsenetDeobfuscate.cpp
/// @brief Obfuscated-name detection and the job-name rename plan.

#include "post/UsenetDeobfuscate.h"

#include "post/UsenetUnpacker.h"
#include "queue/UsenetQueueItem.h"

#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace eMule::usenet {

namespace {

/// Base name and extension the way a rename keeps them: the last suffix only,
/// so `x.eng.srt` is base `x.eng`, ext `.srt`.
struct SplitName {
    QString base;
    QString ext;   ///< with the dot, empty when none
};

SplitName splitName(const QString& fileName)
{
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0)
        return {fileName, {}};
    return {fileName.left(dot), fileName.mid(dot)};
}

/// Disc structures and formats whose names mean something to a player.
bool isFixedNameFile(const QString& path)
{
    static const QStringList kExts = {
        QStringLiteral(".vob"),  QStringLiteral(".par2"), QStringLiteral(".mts"),
        QStringLiteral(".m2ts"), QStringLiteral(".cpi"),  QStringLiteral(".clpi"),
        QStringLiteral(".mpl"),  QStringLiteral(".mpls"), QStringLiteral(".bdm"),
        QStringLiteral(".bdmv")};
    const QString name = QFileInfo(path).fileName();
    return kExts.contains(splitName(name).ext.toLower()) || UsenetUnpacker::isArchiveVolume(name);
}

bool inDiscFolder(const QString& path)
{
    const QString p = QFileInfo(path).absoluteFilePath().toUpper();
    return p.contains(QStringLiteral("/VIDEO_TS/")) || p.contains(QStringLiteral("/BDMV/"))
        || p.contains(QStringLiteral("/AUDIO_TS/"));
}

} // namespace

bool looksObfuscatedName(const QString& baseName)
{
    if (baseName.isEmpty())
        return false;

    static const QRegularExpression hex32(QStringLiteral("^[a-f0-9]{32}$"));
    static const QRegularExpression hexDots(QStringLiteral("^[a-f0-9.]{40,}$"));
    static const QRegularExpression hex30(QStringLiteral("[a-f0-9]{30}"));
    static const QRegularExpression brackets(QStringLiteral("\\[\\w+\\]"));
    if (hex32.match(baseName).hasMatch() || hexDots.match(baseName).hasMatch())
        return true;
    if (hex30.match(baseName).hasMatch()) {
        int n = 0;
        for (auto it = brackets.globalMatch(baseName); it.hasNext(); it.next())
            ++n;
        if (n >= 2)
            return true;
    }
    if (baseName.startsWith(QLatin1StringView("abc.xyz")))
        return true;

    int digits = 0, upper = 0, lower = 0, seps = 0;
    for (const QChar c : baseName) {
        if (c.isDigit())
            ++digits;
        else if (c.isUpper())
            ++upper;
        else if (c.isLower())
            ++lower;
        else if (c == QLatin1Char(' ') || c == QLatin1Char('.') || c == QLatin1Char('_'))
            ++seps;
    }

    // words: mixed case with a separator, several separators, letters and a year-ish number
    if (upper >= 2 && lower >= 2 && seps >= 1)
        return false;
    if (seps >= 3)
        return false;
    if (upper + lower >= 4 && digits >= 4 && seps >= 1)
        return false;
    // "Holiday" style: a capital, then mostly lower case
    if (baseName.at(0).isUpper() && lower > 2 && double(upper) / lower <= 0.25)
        return false;
    return true;
}

QHash<QString, QString> deobfuscatedNames(const QStringList& payload, const QString& jobName)
{
    QHash<QString, QString> renames;

    const QString niceName = sanitizeName(jobName);
    if (jobName.trimmed().isEmpty() || looksObfuscatedName(niceName))
        return renames;   // nothing better to offer
    if (std::ranges::any_of(payload, inDiscFolder))
        return renames;   // DVD/Blu-ray names are part of the format

    // biggest first; it must stand out, or "the" file is a guess
    QList<std::pair<qint64, QString>> bySize;
    for (const QString& path : payload) {
        const QFileInfo fi(path);
        if (fi.isFile())
            bySize.append({fi.size(), path});
    }
    if (bySize.isEmpty())
        return renames;
    std::ranges::sort(bySize, std::greater{});
    if (bySize.size() > 1 && bySize.at(0).first <= 3 * bySize.at(1).first)
        return renames;

    const QString biggest = bySize.at(0).second;
    if (isFixedNameFile(biggest))
        return renames;
    const QFileInfo big(biggest);
    const SplitName parts = splitName(big.fileName());
    if (!looksObfuscatedName(parts.base))
        return renames;

    // a job name that already ends in the extension: no "x.mkv.mkv"
    QString target = niceName;
    if (!parts.ext.isEmpty() && target.endsWith(parts.ext, Qt::CaseInsensitive))
        target.chop(parts.ext.size());

    // the file and its namesakes in the same folder keep their tails
    const QString dir = big.absolutePath();
    for (const QString& path : payload) {
        const QFileInfo fi(path);
        if (fi.absolutePath() != dir || isFixedNameFile(path))
            continue;
        const QString name = fi.fileName();
        if (!name.startsWith(parts.base))
            continue;
        const QString tail = name.mid(parts.base.size());
        // "abc.mkv" and "abc-sample.mkv" yes; "abcdef.txt" no: a tail starts on a separator
        if (!tail.isEmpty() && tail.at(0).isLetterOrNumber())
            continue;
        renames.insert(path, target + tail);
    }
    return renames;
}

} // namespace eMule::usenet
