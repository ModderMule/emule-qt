#include "pch.h"
/// @file TaskbarProgress.cpp
/// @brief Taskbar progress: state rules, caching, Dock icon painting.

#include "utils/TaskbarProgress.h"

#include <QPainter>
#include <QPen>
#include <QRectF>

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
    case TaskbarState::Normal: return QColor(0x35, 0x74, 0xf0);
    case TaskbarState::Paused: return QColor(0xf0, 0xa7, 0x32);
    case TaskbarState::Error:  return QColor(0xe5, 0x48, 0x4d);
    case TaskbarState::None:   break;
    }
    return {};
}

QRectF dockProgressTrackRect(int sizePx)
{
    const qreal size = std::max(16, sizePx);
    const qreal height = std::max(4.0, size * 0.12);
    const qreal side = size * 0.12;
    const qreal bottom = size * 0.08;
    return {side, size - bottom - height, size - 2 * side, height};
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
        // Pill along the bottom, like JetBrains IDEs while indexing
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF track = dockProgressTrackRect(size);
        const qreal outline = std::max(1.0, size / 128.0);
        const qreal radius = track.height() / 2;

        painter.setPen(QPen(QColor(0, 0, 0, 110), outline));
        painter.setBrush(QColor(0xe8, 0xe8, 0xe8));
        painter.drawRoundedRect(track.adjusted(outline / 2, outline / 2, -outline / 2, -outline / 2),
                                radius, radius);

        // Inset fill, never narrower than its height so it stays round
        const qreal pad = std::max(outline * 1.5, track.height() / 6);
        const QRectF inner = track.adjusted(pad, pad, -pad, -pad);
        const int permille = std::clamp(value.permille, 0, 1000);
        if (permille > 0) {
            const qreal width = std::max(inner.height(), inner.width() * permille / 1000.0);
            const qreal r = inner.height() / 2;
            painter.setPen(Qt::NoPen);
            painter.setBrush(dockProgressColor(value.state));
            painter.drawRoundedRect(QRectF(inner.left(), inner.top(), width, inner.height()), r, r);
        }
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
