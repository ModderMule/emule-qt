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

} // namespace eMule::usenet
