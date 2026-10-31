#include "Updater.h"
#include "Platform.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;

namespace updater {

namespace {

constexpr const char* kBackupDir = ".update-backup";

std::string timestamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

bool parseInt(const std::string& s, std::int64_t& out)
{
    if (s.empty())
        return false;
    try {
        std::size_t used = 0;
        out = std::stoll(s, &used);
        return used == s.size();
    } catch (...) {
        return false;
    }
}

std::string quoteForLog(const std::vector<std::string>& argv)
{
    std::string s;
    for (const auto& a : argv) {
        if (!s.empty())
            s += ' ';
        if (a.find_first_of(" \t\"") != std::string::npos)
            s += '"' + a + '"';
        else
            s += a;
    }
    return s;
}

const char* kUsage =
    "usage: emuleqt-updater --package <file> --format dmg|archive|inno --target <dir>\n"
    "  [--version X.Y.Z] [--wait-pid <pid>]... [--wait-timeout <sec>]\n"
    "  [--keep-existing <relpath>]... [--log <file>] [--result <file>]\n"
    "  [--launch <exe> [args...] --end]...\n";

} // namespace

// ---------------------------------------------------------------------------
// Logger
// ---------------------------------------------------------------------------

void Logger::setFile(fs::path path)
{
    m_path = std::move(path);
    std::error_code ec;
    if (m_path.has_parent_path())
        fs::create_directories(m_path.parent_path(), ec);
}

void Logger::line(const std::string& text)
{
    const std::string l = timestamp() + "  " + text + '\n';
    std::cerr << l;
    if (m_path.empty())
        return;
    std::ofstream f(m_path, std::ios::app | std::ios::binary);
    f << l;
}

// ---------------------------------------------------------------------------
// Updater
// ---------------------------------------------------------------------------

int Updater::run(const std::vector<std::string>& args)
{
    const std::string usageError = parseArgs(args, m_opts);
    if (!m_opts.logFile.empty())
        m_log.setFile(m_opts.logFile);
    m_log.line("emuleqt-updater pid " + std::to_string(platform::currentPid()) + ": "
               + quoteForLog(args));
    if (!usageError.empty()) {
        m_log.line("usage error: " + usageError);
        std::cerr << kUsage;
        return ExitUsage;
    }

    bool ok = false;
    if (!platform::waitForPids(m_opts.waitPids, std::chrono::seconds(m_opts.waitTimeoutSec), m_log)) {
        fail("timed out after " + std::to_string(m_opts.waitTimeoutSec)
             + " s waiting for the running processes to quit");
    } else {
        if (!m_opts.waitPids.empty()) {
            // file handles and the core lock go away a little after the exit
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        m_log.line("installing version " + m_opts.version + " into "
                   + platform::fromPath(m_opts.target));
        ok = install();
    }
    m_log.line(ok ? "install ok" : "install failed: " + m_error
                                       + (m_rolledBack ? " (rolled back)" : ""));
    writeResult(ok);
    runLaunches();
    m_log.line("done");
    return ok ? ExitOk : ExitFailed;
}

std::string Updater::parseArgs(const std::vector<std::string>& args, Options& opts)
{
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&](std::string& out) -> bool {
            if (i + 1 >= args.size())
                return false;
            out = args[++i];
            return true;
        };
        std::string v;
        if (a == "--launch") {
            std::vector<std::string> cmd;
            bool ended = false;
            while (++i < args.size()) {
                if (args[i] == "--end") {
                    ended = true;
                    break;
                }
                cmd.push_back(args[i]);
            }
            if (!ended)
                return "--launch without --end";
            if (cmd.empty() || cmd.front().empty())
                return "--launch without an executable";
            opts.launches.push_back(std::move(cmd));
            continue;
        }
        if (a != "--package" && a != "--format" && a != "--target" && a != "--version"
            && a != "--wait-pid" && a != "--wait-timeout" && a != "--keep-existing"
            && a != "--log" && a != "--result")
            return "unknown argument '" + a + "'";
        if (!value(v))
            return "missing value for " + a;
        if (a == "--package") {
            opts.package = platform::toPath(v);
        } else if (a == "--format") {
            if (v == "dmg")
                opts.format = PackageFormat::Dmg;
            else if (v == "archive")
                opts.format = PackageFormat::Archive;
            else if (v == "inno")
                opts.format = PackageFormat::Inno;
            else
                return "unknown format '" + v + "'";
        } else if (a == "--target") {
            opts.target = platform::toPath(v);
        } else if (a == "--version") {
            opts.version = v;
        } else if (a == "--wait-pid") {
            std::int64_t pid = 0;
            if (!parseInt(v, pid) || pid <= 0)
                return "bad pid '" + v + "'";
            opts.waitPids.push_back(pid);
        } else if (a == "--wait-timeout") {
            std::int64_t sec = 0;
            if (!parseInt(v, sec) || sec < 0 || sec > 86400)
                return "bad timeout '" + v + "'";
            opts.waitTimeoutSec = static_cast<int>(sec);
        } else if (a == "--keep-existing") {
            fs::path p = platform::toPath(v).lexically_normal();
            if (p.is_absolute() || p.empty() || *p.begin() == "..")
                return "bad --keep-existing '" + v + "'";
            opts.keepExisting.push_back(std::move(p));
        } else if (a == "--log") {
            opts.logFile = platform::toPath(v);
        } else if (a == "--result") {
            opts.resultFile = platform::toPath(v);
        }
    }
    if (opts.package.empty())
        return "--package missing";
    if (opts.format == PackageFormat::None)
        return "--format missing";
    if (opts.target.empty())
        return "--target missing";
    return {};
}

