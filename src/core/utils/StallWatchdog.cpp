#include "pch.h"
/// @file StallWatchdog.cpp
/// @brief Notices a main thread that stops running its event loop, and says where it is.

#include "utils/StallWatchdog.h"

#include "app/AppConfig.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#ifndef Q_OS_WIN
#include <csignal>
#include <pthread.h>
#include <unistd.h>
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define EMULE_STALL_HAVE_BACKTRACE 1
#endif
#endif
#ifdef Q_OS_LINUX
#include <sys/syscall.h>
#include <sys/uio.h>
#endif
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dbghelp.h>   // types only: dbghelp.dll is loaded at runtime, as in CrashHandler
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__)
#define EMULE_STALL_HAVE_WIN_UNWIND 1
#endif
#endif

namespace eMule {

namespace {

#ifdef EMULE_STALL_HAVE_BACKTRACE
// Filled by the signal handler on the stalled thread; read by the monitor
constexpr int kMaxFrames = 128;
void* s_frames[kMaxFrames];
std::atomic<int> s_frameCount{0};
std::atomic<bool> s_captured{false};

constexpr int kStackSignal = SIGUSR2;

void captureStackHandler(int)
{
    // backtrace() is not on the async-signal-safe list, but safe once warmed up
    // (its first call loads libgcc_s): done in the constructor
    s_frameCount.store(::backtrace(s_frames, kMaxFrames));
    s_captured.store(true);
}

/// The frames the handler caught, symbolized
QString capturedStackText()
{
    const int count = s_frameCount.load();
    QString out;
    if (char** symbols = ::backtrace_symbols(s_frames, count)) {
        // frames 0-1 are the handler and the signal trampoline
        for (int i = 0; i < count; ++i)
            out += QStringLiteral("#%1 %2\n").arg(i).arg(QString::fromLocal8Bit(symbols[i]));
        ::free(symbols);
    }
    return out;
}
#endif

#ifdef Q_OS_LINUX
/// A system call the main thread may wait in; which argument names a file
struct SyscallInfo {
    long nr;
    const char* name;
    int fdArg;     ///< -1: none
    int pathArg;   ///< -1: none; *at calls: the dir fd is the argument before
};

// Only what the core thread does with files; the rest prints as a number
constexpr SyscallInfo kSyscalls[] = {
    {SYS_read, "read", 0, -1},
    {SYS_write, "write", 0, -1},
    {SYS_pread64, "pread64", 0, -1},
    {SYS_pwrite64, "pwrite64", 0, -1},
    {SYS_readv, "readv", 0, -1},
    {SYS_writev, "writev", 0, -1},
    {SYS_fsync, "fsync", 0, -1},
    {SYS_fdatasync, "fdatasync", 0, -1},
    {SYS_ftruncate, "ftruncate", 0, -1},
    {SYS_fallocate, "fallocate", 0, -1},
    {SYS_close, "close", 0, -1},
    {SYS_fstat, "fstat", 0, -1},
    {SYS_getdents64, "getdents64", 0, -1},
    {SYS_sync_file_range, "sync_file_range", 0, -1},
    {SYS_openat, "openat", -1, 1},
    {SYS_newfstatat, "newfstatat", -1, 1},
    {SYS_statx, "statx", -1, 1},
    {SYS_renameat, "renameat", -1, 1},
    {SYS_renameat2, "renameat2", -1, 1},
    {SYS_unlinkat, "unlinkat", -1, 1},
    {SYS_mkdirat, "mkdirat", -1, 1},
    {SYS_statfs, "statfs", -1, 0},
#ifdef SYS_open
    {SYS_open, "open", -1, 0},
    {SYS_stat, "stat", -1, 0},
    {SYS_rename, "rename", -1, 0},
    {SYS_unlink, "unlink", -1, 0},
    {SYS_mkdir, "mkdir", -1, 0},
#endif
};

/// What /proc/self/fd/<fd> points at
QString fdTarget(long fd)
{
    char buf[1024];
    const QByteArray link = QByteArray("/proc/self/fd/") + QByteArray::number(fd);
    const ssize_t n = ::readlink(link.constData(), buf, sizeof(buf));
    return n > 0 ? QString::fromLocal8Bit(buf, static_cast<qsizetype>(n)) : QStringLiteral("?");
}

/// A C string in this process; a bad pointer reads as "?" instead of crashing
QString readOwnString(unsigned long long addr)
{
    if (addr == 0)
        return QStringLiteral("?");
    char buf[1024];
    std::size_t got = 0;
    while (got < sizeof(buf)) {
        // up to the page end: the next page may be unmapped
        const std::size_t room = 4096 - ((addr + got) % 4096);
        iovec local{buf + got, std::min(room, sizeof(buf) - got)};
        iovec remote{reinterpret_cast<void*>(addr + got), local.iov_len};
        const ssize_t n = ::process_vm_readv(::getpid(), &local, 1, &remote, 1, 0);
        if (n <= 0)
            break;
        if (std::memchr(buf + got, 0, static_cast<std::size_t>(n)))
            return QString::fromLocal8Bit(buf);
        got += static_cast<std::size_t>(n);
    }
    return got > 0 ? QString::fromLocal8Bit(buf, static_cast<qsizetype>(got)) + QStringLiteral("…")
                   : QStringLiteral("?");
}
#endif

#ifdef EMULE_STALL_HAVE_WIN_UNWIND
constexpr int kMaxFrames = 128;

/// Suspends the thread, unwinds its stack into frames, resumes it. -1 = could not suspend.
/// Nothing here may allocate or lock: the suspended thread may hold the heap lock.
int unwindSuspended(HANDLE thread, DWORD64* frames, int maxFrames)
{
    if (::SuspendThread(thread) == static_cast<DWORD>(-1))
        return -1;
    int count = 0;
    alignas(16) CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_FULL;
    if (::GetThreadContext(thread, &ctx)) {   // also waits until the suspend took effect
        while (count < maxFrames) {
#if defined(_M_X64) || defined(__x86_64__)
            const DWORD64 pc = ctx.Rip;
            const DWORD64 sp = ctx.Rsp;
#else
            const DWORD64 pc = ctx.Pc;
            const DWORD64 sp = ctx.Sp;
#endif
            if (pc == 0)
                break;
            frames[count++] = pc;
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION fn = ::RtlLookupFunctionEntry(pc, &imageBase, nullptr);
            if (!fn) {
                if (count > 1)
                    break;   // no unwind info below the top: unknown code
                // top frame is a leaf function: return address is at sp (x64) / in lr (ARM64)
#if defined(_M_X64) || defined(__x86_64__)
                ctx.Rip = *reinterpret_cast<const DWORD64*>(ctx.Rsp);
                ctx.Rsp += 8;
#else
                ctx.Pc = ctx.Lr;
#endif
            } else {
                PVOID handlerData = nullptr;
                DWORD64 establisherFrame = 0;
                ::RtlVirtualUnwind(0 /* UNW_FLAG_NHANDLER */, imageBase, pc, fn, &ctx,
                                   &handlerData, &establisherFrame, nullptr);
            }
#if defined(_M_X64) || defined(__x86_64__)
            if (ctx.Rip == pc && ctx.Rsp == sp)
#else
            if (ctx.Pc == pc && ctx.Sp == sp)
#endif
                break;   // no progress
        }
    }
    ::ResumeThread(thread);
    return count;
}

/// "module!symbol+0x12 (file:line)", or "module+0x1234" without symbols
QString symbolizeFrames(const DWORD64* frames, int count)
{
    using SymSetOptionsFn = DWORD(WINAPI*)(DWORD);
    using SymInitializeWFn = BOOL(WINAPI*)(HANDLE, PCWSTR, BOOL);
    using SymFromAddrWFn = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFOW);
    using SymGetLineFromAddrW64Fn = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINEW64);
    using SymCleanupFn = BOOL(WINAPI*)(HANDLE);

