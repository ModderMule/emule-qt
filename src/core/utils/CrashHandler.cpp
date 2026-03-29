/// @file CrashHandler.cpp
/// @brief Cross-platform crash dump handler implementation.

#include "utils/CrashHandler.h"

#include <QDir>


#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// File-scope globals — must be accessible from signal/exception context
// without any heap allocation or Qt calls.
static char s_crashDir[PATH_MAX] = {};
static char s_crashDirQString[PATH_MAX] = {};

// ---------------------------------------------------------------------------
// Async-signal-safe helpers (Unix only)
// ---------------------------------------------------------------------------

#ifndef Q_OS_WIN

#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>

namespace {

/// Write a decimal integer into buf (async-signal-safe). Returns chars written.
int intToStr(char* buf, int bufSize, long value)
{
    if (bufSize < 2) return 0;

    bool negative = value < 0;
    if (negative) value = -value;

    // Write digits in reverse
    char tmp[24];
    int len = 0;
    do {
        tmp[len++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value > 0 && len < 23);

    if (negative) tmp[len++] = '-';

    if (len >= bufSize) len = bufSize - 1;
    for (int i = 0; i < len; ++i)
        buf[i] = tmp[len - 1 - i];
    buf[len] = '\0';
    return len;
}

/// Format timestamp as YYYYMMDD-HHMMSS into buf (async-signal-safe).
int formatTimestamp(char* buf, int bufSize)
{
    if (bufSize < 16) return 0;

    time_t now = time(nullptr);
    struct tm local {};
    localtime_r(&now, &local);

    auto twoDigit = [](char* dst, int val) {
        dst[0] = static_cast<char>('0' + val / 10);
        dst[1] = static_cast<char>('0' + val % 10);
    };

    intToStr(buf, 5, local.tm_year + 1900);
    twoDigit(buf + 4, local.tm_mon + 1);
    twoDigit(buf + 6, local.tm_mday);
    buf[8] = '-';
    twoDigit(buf + 9, local.tm_hour);
    twoDigit(buf + 11, local.tm_min);
    twoDigit(buf + 13, local.tm_sec);
    buf[15] = '\0';
    return 15;
}

/// Write a C-string to fd (async-signal-safe).
void writeStr(int fd, const char* s)
{
    if (!s) return;
    auto len = strlen(s);
    while (len > 0) {
        auto written = ::write(fd, s, len);
        if (written <= 0) break;
        s += written;
        len -= static_cast<size_t>(written);
    }
}

const char* signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGBUS:  return "SIGBUS";
    case SIGFPE:  return "SIGFPE";
    default:      return "UNKNOWN";
    }
}

void crashSignalHandler(int sig)
{
    // Build crash file path: <crashDir>/eMuleQt_YYYYMMDD-HHMMSS.crash
    char path[PATH_MAX];
    size_t dirLen = strlen(s_crashDir);
    if (dirLen == 0 || dirLen >= PATH_MAX - 40) {
        _exit(128 + sig);
    }

    memcpy(path, s_crashDir, dirLen);
    const char prefix[] = "/eMuleQt_";
    memcpy(path + dirLen, prefix, sizeof(prefix) - 1);
    size_t pos = dirLen + sizeof(prefix) - 1;

    char ts[16];
    formatTimestamp(ts, sizeof(ts));
    memcpy(path + pos, ts, 15);
    pos += 15;

    const char suffix[] = ".crash";
    memcpy(path + pos, suffix, sizeof(suffix)); // includes null terminator
    pos += sizeof(suffix) - 1;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        _exit(128 + sig);
    }

    // Write header
    writeStr(fd, "eMule Qt Crash Report\n");
    writeStr(fd, "Signal: ");
    writeStr(fd, signalName(sig));
    writeStr(fd, " (");
    char sigNum[12];
    intToStr(sigNum, sizeof(sigNum), sig);
    writeStr(fd, sigNum);
    writeStr(fd, ")\n");

    writeStr(fd, "Time: ");
    writeStr(fd, ts);
    writeStr(fd, "\n\nStack trace:\n");

    // Capture stack trace
    void* frames[128];
    int count = backtrace(frames, 128);
    backtrace_symbols_fd(frames, count, fd);

    writeStr(fd, "\n--- End of crash report ---\n");
    close(fd);

    // Re-raise with default handler to get proper exit code
    struct sigaction sa {};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
    raise(sig);
}

} // anonymous namespace

#else // Q_OS_WIN

#include <qt_windows.h>

// Forward declarations for dbghelp types loaded dynamically
using MINIDUMP_TYPE = ULONG;
using MiniDumpWriteDumpFunc = BOOL(WINAPI*)(
    HANDLE hProcess, DWORD ProcessId, HANDLE hFile,
    MINIDUMP_TYPE DumpType,
    PVOID ExceptionParam, PVOID UserStreamParam, PVOID CallbackParam);

static LONG WINAPI crashExceptionFilter(EXCEPTION_POINTERS* exInfo)
{
    // Build crash file path
    char path[PATH_MAX];
    size_t dirLen = strlen(s_crashDir);
    if (dirLen == 0 || dirLen >= PATH_MAX - 40)
        return EXCEPTION_CONTINUE_SEARCH;

    SYSTEMTIME st;
    GetLocalTime(&st);

    wsprintfA(path, "%s\\eMuleQt_%04d%02d%02d-%02d%02d%02d.dmp",
              s_crashDir, st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return EXCEPTION_CONTINUE_SEARCH;

    HMODULE dbgHelp = LoadLibraryA("dbghelp.dll");
    if (dbgHelp) {
        auto writeDump = reinterpret_cast<MiniDumpWriteDumpFunc>(
            GetProcAddress(dbgHelp, "MiniDumpWriteDump"));
        if (writeDump) {
            // MINIDUMP_EXCEPTION_INFORMATION
            struct {
                DWORD  ThreadId;
                PEXCEPTION_POINTERS ExceptionPointers;
                BOOL   ClientPointers;
            } mei;
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = exInfo;
            mei.ClientPointers = FALSE;

            // MiniDumpNormal = 0x00000000
            writeDump(GetCurrentProcess(), GetCurrentProcessId(),
                      hFile, 0, &mei, nullptr, nullptr);
        }
        FreeLibrary(dbgHelp);
    }
    CloseHandle(hFile);

    return EXCEPTION_CONTINUE_SEARCH;
}

#endif // Q_OS_WIN

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace eMule {

void CrashHandler::install(const QString& crashDir)
{
    // Create the crashes directory if it doesn't exist
    QDir().mkpath(crashDir);

    // Store as C-string for async-signal-safe access
    const QByteArray dirBytes = crashDir.toLocal8Bit();
    if (dirBytes.size() < PATH_MAX) {
        memcpy(s_crashDir, dirBytes.constData(), static_cast<size_t>(dirBytes.size()));
        s_crashDir[dirBytes.size()] = '\0';
    }

    // Store for QString accessor
    const QByteArray dirUtf8 = crashDir.toUtf8();
    if (dirUtf8.size() < PATH_MAX) {
        memcpy(s_crashDirQString, dirUtf8.constData(), static_cast<size_t>(dirUtf8.size()));
        s_crashDirQString[dirUtf8.size()] = '\0';
    }

#ifndef Q_OS_WIN
    struct sigaction sa {};
    sa.sa_handler = crashSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND; // one-shot: restore default after first signal

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
#else
    SetUnhandledExceptionFilter(crashExceptionFilter);
#endif
}

QString CrashHandler::crashDir()
{
    return QString::fromUtf8(s_crashDirQString);
}

} // namespace eMule
