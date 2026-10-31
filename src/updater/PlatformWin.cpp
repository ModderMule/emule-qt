#ifdef _WIN32

#include "Platform.h"
#include "Updater.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

namespace fs = std::filesystem;

namespace updater::platform {

namespace {

std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string errorText(DWORD code)
{
    wchar_t* buf = nullptr;
    const DWORD n = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                         | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::wstring w = n && buf ? std::wstring(buf, n) : std::wstring();
    if (buf)
        ::LocalFree(buf);
    while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r' || w.back() == L' '))
        w.pop_back();
    return narrow(w) + " (" + std::to_string(code) + ")";
}

// CommandLineToArgvW quoting
void appendQuoted(std::wstring& cmd, const std::wstring& arg)
{
    if (!cmd.empty())
        cmd += L' ';
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }
    cmd += L'"';
    for (auto it = arg.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            cmd.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            cmd.append(backslashes * 2 + 1, L'\\');
            cmd += *it;
        } else {
            cmd.append(backslashes, L'\\');
            cmd += *it;
        }
    }
    cmd += L'"';
}

std::wstring commandLine(const std::vector<std::string>& argv)
{
    std::wstring cmd;
    for (const auto& a : argv)
        appendQuoted(cmd, widen(a));
    return cmd;
}

bool hasSeparator(const std::string& s)
{
    return s.find_first_of("\\/:") != std::string::npos;
}

} // namespace

fs::path toPath(const std::string& utf8)
{
    return fs::path(widen(utf8));
}

std::string fromPath(const fs::path& p)
{
    return narrow(p.native());
}

bool waitForPids(const std::vector<std::int64_t>& pids, std::chrono::seconds timeout, Logger& log)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (const auto pid : pids) {
        log.line("waiting for pid " + std::to_string(pid));
        HANDLE h = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (!h) {
            const DWORD err = ::GetLastError();
            if (err != ERROR_INVALID_PARAMETER)
                log.line("cannot open pid " + std::to_string(pid) + ": " + errorText(err) + ", assuming gone");
            continue;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        const DWORD ms = left.count() > 0 ? static_cast<DWORD>(left.count()) : 0;
        const DWORD rc = ::WaitForSingleObject(h, ms);
        ::CloseHandle(h);
        if (rc == WAIT_TIMEOUT) {
            log.line("pid " + std::to_string(pid) + " still running");
            return false;
        }
    }
    return true;
}

int runProcess(const std::vector<std::string>& argv, Logger& log)
{
    if (argv.empty())
        return -1;
    std::wstring cmd = commandLine(argv);
    log.line("run: " + narrow(cmd));

    SECURITY_ATTRIBUTES sa{static_cast<DWORD>(sizeof(SECURITY_ATTRIBUTES)), nullptr, TRUE};
    HANDLE nul = ::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                               OPEN_EXISTING, 0, nullptr);
    HANDLE out = INVALID_HANDLE_VALUE;
    if (!log.file().empty())
        out = ::CreateFileW(log.file().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = static_cast<DWORD>(sizeof si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = out;
    si.hStdError = out;
    PROCESS_INFORMATION pi{};
    const std::wstring app = hasSeparator(argv.front()) ? widen(argv.front()) : std::wstring();
    const BOOL ok = ::CreateProcessW(app.empty() ? nullptr : app.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                     CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si, &pi);
    const DWORD err = ::GetLastError();
    if (nul != INVALID_HANDLE_VALUE)
        ::CloseHandle(nul);
    if (out != INVALID_HANDLE_VALUE)
        ::CloseHandle(out);
    if (!ok) {
        log.line("cannot start " + argv.front() + ": " + errorText(err));
        return -1;
    }
    ::CloseHandle(pi.hThread);
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    ::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hProcess);
    if (code != 0)
        log.line(argv.front() + " exited with " + std::to_string(code));
    return static_cast<int>(code);
}

bool launchDetached(const std::vector<std::string>& argv, std::string& error)
{
    if (argv.empty()) {
        error = "empty command";
        return false;
    }
    std::wstring cmd = commandLine(argv);
    const std::wstring app = hasSeparator(argv.front()) ? widen(argv.front()) : std::wstring();
    STARTUPINFOW si{};
    si.cb = static_cast<DWORD>(sizeof si);
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(app.empty() ? nullptr : app.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                          DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT,
                          nullptr, nullptr, &si, &pi)) {
        error = errorText(::GetLastError());
        return false;
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return true;
}

bool runInnoSetup(const fs::path& package, Logger& log, std::string& error)
{
    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    std::wstring params = L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-";
    if (!log.file().empty())
        appendQuoted(params, L"/LOG=" + (log.file().parent_path() / L"inno-setup.log").native());
    log.line("running installer " + fromPath(package) + " " + narrow(params));

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = static_cast<DWORD>(sizeof sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    const std::wstring file = package.native();
    sei.lpFile = file.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    const BOOL started = ::ShellExecuteExW(&sei);
    const DWORD err = ::GetLastError();
    bool ok = false;
    if (!started) {
        error = err == ERROR_CANCELLED ? std::string("the elevation prompt was cancelled")
                                       : "cannot start the installer: " + errorText(err);
    } else if (!sei.hProcess) {
        error = "installer started without a process handle";
    } else {
        ::WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD code = 1;
        ::GetExitCodeProcess(sei.hProcess, &code);
        ::CloseHandle(sei.hProcess);
        log.line("installer exited with " + std::to_string(code));
        ok = code == 0;
        if (!ok)
            error = "installer failed with exit code " + std::to_string(code);
    }
    if (SUCCEEDED(hr))
        ::CoUninitialize();
    return ok;
}

std::string systemTar()
{
    wchar_t dir[MAX_PATH];
    const UINT n = ::GetSystemDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return "tar.exe";
    return narrow(std::wstring(dir, n) + L"\\tar.exe");
}

bool makeTempDir(const fs::path& base, const std::string& prefix, fs::path& out)
{
    for (unsigned i = 0; i < 100; ++i) {
        const fs::path p = base / widen(prefix + std::to_string(::GetCurrentProcessId()) + "-"
                                        + std::to_string(::GetTickCount64()) + "-" + std::to_string(i));
        std::error_code ec;
        if (fs::create_directory(p, ec)) {
            out = p;
            return true;
        }
    }
    return false;
}

std::int64_t currentPid()
{
    return static_cast<std::int64_t>(::GetCurrentProcessId());
}

} // namespace updater::platform

#endif // _WIN32
