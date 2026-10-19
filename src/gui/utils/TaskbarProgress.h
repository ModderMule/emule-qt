#pragma once

/// @file TaskbarProgress.h
/// @brief Overall download progress on the taskbar button / dock / launcher icon.
///
/// Port of MFC CemuleDlg::UpdateStatusBarProgress (srchybrid/EmuleDlg.cpp:3688):
/// completed bytes over the size of every file in the download queue, coloured by
/// state. One backend per platform:
/// - Windows: ITaskbarList3 on the main window's taskbar button
/// - Linux:   com.canonical.Unity.LauncherEntry over D-Bus (KDE Plasma, Ubuntu Dock,
///            Dash to Dock, Plank) -- works on X11 and Wayland alike
/// - macOS:   progress bar painted onto the Dock icon
///
/// ToDo: MFC's optional up/down overlay icon (IsShowUpDownIconInTaskbar)

#include <QIcon>
#include <QPixmap>
#include <QRectF>
#include <QtGlobal>

#include <cstdint>
#include <memory>
#include <optional>

class QWidget;

namespace eMule {

enum class TaskbarState : std::uint8_t {
    None,       ///< no downloads: hidden
    Normal,     ///< something is transferring
    Paused,     ///< downloads exist, none transferring
    Error       ///< any file in error
};

struct TaskbarProgressValue {
    TaskbarState state = TaskbarState::None;
    int permille = 0;       ///< 0..1000

    bool operator==(const TaskbarProgressValue&) const = default;
};

/// MFC state rules: no size -> None, error beats transferring, idle -> Paused.
/// Rounds down, so a bar is never full before the last byte.
[[nodiscard]] TaskbarProgressValue computeTaskbarProgress(qint64 totalSize, qint64 totalDone,
                                                          qint64 transferring, qint64 errors);

/// Bar colour of the painted Dock progress for @p state.
[[nodiscard]] QColor dockProgressColor(TaskbarState state);

/// Outer rect of the rounded Dock progress track on a @p sizePx icon.
[[nodiscard]] QRectF dockProgressTrackRect(int sizePx);

/// @p base at @p sizePx with a progress bar along its bottom. None returns the
/// plain icon. Pure painting, so it is tested on every platform.
[[nodiscard]] QPixmap renderDockProgressIcon(const QIcon& base, const TaskbarProgressValue& value,
                                             int sizePx = 256);

/// The native side. apply(None) hides the progress.
class TaskbarProgressBackend {
public:
    virtual ~TaskbarProgressBackend() = default;
    virtual void apply(const TaskbarProgressValue& value) = 0;
    /// Native handle went stale (Windows: Explorer restarted); drop it.
    virtual void reinitialize() {}
};

/// Caches what is shown, so an unchanged value costs no native call: Linux pays a
/// D-Bus signal per update and macOS a Dock icon repaint.
class TaskbarProgress {
public:
    explicit TaskbarProgress(std::unique_ptr<TaskbarProgressBackend> backend);
    ~TaskbarProgress();

    TaskbarProgress(const TaskbarProgress&) = delete;
    TaskbarProgress& operator=(const TaskbarProgress&) = delete;

    /// Backend for this platform, nullptr where there is none. Defined in the
    /// platform's TaskbarProgress<Platform>.cpp.
    [[nodiscard]] static std::unique_ptr<TaskbarProgressBackend> createPlatformBackend(QWidget* window);

    void setEnabled(bool on);
    [[nodiscard]] bool isEnabled() const { return m_enabled; }

    void update(const TaskbarProgressValue& value);
    void clear() { update({}); }

    /// Forget the native state and show the current value again.
    void reinitialize();

#ifdef Q_OS_WIN
    /// The "TaskbarButtonCreated" message: sent on first show, after Explorer
    /// restarts and when the window comes back from the tray. @p msg is a MSG*.
    [[nodiscard]] static bool isTaskbarButtonCreated(const void* msg);
#endif

private:
    void push();

    std::unique_ptr<TaskbarProgressBackend> m_backend;
    bool m_enabled = true;
    TaskbarProgressValue m_wanted;
    std::optional<TaskbarProgressValue> m_shown = TaskbarProgressValue{};   ///< nullopt = unknown
};

} // namespace eMule
