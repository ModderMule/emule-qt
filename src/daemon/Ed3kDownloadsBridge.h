#pragma once

/// @file Ed3kDownloadsBridge.h
/// @brief Plain ed3k downloads over IPC (requests 1140-1143, push 1180), for Ed3kBridge.
/// Free of the ed3k module like Ed3kBridge.h: without it GetEd3kDownloads answers an empty
/// list and every other request [false, {error}].
/// A row's bar: `blocks` (held, encodeBlockRuns of BlockRuns.h) of `blockSize` bytes,
/// `pending` (asked for; only while running), `avail` (a byte per `pieceSize` piece: sources
/// holding it; only when one told its bitmap).

#include "IpcMessage.h"

#include <QCborMap>

#include <optional>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kDownloads;
} // namespace ed3k

struct UsenetStreamSource;

namespace Ed3kDownloadsBridge {

/// Set by DaemonApp, null when absent.
void setDownloads(ed3k::Ed3kDownloads* downloads);

/// Whether an opcode is one of 1140-1149.
[[nodiscard]] bool isDownloadsRequest(Ipc::IpcMsgType type);

/// Answers a downloads request through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// An ed3k download of @p root (32 B): `inProgress` set true unless it is complete, `folder`
/// to where its file is. false: none (or no node).
[[nodiscard]] bool downloadOf(const QByteArray& root, bool& inProgress, QString& folder);

/// The file of an ed3k download named by @p hexRoot (64 hex): the part file while unfinished,
/// else the final one. false: none, failed, or no node. Backs preview and FlushDownload.
[[nodiscard]] bool fileOf(const QString& hexRoot, QString& path, QString& name);

/// Preview with seeking on: the download of @p hexRoot read by a player from @p offset —
/// its file, size and the end of the bytes held from there (Ed3kDownloads::playFrom, which
/// fetches the blocks there first). `found` false: none, failed, or no node.
[[nodiscard]] UsenetStreamSource streamFrom(const QString& hexRoot, qint64 offset);

/// "Move to" (#187) of the download of @p hexRoot. Empty: started; else why not.
[[nodiscard]] QString moveTo(const QString& hexRoot, const QString& dir);

/// The HTTP (ws=) source row of the download of @p root (32 B), as an eD2K URL source
/// (isUrl); @p peerRate is what its ed3k sources bring, the rest of the rate is the HTTP
/// source's. Empty: none attached.
[[nodiscard]] std::optional<QCborMap> webSourceRow(const QByteArray& root, qint64 peerRate);

/// GetDownloadDetails of the ed3k download @p hexRoot: the General page's fields as eD2K
/// sends them (FileDetailDialog), network "ed3k", plus ed3kKey and ed3kAliases. Empty: none.
[[nodiscard]] std::optional<QCborMap> detailsOf(const QString& hexRoot);

/// GetEd3kDownloads, also what PushEd3kDownloads carries.
[[nodiscard]] QCborMap listToCbor();

/// Broadcasts PushEd3kDownloads on every change of @p downloads for as long as it lives.
void connectPushes(ed3k::Ed3kDownloads& downloads, std::function<void(const Ipc::IpcMessage&)> broadcast);

} // namespace Ed3kDownloadsBridge
} // namespace eMule
