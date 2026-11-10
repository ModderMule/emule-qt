#pragma once

/// @file SaveFolderSpace.h
/// @brief Free space in the default save folder of each download network (#165).
///
/// One row per network: eD2K incoming, the torrent save folder, the Usenet and the
/// hoster target. A network that is switched off has no row. Measured on a worker
/// and cached: a sleeping or network disk must not hold the core (report 24).

#include <QString>

#include <optional>
#include <vector>

class QCborMap;
class QJsonObject;

namespace eMule {

struct SaveFolderSpace {
    /// "incoming", "torrent", "usenet" or "hoster": the wire key
    QString key;
    QString path;
    /// bytes available to us; empty = not measured yet or unreadable
    std::optional<qint64> free;
};

/// The rows with their last measured space. A refresh starts on the thread pool
/// when the reading is older than 5 s or the folders changed.
[[nodiscard]] std::vector<SaveFolderSpace> saveFolderSpace();

/// {key: {path, free}}, free absent when unknown: stats "saveFolderSpace", REST
[[nodiscard]] QCborMap saveFolderSpaceToCbor(const std::vector<SaveFolderSpace>& rows);
[[nodiscard]] QJsonObject saveFolderSpaceToJson(const std::vector<SaveFolderSpace>& rows);

} // namespace eMule
