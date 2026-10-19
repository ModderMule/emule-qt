#include "pch.h"
/// @file TaskbarProgress.cpp
/// @brief Taskbar progress: state rules, caching, Dock icon painting.

#include "utils/TaskbarProgress.h"

#include <QPainter>
#include <QRect>

#include <algorithm>

namespace eMule {

TaskbarProgressValue computeTaskbarProgress(qint64 totalSize, qint64 totalDone,
                                            qint64 transferring, qint64 errors)
{
    if (totalSize <= 0)
        return {};

    // srchybrid/EmuleDlg.cpp:3700-3706
    TaskbarProgressValue value;
    if (errors > 0)
        value.state = TaskbarState::Error;
    else if (transferring > 0)
        value.state = TaskbarState::Normal;
    else
        value.state = TaskbarState::Paused;

    const qint64 done = std::clamp<qint64>(totalDone, 0, totalSize);
    value.permille = static_cast<int>(done * 1000 / totalSize);
    return value;
}

QColor dockProgressColor(TaskbarState state)
{
    switch (state) {
    case TaskbarState::Normal: return QColor(0x2f, 0x7f, 0xf0);
    case TaskbarState::Paused: return QColor(0xf0, 0xb4, 0x00);
    case TaskbarState::Error:  return QColor(0xe0, 0x30, 0x30);
    case TaskbarState::None:   break;
    }
    return {};
}

QPixmap renderDockProgressIcon(const QIcon& base, const TaskbarProgressValue& value, int sizePx)
{
    const int size = std::max(16, sizePx);

    QPixmap canvas(size, size);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QPixmap basePixmap = base.pixmap(QSize(size, size), 1.0);
    if (!basePixmap.isNull()) {
        basePixmap.setDevicePixelRatio(1.0);
        if (basePixmap.size() != QSize(size, size))
            basePixmap = basePixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawPixmap((size - basePixmap.width()) / 2,
                           (size - basePixmap.height()) / 2, basePixmap);
    }

    if (value.state != TaskbarState::None) {
        // Bar along the bottom, like Finder's copy progress on the Dock
        const int margin = size / 10;
        const int border = std::max(1, size / 64);
        const QRect outer(margin, size - margin - size / 8, size - 2 * margin, size / 8);
        painter.fillRect(outer, QColor(0xf0, 0xf0, 0xf0));
        const QRect inner = outer.adjusted(border, border, -border, -border);
        painter.fillRect(inner, QColor(0x40, 0x40, 0x40));
        const int filled = inner.width() * std::clamp(value.permille, 0, 1000) / 1000;
        if (filled > 0)
            painter.fillRect(QRect(inner.left(), inner.top(), filled, inner.height()),
                             dockProgressColor(value.state));
    }
    painter.end();
    return canvas;
}

TaskbarProgress::TaskbarProgress(std::unique_ptr<TaskbarProgressBackend> backend)
    : m_backend(std::move(backend))
{
}

TaskbarProgress::~TaskbarProgress()
{
    // Don't leave a stale bar behind on a launcher that outlives us
    m_wanted = {};
    push();
}

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS) && !defined(Q_OS_LINUX)
std::unique_ptr<TaskbarProgressBackend> TaskbarProgress::createPlatformBackend(QWidget*)
{
    return nullptr;
}
#endif

void TaskbarProgress::setEnabled(bool on)
{
    m_enabled = on;
    push();
}

void TaskbarProgress::update(const TaskbarProgressValue& value)
{
    m_wanted = value;
    push();
}

void TaskbarProgress::reinitialize()
{
    if (m_backend)
        m_backend->reinitialize();
    m_shown.reset();
    push();
}

void TaskbarProgress::push()
{
    const TaskbarProgressValue effective = m_enabled ? m_wanted : TaskbarProgressValue{};
    if (!m_backend || m_shown == effective)
        return;
    m_backend->apply(effective);
    m_shown = effective;
}

} // namespace eMule