    HMODULE dbgHelp = ::LoadLibraryW(L"dbghelp.dll");
    SymFromAddrWFn fromAddr = nullptr;
    SymGetLineFromAddrW64Fn lineFromAddr = nullptr;
    SymCleanupFn cleanup = nullptr;
    const HANDLE process = ::GetCurrentProcess();
    bool symbols = false;
    if (dbgHelp) {
        auto setOptions = reinterpret_cast<SymSetOptionsFn>(::GetProcAddress(dbgHelp, "SymSetOptions"));
        auto initialize = reinterpret_cast<SymInitializeWFn>(::GetProcAddress(dbgHelp, "SymInitializeW"));
        fromAddr = reinterpret_cast<SymFromAddrWFn>(::GetProcAddress(dbgHelp, "SymFromAddrW"));
        lineFromAddr = reinterpret_cast<SymGetLineFromAddrW64Fn>(
            ::GetProcAddress(dbgHelp, "SymGetLineFromAddrW64"));
        cleanup = reinterpret_cast<SymCleanupFn>(::GetProcAddress(dbgHelp, "SymCleanup"));
        if (setOptions && initialize && fromAddr && cleanup) {
            setOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
            symbols = initialize(process, nullptr, TRUE) != FALSE;
        }
    }

    QString out;
    for (int i = 0; i < count; ++i) {
        // a return address points past the call: look up the call itself
        const DWORD64 addr = i == 0 ? frames[i] : frames[i] - 1;
        QString module = QStringLiteral("?");
        DWORD64 moduleBase = 0;
        HMODULE mod = nullptr;
        if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                     | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 reinterpret_cast<LPCWSTR>(addr), &mod)) {
            wchar_t name[MAX_PATH];
            const DWORD len = ::GetModuleFileNameW(mod, name, MAX_PATH);
            module = QString::fromWCharArray(name, static_cast<qsizetype>(len));
            module = module.mid(module.lastIndexOf(u'\\') + 1);
            moduleBase = reinterpret_cast<DWORD64>(mod);
        }
        QString line = QStringLiteral("#%1 %2").arg(i).arg(module);

        alignas(SYMBOL_INFOW) char buf[sizeof(SYMBOL_INFOW) + 256 * sizeof(wchar_t)]{};
        auto* sym = reinterpret_cast<PSYMBOL_INFOW>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFOW);
        sym->MaxNameLen = 256;
        DWORD64 symOffset = 0;
        if (symbols && fromAddr(process, addr, &symOffset, sym)) {
            line += QStringLiteral("!%1+0x%2")
                        .arg(QString::fromWCharArray(sym->Name, static_cast<qsizetype>(sym->NameLen)))
                        .arg(symOffset, 0, 16);
            IMAGEHLP_LINEW64 src{};
            src.SizeOfStruct = sizeof(src);
            DWORD lineOffset = 0;
            if (lineFromAddr && lineFromAddr(process, addr, &lineOffset, &src) && src.FileName)
                line += QStringLiteral(" (%1:%2)").arg(QString::fromWCharArray(src.FileName)).arg(src.LineNumber);
        } else if (moduleBase != 0) {
            line += QStringLiteral("+0x%1").arg(addr - moduleBase, 0, 16);
        } else {
            line += QStringLiteral(" 0x%1").arg(addr, 0, 16);
        }
        out += line + u'\n';
    }
    if (symbols)
        cleanup(process);
    if (dbgHelp)
        ::FreeLibrary(dbgHelp);
    return out;
}
#endif

} // namespace

