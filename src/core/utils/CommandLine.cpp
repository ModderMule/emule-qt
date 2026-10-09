/// @file CommandLine.cpp
/// @brief QCommandLineParser::process() without the message box that never closes.

#include "utils/CommandLine.h"

#include <QCommandLineParser>
#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>

#include <cstdlib>
#include <string>
#include <string_view>
#endif

namespace eMule {

#ifdef Q_OS_WIN
namespace {

constexpr ULONGLONG kAutoCloseMs = 10'000;

ULONGLONG s_boxDeadline = 0;
std::wstring s_okLabel;

// Same test as Qt's: a console or redirected output gets text, not a box
bool wouldShowMessageBox()
{
    if (GetConsoleWindow() || qEnvironmentVariableIsSet("QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES"))
        return false;
    STARTUPINFOW info{};
    info.cb = sizeof(info);
    GetStartupInfoW(&info);
    return !(info.dwFlags & STARTF_USESTDHANDLES);
}

BOOL CALLBACK findDialog(HWND hwnd, LPARAM out)
{
    wchar_t cls[16] = {};
    GetClassNameW(hwnd, cls, 16);
    if (std::wstring_view(cls) != L"#32770")
        return TRUE;
    *reinterpret_cast<HWND*>(out) = hwnd;
    return FALSE;
}

// Thread timer: runs inside MessageBoxW's own message loop
void CALLBACK onBoxTick(HWND, UINT, UINT_PTR, DWORD)
{
    HWND box = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), findDialog, reinterpret_cast<LPARAM>(&box));
    if (!box)
        return;

    const ULONGLONG now = GetTickCount64();
    if (now >= s_boxDeadline) {
        PostMessageW(box, WM_CLOSE, 0, 0);
        return;
    }
    // By class, not by id: the lone button of an MB_OK box is not IDOK
    const HWND button = FindWindowExW(box, nullptr, L"Button", nullptr);
    if (!button)
        return;
    if (s_okLabel.empty()) {
        // the system's own (localized) "OK"
        wchar_t text[64] = {};
        GetWindowTextW(button, text, 64);
        s_okLabel = text;
    }
    const ULONGLONG secs = (s_boxDeadline - now + 999) / 1000;
    SetWindowTextW(button, (s_okLabel + L" (" + std::to_wstring(secs) + L")").c_str());
}

void showTimedMessageBox(const QString& text, bool isError)
{
    const QString title = QCoreApplication::applicationName();
    s_okLabel.clear();
    s_boxDeadline = GetTickCount64() + kAutoCloseMs;
    const UINT_PTR timer = SetTimer(nullptr, 0, 200, onBoxTick);
    MessageBoxW(nullptr, reinterpret_cast<const wchar_t*>(text.utf16()),
                reinterpret_cast<const wchar_t*>(title.utf16()),
                MB_OK | MB_TOPMOST | MB_SETFOREGROUND | (isError ? MB_ICONERROR : MB_ICONINFORMATION));
    KillTimer(nullptr, timer);
}

} // namespace
#endif // Q_OS_WIN

void processCommandLine(QCommandLineParser& parser, const QCoreApplication& app)
{
#ifdef Q_OS_WIN
    if (wouldShowMessageBox()) {
        QString text;
        bool isError = false;
        if (!parser.parse(app.arguments())) {
            text = parser.errorText();
            isError = true;
        } else {
            const QStringList set = parser.optionNames();
            if (set.contains(QStringLiteral("v")) || set.contains(QStringLiteral("version")))
                text = QCoreApplication::applicationName() + QLatin1Char(' ')
                       + QCoreApplication::applicationVersion();
            else if (set.contains(QStringLiteral("h")) || set.contains(QStringLiteral("help"))
                     || set.contains(QStringLiteral("?")) || set.contains(QStringLiteral("help-all")))
                text = parser.helpText();
            else
                return;
        }
        showTimedMessageBox(text, isError);
        std::exit(isError ? EXIT_FAILURE : EXIT_SUCCESS);
    }
#endif
    parser.process(app);
}

} // namespace eMule