std::string Updater::jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buf;
            } else {
                out += c; // UTF-8 passes through
            }
        }
    }
    return out;
}

bool Updater::isUnder(const fs::path& rel, const fs::path& base)
{
    auto r = rel.begin();
    for (const auto& part : base) {
        if (part.empty() || part == ".")
            continue;
        if (r == rel.end() || *r != part)
            return false;
        ++r;
    }
    return true;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

bool Updater::install()
{
    std::error_code ec;
    if (!fs::is_regular_file(m_opts.package, ec)) {
        fail("package not found: " + platform::fromPath(m_opts.package));
        return false;
    }
    switch (m_opts.format) {
    case PackageFormat::Dmg: return installDmg();
    case PackageFormat::Archive: return installArchive();
    case PackageFormat::Inno: return installInno();
    case PackageFormat::None: break;
    }
    fail("no format");
    return false;
}

bool Updater::installArchive()
{
    std::error_code ec;
    if (!fs::is_directory(m_opts.target, ec)) {
        fail("target is not a directory: " + platform::fromPath(m_opts.target));
        return false;
    }
    fs::path base = m_opts.package.parent_path();
    if (base.empty())
        base = fs::current_path(ec);
    fs::path staging;
    if (!platform::makeTempDir(base, ".emuleqt-staging-", staging)) {
        fail("cannot create staging dir in " + platform::fromPath(base));
        return false;
    }
    m_log.line("extracting to " + platform::fromPath(staging));
    const int rc = platform::runProcess(
        {platform::systemTar(), "-xf", platform::fromPath(m_opts.package), "-C", platform::fromPath(staging)},
        m_log);
    bool ok = false;
    if (rc != 0) {
        fail("extracting the package failed (tar exit " + std::to_string(rc) + ")");
    } else {
        // tarball has a top eMuleQt/, the zip may have eMule/
        fs::path root = staging;
        std::vector<fs::directory_entry> top;
        for (const auto& e : fs::directory_iterator(staging, ec))
            top.push_back(e);
        if (top.size() == 1 && top.front().is_directory(ec) && !top.front().is_symlink(ec))
            root = top.front().path();
        if (top.empty())
            fail("package is empty");
        else
            ok = overlay(root);
    }
    fs::remove_all(staging, ec);
    if (ec)
        m_log.line("could not remove staging dir: " + ec.message());
    return ok;
}

bool Updater::installDmg()
{
#ifdef __APPLE__
    std::error_code ec;
    const fs::path& target = m_opts.target;
    const fs::path newApp = platform::toPath(platform::fromPath(target) + ".new");
    const fs::path oldApp = platform::toPath(platform::fromPath(target) + ".old");

    // a crash between the two renames left only .old
    if (!fs::exists(fs::symlink_status(target, ec)) && fs::exists(fs::symlink_status(oldApp, ec))) {
        m_log.line("restoring leftover " + platform::fromPath(oldApp));
        fs::rename(oldApp, target, ec);
    }
    fs::remove_all(newApp, ec);
    fs::remove_all(oldApp, ec);

    fs::path mnt;
    if (!platform::makeTempDir(fs::temp_directory_path(ec), "emuleqt-dmg-", mnt)) {
        fail("cannot create mount point");
        return false;
    }
    const std::string mntStr = platform::fromPath(mnt);
    if (platform::runProcess({"/usr/bin/hdiutil", "attach", "-nobrowse", "-readonly", "-noautoopen",
                              "-mountpoint", mntStr, platform::fromPath(m_opts.package)}, m_log) != 0) {
        fail("hdiutil attach failed");
        fs::remove(mnt, ec);
        return false;
    }

    bool ok = false;
    bool moved = false;
    do {
        std::vector<fs::path> apps;
        for (const auto& e : fs::directory_iterator(mnt, ec)) {
            if (e.path().extension() == ".app" && e.is_directory(ec) && !e.is_symlink(ec))
                apps.push_back(e.path());
        }
        if (apps.size() != 1) {
            fail("expected one .app in the image, found " + std::to_string(apps.size()));
            break;
        }
        m_log.line("copying " + platform::fromPath(apps.front()));
        if (platform::runProcess({"/usr/bin/ditto", platform::fromPath(apps.front()),
                                  platform::fromPath(newApp)}, m_log) != 0) {
            fail("ditto failed");
            break;
        }
        if (fs::exists(fs::symlink_status(target, ec))) {
            fs::rename(target, oldApp, ec);
            if (ec) {
                fail("cannot move the old app aside: " + ec.message());
                break;
            }
            moved = true;
        }
        fs::rename(newApp, target, ec);
        if (ec) {
            fail("cannot move the new app in place: " + ec.message());
            break;
        }
        ok = true;
    } while (false);

    if (ok) {
        platform::runProcess({"/usr/bin/xattr", "-dr", "com.apple.quarantine", platform::fromPath(target)}, m_log);
        fs::remove_all(oldApp, ec);
        if (ec)
            m_log.line("could not remove " + platform::fromPath(oldApp) + ": " + ec.message());
    } else {
        if (moved) {
            fs::remove_all(target, ec);
            fs::rename(oldApp, target, ec);
            if (ec)
                m_log.line("ROLLBACK FAILED, old app left at " + platform::fromPath(oldApp) + ": " + ec.message());
            m_rolledBack = true;
        }
        fs::remove_all(newApp, ec);
    }

    if (platform::runProcess({"/usr/bin/hdiutil", "detach", mntStr}, m_log) != 0) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        platform::runProcess({"/usr/bin/hdiutil", "detach", "-force", mntStr}, m_log);
    }
    fs::remove(mnt, ec);
    return ok;
#else
    fail("dmg packages are only supported on macOS");
    return false;
#endif
}