StallWatchdog::StallWatchdog(QString crashDir, std::chrono::milliseconds limit)
    : m_crashDir(std::move(crashDir))
    , m_limit(limit)
{
    m_beat->lastNs.store(nowNs());
#ifdef Q_OS_WIN
    m_mainThread = ::OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                FALSE, ::GetCurrentThreadId());
#else
    m_mainThread = ::pthread_self();
#ifdef Q_OS_LINUX
    m_mainTid = static_cast<long>(::syscall(SYS_gettid));
#endif
#endif
#ifdef EMULE_STALL_HAVE_BACKTRACE
    void* warm[2];
    static_cast<void>(::backtrace(warm, 2));
    struct sigaction sa {};
    sa.sa_handler = captureStackHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;   // the stalled call carries on after the capture
    ::sigaction(kStackSignal, &sa, nullptr);
#endif
    m_thread = std::thread([this] { watch(); });
}

StallWatchdog::~StallWatchdog()
{
    {
        const std::scoped_lock lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_one();
    m_thread.join();
#ifdef Q_OS_WIN
    if (m_mainThread)
        ::CloseHandle(m_mainThread);
#endif
}

QString StallWatchdog::lastReportPath() const
{
    const std::scoped_lock lock(m_mutex);
    return m_lastReport;
}

int64_t StallWatchdog::nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void StallWatchdog::watch()
{
    const auto tick = std::min<std::chrono::milliseconds>(std::chrono::seconds(1), m_limit / 4);
    int64_t stallFromNs = 0;   // != 0: a stall was reported, waiting for the loop to run
    std::unique_lock lock(m_mutex);
    while (!m_cv.wait_for(lock, tick, [this] { return m_stop; })) {
        lock.unlock();
        const int64_t last = m_beat->lastNs.load();
        if (stallFromNs != 0 && last > stallFromNs) {
            const auto ms = (last - stallFromNs) / 1'000'000;
            logWarning(QStringLiteral("Core main thread ran again after a %1 ms stall").arg(ms));
            stallFromNs = 0;
            if (m_lateStackPending)
                reportLateStack(true);
        }
        if (m_lateStackPending)
            reportLateStack(false);
        if (!m_beat->pending.load())
            postBeat();
        const auto stalled = std::chrono::milliseconds((nowNs() - m_beat->lastNs.load()) / 1'000'000);
        if (stallFromNs == 0 && stalled >= m_limit) {
            stallFromNs = m_beat->lastNs.load();
            report(stalled);
        }
        lock.lock();
    }
}

void StallWatchdog::postBeat()
{
    QCoreApplication* app = QCoreApplication::instance();
    if (!app)
        return;
    m_beat->pending.store(true);
    QMetaObject::invokeMethod(app, [beat = m_beat] {
        beat->lastNs.store(nowNs());
        beat->pending.store(false);
    }, Qt::QueuedConnection);
}

void StallWatchdog::report(std::chrono::milliseconds stalledFor)
{
    m_stalls.fetch_add(1);

    QString text;
    text += QStringLiteral("eMule Qt core v%1: main thread stalled for %2 ms at %3\n")
                .arg(kAppVersion)
                .arg(stalledFor.count())
                .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    if (const QString kernel = mainThreadKernelState(); !kernel.isEmpty())
        text += kernel + u'\n';
    if (const QString call = mainThreadSyscall(); !call.isEmpty())
        text += call + u'\n';
    text += QStringLiteral("\nMain thread stack:\n") + mainThreadStack();
#ifdef EMULE_STALL_HAVE_BACKTRACE
    m_lateStackPending = !s_captured.load();
#endif

    QString path;
    if (!m_crashDir.isEmpty() && QDir().mkpath(m_crashDir)) {
        path = QDir(m_crashDir).filePath(
            QStringLiteral("stall-%1.stall")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(text.toUtf8());
        else
            path.clear();
        pruneReports();
    }
    {
        const std::scoped_lock lock(m_mutex);
        m_lastReport = path;
    }

    // Reaches the GUI once the loop runs again; the file is there even if never
    logWarning(QStringLiteral("Core main thread stalled for %1 ms%2")
                   .arg(stalledFor.count())
                   .arg(path.isEmpty() ? QString() : QStringLiteral(", stack in ") + path));
    for (const QString& line : text.split(u'\n', Qt::SkipEmptyParts).mid(1, 40))
        logWarning(QStringLiteral("  stall: ") + line);
}

void StallWatchdog::reportLateStack(bool loopRan)
{
#ifdef EMULE_STALL_HAVE_BACKTRACE
    // the signal stays pending while the call sleeps, the handler runs as it returns:
    // its frames are the caller of the blocked call
    QString stack;
    if (s_captured.load())
        stack = capturedStackText();
    else if (loopRan)
        stack = QStringLiteral("(the main thread never answered)\n");
    else
        return;
    m_lateStackPending = false;

    const QString text = QStringLiteral("\nMain thread stack once the kernel call returned:\n") + stack;
    if (const QString path = lastReportPath(); !path.isEmpty()) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Append))
            f.write(text.toUtf8());
    }
    for (const QString& line : text.split(u'\n', Qt::SkipEmptyParts).mid(0, 40))
        logWarning(QStringLiteral("  stall: ") + line);
