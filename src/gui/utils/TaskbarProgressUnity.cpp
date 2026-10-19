#include "pch.h"
/// @file TaskbarProgressUnity.cpp
/// @brief Linux backend: com.canonical.Unity.LauncherEntry progress over D-Bus.
///
/// Plain session-bus signal, so it does not care about X11 vs Wayland. Picked up by
/// KDE Plasma's task manager, Ubuntu Dock / Dash to Dock and Plank; the launcher
/// is matched by .desktop id. The protocol has no paused/error colour, only the
/// value -- "urgent" exists but flashes the button, which is too loud for an error.

#include "utils/TaskbarProgress.h"

#ifdef Q_OS_LINUX

#include "utils/FileAssociation.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QVariantMap>

namespace eMule {

namespace {

class LauncherEntryBackend final : public TaskbarProgressBackend {
public:
    void apply(const TaskbarProgressValue& value) override
    {
        auto bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            return;

        QDBusMessage signal = QDBusMessage::createSignal(
            QStringLiteral("/org/emule/emuleqt/LauncherEntry"),
            QStringLiteral("com.canonical.Unity.LauncherEntry"),
            QStringLiteral("Update"));
        const QVariantMap props{
            {QStringLiteral("progress"), value.permille / 1000.0},
            {QStringLiteral("progress-visible"), value.state != TaskbarState::None},
        };
        signal << QStringLiteral("application://%1.desktop")
                      .arg(QLatin1String(gui::FileAssociation::kDesktopId))
               << props;
        bus.send(signal);
    }
};

} // anonymous namespace

std::unique_ptr<TaskbarProgressBackend> TaskbarProgress::createPlatformBackend(QWidget*)
{
    return std::make_unique<LauncherEntryBackend>();
}

} // namespace eMule

#endif // Q_OS_LINUX
