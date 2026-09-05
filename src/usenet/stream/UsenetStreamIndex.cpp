#include "stream/UsenetStreamIndex.h"

#include "post/UsenetUnpacker.h"
#include "queue/UsenetQueueItem.h"
#include "utils/OtherFunctions.h"

#include <QCoreApplication>
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

/// The same test ED2K's PartFile::isPreviewPossible() uses, so the two networks
/// cannot disagree about what "playable" means.
bool isPlayableName(const QString& name)
{
    const ED2KFileType type = getED2KFileTypeID(name);
    return type == ED2KFileType::Video || type == ED2KFileType::Audio;
}

QString tr(const char* text)
{
    return QCoreApplication::translate("eMule::UsenetStreamIndex", text);
}

} // namespace

StreamResolve UsenetStreamIndex::resolve(const UsenetQueueItem& item, int fileIndex,
                                         int memberIndex, qint64 wantOffset)
{
    if (fileIndex < 0 || fileIndex >= item.files.size() || fileIndex >= item.nzb.files.size())
        return {};

    StreamResolve out;
    SetInfo* set = setFor(item, fileIndex, out);
    if (!set) {
        if (out.plan != StreamPlan::Unknown)
            return out;   // needs bytes, or the set was refused outright

        const QList<int> members = splitMembersOf(item, fileIndex);
        if (members.size() > 1)
            return resolveSplitSet(item, members);
        return resolveRawFile(item, fileIndex);
    }

    // Enumerate only as far as the answer needs. A seek into member 0 of a
    // hundred-volume set must not pay for listing members it will never touch.
    if (!scanUntil(item, *set, memberIndex, /*all*/ false, out))
        return out;

    const int m = pickMember(*set, memberIndex);
    if (m < 0) {
        return notSeekable(set->members.isEmpty()
                               ? tr("Archive volume holds no mappable file")
                               : tr("The archive does not contain a playable file"));
    }

    const MemberInfo& mi = set->members.at(m);
    if (!mi.note.isEmpty())
        return notSeekable(mi.note);

    return memberExtents(item, *set, m, wantOffset);
}

