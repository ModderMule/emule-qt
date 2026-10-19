#include "pch.h"
/// @file TaskbarProgressWin.cpp
/// @brief Windows backend: ITaskbarList3 progress on the main window's button.

#include "utils/TaskbarProgress.h"

#ifdef Q_OS_WIN

#include "utils/Log.h"

#include <QPointer>
#include <QWidget>

#include <qt_windows.h>
#include <shobjidl.h>

namespace eMule {

namespace {

UINT taskbarButtonCreatedMessage()
{
    static const UINT msg = ::RegisterWindowMessageW(L"TaskbarButtonCreated");
    return msg;
}

TBPFLAG toFlag(TaskbarState state)
{
    switch (state) {
    case TaskbarState::Normal: return TBPF_NORMAL;
    case TaskbarState::Paused: return TBPF_PAUSED;
    case TaskbarState::Error:  return TBPF_ERROR;
    case TaskbarState::None:   break;
    }
    return TBPF_NOPROGRESS;
}

class WindowsTaskbarBackend final : public TaskbarProgressBackend {
public:
    explicit WindowsTaskbarBackend(QWidget* window)
        : m_window(window)
    {
        taskbarButtonCreatedMessage();   // register before the shell sends it
    }

    ~WindowsTaskbarBackend() override { release(); }

    void apply(const TaskbarProgressValue& value) override
    {
        if (!m_window || !ensureList())
            return;
        const auto hwnd = reinterpret_cast<HWND>(m_window->winId());
        // Value first: SetProgressValue switches a NOPROGRESS button to NORMAL
        if (value.state != TaskbarState::None)
            m_list->SetProgressValue(hwnd, static_cast<ULONGLONG>(value.permille), 1000);
        m_list->SetProgressState(hwnd, toFlag(value.state));
    }

    void reinitialize() override
    {
        release();
        m_failed = false;
    }

private:
    bool ensureList()
    {
        if (m_list)
            return true;
        if (m_failed)
            return false;
        ITaskbarList3* list = nullptr;
        HRESULT hr = ::CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&list));
        if (SUCCEEDED(hr))
            hr = list->HrInit();
        if (FAILED(hr)) {
            if (list)
                list->Release();
            m_failed = true;   // until the next TaskbarButtonCreated
            logWarning(QStringLiteral("Taskbar progress unavailable (0x%1)")
                           .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
            return false;
        }
        m_list = list;
        return true;
    }

    void release()
    {
        if (m_list) {
            m_list->Release();
            m_list = nullptr;
        }
    }

    QPointer<QWidget> m_window;
    ITaskbarList3* m_list = nullptr;
    bool m_failed = false;
};

} // anonymous namespace

std::unique_ptr<TaskbarProgressBackend> TaskbarProgress::createPlatformBackend(QWidget* window)
{
    return std::make_unique<WindowsTaskbarBackend>(window);
}

bool TaskbarProgress::isTaskbarButtonCreated(const void* msg)
{
    return msg && static_cast<const MSG*>(msg)->message == taskbarButtonCreatedMessage();
}

} // namespace eMule

#endif // Q_OS_WIN
