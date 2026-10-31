#pragma once

// emuleqt-updater: replaces an installed eMuleQt with a downloaded package after
// the core and GUIs quit, then restarts them. Started detached by the daemon.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace updater {

// Appends timestamped lines to the log file (opened per line, so child
// processes can append too) and echoes them to stderr.
class Logger {
public:
    void setFile(std::filesystem::path path);
    const std::filesystem::path& file() const { return m_path; }
    void line(const std::string& text);

private:
    std::filesystem::path m_path;
};

enum class PackageFormat { None, Dmg, Archive, Inno };

struct Options {
    std::filesystem::path package;
    PackageFormat format = PackageFormat::None;
    std::filesystem::path target;
    std::string version;
    std::vector<std::int64_t> waitPids;
    int waitTimeoutSec = 120;
    std::vector<std::filesystem::path> keepExisting;
    std::filesystem::path logFile;
    std::filesystem::path resultFile;
    std::vector<std::vector<std::string>> launches;
};

class Updater {
public:
    static constexpr int ExitOk = 0;
    static constexpr int ExitFailed = 1;
    static constexpr int ExitUsage = 2;

    // args without argv[0], UTF-8
    int run(const std::vector<std::string>& args);

    // Fills opts; returns an error text on bad usage, empty when fine
    static std::string parseArgs(const std::vector<std::string>& args, Options& opts);
    static std::string jsonEscape(const std::string& s);
    static bool isUnder(const std::filesystem::path& rel, const std::filesystem::path& base);

private:
    bool install();
    bool installArchive();
    bool installDmg();
    bool installInno();
    bool overlay(const std::filesystem::path& from);
    void writeResult(bool ok);
    void runLaunches();
    void fail(const std::string& error);

    Options m_opts;
    Logger m_log;
    std::string m_error;
    bool m_rolledBack = false;
};

} // namespace updater