bool Updater::installInno()
{
#ifdef _WIN32
    std::string error;
    if (!platform::runInnoSetup(m_opts.package, m_log, error)) {
        fail(error);
        return false;
    }
    return true;
#else
    fail("inno packages are only supported on Windows");
    return false;
#endif
}

// Copies the tree at from onto the target; files already there move to
// .update-backup first so a failure can put everything back.
bool Updater::overlay(const fs::path& from)
{
    const fs::path& target = m_opts.target;
    const fs::path backup = target / kBackupDir;
    std::error_code ec;
    if (fs::exists(fs::symlink_status(backup, ec))) {
        m_log.line("removing stale " + platform::fromPath(backup));
        fs::remove_all(backup, ec);
    }

    struct Change {
        fs::path rel;
        bool backedUp;
    };
    std::vector<Change> changes;
    std::vector<fs::path> newDirs;
    int replaced = 0, added = 0, kept = 0;
    bool ok = true;

    auto keptPath = [this](const fs::path& rel) {
        for (const auto& k : m_opts.keepExisting)
            if (isUnder(rel, k))
                return true;
        return false;
    };

    fs::recursive_directory_iterator it(from, ec);
    if (ec) {
        fail("cannot read staging dir: " + ec.message());
        return false;
    }
    for (fs::recursive_directory_iterator end; it != end; it.increment(ec)) {
        if (ec) {
            fail("walking the staging dir: " + ec.message());
            ok = false;
            break;
        }
        const fs::path src = it->path();
        const fs::path rel = src.lexically_relative(from);
        const fs::path dst = target / rel;
        const auto st = it->symlink_status(ec);
        if (isUnder(rel, kBackupDir)) {
            if (fs::is_directory(st))
                it.disable_recursion_pending();
            continue;
        }
        std::error_code dec;
        const auto dstSt = fs::symlink_status(dst, dec);
        const bool dstExists = fs::exists(dstSt);

        if (fs::is_directory(st)) {
            if (!dstExists) {
                fs::create_directory(dst, ec);
                if (ec) {
                    fail("cannot create " + platform::fromPath(dst) + ": " + ec.message());
                    ok = false;
                    break;
                }
                newDirs.push_back(dst);
            } else if (!fs::is_directory(fs::status(dst, dec))) {
                fail("not a directory in target: " + platform::fromPath(rel));
                ok = false;
                break;
            }
            continue;
        }
        if (!fs::is_regular_file(st) && !fs::is_symlink(st)) {
            m_log.line("skipping special file " + platform::fromPath(rel));
            continue;
        }
        if (dstExists && keptPath(rel)) {
            ++kept;
            continue;
        }
        bool backedUp = false;
        if (dstExists) {
            if (fs::is_directory(dstSt)) {
                fail("directory in the way of " + platform::fromPath(rel));
                ok = false;
                break;
            }
            const fs::path bak = backup / rel;
            fs::create_directories(bak.parent_path(), ec);
            if (!ec)
                fs::rename(dst, bak, ec);
            if (ec) {
                fail("cannot move aside " + platform::fromPath(dst) + ": " + ec.message());
                ok = false;
                break;
            }
            backedUp = true;
        }
        changes.push_back({rel, backedUp});
        if (fs::is_symlink(st)) {
            fs::copy_symlink(src, dst, ec);
        } else {
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (!ec)
                fs::permissions(dst, fs::status(src).permissions(), fs::perm_options::replace, ec);
        }
        if (ec) {
            fail("cannot copy " + platform::fromPath(rel) + ": " + ec.message());
            ok = false;
            break;
        }
        backedUp ? ++replaced : ++added;
    }

    if (ok) {
        m_log.line("overlay: " + std::to_string(replaced) + " replaced, " + std::to_string(added)
                   + " added, " + std::to_string(kept) + " kept");
        fs::remove_all(backup, ec);
        return true;
    }

    m_log.line("rolling back " + std::to_string(changes.size()) + " files");
    bool clean = true;
    for (auto c = changes.rbegin(); c != changes.rend(); ++c) {
        const fs::path dst = target / c->rel;
        fs::remove(dst, ec);
        if (c->backedUp) {
            fs::rename(backup / c->rel, dst, ec);
            if (ec) {
                clean = false;
                m_log.line("ROLLBACK FAILED for " + platform::fromPath(c->rel) + ": " + ec.message());
            }
        }
    }
    for (auto d = newDirs.rbegin(); d != newDirs.rend(); ++d)
        fs::remove(*d, ec);
    if (clean)
        fs::remove_all(backup, ec);
    else
        m_log.line("originals kept in " + platform::fromPath(backup));
    m_rolledBack = !changes.empty() || !newDirs.empty();
    return false;
}

