#ifndef _WIN32

#include "Platform.h"
#include "Updater.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#ifdef __APPLE__
#include <libproc.h>
#include <sys/proc_info.h>
#ifndef SZOMB
#define SZOMB 5 // sys/proc.h
#endif
#endif

extern char** environ;

namespace fs = std::filesystem;

namespace updater::platform {

namespace {

// a zombie still answers kill(pid, 0) — count it as gone
bool isZombie(pid_t pid)
{
#ifdef __APPLE__
    proc_bsdinfo info{};
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof info) == static_cast<int>(sizeof info))
        return info.pbi_status == SZOMB;
    return false;
#else
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string stat;
    std::getline(f, stat);
    const auto close = stat.rfind(')');
    return close != std::string::npos && close + 2 < stat.size() && stat[close + 2] == 'Z';
#endif
}

bool isAlive(std::int64_t pid)
{
    const auto p = static_cast<pid_t>(pid);
    if (::kill(p, 0) == 0)
        return !isZombie(p);
    return errno == EPERM; // exists, other user
}

std::vector<char*> cArgv(const std::vector<std::string>& argv)
{
    std::vector<char*> out;
    out.reserve(argv.size() + 1);
    for (const auto& a : argv)
        out.push_back(const_cast<char*>(a.c_str()));
    out.push_back(nullptr);
    return out;
}

} // namespace

fs::path toPath(const std::string& utf8)
{
    return fs::path(utf8);
}

std::string fromPath(const fs::path& p)
{
    return p.string();
}

bool waitForPids(const std::vector<std::int64_t>& pids, std::chrono::seconds timeout, Logger& log)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (const auto pid : pids) {
        log.line("waiting for pid " + std::to_string(pid));
        while (isAlive(pid)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                log.line("pid " + std::to_string(pid) + " still running");
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    return true;
}

int runProcess(const std::vector<std::string>& argv, Logger& log)
{
    if (argv.empty())
        return -1;
    log.line("run: " + [&] {
        std::string s;
        for (const auto& a : argv)
            s += (s.empty() ? "" : " ") + a;
        return s;
    }());

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    const std::string logPath = log.file().string();
    if (!logPath.empty()) {
        posix_spawn_file_actions_addopen(&fa, 1, logPath.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
        posix_spawn_file_actions_adddup2(&fa, 1, 2);
    }
    auto args = cArgv(argv);
    pid_t pid = 0;
    const bool search = argv.front().find('/') == std::string::npos;
    const int rc = search ? posix_spawnp(&pid, args[0], &fa, nullptr, args.data(), environ)
                          : posix_spawn(&pid, args[0], &fa, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) {
        log.line("cannot start " + argv.front() + ": " + std::strerror(rc));
        return -1;
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    if (code != 0)
        log.line(argv.front() + " exited with " + std::to_string(code));
    return code;
}

bool launchDetached(const std::vector<std::string>& argv, std::string& error)
{
    if (argv.empty()) {
        error = "empty command";
        return false;
    }
    // grandchild reports an exec failure through this pipe; CLOEXEC closes it on success
    int fds[2];
    if (::pipe(fds) != 0) {
        error = std::strerror(errno);
        return false;
    }
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);

    auto args = cArgv(argv);
    const bool search = argv.front().find('/') == std::string::npos;
    const pid_t child = ::fork();
    if (child < 0) {
        error = std::strerror(errno);
        ::close(fds[0]);
        ::close(fds[1]);
        return false;
    }
    if (child == 0) {
        ::setsid();
        const pid_t grandchild = ::fork();
        if (grandchild != 0)
            ::_exit(grandchild < 0 ? 1 : 0);
        const int null = ::open("/dev/null", O_RDWR);
        if (null >= 0) {
            ::dup2(null, 0);
            ::dup2(null, 1);
            ::dup2(null, 2);
        }
        const long maxFd = std::min(::sysconf(_SC_OPEN_MAX), 4096L);
        for (int fd = 3; fd < maxFd; ++fd)
            if (fd != fds[1])
                ::close(fd);
        sigset_t none;
        sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr);
        if (search)
            ::execvp(args[0], args.data());
        else
            ::execv(args[0], args.data());
        const int err = errno;
        [[maybe_unused]] const auto n = ::write(fds[1], &err, sizeof err);
        ::_exit(127);
    }
    ::close(fds[1]);
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    int err = 0;
    ssize_t n = 0;
    do {
        n = ::read(fds[0], &err, sizeof err);
    } while (n < 0 && errno == EINTR);
    ::close(fds[0]);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = "fork failed";
        return false;
    }
    if (n == static_cast<ssize_t>(sizeof err)) {
        error = std::strerror(err);
        return false;
    }
    return true;
}

bool runInnoSetup(const fs::path&, Logger&, std::string& error)
{
    error = "inno packages are only supported on Windows";
    return false;
}

std::string systemTar()
{
    std::error_code ec;
    for (const char* p : {"/usr/bin/tar", "/bin/tar"})
        if (fs::exists(p, ec))
            return p;
    return "tar";
}

bool makeTempDir(const fs::path& base, const std::string& prefix, fs::path& out)
{
    std::string templ = (base / (prefix + "XXXXXX")).string();
    if (::mkdtemp(templ.data()) == nullptr)
        return false;
    out = templ;
    return true;
}

std::int64_t currentPid()
{
    return static_cast<std::int64_t>(::getpid());
}

} // namespace updater::platform

#endif // !_WIN32
