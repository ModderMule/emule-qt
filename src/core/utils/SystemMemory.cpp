#include "pch.h"
/// @file SystemMemory.cpp
/// @brief Physical RAM of the machine — implementation.

#include "utils/SystemMemory.h"

#include <QtGlobal>

#include <algorithm>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
#include <mach/mach.h>
#include <sys/sysctl.h>
#elif defined(Q_OS_LINUX)
#include <QFile>
#endif

namespace eMule {

#if defined(Q_OS_LINUX)
namespace {
std::optional<std::uint64_t> procKibField(const QByteArray& line, const char* name);
}
#endif

std::optional<SystemMemory> systemMemory()
{
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (!GlobalMemoryStatusEx(&st))
        return std::nullopt;
    return SystemMemory{st.ullTotalPhys, st.ullAvailPhys};
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    std::uint64_t total = 0;
    size_t len = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &len, nullptr, 0) != 0 || total == 0)
        return std::nullopt;
    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    const mach_port_t host = mach_host_self();
    const kern_return_t kr = host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count);
    mach_port_deallocate(mach_task_self(), host);
    if (kr != KERN_SUCCESS)
        return std::nullopt;
    const std::uint64_t page = vm_kernel_page_size ? vm_kernel_page_size : 4096;
    // free + file cache the kernel drops at once (Activity Monitor's "available" is near this)
    const std::uint64_t pages = std::uint64_t(vm.free_count) + vm.inactive_count + vm.purgeable_count;
    return SystemMemory{total, std::min(total, pages * page)};
#elif defined(Q_OS_LINUX)
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    std::uint64_t total = 0, avail = 0;
    for (const QByteArray& line : f.readAll().split('\n')) {
        const auto field = [&](const char* name) { return procKibField(line, name); };
        if (auto v = field("MemTotal:"))
            total = *v;
        else if (auto v = field("MemAvailable:"))
            avail = *v;
    }
    if (total == 0)
        return std::nullopt;
    return SystemMemory{total, avail};
#else
    return std::nullopt;
#endif
}

std::optional<ProcessMemory> processMemory()
{
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return std::nullopt;
    // commit charge: Task Manager's "Commit size"; its "Memory" column is the private working set
    return ProcessMemory{pmc.PrivateUsage, pmc.PeakPagefileUsage};
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    task_vm_info_data_t vm{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) != KERN_SUCCESS
        || count < TASK_VM_INFO_REV1_COUNT)
        return std::nullopt;
    const std::uint64_t peak = count >= TASK_VM_INFO_REV3_COUNT ? std::uint64_t(std::max<int64_t>(0, vm.ledger_phys_footprint_peak)) : 0;
    return ProcessMemory{vm.phys_footprint, peak};
#elif defined(Q_OS_LINUX)
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    ProcessMemory m;
    for (const QByteArray& line : f.readAll().split('\n')) {
        const auto field = [&](const char* name) { return procKibField(line, name); };
        if (auto v = field("VmRSS:"))
            m.current = *v;
        else if (auto v = field("VmHWM:"))
            m.peak = *v;
    }
    if (m.current == 0)
        return std::nullopt;
    return m;
#else
    return std::nullopt;
#endif
}

bool systemMemoryLow(int percent)
{
    if (percent <= 0)
        return false;
    const auto m = systemMemory();
    return m && m->total > 0 && m->available * 100 < m->total * std::uint64_t(percent);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

#if defined(Q_OS_LINUX)
namespace {

/// "Name:   1234 kB" of a /proc file, in bytes
std::optional<std::uint64_t> procKibField(const QByteArray& line, const char* name)
{
    if (!line.startsWith(name))
        return std::nullopt;
    bool ok = false;
    const auto kib = line.mid(qsizetype(qstrlen(name))).trimmed().split(' ').value(0).toULongLong(&ok);
    return ok ? std::optional(kib * 1024) : std::nullopt;
}

} // namespace
#endif

} // namespace eMule