#else
    Q_UNUSED(loopRan);
    m_lateStackPending = false;
#endif
}

void StallWatchdog::pruneReports() const
{
    QDir dir(m_crashDir);
    const QFileInfoList old = dir.entryInfoList({QStringLiteral("stall-*.stall")}, QDir::Files, QDir::Time);
    for (qsizetype i = kKeptReports; i < old.size(); ++i)
        QFile::remove(old.at(i).absoluteFilePath());
}

QString StallWatchdog::mainThreadStack() const
{
#ifdef EMULE_STALL_HAVE_BACKTRACE
    s_captured.store(false);
    if (::pthread_kill(m_mainThread, kStackSignal) != 0)
        return QStringLiteral("(could not signal the main thread)\n");
    // a thread asleep in the kernel takes the signal once its call returns
    const int64_t until = nowNs() + 2'000'000'000;
    while (!s_captured.load() && nowNs() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!s_captured.load())
        return QStringLiteral("(no answer in 2 s: blocked in an uninterruptible kernel call, "
                              "see the state above; the stack follows once the call returns)\n");
    return capturedStackText();
#elif defined(EMULE_STALL_HAVE_WIN_UNWIND)
    // a thread waiting in the kernel can be suspended too: its stack shows the call it waits in
    if (!m_mainThread)
        return QStringLiteral("(could not open the main thread)\n");
    DWORD64 frames[kMaxFrames];
    const int count = unwindSuspended(static_cast<HANDLE>(m_mainThread), frames, kMaxFrames);
    if (count < 0)
        return QStringLiteral("(could not suspend the main thread)\n");
    return symbolizeFrames(frames, count);
#else
    return QStringLiteral("(stack capture not supported on this platform)\n");
#endif
}

