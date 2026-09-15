#include "post/UsenetReleaseChecks.h"

#include "decode/YencDecoder.h"
#include "media/ContainerSniffer.h"
#include "stream/UsenetStreamIndex.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>
#include <string_view>

namespace eMule::usenet {

namespace {

constexpr qint64 kCrcChunkBytes = 1 << 20;

/// An .sfv is a line per file; anything larger is not one.
constexpr qint64 kMaxSfvBytes = 1 << 20;

/// Names a message lists before "and N more".
constexpr int kMaxNamedFiles = 3;

/// Heads that are programs or archives, never media.
constexpr std::array<std::string_view, 8> kNotMediaSignatures{
    std::string_view("MZ", 2),                     // Windows executable
    std::string_view("\x7f" "ELF", 4),             // Linux executable
    std::string_view("\xcf\xfa\xed\xfe", 4),       // Mach-O, 64-bit
    std::string_view("\xca\xfe\xba\xbe", 4),       // Mach-O universal, Java class
    std::string_view("PK\x03\x04", 4),             // ZIP, JAR, APK
    std::string_view("Rar!\x1a\x07", 6),
    std::string_view("7z\xbc\xaf\x27\x1c", 6),
    std::string_view("#!", 2),                     // script
};

} // namespace

QList<SfvEntry> parseSfv(QByteArrayView text)
{
    static const QRegularExpression re(QStringLiteral(R"(^(.+?)\s+([0-9A-Fa-f]{8})$)"));

    QList<SfvEntry> out;
    for (const QByteArray& raw : text.toByteArray().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith(u';'))
            continue;

        const QRegularExpressionMatch m = re.match(line);
        if (!m.hasMatch())
            continue;

        // A bare name, never a path: an SFV comes off Usenet as untrusted as a
        // subject, and a Windows one separates with backslashes.
        QString name = m.captured(1).trimmed();
        if (name.size() >= 2 && name.startsWith(u'"') && name.endsWith(u'"'))
            name = name.mid(1, name.size() - 2);
        name.replace(u'\\', u'/');
        name = QFileInfo(name).fileName();
        if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
            continue;

        bool ok = false;
        const quint32 crc = m.captured(2).toUInt(&ok, 16);
        if (ok)
            out.append({name, crc});
    }
    return out;
}

std::optional<quint32> fileCrc32(const QString& path, const CheckCancelFn& cancel)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;

    quint32 crc = 0;
    QByteArray buffer(kCrcChunkBytes, Qt::Uninitialized);
    for (;;) {
        if (cancel && cancel())
            return std::nullopt;
        const qint64 n = f.read(buffer.data(), buffer.size());
        if (n < 0)
            return std::nullopt;
        if (n == 0)
            break;
        crc = yencCrc32(crc, QByteArrayView(buffer.constData(), n));
    }
    return crc;
}

