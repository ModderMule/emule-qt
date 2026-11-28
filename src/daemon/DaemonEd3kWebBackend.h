#pragma once

/// @file DaemonEd3kWebBackend.h
/// @brief The REST API's view of the ed3k downloads, as the daemon provides it.
///
/// Every call goes through Ed3kDownloadsBridge, the IPC's one implementation,
/// so the GUI and the API cannot drift apart.

#include "webserver/Ed3kWebBackend.h"

namespace eMule {

class DaemonEd3kWebBackend final : public Ed3kWebBackend {
public:
    DaemonEd3kWebBackend() = default;

    [[nodiscard]] bool available() const override;
    [[nodiscard]] QCborArray downloads() const override;
    [[nodiscard]] QCborMap status() const override;
    AddResult add(const QString& hexRoot, qint64 size, const QString& name, int category, bool paused) override;
    AddResult addLink(const QString& link, int category, bool paused) override;
    bool control(const QString& hexRoot, Control action, qint64 arg) override;
    QString moveTo(const QString& hexRoot, const QString& dir) override;
    QString rename(const QString& hexRoot, const QString& name) override;
};

} // namespace eMule