QString StallWatchdog::mainThreadKernelState() const
{
#ifdef Q_OS_LINUX
    // state D + a wchan like fuse_wait_answer / folio_wait_bit = waiting for the disk
    const QString task = QStringLiteral("/proc/self/task/%1/").arg(m_mainTid);
    QString state;
    QString majorFaults;
    if (QFile f(task + QStringLiteral("stat")); f.open(QIODevice::ReadOnly)) {
        const QByteArray stat = f.readAll();
        // "tid (comm) S ...": the state follows the last ')', majflt is the 10th field after it
        if (const qsizetype close = stat.lastIndexOf(')'); close >= 0 && close + 2 < stat.size()) {
            const QList<QByteArray> fields = stat.mid(close + 2).split(' ');
            state = QString::fromLatin1(fields.value(0));
            majorFaults = QString::fromLatin1(fields.value(9));
        }
    }
    // report 29: a page wait on a plain heap read = RAM ran out; show who holds it
    const auto procFields = [](const QString& path, std::initializer_list<QByteArrayView> keys) {
        QStringList parts;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return parts;
        for (const QByteArray& line : f.readAll().split('\n')) {
            for (const QByteArrayView key : keys)
                if (line.startsWith(key) && line.size() > key.size() && line.at(key.size()) == ':')
                    parts.append(QString::fromLatin1(line.simplified()));
        }
        return parts;
    };
    QStringList memory = procFields(QStringLiteral("/proc/self/status"), {"VmRSS", "VmSwap"});
    memory += procFields(QStringLiteral("/proc/meminfo"), {"MemTotal", "MemAvailable", "SwapTotal", "SwapFree", "Dirty", "Writeback"});
    QString wchan;
    if (QFile f(task + QStringLiteral("wchan")); f.open(QIODevice::ReadOnly))
        wchan = QString::fromLatin1(f.readAll()).trimmed();
    QString kernelStack;   // root only, usually empty
    if (QFile f(task + QStringLiteral("stack")); f.open(QIODevice::ReadOnly))
        kernelStack = QString::fromLatin1(f.readAll()).trimmed();
    QString out = QStringLiteral("Kernel: state %1, wchan %2")
                      .arg(state.isEmpty() ? QStringLiteral("?") : state,
                           wchan.isEmpty() || wchan == u"0" ? QStringLiteral("-") : wchan);
    if (!majorFaults.isEmpty())
        out += QStringLiteral(", major faults %1").arg(majorFaults);
    if (!memory.isEmpty())
        out += QStringLiteral("\nMemory: ") + memory.join(QStringLiteral(", "));
    // PSI: share of time tasks waited for memory / disk (kernel 4.20+)
    for (const auto* kind : {"memory", "io"}) {
        QFile f(QStringLiteral("/proc/pressure/%1").arg(QLatin1StringView(kind)));
        if (f.open(QIODevice::ReadOnly))
            out += QStringLiteral("\nPressure %1: %2").arg(QLatin1StringView(kind), QString::fromLatin1(f.readLine()).trimmed());
    }
    if (!kernelStack.isEmpty())
        out += QStringLiteral("\nKernel stack:\n") + kernelStack;
    return out;
#else
    return {};
#endif
}

