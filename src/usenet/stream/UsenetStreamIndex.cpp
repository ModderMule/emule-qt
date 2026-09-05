#include "stream/UsenetStreamIndex.h"

#include "post/UsenetUnpacker.h"
#include "queue/UsenetQueueItem.h"

#include <QFile>
#include <QRegularExpression>

#include <algorithm>

namespace eMule::usenet {

namespace {

/// `Movie.mkv.001` — a raw file cut into numbered pieces, with no container at
/// all. UsenetUnpacker's reNumbered() only matches when the base carries an
/// *archive* extension, so this is the case it deliberately leaves alone.
const QRegularExpression& reRawSplit()
{
    static const QRegularExpression re(QStringLiteral(R"(^(.*\.[A-Za-z0-9]{1,5})\.(\d{3,})$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

StreamResolve needBytes(int fileIndex, qint64 offset, qint64 length)
{
    StreamResolve out;
    out.plan = StreamPlan::NeedBytes;
    out.needFileIndex = fileIndex;
    out.needOffset = offset;
    out.needLength = length;
    return out;
}

StreamResolve notSeekable(const QString& reason)
{
    StreamResolve out;
    out.plan = StreamPlan::NotSeekable;
    out.reason = reason;
    return out;
}

} // namespace

StreamResolve UsenetStreamIndex::resolve(const UsenetQueueItem& item, int fileIndex,
                                         qint64 wantOffset)
{
    if (fileIndex < 0 || fileIndex >= item.files.size() || fileIndex >= item.nzb.files.size())
        return {};

    const QString name = fileNameOf(item, fileIndex);
    if (name.isEmpty()) {
        // An obfuscated post says nothing until its first article arrives, and
        // `=ybegin name=` is then the only place the real name exists.
        return needBytes(fileIndex, 0, kRarHeaderProbeBytes);
    }

    const auto pos = UsenetUnpacker::volumePositionOf(name);
    if (pos.index < 0) {
        const QList<int> members = splitMembersOf(item, fileIndex);
        if (members.size() > 1)
            return resolveSplitSet(item, members);
        return resolveRawFile(item, fileIndex);
    }

    SetInfo& set = m_sets[pos.baseName];
    if (set.refused)
        return notSeekable(set.reason);

    if (set.volumes.isEmpty()) {
        QList<QPair<int, int>> ordered;   // (volume index, NZB file index)
        for (int i = 0; i < item.nzb.files.size() && i < item.files.size(); ++i) {
            const QString n = fileNameOf(item, i);
            if (n.isEmpty()) {
                // One unnamed file is enough to make the set's order a guess.
                // §7.2 calls this the mandatory prefetch pass: one article per
                // file, and only on releases whose NZB hides the names.
                if (!item.nzb.files.at(i).isPar2())
                    return needBytes(i, 0, kRarHeaderProbeBytes);
                continue;
            }
            const auto p = UsenetUnpacker::volumePositionOf(n);
            if (p.index >= 0 && p.baseName == pos.baseName)
                ordered.append({p.index, i});
        }
        if (ordered.isEmpty())
            return resolveRawFile(item, fileIndex);

        std::sort(ordered.begin(), ordered.end());
        for (const auto& v : std::as_const(ordered))
            set.volumes.append(v.second);
        set.isRar = true;
    }

    StreamResolve out = resolveRarSet(item, set, wantOffset);
    if (out.plan == StreamPlan::NotSeekable) {
        // Cache the refusal: the preview poll asks four times a second, and
        // re-probing a solid archive on every tick would fetch nothing but
        // still burn a file read each time.
        set.refused = true;
        set.reason = out.reason;
    }
    return out;
}

void UsenetStreamIndex::invalidate()
{
    m_sets.clear();
    m_volumes.clear();
}

// --- private ---------------------------------------------------------------

QString UsenetStreamIndex::fileNameOf(const UsenetQueueItem& item, int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= item.files.size() || fileIndex >= item.nzb.files.size())
        return {};
    const QString fromArticle = item.files.at(fileIndex).articleFileName;
    return fromArticle.isEmpty() ? item.nzb.files.at(fileIndex).fileName : fromArticle;
}

QList<int> UsenetStreamIndex::splitMembersOf(const UsenetQueueItem& item, int fileIndex)
{
    const auto m = reRawSplit().match(fileNameOf(item, fileIndex));
    if (!m.hasMatch())
        return {};

    const QString base = m.captured(1).toLower();
    QList<QPair<int, int>> ordered;
    for (int i = 0; i < item.nzb.files.size() && i < item.files.size(); ++i) {
        const auto mi = reRawSplit().match(fileNameOf(item, i));
        if (mi.hasMatch() && mi.captured(1).toLower() == base)
            ordered.append({mi.captured(2).toInt(), i});
    }

    std::sort(ordered.begin(), ordered.end());
    QList<int> out;
    for (const auto& v : std::as_const(ordered))
        out.append(v.second);
    return out;
}

bool UsenetStreamIndex::readIfAvailable(const UsenetQueueItem& item, int fileIndex,
                                        qint64 offset, qint64 length, QByteArray& out)
{
    if (fileIndex < 0 || fileIndex >= item.files.size() || offset < 0 || length <= 0)
        return false;

    const UsenetFileState& st = item.files.at(fileIndex);
    if (st.declaredSize <= 0 || offset >= st.declaredSize)
        return false;

    // Return what is there, not all that was asked for. A RAR header is a few
    // dozen bytes and lands inside the first article; demanding the whole probe
    // window would refuse a volume whose header is already fully readable, and
    // the caller would ask for bytes it does not need. parseRarVolume() says
    // NeedMoreBytes if the prefix really is too short.
    const qint64 have = st.availableFrom(offset) - offset;
    if (have <= 0)
        return false;

    const qint64 want = qMin(qMin(length, st.declaredSize - offset), have);
    if (want <= 0)
        return false;

    const QString path = !st.finalPath.isEmpty() ? st.finalPath : st.tempPath;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(offset))
        return false;

    out = f.read(want);
    return out.size() == want;
}

StreamResolve UsenetStreamIndex::resolveRawFile(const UsenetQueueItem& item, int fileIndex) const
{
    const UsenetFileState& st = item.files.at(fileIndex);
    if (st.declaredSize <= 0) {
        // The size comes from `=ybegin size=`; nothing else knows it.
        return needBytes(fileIndex, 0, kRarHeaderProbeBytes);
    }

    StreamResolve out;
    out.plan = StreamPlan::Ready;
    out.fileName = fileNameOf(item, fileIndex);
    out.totalSize = st.declaredSize;
    out.extents.append({fileIndex, 0, 0, st.declaredSize});
    return out;
}

StreamResolve UsenetStreamIndex::resolveSplitSet(const UsenetQueueItem& item,
                                                 const QList<int>& members) const
{
    StreamResolve out;
    qint64 cursor = 0;
    for (const int idx : members) {
        const UsenetFileState& st = item.files.at(idx);
        if (st.declaredSize <= 0)
            return needBytes(idx, 0, kRarHeaderProbeBytes);
        out.extents.append({idx, cursor, 0, st.declaredSize});
        cursor += st.declaredSize;
    }

    out.plan = StreamPlan::Ready;
    out.totalSize = cursor;
    // The pieces are named `Movie.mkv.001`; the logical file is the base.
    out.fileName = reRawSplit().match(fileNameOf(item, members.first())).captured(1);
    return out;
}

bool UsenetStreamIndex::volumeHeader(const UsenetQueueItem& item, int fileIndex,
                                     StreamResolve& out)
{
    auto it = m_volumes.constFind(fileIndex);
    if (it == m_volumes.constEnd()) {
        QByteArray head;
        if (!readIfAvailable(item, fileIndex, 0, kRarHeaderProbeBytes, head)) {
            out = needBytes(fileIndex, 0, kRarHeaderProbeBytes);
            return false;
        }

        RarVolume v = parseRarVolume(head);

        if (v.status == RarParse::NeedMoreBytes) {
            // Short only while the first article is still landing. Not cached:
            // the answer changes as bytes arrive.
            if (head.size() < kRarHeaderProbeBytes) {
                out = needBytes(fileIndex, 0, kRarHeaderProbeBytes);
                return false;
            }
            v.status = RarParse::Unsupported;
            v.reason = QStringLiteral("Archive header is larger than expected");
        }

        // Fold the entry-level refusals into the volume's own status so they
        // cache with it. Otherwise a compressed set is re-read from disk on
        // every preview poll, four times a second, forever.
        if (v.status == RarParse::Ok) {
            if (v.entries.isEmpty()) {
                v.status = RarParse::Unsupported;
                v.reason = QStringLiteral("Archive volume holds no mappable file");
            } else if (!v.entries.first().stored) {
                v.status = RarParse::Unsupported;
                v.reason =
                    QStringLiteral("Compressed archive — cannot seek without decompressing");
            } else if (v.entries.first().encrypted) {
                v.status = RarParse::Unsupported;
                v.reason = QStringLiteral("Encrypted archive");
            }
        }

        m_volumes.insert(fileIndex, v);
        it = m_volumes.constFind(fileIndex);
    }

    if (it->status == RarParse::Ok)
        return true;

    out = notSeekable(it->reason.isEmpty() ? QStringLiteral("Not a readable archive")
                                           : it->reason);
    return false;
}

StreamResolve UsenetStreamIndex::resolveRarSet(const UsenetQueueItem& item, SetInfo& set,
                                               qint64 wantOffset)
{
    const int n = int(set.volumes.size());
    StreamResolve out;

    if (!volumeHeader(item, set.volumes.at(0), out))
        return out;

    const RarEntry first = m_volumes.value(set.volumes.at(0)).entries.first();
    set.innerName = first.name;
    set.totalSize = first.unpackedSize;
    set.p1 = first.packedSize;

    out.fileName = set.innerName;
    out.totalSize = set.totalSize;

    if (n == 1) {
        out.plan = StreamPlan::Ready;
        out.extents.append({set.volumes.at(0), 0, first.dataOffset, first.packedSize});
        if (set.totalSize <= 0)
            out.totalSize = first.packedSize;
        return out;
    }

    if (set.totalSize <= 0) {
        out = notSeekable(QStringLiteral("Archive does not declare its unpacked size"));
        return out;
    }

    // Volumes 2 and N make the set's shape provable: `rar -v` pads every volume
    // to the same size, so if p1 + (N-2)*p2 + pN accounts for the whole file,
    // every volume in between is p2 long and none of them has to be read.
    if (!set.uniformDecided) {
        if (!volumeHeader(item, set.volumes.at(1), out))
            return out;
        if (!volumeHeader(item, set.volumes.at(n - 1), out))
            return out;
        set.p2 = m_volumes.value(set.volumes.at(1)).entries.first().packedSize;
        set.pN = m_volumes.value(set.volumes.at(n - 1)).entries.first().packedSize;
        set.uniform = (set.p1 + qint64(n - 2) * set.p2 + set.pN) == set.totalSize;
        set.uniformDecided = true;
    }

    const auto expectedLength = [&](int k) -> qint64 {
        if (k == 0)
            return set.p1;
        if (k == n - 1)
            return set.pN;
        return set.p2;
    };
    const auto expectedStart = [&](int k) -> qint64 {
        return k == 0 ? 0 : set.p1 + qint64(k - 1) * set.p2;
    };

    if (set.uniform) {
        // Serve only from volumes whose own header we hold: the model places a
        // volume, it does not say where inside it the payload begins. RAR5's
        // volume-number field grows a byte at volume 128, and a header assumed
        // one byte short maps every read in that volume off by one.
        for (int k = 0; k < n; ++k) {
            const int idx = set.volumes.at(k);

            // Opportunistic: parse any volume whose header is already on disk.
            // readIfAvailable() checks the written ranges before it opens
            // anything, so a volume nobody has fetched costs nothing here and,
            // crucially, is not fetched *for* this — which is the whole point of
            // a seek that skips it.
            StreamResolve ignored;
            if (!m_volumes.contains(idx))
                (void)volumeHeader(item, idx, ignored);

            const auto it = m_volumes.constFind(idx);
            if (it != m_volumes.constEnd() && it->status == RarParse::Ok) {
                const RarEntry& e = it->entries.first();
                if (e.packedSize != expectedLength(k)) {
                    set.uniform = false;   // fall through to the walk below
                    break;
                }
                out.extents.append({idx, expectedStart(k), e.dataOffset, e.packedSize});
            }
        }

        if (set.uniform) {
            const qint64 target = qBound<qint64>(0, wantOffset, set.totalSize - 1);
            for (int k = 0; k < n; ++k) {
                if (target < expectedStart(k) + expectedLength(k)) {
                    if (!volumeHeader(item, set.volumes.at(k), out))
                        return out;
                    if (!extentAt(out.extents, target)) {
                        const RarEntry& e = m_volumes.value(set.volumes.at(k)).entries.first();
                        out.extents.append({set.volumes.at(k), expectedStart(k),
                                            e.dataOffset, e.packedSize});
                        std::sort(out.extents.begin(), out.extents.end(),
                                  [](const StreamExtent& a, const StreamExtent& b) {
                                      return a.virtualOffset < b.virtualOffset;
                                  });
                    }
                    break;
                }
            }
            out.plan = StreamPlan::Ready;
            return out;
        }
        out.extents.clear();
    }

    // Non-uniform: no model to lean on, so walk the volumes in order and place
    // each from its own header. Correct, one article per volume, and rare —
    // it takes a set that was not produced by a plain `rar -v`.
    qint64 cursor = 0;
    for (int k = 0; k < n; ++k) {
        const int idx = set.volumes.at(k);
        const auto it = m_volumes.constFind(idx);
        if (it == m_volumes.constEnd() || it->status != RarParse::Ok) {
            if (cursor > wantOffset) {
                // Already past what was asked for; stop rather than pull in the
                // whole set to answer one read.
                break;
            }
            if (!volumeHeader(item, idx, out))
                return out;
        }
        const RarEntry& e = m_volumes.value(idx).entries.first();
        out.extents.append({idx, cursor, e.dataOffset, e.packedSize});
        cursor += e.packedSize;
    }

    out.plan = StreamPlan::Ready;
    return out;
}

// --- free functions --------------------------------------------------------

const StreamExtent* extentAt(const QList<StreamExtent>& extents, qint64 virtualOffset)
{
    for (const StreamExtent& e : extents) {
        if (virtualOffset >= e.virtualOffset && virtualOffset < e.virtualOffset + e.length)
            return &e;
    }
    return nullptr;
}

qint64 availableFrom(const UsenetQueueItem& item, const QList<StreamExtent>& extents,
                     qint64 virtualOffset)
{
    qint64 pos = qMax<qint64>(0, virtualOffset);

    for (const StreamExtent& e : extents) {
        if (e.virtualOffset + e.length <= pos)
            continue;
        if (e.virtualOffset > pos)
            break;                     // a gap in the map stops the run
        if (e.fileIndex < 0 || e.fileIndex >= item.files.size())
            break;

        const qint64 into = pos - e.virtualOffset;
        const qint64 at = e.fileOffset + into;
        const qint64 gained = item.files.at(e.fileIndex).availableFrom(at) - at;
        const qint64 usable = qMin(gained, e.length - into);
        pos += qMax<qint64>(0, usable);

        if (pos < e.virtualOffset + e.length)
            break;                     // stopped inside this volume
    }

    return pos;
}

} // namespace eMule::usenet
