#include "queue/UsenetQueueItem.h"

#include "prefs/Preferences.h"
#include "utils/OtherFunctions.h"

#include <QDir>
#include <QObject>

namespace eMule::usenet {

namespace {

/// Strip anything a file system would object to, and anything that would let a
/// crafted NZB escape the temp directory. An NZB is untrusted input: its subject
/// line is attacker-controlled, and `fileName` is derived from it.
[[nodiscard]] QString sanitizeName(const QString& raw)
{
    QString out;
    out.reserve(raw.size());
    for (const QChar c : raw) {
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':')
            || c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('"')
            || c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char('|')
            || c.unicode() < 0x20) {
            out += QLatin1Char('_');
        } else {
            out += c;
        }
    }
    out = out.trimmed();

    // "." and ".." would resolve to the parent directory.
    while (out.startsWith(QLatin1Char('.')))
        out.remove(0, 1);

    if (out.isEmpty())
        out = QStringLiteral("file");
    return out.left(180);
}

} // namespace

QString describeUsenetItemStatus(UsenetItemStatus s)
{
    switch (s) {
    case UsenetItemStatus::Queued:      return QObject::tr("Queued");
    case UsenetItemStatus::Downloading: return QObject::tr("Downloading");
    case UsenetItemStatus::Paused:      return QObject::tr("Paused");
    case UsenetItemStatus::Complete:    return QObject::tr("Complete");
    case UsenetItemStatus::Failed:      return QObject::tr("Failed");
    case UsenetItemStatus::Verifying:   return QObject::tr("Verifying");
    case UsenetItemStatus::Repairing:   return QObject::tr("Repairing");
    case UsenetItemStatus::Unpacking:   return QObject::tr("Unpacking");
    }
    return QObject::tr("Unknown");
}

bool UsenetFileState::allSegmentsDone() const
{
    for (qsizetype i = 0; i < done.size(); ++i) {
        if (!done.testBit(i))
            return false;
    }
    return done.size() > 0;
}

void UsenetFileState::addWritten(qint64 start, qint64 length)
{
    if (start < 0 || length <= 0)
        return;

    const qint64 end = start + length;

    // Insert in order, then coalesce with whatever it now touches. Consecutive
    // articles produce adjacent ranges, so the list collapses back to one entry
    // almost every time.
    int at = 0;
    while (at < written.size() && written.at(at).first < start)
        ++at;
    written.insert(at, {start, end});

    QList<QPair<qint64, qint64>> merged;
    for (const auto& r : std::as_const(written)) {
        if (!merged.isEmpty() && r.first <= merged.last().second)
            merged.last().second = qMax(merged.last().second, r.second);
        else
            merged.append(r);
    }
    written = std::move(merged);
}

qint64 UsenetFileState::availableEnd() const
{
    return availableFrom(0);
}

qint64 UsenetFileState::availableFrom(qint64 offset) const
{
    if (offset < 0)
        return 0;
    for (const auto& r : written) {
        if (r.first > offset)
            break;                  // sorted: nothing further can contain it
        if (r.second > offset)
            return r.second;
    }
    return offset;
}

qint64 UsenetQueueItem::decodedBytes() const
{
    qint64 total = 0;
    for (const auto& f : files)
        total += f.decodedBytes;
    return total;
}

int UsenetQueueItem::doneSegmentCount() const
{
    int n = 0;
    for (const auto& f : files) {
        for (qsizetype i = 0; i < f.done.size(); ++i) {
            if (f.done.testBit(i))
                ++n;
        }
    }
    return n;
}

bool UsenetQueueItem::isFilePreviewable(int fileIndex) const
{
    if (fileIndex < 0 || fileIndex >= nzb.files.size() || fileIndex >= files.size())
        return false;

    const NzbFileInfo& info = nzb.files.at(fileIndex);
    if (info.isPar2())
        return false;

    // The name yEnc declared wins: an obfuscated post's subject carries no
    // usable extension, and the extension is the whole of this test.
    const UsenetFileState& st = files.at(fileIndex);
    QString candidate = st.articleFileName;
    if (candidate.isEmpty())
        candidate = info.fileName;
    if (candidate.isEmpty())
        return false;

    // The same helper ED2K's PartFile::isPreviewPossible() uses, so the two
    // networks cannot disagree about what "previewable" means.
    const ED2KFileType type = getED2KFileTypeID(candidate);
    return type == ED2KFileType::Video || type == ED2KFileType::Audio;
}

int UsenetQueueItem::percentComplete() const
{
    const int total = segmentCount();
    if (total <= 0)
        return status == UsenetItemStatus::Complete ? 100 : 0;
    return int(qint64(doneSegmentCount()) * 100 / total);
}

void UsenetQueueItem::initFileStates(const QString& tempRoot)
{
    files.resize(nzb.files.size());

    const QString itemDir = QDir(tempRoot).filePath(id);
    for (int i = 0; i < nzb.files.size(); ++i) {
        const NzbFileInfo& info = nzb.files.at(i);
        UsenetFileState& st = files[i];

        if (st.done.size() != info.segments.size())
            st.done.resize(int(info.segments.size()));

        if (st.tempPath.isEmpty()) {
            // Numbered, not named: the real filename is often unknown until the
            // first article's =ybegin arrives, and an obfuscated post never
            // reveals it in the subject at all. The scratch name only has to be
            // unique and unshareable; the real one is applied on completion.
            const QString base = info.fileName.isEmpty()
                                     ? QStringLiteral("part%1").arg(i, 4, 10, QLatin1Char('0'))
                                     : sanitizeName(info.fileName);
            st.tempPath = QDir(itemDir).filePath(
                base + QString(Preferences::kUsenetPartSuffix));
        }
    }
}

} // namespace eMule::usenet
