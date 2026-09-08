#include "nzb/NzbInfo.h"

#include <QRegularExpression>

#include <algorithm>

namespace eMule::usenet {

qint64 NzbFileInfo::encodedBytes() const
{
    qint64 total = 0;
    for (const auto& segment : segments)
        total += segment.bytes;
    return total;
}

bool NzbFileInfo::hasAllSegments() const
{
    if (partsTotal <= 0)
        return segments.size() > 0;
    if (segments.size() != partsTotal)
        return false;

    // Numbers must cover 1..partsTotal exactly. Duplicates would otherwise pass
    // a size check while leaving a hole.
    QList<int> numbers;
    numbers.reserve(segments.size());
    for (const auto& segment : segments)
        numbers.append(segment.number);
    std::ranges::sort(numbers);
    for (int i = 0; i < numbers.size(); ++i) {
        if (numbers.at(i) != i + 1)
            return false;
    }
    return true;
}

int NzbFileInfo::missingSegmentCount() const
{
    // No counter in the subject: the honest answer is "no opinion", and 0 is how
    // that is spelled here. NzbShortfall::unknownFiles is what carries the fact
    // that an opinion was unavailable.
    if (partsTotal <= 0)
        return 0;
    const int listed = int(segments.size());
    return partsTotal > listed ? partsTotal - listed : 0;
}

qint64 NzbFileInfo::meanSegmentBytes() const
{
    if (segments.isEmpty())
        return 0;
    return encodedBytes() / segments.size();
}

bool NzbFileInfo::isPar2() const
{
    // The name is not always available (obfuscated posts), so fall back to the
    // subject, which usually still carries the ".par2" token even when the
    // filename itself has been scrambled.
    const QString haystack = fileName.isEmpty() ? subject : fileName;
    return haystack.contains(QLatin1String(".par2"), Qt::CaseInsensitive);
}

int NzbFileInfo::par2RecoveryBlocks() const
{
    // Same fall-back-to-subject rule isPar2() uses: an obfuscated post scrambles
    // the filename but usually leaves the par2 token in the subject.
    const QString haystack = fileName.isEmpty() ? subject : fileName;

    static const QRegularExpression re(
        QStringLiteral(R"(\.vol(\d+)\+(\d+)\.par2)"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = re.match(haystack);
    if (!match.hasMatch())
        return 0;

    // The second number is the block count; the first is the starting exponent
    // and says nothing about how much recovery data is here.
    return match.captured(2).toInt();
}

qint64 NzbInfo::totalEncodedBytes() const
{
    qint64 total = 0;
    for (const auto& file : files)
        total += file.encodedBytes();
    return total;
}

int NzbInfo::segmentCount() const
{
    int total = 0;
    for (const auto& file : files)
        total += int(file.segments.size());
    return total;
}

int NzbShortfall::percent() const
{
    const int claimed = listedSegments + missingSegments;
    if (claimed <= 0)
        return 100;
    return int(qint64(listedSegments) * 100 / claimed);
}

NzbShortfall NzbInfo::shortfall() const
{
    NzbShortfall out;
    for (const NzbFileInfo& file : files) {
        out.listedSegments += int(file.segments.size());

        if (file.partsTotal <= 0)
            out.unknownFiles += 1;

        const int missing = file.missingSegmentCount();
        out.missingSegments += missing;
        // Priced at this file's own mean, not the release's: a release mixes
        // 700 KB archive volumes with a 2 KB .nfo, and one mean over all of them
        // would misprice whichever kind actually lost articles.
        out.missingBytes += qint64(missing) * file.meanSegmentBytes();

        if (file.isPar2Volume())
            out.recoveryBytes += file.encodedBytes();
    }
    return out;
}

} // namespace eMule::usenet
