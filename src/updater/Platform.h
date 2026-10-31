#pragma once

// OS layer of emuleqt-updater: processes, pid waits, UTF-8 <-> path.
// Plain std + OS APIs, no Qt (the Qt libs get replaced while this runs).

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace updater {

class Logger;

namespace platform {

// UTF-8 string <-> filesystem path (wide on Windows)
std::filesystem::path toPath(const std::string& utf8);
std::string fromPath(const std::filesystem::path& p);

// Waits until every pid exited; false on timeout (nothing killed)
bool waitForPids(const std::vector<std::int64_t>& pids, std::chrono::seconds timeout, Logger& log);

// Runs argv[0] (PATH lookup when it has no separator), stdout/stderr appended to
// the log file. Returns the exit code, -1 when it could not be started.
int runProcess(const std::vector<std::string>& argv, Logger& log);

// Starts argv detached from this process (own session, stdio to null)
bool launchDetached(const std::vector<std::string>& argv, std::string& error);

// Windows: runs an Inno Setup installer silently (one UAC prompt) and waits
bool runInnoSetup(const std::filesystem::path& package, Logger& log, std::string& error);

// System tar: bsdtar on Windows 10+ / macOS, GNU tar on Linux
std::string systemTar();

// Creates a fresh unique directory below base
bool makeTempDir(const std::filesystem::path& base, const std::string& prefix,
                 std::filesystem::path& out);

std::int64_t currentPid();

} // namespace platform
} // namespace updater