StreamListing UsenetStreamIndex::list(const UsenetQueueItem& item, int fileIndex)
{
    StreamListing listing;

    if (fileIndex < 0 || fileIndex >= item.files.size() || fileIndex >= item.nzb.files.size())
        return listing;

    StreamResolve probe;
    SetInfo* set = setFor(item, fileIndex, probe);
    if (!set) {
        if (probe.plan == StreamPlan::NeedBytes) {
            listing.plan = StreamPlan::NeedBytes;
            listing.needFileIndex = probe.needFileIndex;
            listing.needOffset = probe.needOffset;
            listing.needLength = probe.needLength;
            return listing;
        }
        if (probe.plan == StreamPlan::NotSeekable) {
            listing.plan = StreamPlan::NotSeekable;
            listing.reason = probe.reason;
            return listing;
        }

        // A raw post or a `.001` split: exactly one logical file, and there was
        // never a choice to offer.
        const QList<int> members = splitMembersOf(item, fileIndex);
        const StreamResolve one = members.size() > 1 ? resolveSplitSet(item, members)
                                                     : resolveRawFile(item, fileIndex);
        if (one.plan != StreamPlan::Ready) {
            listing.plan = one.plan;
            listing.reason = one.reason;
            listing.needFileIndex = one.needFileIndex;
            listing.needOffset = one.needOffset;
            listing.needLength = one.needLength;
            return listing;
        }
        listing.plan = StreamPlan::Ready;
        listing.complete = true;
        listing.members.append(StreamMember{0, one.fileName, one.totalSize,
                                            isPlayableName(one.fileName), true, {}});
        return listing;
    }

    listing.isArchive = true;

    StreamResolve out;
    const bool done = scanUntil(item, *set, /*wantMember*/ -1, /*all*/ true, out);

    for (int i = 0; i < set->members.size(); ++i) {
        const MemberInfo& mi = set->members.at(i);
        StreamMember row;
        row.index = i;
        row.name = mi.name;
        row.size = mi.unpackedSize;
        row.playable = isPlayableName(mi.name);
        row.mappable = mi.note.isEmpty();
        row.note = mi.note;
        if (row.mappable && !row.playable)
            row.note = tr("Not playable");
        listing.members.append(row);
    }

    if (!done) {
        if (out.plan == StreamPlan::NotSeekable && listing.members.isEmpty()) {
            listing.plan = StreamPlan::NotSeekable;
            listing.reason = out.reason;
            return listing;
        }
        // Bytes are missing, but whatever has been enumerated so far is real and
        // final — members only ever grow at the tail.
        listing.plan = out.plan;
        listing.needFileIndex = out.needFileIndex;
        listing.needOffset = out.needOffset;
        listing.needLength = out.needLength;
        return listing;
    }

    listing.plan = StreamPlan::Ready;
    listing.complete = set->complete;
    return listing;
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

UsenetStreamIndex::SetInfo* UsenetStreamIndex::setFor(const UsenetQueueItem& item, int fileIndex,
                                                      StreamResolve& out)
{
    const QString name = fileNameOf(item, fileIndex);
    if (name.isEmpty()) {
        // An obfuscated post says nothing until its first article arrives, and
        // `=ybegin name=` is then the only place the real name exists.
        out = needBytes(fileIndex, 0, kRarHeaderProbeBytes);
        return nullptr;
    }

    const auto pos = UsenetUnpacker::volumePositionOf(name);
    if (pos.index < 0)
        return nullptr;   // not an archive volume; out stays Unknown

    SetInfo& set = m_sets[pos.baseName];
    if (set.refused) {
        out = notSeekable(set.reason);
        return nullptr;
    }

    if (set.volumes.isEmpty()) {
        QList<QPair<int, int>> ordered;   // (volume index, NZB file index)
        for (int i = 0; i < item.nzb.files.size() && i < item.files.size(); ++i) {
            const QString n = fileNameOf(item, i);
            if (n.isEmpty()) {
                // One unnamed file is enough to make the set's order a guess.
                // §7.2 calls this the mandatory prefetch pass: one article per
                // file, and only on releases whose NZB hides the names.
                if (!item.nzb.files.at(i).isPar2()) {
                    out = needBytes(i, 0, kRarHeaderProbeBytes);
                    return nullptr;
                }
                continue;
            }
            const auto p = UsenetUnpacker::volumePositionOf(n);
            if (p.index >= 0 && p.baseName == pos.baseName)
                ordered.append({p.index, i});
        }
        if (ordered.isEmpty())
            return nullptr;

        std::sort(ordered.begin(), ordered.end());
        for (const auto& v : std::as_const(ordered))
            set.volumes.append(v.second);
    }

    return &set;
}

bool UsenetStreamIndex::volumeHead(const UsenetQueueItem& item, const SetInfo& set, int slot,
                                   StreamResolve& out)
{
    if (slot < 0 || slot >= set.volumes.size())
        return false;

    const int fileIndex = set.volumes.at(slot);
    auto it = m_volumes.constFind(fileIndex);
    if (it == m_volumes.constEnd()) {
        QByteArray head;
        if (!readIfAvailable(item, fileIndex, 0, kRarHeaderProbeBytes, head)) {
            out = needBytes(fileIndex, 0, kRarHeaderProbeBytes);
            return false;
        }

        RarVolume v = parseRarVolume(head);

        if (v.status == RarParse::NeedMoreBytes) {
            // Short only while the first article is still landing. Compare
            // against what this volume *can* return: a volume shorter than the
            // probe window can never fill it, and asking again forever is how a
            // mid-volume probe near the end used to hang.
            const qint64 cap = qMin<qint64>(kRarHeaderProbeBytes,
                                            item.files.at(fileIndex).declaredSize);
            if (head.size() < cap) {
                out = needBytes(fileIndex, 0, kRarHeaderProbeBytes);
                return false;
            }
            v.status = RarParse::Unsupported;
            v.reason = tr("Archive header is larger than expected");
        }

        VolumeCache vc;
        vc.status = v.status;
        vc.format = v.format;
        vc.volumeNumber = v.volumeNumber;
        vc.isFirstVolume = v.isFirstVolume;
        vc.endOfArchive = v.endOfArchive;
        vc.reason = v.reason;
        vc.scanned = v.nextOffset;
        for (const RarEntry& e : std::as_const(v.entries))
            vc.blocks.insert(e.headerOffset, e);
        it = m_volumes.insert(fileIndex, vc);
    }

    if (it->status == RarParse::Ok)
        return true;

    // Only whole-archive verdicts reach here — solid, encrypted headers, not a
    // RAR at all. A compressed or encrypted *entry* is a property of that member
    // and must never refuse the set around it.
    out = notSeekable(it->reason.isEmpty() ? tr("Not a readable archive") : it->reason);
    return false;
}

bool UsenetStreamIndex::blocksAt(const UsenetQueueItem& item, const SetInfo& set, int slot,
                                 qint64 offset, RarEntry& entry, qint64& nextOffset,
                                 bool& exhausted, StreamResolve& out)
{
    exhausted = false;

    const int fileIndex = set.volumes.at(slot);
    const qint64 declared = item.files.at(fileIndex).declaredSize;
    if (declared > 0 && offset >= declared) {
        exhausted = true;
        nextOffset = offset;
        return true;
    }

    VolumeCache& vc = m_volumes[fileIndex];

    // Parsing always starts at a block boundary and walks forward, so the first
    // cached block at or after the cursor is the one the cursor names.
    const auto cached = vc.blocks.lowerBound(offset);
    if (cached != vc.blocks.constEnd()) {
        entry = cached.value();
        nextOffset = entry.dataOffset + entry.packedSize;
        return true;
    }

    if (vc.endOfArchive && offset >= vc.scanned) {
        exhausted = true;
        nextOffset = offset;
        return true;
    }

    QByteArray window;
    if (!readIfAvailable(item, fileIndex, offset, kRarHeaderProbeBytes, window)) {
        out = needBytes(fileIndex, offset, kRarHeaderProbeBytes);
        return false;
    }

    const RarVolume v = (offset == 0) ? parseRarVolume(window)
                                      : parseRarBlocks(vc.format, window, offset);

    if (v.status == RarParse::NeedMoreBytes) {
        const qint64 cap = qMin<qint64>(kRarHeaderProbeBytes, declared - offset);
        if (window.size() < cap) {
            out = needBytes(fileIndex, offset, kRarHeaderProbeBytes);
            return false;
        }
        // Every byte that can be here is here and nothing parsed: the volume's
        // blocks are done, whatever the trailing padding is.
        exhausted = true;
        nextOffset = offset;
        return true;
    }

    if (v.status != RarParse::Ok) {
        out = notSeekable(v.reason.isEmpty() ? tr("Not a readable archive") : v.reason);
        return false;
    }

    for (const RarEntry& e : std::as_const(v.entries))
        vc.blocks.insert(e.headerOffset, e);
    vc.scanned = qMax(vc.scanned, v.nextOffset);
    vc.endOfArchive = vc.endOfArchive || v.endOfArchive;

    if (v.entries.isEmpty()) {
        exhausted = true;
        nextOffset = qMax(offset, v.nextOffset);
        return true;
    }

    entry = v.entries.first();
    nextOffset = entry.dataOffset + entry.packedSize;
    return true;
}

bool UsenetStreamIndex::scanUntil(const UsenetQueueItem& item, SetInfo& set, int wantMember,
                                  bool all, StreamResolve& out)
{
    const auto satisfied = [&] {
        if (set.complete)
            return true;
        if (all)
            return false;
        if (wantMember >= 0)
            return wantMember < set.members.size() && set.members.at(wantMember).placed;
        // The first playable member is enough for a preview with no entry named.
        for (const MemberInfo& mi : std::as_const(set.members)) {
            if (mi.placed && mi.note.isEmpty() && isPlayableName(mi.name))
                return true;
        }
        return false;
    };

    for (;;) {
        if (satisfied())
            return true;

        // A member left unplaced by a NeedBytes must be finished, not appended
        // a second time.
        if (!set.members.isEmpty() && !set.members.last().placed) {
            if (!placeMember(item, set, int(set.members.size()) - 1, out))
                return false;
            const MemberInfo& done = set.members.last();
            set.scanSlot = done.endSlot;
            set.scanOffset = done.endDataOffset + done.endPartSize;
            continue;
        }

        if (set.scanSlot >= set.volumes.size()) {
            set.complete = true;
            return true;
        }

        if (!volumeHead(item, set, set.scanSlot, out))
            return false;
        if (set.format == RarFormat::Unknown)
            set.format = m_volumes.value(set.volumes.at(set.scanSlot)).format;

        RarEntry entry;
        qint64 next = 0;
        bool exhausted = false;
        if (!blocksAt(item, set, set.scanSlot, set.scanOffset, entry, next, exhausted, out))
            return false;

        if (exhausted) {
            ++set.scanSlot;
            set.scanOffset = 0;
            continue;
        }

        if (entry.splitBefore) {
            // A continuation with no member of ours in front of it. Either the
            // volume order is wrong or we joined the set mid-file; skip past it
            // rather than inventing a member that starts nowhere.
            set.scanOffset = next;
            continue;
        }

        MemberInfo mi;
        mi.name = entry.name;
        mi.unpackedSize = entry.unpackedSize;
        mi.stored = entry.stored;
        mi.encrypted = entry.encrypted;
        mi.firstSplitAfter = entry.splitAfter;
        mi.startSlot = set.scanSlot;
        mi.startDataOffset = entry.dataOffset;
        mi.firstPartSize = entry.packedSize;
        set.members.append(mi);
    }
}

bool UsenetStreamIndex::placeMember(const UsenetQueueItem& item, SetInfo& set, int m,
                                    StreamResolve& out)
{
    MemberInfo& mi = set.members[m];
    const int n = int(set.volumes.size());

    const auto endHere = [&](const QString& note) {
        mi.endSlot = mi.startSlot;
        mi.endDataOffset = mi.startDataOffset;
        mi.endPartSize = mi.firstPartSize;
        mi.note = note;
        mi.placed = true;
    };

    if (mi.encrypted) {
        endHere(tr("Encrypted archive"));
        return true;
    }

    // Contained in one volume: nothing to predict, and the cursor lands on the
    // next member's header inside this same window.
    if (!mi.firstSplitAfter) {
        mi.endSlot = mi.startSlot;
        mi.endDataOffset = mi.startDataOffset;
        mi.endPartSize = mi.firstPartSize;
        mi.uniform = true;
        mi.midPartSize = 0;
        mi.placed = true;
        if (!mi.stored)
            mi.note = tr("Compressed archive — cannot seek without decompressing");
        return true;
    }

    if (!mi.stored) {
        // A compressed member that spans volumes cannot be skipped: RAR states
        // no total *packed* size, so where it ends is unknowable without walking
        // every volume — for a set we are refusing anyway. List it and stop.
        endHere(tr("Compressed archive — cannot seek without decompressing"));
        set.complete = true;
        return true;
    }

    if (mi.unpackedSize <= 0) {
        endHere(tr("Archive does not declare its unpacked size"));
        set.complete = true;
        return true;
    }

    if (mi.startSlot + 1 >= n) {
        endHere(tr("The archive is missing a volume"));
        set.complete = true;
        return true;
    }

    if (!volumeHead(item, set, mi.startSlot + 1, out))
        return false;

    const RarEntry* cont = firstEntryOf(set, mi.startSlot + 1);
    if (!cont || cont->name != mi.name || !cont->splitBefore) {
        endHere(tr("Archive volumes are out of order"));
        set.complete = true;
        return true;
    }

    mi.midPartSize = cont->packedSize;

    if (!cont->splitAfter) {
        mi.endSlot = mi.startSlot + 1;
        mi.endDataOffset = cont->dataOffset;
        mi.endPartSize = cont->packedSize;
        mi.uniform = true;
        mi.placed = true;
        return true;
    }

    if (mi.midPartSize <= 0) {
        endHere(tr("Archive volumes are out of order"));
        set.complete = true;
        return true;
    }

    // `rar -v` pads every volume alike, so the run's length follows from one
    // interior part size. ceil, not 1 + R/p: a member ending flush on a volume
    // boundary would otherwise be placed one volume past its end, which is the
    // common case for the first member of a set cut to a round size.
    const qint64 R = mi.unpackedSize - mi.firstPartSize;
    const int span = int((R + mi.midPartSize - 1) / mi.midPartSize);
    const int predicted = qBound(mi.startSlot + 1, mi.startSlot + span, n - 1);

    if (!volumeHead(item, set, predicted, out))
        return false;
    if (acceptEnd(set, mi, predicted)) {
        mi.uniform = true;
        mi.placed = true;
        return true;
    }

    // The prediction was wrong — a RAR5 set whose volume-number vint grew, or a
    // set no plain `rar -v` produced. "Volume s still holds this member" is
    // monotone in s, so the real end is a binary search away rather than a walk
    // of every volume between.
    int lo = mi.startSlot + 1;
    int hi = n - 1;
    while (lo < hi) {
        const int mid = lo + (hi - lo + 1) / 2;
        if (!volumeHead(item, set, mid, out))
            return false;
        const RarEntry* e = firstEntryOf(set, mid);
        if (e && e->name == mi.name && e->splitBefore)
            lo = mid;
        else
            hi = mid - 1;
    }

    if (acceptEnd(set, mi, lo)) {
        mi.uniform = true;
        mi.placed = true;
        return true;
    }

    // Interior volumes are not uniform after all. Place the run from its own
    // headers — one probe per volume of *this member*, not of the set.
    mi.uniform = false;
    mi.extents.clear();
    qint64 cursor = mi.firstPartSize;
    mi.extents.append({set.volumes.at(mi.startSlot), 0, mi.startDataOffset, mi.firstPartSize});
    for (int s = mi.startSlot + 1; s <= lo; ++s) {
        if (!volumeHead(item, set, s, out))
            return false;
        const RarEntry* e = firstEntryOf(set, s);
        if (!e || e->name != mi.name)
            break;
        mi.extents.append({set.volumes.at(s), cursor, e->dataOffset, e->packedSize});
        cursor += e->packedSize;
        mi.endSlot = s;
        mi.endDataOffset = e->dataOffset;
        mi.endPartSize = e->packedSize;
    }
    mi.placed = true;
    return true;
}

const RarEntry* UsenetStreamIndex::firstEntryOf(const SetInfo& set, int slot) const
{
    if (slot < 0 || slot >= set.volumes.size())
        return nullptr;
    const auto vc = m_volumes.constFind(set.volumes.at(slot));
    if (vc == m_volumes.constEnd() || vc->status != RarParse::Ok || vc->blocks.isEmpty())
        return nullptr;
    return &vc->blocks.constBegin().value();
}

bool UsenetStreamIndex::acceptEnd(const SetInfo& set, MemberInfo& mi, int slot) const
{
    const RarEntry* e = firstEntryOf(set, slot);
    if (!e || e->name != mi.name || !e->splitBefore || e->splitAfter)
        return false;

    const qint64 interior = qint64(slot - mi.startSlot - 1) * mi.midPartSize;
    if (mi.firstPartSize + interior + e->packedSize != mi.unpackedSize)
        return false;

    mi.endSlot = slot;
    mi.endDataOffset = e->dataOffset;
    mi.endPartSize = e->packedSize;
    return true;
}

int UsenetStreamIndex::pickMember(const SetInfo& set, int requested) const
{
    if (requested >= 0)
        return requested < set.members.size() ? requested : -1;

    for (int i = 0; i < set.members.size(); ++i) {
        const MemberInfo& mi = set.members.at(i);
        if (mi.placed && mi.note.isEmpty() && isPlayableName(mi.name))
            return i;
    }
    return -1;
}

StreamResolve UsenetStreamIndex::memberExtents(const UsenetQueueItem& item, SetInfo& set, int m,
                                               qint64 wantOffset)
{
    MemberInfo& mi = set.members[m];

    StreamResolve out;
    out.fileName = mi.name;
    out.totalSize = mi.unpackedSize;
    out.plan = StreamPlan::Ready;

    if (!mi.uniform) {
        out.extents = mi.extents;
        return out;
    }

    const int first = mi.startSlot;
    const int last = mi.endSlot;
    const int parts = last - first + 1;

    const auto expectedLength = [&](int k) -> qint64 {
        if (k == first)
            return mi.firstPartSize;
        if (k == last)
            return mi.endPartSize;
        return mi.midPartSize;
    };
    const auto expectedStart = [&](int k) -> qint64 {
        return k == first ? 0 : mi.firstPartSize + qint64(k - first - 1) * mi.midPartSize;
    };

    if (parts == 1) {
        out.extents.append({set.volumes.at(first), 0, mi.startDataOffset, mi.firstPartSize});
        if (out.totalSize <= 0)
            out.totalSize = mi.firstPartSize;
        return out;
    }

    // Serve only from volumes whose own header we hold: the model places a
    // volume, it does not say where inside it the payload begins. RAR5's
    // volume-number field grows a byte at volume 128, and a header assumed one
    // byte short maps every read in that volume off by one.
    for (int k = first; k <= last; ++k) {
        // Opportunistic: parse any volume whose header is already on disk.
        // readIfAvailable() checks the written ranges before it opens anything,
        // so a volume nobody has fetched costs nothing here and, crucially, is
        // not fetched *for* this — which is the whole point of a seek that
        // skips it.
        if (!m_volumes.contains(set.volumes.at(k))) {
            StreamResolve ignored;
            (void)volumeHead(item, set, k, ignored);
        }

        const RarEntry* e = firstEntryOf(set, k);
        if (k == first) {
            out.extents.append({set.volumes.at(k), 0, mi.startDataOffset, mi.firstPartSize});
            continue;
        }
        if (e && e->name == mi.name && e->packedSize == expectedLength(k))
            out.extents.append({set.volumes.at(k), expectedStart(k), e->dataOffset, e->packedSize});
    }

    // Make sure the volume the read lands on is placed, even if nothing else is.
    const qint64 target = qBound<qint64>(0, wantOffset, qMax<qint64>(0, mi.unpackedSize - 1));
    for (int k = first; k <= last; ++k) {
        if (target >= expectedStart(k) + expectedLength(k))
            continue;
        if (!extentAt(out.extents, target)) {
            StreamResolve probe;
            if (!volumeHead(item, set, k, probe))
                return probe;
            const RarEntry* e = firstEntryOf(set, k);
            if (e) {
                out.extents.append({set.volumes.at(k), expectedStart(k), e->dataOffset,
                                    e->packedSize});
            }
        }
        break;
    }

    std::sort(out.extents.begin(), out.extents.end(),
              [](const StreamExtent& a, const StreamExtent& b) {
                  return a.virtualOffset < b.virtualOffset;
              });
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
