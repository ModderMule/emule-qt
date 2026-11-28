#pragma once

/// @file Ed3kUploadsBridge.h
/// @brief The ed3k upload queue (Ed3kUploads) as rows of GetUploads, next to eD2K's clients.
/// Free of the ed3k module like Ed3kBridge.h: without it nothing is added.
///
/// A row is a client map as IpcClientHandler::toCbor writes one, with `network: "ed3k"`,
/// `userHash` = node ID (hex), `uploadState` Transferring / OnQueue, `sessionUp` = bytes of
/// the turn, `uploadFileName`/`uploadFileSize` when known, `queueRank` = place by waiting.

#include <QByteArray>
#include <QCborArray>
#include <QCborMap>
#include <QString>

#include <optional>

namespace eMule {

namespace ed3k {
class Ed3kUploads;
} // namespace ed3k

namespace Ed3kUploadsBridge {

/// Set by DaemonApp, null when absent.
void setUploads(ed3k::Ed3kUploads* uploads);

/// Appends the ed3k rows to GetUploads' two lists.
void appendRows(QCborArray& uploading, QCborArray& waiting);

/// GetClientDetails key of an ed3k row: "ed3k:<node hex>", "ed3k:anon:<node hex>" for the
/// anonymous node's queue (an eD2K user hash is 32 hex too, so the prefix tells them apart).
[[nodiscard]] bool isDetailsKey(const QString& key);

/// The queue row behind @p key as a GetClientDetails map (`userHash` = @p key, `nodeId`,
/// `anonymous`, `router`/`indexNode` from Hello.caps, `sentTotal`/`receivedTotal` this run,
/// `ed2kUserHash` derived), or the download source of that node; `source` {fileName, fileSize, root, state, otherFile,
/// parts, availParts} when it is one. nullopt: neither (any more) or no ed3k.
[[nodiscard]] std::optional<QCborMap> details(const QString& key);

/// GetDownloadSources rows of the ed3k sources of @p root (32 B) in every join: SourceRow
/// fields with `network: "ed3k"`, `userHash` = details() key, `sourcePartMap` per 4 MiB
/// piece. Empty: none, or no ed3k.
[[nodiscard]] QCborArray downloadSources(const QByteArray& root);
/// GetDownloadClients rows of the ed3k sources holding a slot for us (every join).
void appendDownloading(QCborArray& out);

/// The node-key row whose derived eD2K user hash (§14.2) is @p userHash (16 bytes), as
/// details() writes it: the same peer seen by eD2K too.
[[nodiscard]] std::optional<QCborMap> detailsByEd2kHash(const QByteArray& userHash);

/// Other names of ed3k @p root (32 B) known here: the eD2K, torrent, usenet and hoster jobs
/// sharing or downloading it (Ed3kJoinBase::aliasJobsAt), as {network, name, id[, host]}.
[[nodiscard]] QCborArray aliasesOf(const QByteArray& root);

/// File Details "Joins" of ed3k @p root (32 B): one row per network join, always all four
/// in order eD2K, BitTorrent, usenet, hoster (Ed3kJoinBase::joinStatesAt), as
/// {network: "ed2k"|"torrent"|"usenet"|"hoster", state: "joined"|"skipped"|"available"|"off",
/// reason ("anonKey", "notRunning", "noEd2kHash"), jobs: aliasesOf-style maps}. Empty: no ed3k.
[[nodiscard]] QCborArray joinsOf(const QByteArray& root);

/// GetDownloadClients: the BitTorrent peers, news servers and hosters that torrent, usenet
/// and hoster jobs joined with ed3k get bytes from now (Ed3kJoinBase::networkJobsAt); in single
/// transfer mode those of every job of that network fetching now, its window is hidden.
void appendNetworkDownloading(QCborArray& out);
/// GetUploads: the BitTorrent peers torrents joined with ed3k send bytes to now; in single
/// transfer mode (btSingleTransferOn) those of every torrent, the Torrents window is hidden.
void appendNetworkUploading(QCborArray& uploading);

/// downloadSources rows of the ed3k sources of job @p owner of the torrent/usenet/hoster
/// join @p tag (Ed3kJoinBase::jobSourcesAt), plus `fileName`: the job's file it serves.
[[nodiscard]] QCborArray jobSources(const QString& tag, const QString& owner);

/// Whether job @p owner of the torrent/usenet/hoster join @p tag is joined with ed3k
/// (Ed3kJoinBase::joinedAt); false while the join is not started or ed3k is off.
[[nodiscard]] bool jobJoined(const QString& tag, const QString& owner);

} // namespace Ed3kUploadsBridge
} // namespace eMule
