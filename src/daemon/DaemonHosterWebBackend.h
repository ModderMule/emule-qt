#pragma once

/// @file DaemonHosterWebBackend.h
/// @brief The web page's view of the hoster downloads, as the daemon provides it.
///
/// Every call goes through Ed3kHosterBridge::handleRequest, the IPC's one
/// implementation, so the GUI and the web page cannot drift apart.

#include "IpcProtocol.h"

#include "webserver/HosterWebBackend.h"

#include <QCborValue>

#include <optional>

namespace eMule {

class DaemonHosterWebBackend final : public HosterWebBackend {
public:
    DaemonHosterWebBackend() = default;

    [[nodiscard]] bool available() const override;
    [[nodiscard]] QCborArray items() const override;
    int addLinks(const QString& text, const QString& package) override;
    bool action(quint32 id, const QString& op) override;

private:
    /// The data of a successful Result (null when it has none), else nullopt.
    [[nodiscard]] static std::optional<QCborValue> ask(Ipc::IpcMsgType type, const QCborArray& fields);
};

} // namespace eMule
