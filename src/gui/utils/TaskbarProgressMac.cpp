#include "pch.h"
/// @file TaskbarProgressMac.cpp
/// @brief macOS backend: progress bar painted onto the Dock icon.
///
/// No Objective-C needed: the cocoa platform plugin hands
/// QGuiApplication::setWindowIcon() to NSApp.applicationIconImage, which is what
/// the Dock shows.

#include "utils/TaskbarProgress.h"

#ifdef Q_OS_MACOS

#include <QGuiApplication>

namespace eMule {

namespace {

class DockProgressBackend final : public TaskbarProgressBackend {
public:
    DockProgressBackend()
        : m_base(QGuiApplication::windowIcon())
    {
    }

    void apply(const TaskbarProgressValue& value) override
    {
        if (value.state == TaskbarState::None) {
            if (m_lastKey != kPlain)
                QGuiApplication::setWindowIcon(m_base);
            m_lastKey = kPlain;
            return;
        }
        // One repaint per visible step, not per permille
        const int key = static_cast<int>(value.state) * 1000
                        + value.permille * kIconSize / 1000;
        if (key == m_lastKey)
            return;
        m_lastKey = key;
        QGuiApplication::setWindowIcon(QIcon(renderDockProgressIcon(m_base, value, kIconSize)));
    }

private:
    static constexpr int kIconSize = 256;
    static constexpr int kPlain = -1;

    QIcon m_base;
    int m_lastKey = kPlain;
};

} // anonymous namespace

std::unique_ptr<TaskbarProgressBackend> TaskbarProgress::createPlatformBackend(QWidget*)
{
    return std::make_unique<DockProgressBackend>();
}

} // namespace eMule

#endif // Q_OS_MACOS
