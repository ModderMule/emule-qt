#include "queue/UsenetQueueItem.h"

#include "prefs/Preferences.h"

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