SfvCheck verifySfv(const QString& dir, const QStringList& expectedNames,
                   const CheckProgressFn& progress, const CheckCancelFn& cancel)
{
    SfvCheck check;

    // Exact name first, then case-folded: an SFV names files in whatever case
    // the poster's disk had.
    QHash<QString, QString> exact;
    QHash<QString, QString> folded;
    QList<SfvEntry> entries;
    for (const QFileInfo& fi : QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                       QDir::Name)) {
        exact.insert(fi.fileName(), fi.absoluteFilePath());
        folded.insert(fi.fileName().toLower(), fi.absoluteFilePath());

        if (fi.suffix().compare(QLatin1String("sfv"), Qt::CaseInsensitive) != 0
            || fi.size() > kMaxSfvBytes) {
            continue;
        }
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        check.sfvFiles.append(fi.absoluteFilePath());
        entries += parseSfv(f.readAll());
    }
    if (check.sfvFiles.isEmpty())
        return check;

    QSet<QString> expected;
    for (const QString& name : expectedNames)
        expected.insert(name.toLower());

    struct Target {
        QString name;
        QString path;
        quint32 crc = 0;
    };
    QList<Target> targets;
    QSet<QString> seen;   // path + crc: two .sfv files listing one file cost one read
    qint64 totalBytes = 0;

    for (const SfvEntry& e : std::as_const(entries)) {
        QString path = exact.value(e.fileName);
        if (path.isEmpty())
            path = folded.value(e.fileName.toLower());

        if (path.isEmpty()) {
            (expected.contains(e.fileName.toLower()) ? check.damaged : check.unposted)
                .append(e.fileName);
            continue;
        }

        const QString key = path + QLatin1Char('\n') + QString::number(e.crc, 16);
        if (seen.contains(key))
            continue;
        seen.insert(key);

        targets.append({QFileInfo(path).fileName(), path, e.crc});
        totalBytes += QFileInfo(path).size();
    }
    check.damaged.removeDuplicates();
    check.unposted.removeDuplicates();

    if (targets.isEmpty() && check.damaged.isEmpty()) {
        check.outcome = SfvCheck::Outcome::NothingToCheck;
        return check;
    }

    qint64 doneBytes = 0;
    for (const Target& t : std::as_const(targets)) {
        if (progress)
            progress(totalBytes > 0 ? int(doneBytes * 100 / totalBytes) : 0, t.name);

        const std::optional<quint32> crc = fileCrc32(t.path, cancel);
        if (cancel && cancel()) {
            check.outcome = SfvCheck::Outcome::Cancelled;
            return check;
        }

        ++check.checked;
        if (!crc || *crc != t.crc)
            check.damaged.append(t.name);
        doneBytes += QFileInfo(t.path).size();
    }

    check.outcome = check.damaged.isEmpty() ? SfvCheck::Outcome::Clean
                                            : SfvCheck::Outcome::Damaged;
    return check;
}

QStringList parseExtensionList(const QString& text)
{
    static const QRegularExpression separators(QStringLiteral(R"([,;\s]+)"));

    QStringList out;
    for (QString token : text.split(separators, Qt::SkipEmptyParts)) {
        token.remove(u'*');
        while (token.startsWith(u'.'))
            token.remove(0, 1);
        token = token.toLower();
        if (!token.isEmpty() && !out.contains(token))
            out.append(token);
    }
    return out;
}

bool looksLikeVideoReleaseName(const QString& name)
{
    static const QRegularExpression re(
        QStringLiteral(R"((?:^|[\s._\-\[(])(?:480p|576p|720p|1080[pi]|2160p|x26[45]|)"
                       R"(h\.?26[45]|hevc|xvid|divx|web-?dl|webrip|blu-?ray|bdrip|brrip|)"
                       R"(dvdrip|hdtv|remux|s\d{1,2}e\d{1,3})(?=$|[\s._\-\])]))"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(name).hasMatch();
}

QStringList unwantedFileNames(const QStringList& names, const QStringList& extensions,
                              bool mediaRelease)
{
    if (extensions.isEmpty())
        return {};

    bool media = mediaRelease;
    for (const QString& name : names) {
        if (media)
            break;
        media = isPlayableName(QFileInfo(name).fileName());
    }

    QStringList out;
    for (const QString& name : names) {
        const QString bare = QFileInfo(name).fileName();
        const QString extension = QFileInfo(bare).suffix().toLower();
        if (extension.isEmpty() || !extensions.contains(extension))
            continue;

        // Dressed up as the thing the user came for.
        const bool disguised = isPlayableName(QFileInfo(bare).completeBaseName());
        if ((media || disguised) && !out.contains(bare))
            out.append(bare);
    }
    return out;
}

bool isFakeMediaFile(const QString& path)
{
    const QString name = QFileInfo(path).fileName();
    if (!isPlayableName(name))
        return false;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = f.read(kContainerHeadBytes);
    if (checkHead(head, name).verdict != ContainerVerdict::NoKnownContainer)
        return false;

    // Not merely a container the sniffer does not know, which a real but
    // unusual file would also be: something that is provably not media.
    return std::ranges::any_of(kNotMediaSignatures, [&head](std::string_view signature) {
        return head.startsWith(QByteArrayView(signature.data(), qsizetype(signature.size())));
    });
}

QString describeFileList(const QStringList& names)
{
    const QStringList shown = names.mid(0, kMaxNamedFiles);
    QString text = shown.join(QStringLiteral(", "));
    if (names.size() > kMaxNamedFiles) {
        text += QCoreApplication::translate("Usenet", " and %n more", nullptr,
                                            int(names.size() - kMaxNamedFiles));
    }
    return text;
}

} // namespace eMule::usenet
