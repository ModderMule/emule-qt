#include "queue/UsenetRepairEstimate.h"

#include "queue/UsenetQueueItem.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace eMule::usenet {

namespace {

/// How sealFile() names a file on disk, which is how the set will know it.
[[nodiscard]] QString bareName(const QString& name)
{
    return sanitizeName(QFileInfo(name).fileName());
}

} // namespace

UsenetRepairEstimate estimateRepair(const UsenetQueueItem& item, qint64 blockSize,
                                    const QList<Par2SetFile>& setFiles)
{
    if (blockSize <= 0 || setFiles.isEmpty())
        return {};

    QHash<QString, qint64> covered;   // recoverable file -> size the set declares
    QSet<QString> listed;             // every file the set mentions
    for (const Par2SetFile& f : setFiles) {
        const QString name = bareName(f.fileName);
        listed.insert(name);
        if (f.recoverable)
            covered.insert(name, f.size);
    }

    // Per set file, the least damaged NZB copy of it: a repost puts the same
    // volume in the NZB twice, and par2 takes its blocks from whichever is good.
    QHash<QString, qint64> damage;
    qint64 recovery = 0;

    const qsizetype count = std::min(item.files.size(), item.nzb.files.size());
    for (qsizetype f = 0; f < count; ++f) {
        const NzbFileInfo& info = item.nzb.files.at(f);
        const UsenetFileState& st = item.files.at(f);

        qint64 notMissingBytes = 0;
        qint64 pendingBytes = 0;
        bool allResolved = true;
        QSet<int> arrivedParts;
        QList<int> missingParts;

        for (qsizetype s = 0; s < info.segments.size(); ++s) {
            const NzbSegment& segment = info.segments.at(s);
            // An NZB that does not say how big its articles are is not one to
            // stop a download on.
            if (segment.bytes <= 0)
                return {};

            const bool done = s < st.done.size() && st.done.testBit(s);
            const bool missing = done && s < st.missing.size() && st.missing.testBit(s);
            if (!done) {
                allResolved = false;
                pendingBytes += segment.bytes;
                notMissingBytes += segment.bytes;
            } else if (missing) {
                missingParts.append(segment.number);
            } else {
                arrivedParts.insert(segment.number);
                notMissingBytes += segment.bytes;
            }
        }

        const QString name = bareName(item.bestFileName(int(f)));
        const auto source = covered.constFind(name);

        if (source != covered.cend() && !info.isPar2()) {
            qint64 blocks = 0;
            // Still downloading: the articles on their way cannot be priced
            // without trusting the NZB's sizes, so this copy claims no damage.
            if (allResolved) {
                // A part number another segment supplied is a duplicate, not a hole.
                const bool hole = std::ranges::any_of(missingParts, [&](int part) {
                    return !arrivedParts.contains(part);
                });
                if (hole) {
                    const qint64 shortBy = std::max<qint64>(0, *source - st.decodedBytes);
                    blocks = std::max<qint64>(1, (shortBy + blockSize - 1) / blockSize);
                }
            }
            const auto seen = damage.constFind(name);
            damage.insert(name, seen == damage.cend() ? blocks : std::min(*seen, blocks));
            continue;
        }

        // Listed but not recoverable: neither damage nor recovery data.
        if (listed.contains(name) && !info.isPar2())
            continue;

        // Anything else may hold recovery data, whatever it is called. Each block
        // takes at least blockSize bytes, and encoded bytes outnumber decoded ones.
        const qint64 bytes = std::max(notMissingBytes, st.decodedBytes + pendingBytes);
        recovery += std::max<qint64>(bytes / blockSize, info.par2RecoveryBlocks());
    }

    if (damage.isEmpty())
        return {};

    UsenetRepairEstimate out;
    for (const qint64 blocks : std::as_const(damage))
        out.damagedBlocks += blocks;
    out.recoveryBlocks = recovery;
    out.verdict = out.damagedBlocks > out.recoveryBlocks
                      ? UsenetRepairEstimate::Verdict::Unrepairable
                      : UsenetRepairEstimate::Verdict::Repairable;
    return out;
}

} // namespace eMule::usenet
