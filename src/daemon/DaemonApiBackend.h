#pragma once

/// @file DaemonApiBackend.h
/// @brief The daemon-side half of the REST API.

#include "webserver/ApiBackend.h"

namespace eMule {

class IpcServer;

class DaemonApiBackend final : public ApiBackend {
public:
    explicit DaemonApiBackend(IpcServer* ipcServer);

    [[nodiscard]] QList<LogRecord> logs(qint64 sinceId) const override;
    [[nodiscard]] QJsonArray categories() const override;
    ops::Status setCategories(const QJsonArray& categories) override;
    ops::Status applyPreferences(const PrefChanges& changes) override;
    void requestShutdown() override;

private:
    IpcServer* m_ipcServer;   ///< not owned; tells the GUIs what changed
};

} // namespace eMule
