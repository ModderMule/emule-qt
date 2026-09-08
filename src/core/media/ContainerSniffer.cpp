#include "pch.h"
/// @file ContainerSniffer.cpp
/// @brief Mandatory container signatures, and the verdict drawn from them.

#include "ContainerSniffer.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

#include <cstddef>
#include <initializer_list>

namespace eMule {

namespace {

/// A tiny lookup table. A dozen entries each, scanned linearly, so the media
/// tables below need no extra container include.
struct MediaPair
{
    const char* key;
    const char* value;
};

template <std::size_t N>
QString lookupPair(const MediaPair (&table)[N], const QString& key)
{
    for (const auto& [k, v] : table) {
        if (key == QLatin1StringView(k))
            return QString::fromLatin1(v);
    }
    return {};
}

} // namespace

/// The container a file's first bytes actually are; empty when unrecognised.
///
/// Every signature here is mandatory in its format: the file opens with these
/// bytes or it is not that container. Deliberately no heuristics -- an MPEG
/// audio frame sync is 11 set bits, which one file in 2048 matches by chance,
/// and the fake that prompted all this is one of them: 529 MB of random padding
/// that file(1) confidently calls a 32 kbps MP3 and no player can open.
///
/// QMimeDatabase cannot do this job either. Asked to match on content it calls
/// that same file application/octet-stream, and it demotes every real .mp4 to
/// video/quicktime, which some browsers then refuse. So read the magic here.
QString sniffContainer(const QByteArray& head)
{
    // 12 bytes covers every signature here, incl. RIFF's form type at offset 8.
    if (head.size() < kContainerHeadBytes)
        return {};

    const auto at = [&head](int off, std::initializer_list<quint8> sig) {
        int i = off;
        for (const quint8 b : sig) {
            if (static_cast<quint8>(head.at(i++)) != b)
                return false;
        }
        return true;
    };

    if (at(0, {0x30, 0x26, 0xB2, 0x75}))                          return QStringLiteral("ASF");
    if (at(4, {'f', 't', 'y', 'p'}))                              return QStringLiteral("MP4");
    if (at(0, {'R','I','F','F'}) && at(8, {'A','V','I',' '}))     return QStringLiteral("AVI");
    if (at(0, {'R','I','F','F'}) && at(8, {'W','A','V','E'}))     return QStringLiteral("WAVE");
    if (at(0, {0x1A, 0x45, 0xDF, 0xA3}))                          return QStringLiteral("Matroska");
    if (at(0, {'O','g','g','S'}))                                 return QStringLiteral("Ogg");
    if (at(0, {'f','L','a','C'}))                                 return QStringLiteral("FLAC");
    if (at(0, {'F','L','V'}))                                     return QStringLiteral("FLV");
    if (at(0, {'.','R','M','F'}))                                 return QStringLiteral("RealMedia");
    if (at(0, {'I','D','3'}))                                     return QStringLiteral("MPEG audio");
    if (at(0, {0x00, 0x00, 0x01, 0xBA}) || at(0, {0x00, 0x00, 0x01, 0xB3}))
        return QStringLiteral("MPEG program stream");

    return {};
}

/// The container an extension promises; empty when we promise nothing for it.
QString expectedContainer(const QString& ext)
{
    static constexpr MediaPair kByExt[] = {
        {"wmv", "ASF"},       {"asf", "ASF"},       {"wma", "ASF"},
        {"mp4", "MP4"},       {"m4v", "MP4"},       {"m4a", "MP4"},  {"mov", "MP4"},
        {"avi", "AVI"},       {"wav", "WAVE"},
        {"mkv", "Matroska"},  {"webm", "Matroska"},
        {"ogg", "Ogg"},       {"ogv", "Ogg"},       {"oga", "Ogg"},
        {"flac", "FLAC"},     {"flv", "FLV"},
        {"rm", "RealMedia"},  {"rmvb", "RealMedia"},
        {"mpg", "MPEG program stream"}, {"mpeg", "MPEG program stream"},
        // No .mp3 on purpose: a tagless MP3 is perfectly legal and starts with
        // no mandatory bytes, so there is nothing here we could hold it to.
    };
    return lookupPair(kByExt, ext);
}

/// What to send for a container we identified from its bytes.
QString containerMimeType(const QString& container)
{
    static constexpr MediaPair kByContainer[] = {
        {"ASF", "video/x-ms-asf"},
        {"MP4", "video/mp4"},
        {"AVI", "video/x-msvideo"},
        {"WAVE", "audio/wav"},
        {"Matroska", "video/x-matroska"},
        {"Ogg", "application/ogg"},
        {"FLAC", "audio/flac"},
        {"FLV", "video/x-flv"},
        {"RealMedia", "application/vnd.rn-realmedia"},
        {"MPEG audio", "audio/mpeg"},
        {"MPEG program stream", "video/mpeg"},
    };
    return lookupPair(kByContainer, container);
}

ContainerCheck checkHead(const QByteArray& head, const QString& fileName)
{
    ContainerCheck result;
    result.expected = expectedContainer(QFileInfo(fileName).suffix().toLower());
    if (result.expected.isEmpty())
        return result;   // Unchecked: we made no promise about this extension.

    const QString actual = sniffContainer(head);
    if (actual == result.expected) {
        result.verdict = ContainerVerdict::Matches;
        return result;
    }

    // Knowing the file is not what it claims is not the same as knowing what it
    // is. Keep the two apart: naming a container we did not positively identify
    // would just trade one wrong answer for another.
    result.actual = actual;
    result.mimeType = containerMimeType(actual);
    result.verdict = actual.isEmpty() ? ContainerVerdict::NoKnownContainer
                                      : ContainerVerdict::WrongContainer;
    return result;
}

ContainerCheck checkFile(const QString& absPath, const QString& fileName)
{
    // Ask the name first: an extension we hold no promise over is not worth an
    // open, and most of a shared folder is exactly that.
    if (expectedContainer(QFileInfo(fileName).suffix().toLower()).isEmpty())
        return {};

    QFile file(absPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    return checkHead(file.read(kContainerHeadBytes), fileName);
}

QString containerWarningText(const ContainerCheck& check, const QString& fileName)
{
    if (!check.isSuspect())
        return {};

    // Two different things to say, and they call for different reactions: a
    // container we did name still plays in the right player, one we could not name
    // plays nowhere. Sending someone to VLC for the second wastes the trip twice.
    const QString ext = QFileInfo(fileName).suffix().toLower();
    if (check.actual.isEmpty()) {
        return QCoreApplication::translate(
                   "ContainerSniffer",
                   "Named .%1 but matches no media container we recognise — very likely a fake.")
            .arg(ext);
    }
    return QCoreApplication::translate("ContainerSniffer", "Named .%1 but the contents are %2.")
        .arg(ext, check.actual);
}

} // namespace eMule
