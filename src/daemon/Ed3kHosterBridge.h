#pragma once

/// @file Ed3kHosterBridge.h
/// @brief Hoster links over IPC (1100-1109, pushes 1150-1159, hoster.md), the Link Grabber
/// (1210-1218, push 1190) and the "hoster"
/// map of GetEd3kStatus, for Ed3kBridge. Free of the ed3k module like Ed3kBridge.h: without
/// it every request answers [false, {error}] and the status has no "hoster".

#include "IpcMessage.h"
#include "SingleTransfer.h"

#include <QCborArray>
#include <QCborMap>

#include <QStringList>

#include <functional>
#include <optional>

namespace eMule {

namespace ed3k {
class Ed3kHosterJoin;
class Ed3kResolver;
class Ed3kJavaRuntime;
class Ed3kLinkGrabber;
} // namespace ed3k

namespace Ed3kHosterBridge {

/// Set by DaemonApp, null when absent. @p java is null without EMULE_HAVE_HOSTER_JD.
void setHoster(ed3k::Ed3kHosterJoin* join, ed3k::Ed3kResolver* resolver, ed3k::Ed3kJavaRuntime* java);

/// The Link Grabber, null when absent. With it on, folder links go there; with takesAll(),
/// Click'n'Load and container links too (connectPushes routes them).
void setGrabber(ed3k::Ed3kLinkGrabber* grabber);

/// Opens a container for an in-daemon caller (the web page) and adds all its links, as
/// the GUI's Open DLC with every link ticked. No push, so no GUI asks. @p done runs once,
/// mostly later: links added, else 0 and why.
void openContainerAndAdd(const QByteArray& data, const QString& fileName,
                         std::function<void(int added, const QString& error)> done);

/// "Move to" (#187) of hoster item @p id (decimal). Empty: started; else why not.
[[nodiscard]] QString moveTo(const QString& id, const QString& dir);

[[nodiscard]] bool isHosterRequest(Ipc::IpcMsgType type);
/// Whether it carries a password: local IPC clients only.
[[nodiscard]] bool carriesSecret(Ipc::IpcMsgType type);

/// Answers a hoster request through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// The join's signals as pushes; item changes coalesced per item.
void connectPushes(ed3k::Ed3kHosterJoin& join, const std::function<void(const Ipc::IpcMessage&)>& broadcast);

/// The grabber's changes as PushLinkGrabber, its ask results as PushHosterLinks.
void connectGrabberPushes(ed3k::Ed3kLinkGrabber& grabber, const std::function<void(const Ipc::IpcMessage&)>& broadcast);

/// Hoster item of join owner @p owner ("h<id>") as another name of an ed3k root (Transfers
/// tooltip, Details): {network: "hoster", name, id: its link, host}; empty: none.
[[nodiscard]] QCborMap aliasOf(const QString& owner);

/// The hoster item of @p owner downloading now, as a GetDownloadClients row (network "hoster").
void appendDownloading(QCborArray& out, const QString& owner);

/// Hoster item of join owner @p owner as a Transfers row in single transfer mode; nullopt: none.
[[nodiscard]] std::optional<SingleTransfer::Job> transferJob(const QString& owner);
/// SingleTransfer::control on the item of @p owner. False: not done.
bool transferControl(const QString& owner, int action, qint64 value);
/// Join owners ("h<id>") of every hoster item: all of them are ed3k joins.
[[nodiscard]] QStringList owners();

/// The "hoster" map of GetEd3kStatus: {compiled, enabled, java: {state, path, version, error,
/// progress}, resolver: {available, reason, description}, clipboard, linkGrabber, grabAll,
/// deepCrawl}; empty without a join.
[[nodiscard]] QCborMap statusToCbor();

} // namespace Ed3kHosterBridge
} // namespace eMule
