#include "pch.h"
/// @file Ed3kDownloadRows.cpp
/// @brief ed3k download rows for the Transfers list — implementation.

#include "controls/Ed3kDownloadRows.h"

#include "utils/BlockRuns.h"
#include "utils/OtherFunctions.h"

#include <QCborArray>

#include <algorithm>

namespace eMule {

DownloadRow ed3kDownloadRow(const QCborMap& m)
{
    static const QString prio[] = {QStringLiteral("low"), QStringLiteral("normal"), QStringLiteral("high"),
                                   QStringLiteral("veryHigh"), QStringLiteral("veryLow")};
    DownloadRow row;
    row.network = 1;
    row.hash = m.value(QStringLiteral("root")).toString();
    row.fileName = m.value(QStringLiteral("name")).toString();
    row.fileSize = m.value(QStringLiteral("size")).toInteger();
    row.completedSize = m.value(QStringLiteral("done")).toInteger();
    row.percentCompleted = row.fileSize > 0 ? 100.0 * double(row.completedSize) / double(row.fileSize) : 0.0;
    row.datarate = m.value(QStringLiteral("rate")).toInteger();
    row.sourceCount = int(m.value(QStringLiteral("sources")).toInteger());
    row.transferringSrcCount = int(m.value(QStringLiteral("transferring")).toInteger());
    row.availableSrcCount = row.sourceCount;
    row.priority = prio[std::clamp<qint64>(m.value(QStringLiteral("priority")).toInteger(1), 0, 4)];
    row.category = m.value(QStringLiteral("category")).toInteger();
    row.addedOn = m.value(QStringLiteral("addedOn")).toInteger();
    row.ed3kPath = m.value(QStringLiteral("path")).toString();
    row.fileType = getFileTypeByName(row.fileName);
    // the eD2K status tokens (statusText): verifying shows as "Completing (Hashing)"
    switch (m.value(QStringLiteral("state")).toInteger()) {
    case 1: row.status = QStringLiteral("paused"); row.isPaused = true; break;
    case 2: row.status = QStringLiteral("completing"); row.fileOp = 1; break;
    case 3: row.status = QStringLiteral("complete"); row.percentCompleted = 100.0; break;
    case 4: row.status = QStringLiteral("error"); break;
    default: row.status = row.transferringSrcCount > 0 ? QStringLiteral("ready") : QStringLiteral("empty"); break;
    }
    if (row.datarate > 0 && row.fileSize > row.completedSize)
        row.timeRemaining = (row.fileSize - row.completedSize) / row.datarate;
    if (row.fileSize <= 0)
        return row;

    // the bar: the blocks held, those asked for, sources per piece (eD2K's painter)
    row.hasBarRanges = true;
    if (row.isComplete())
        return row;
    const qint64 block = m.value(QStringLiteral("blockSize")).toInteger();
    const auto count = block > 0 ? std::size_t((row.fileSize + block - 1) / block) : 0;
    const auto held = count > 0 ? decodeBlockRuns(m.value(QStringLiteral("blocks")).toByteArray(), count) : std::nullopt;
    if (!held) {
        // was the only bar: what is held drawn as one run from the start (an older core)
        if (row.completedSize < row.fileSize)
            row.gaps = {row.completedSize, row.fileSize - 1};
        return row;
    }
    row.gaps = blockRanges(*held, false, block, row.fileSize);
    if (const auto pending = decodeBlockRuns(m.value(QStringLiteral("pending")).toByteArray(), count))
        row.pending = blockRanges(*pending, true, block, row.fileSize);
    const QByteArray avail = m.value(QStringLiteral("avail")).toByteArray();
    const qint64 piece = m.value(QStringLiteral("pieceSize")).toInteger();
    if (!avail.isEmpty() && piece > 0) {
        row.partFreqUnit = piece;
        row.partFreq.reserve(avail.size());
        for (const char c : avail)
            row.partFreq << quint16(std::uint8_t(c));
    }
    return row;
}

std::vector<DownloadRow> ed3kDownloadRows(const QCborMap& list)
{
    std::vector<DownloadRow> rows;
    for (const auto& v : list.value(QStringLiteral("downloads")).toArray())
        rows.push_back(ed3kDownloadRow(v.toMap()));
    return rows;
}

} // namespace eMule