void Updater::writeResult(bool ok)
{
    if (m_opts.resultFile.empty())
        return;
    const std::string json = std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"version\":\""
                             + jsonEscape(m_opts.version) + "\",\"error\":\"" + jsonEscape(m_error)
                             + "\",\"rolledBack\":" + (m_rolledBack ? "true" : "false") + "}\n";
    std::error_code ec;
    if (m_opts.resultFile.has_parent_path())
        fs::create_directories(m_opts.resultFile.parent_path(), ec);
    const fs::path tmp = platform::toPath(platform::fromPath(m_opts.resultFile) + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << json;
        f.flush();
        if (!f) {
            m_log.line("cannot write " + platform::fromPath(tmp));
            return;
        }
    }
    fs::rename(tmp, m_opts.resultFile, ec);
    if (ec)
        m_log.line("cannot write result file: " + ec.message());
}

void Updater::runLaunches()
{
    for (const auto& cmd : m_opts.launches) {
        std::string error;
        if (platform::launchDetached(cmd, error))
            m_log.line("launched " + quoteForLog(cmd));
        else
            m_log.line("launch failed: " + quoteForLog(cmd) + ": " + error);
    }
}

void Updater::fail(const std::string& error)
{
    m_log.line("error: " + error);
    if (m_error.empty())
        m_error = error;
}

} // namespace updater
