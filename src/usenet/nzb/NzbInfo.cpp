#include "nzb/NzbInfo.h"

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
