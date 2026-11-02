#pragma once

/// @file Ed3kLinkActions.h
/// @brief Small ed3k link actions shared by ExternalLinkHandler and the Options pages
/// (batch 15): the words of an ed3k error, a node list file opened by hand.

#include <QString>

class QWidget;

namespace eMule {

namespace Ipc {
class IpcMessage;
}
class IpcClient;

namespace Ed3kLinkActions {

/// ED3K_ERR_* of ed3k.h in words (the GUI does not link packmule); 0: no ed3k node.
[[nodiscard]] QString errorText(int code);
/// The error of a [false, {error}] reply (or an Error message) in words.
[[nodiscard]] QString replyError(const Ipc::IpcMessage& reply);
/// A node list file opened by hand (bootstrap.md §3 "By hand": opening it is trusting it):
/// its bytes to the core (OpenEd3kNodeListFile), the outcome told in a box on @p parent.
void sendNodeListFile(QWidget* parent, IpcClient* ipc, const QString& path);
/// Whether @p path names an ed3k node list file (`.ed3knod`, bootstrap.md §2).
[[nodiscard]] bool isNodeListPath(const QString& path);

} // namespace Ed3kLinkActions
} // namespace eMule
