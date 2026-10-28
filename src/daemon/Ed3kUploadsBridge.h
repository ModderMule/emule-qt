#pragma once

/// @file Ed3kUploadsBridge.h
/// @brief The ed3k upload queue (Ed3kUploads) as rows of GetUploads, next to eD2K's clients.
/// Free of the ed3k module like Ed3kBridge.h: without it nothing is added.
///
/// A row is a client map as IpcClientHandler::toCbor writes one, with `network: "ed3k"`,
/// `userHash` = node ID (hex), `uploadState` Transferring / OnQueue, `sessionUp` = bytes of
/// the turn, `uploadFileName`/`uploadFileSize` when known, `queueRank` = place by waiting.

#include <QCborArray>

namespace eMule {

namespace ed3k {
class Ed3kUploads;
} // namespace ed3k

namespace Ed3kUploadsBridge {

/// Set by DaemonApp, null when absent.
void setUploads(ed3k::Ed3kUploads* uploads);

/// Appends the ed3k rows to GetUploads' two lists.
void appendRows(QCborArray& uploading, QCborArray& waiting);

} // namespace Ed3kUploadsBridge
} // namespace eMule
