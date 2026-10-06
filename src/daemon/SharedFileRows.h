#pragma once

/// @file SharedFileRows.h
/// @brief One shared-files row as the GUI gets it — in the list reply and in a push.

#include "files/KnownFileList.h"
#include "files/SharedFileList.h"

#include <QCborMap>

#include <unordered_map>

namespace eMule {

class KnownFile;

/// What the upload queue holds for one file.
struct SharedFileLoad {
    int queuedClients = 0;
    qint64 uploadDataRate = 0;
};

/// Per requested file, from one pass over the waiting and the upload list.
[[nodiscard]] std::unordered_map<MD4Key, SharedFileLoad> sharedFileLoads();
/// The same for a single file.
[[nodiscard]] SharedFileLoad sharedFileLoad(const uint8* fileHash);

/// The row of a shared file, or of a part file with data that is not shared yet.
[[nodiscard]] QCborMap sharedFileRow(KnownFile* kf, const SharedFileList::ShareRules& rules,
                                     const SharedFileLoad& load);

} // namespace eMule