QString StallWatchdog::mainThreadSyscall() const
{
#ifdef Q_OS_LINUX
    // "nr arg1..arg6 sp pc" while in a call, "-1 sp pc" when blocked outside one (a
    // page fault), "running" otherwise. Its own process may read it without ptrace rights.
    QFile f(QStringLiteral("/proc/self/task/%1/syscall").arg(m_mainTid));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QList<QByteArray> fields = f.readAll().trimmed().split(' ');
    bool ok = false;
    const long nr = fields.value(0).toLong(&ok);
    if (!ok)
        return QStringLiteral("Syscall: %1").arg(QString::fromLatin1(fields.value(0)));
    if (nr < 0)
        return QStringLiteral("Syscall: none (a page fault or a kernel wait outside a call)");
    const auto arg = [&fields](int i) { return fields.value(1 + i).toULongLong(nullptr, 0); };

    const auto* info = std::find_if(std::begin(kSyscalls), std::end(kSyscalls),
                                    [nr](const SyscallInfo& sc) { return sc.nr == nr; });
    if (info == std::end(kSyscalls))
        return QStringLiteral("Syscall: %1").arg(nr);
    QString out = QStringLiteral("Syscall: %1").arg(QLatin1StringView(info->name));
    if (info->fdArg >= 0) {
        const auto fd = static_cast<long>(arg(info->fdArg));
        out += QStringLiteral(" fd %1 → %2").arg(fd).arg(fdTarget(fd));
    }
    if (info->pathArg >= 0) {
        out += QStringLiteral(" \"%1\"").arg(readOwnString(arg(info->pathArg)));
        if (info->pathArg > 0) {   // *at: relative to a dir fd (AT_FDCWD = -100)
            const auto dirFd = static_cast<long>(static_cast<int>(arg(info->pathArg - 1)));
            if (dirFd >= 0)
                out += QStringLiteral(" in %1").arg(fdTarget(dirFd));
        }
        if (nr == SYS_renameat || nr == SYS_renameat2)
            out += QStringLiteral(" → \"%1\"").arg(readOwnString(arg(3)));
    }
    return out;
#else
    return {};
#endif
}

} // namespace eMule
