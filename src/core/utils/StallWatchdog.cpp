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
        }
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
    text += QStringLiteral("\nMain thread stack:\n") + mainThreadStack();

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
                              "see the state above)\n");

    const int count = s_frameCount.load();
    QString out;
    if (char** symbols = ::backtrace_symbols(s_frames, count)) {
        // frames 0-1 are the handler and the signal trampoline
        for (int i = 0; i < count; ++i)
            out += QStringLiteral("#%1 %2\n").arg(i).arg(QString::fromLocal8Bit(symbols[i]));
        ::free(symbols);
    }
    return out;
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
    if (QFile f(task + QStringLiteral("stat")); f.open(QIODevice::ReadOnly)) {
        const QByteArray stat = f.readAll();
        // "tid (comm) S ...": the state follows the last ')'
        if (const qsizetype close = stat.lastIndexOf(')'); close >= 0 && close + 2 < stat.size())
            state = QString::fromLatin1(stat.mid(close + 2, 1));
    }
    QString wchan;
    if (QFile f(task + QStringLiteral("wchan")); f.open(QIODevice::ReadOnly))
        wchan = QString::fromLatin1(f.readAll()).trimmed();
    QString kernelStack;   // root only, usually empty
    if (QFile f(task + QStringLiteral("stack")); f.open(QIODevice::ReadOnly))
        kernelStack = QString::fromLatin1(f.readAll()).trimmed();
    QString out = QStringLiteral("Kernel: state %1, wchan %2")
                      .arg(state.isEmpty() ? QStringLiteral("?") : state,
                           wchan.isEmpty() || wchan == u"0" ? QStringLiteral("-") : wchan);
    if (!kernelStack.isEmpty())
        out += QStringLiteral("\nKernel stack:\n") + kernelStack;
    return out;
#else
    return {};
#endif
}

} // namespace eMule
