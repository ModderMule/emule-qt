#pragma once

/// @file CoreConnectScript.h
/// @brief Script that starts the GUI against a given core: PowerShell on Windows,
///        bash elsewhere (3.2-safe: stock macOS bash, any Linux bash).
///
/// The script runs `emuleqt --core-host H --core-port P` with the token in
/// EMULEQT_CORE_TOKEN, so it never shows in a process list. An empty token makes the
/// script ask for it. Not a .lnk: a shortcut can neither set the env var nor prompt.

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace eMule {

class CoreConnectScript {
public:
    enum class Kind { PowerShell, Bash };
    /// OS the script runs on: the GUI's computer, not the core's
    enum class Target { Windows, MacOS, Linux };

    struct Params {
        QString host;      ///< IP or name, no brackets
        uint16_t port = 0;
        QString token;     ///< empty = script prompts
        QString appPath;   ///< GUI executable to start; searched for when gone
    };

    /// Host safe to embed and to pass on: letters, digits, . - _ : % (IPv6, scope).
    /// Brackets around an IPv6 host are allowed and dropped by normalizedHost().
    [[nodiscard]] static bool isValidHost(const QString& host);
    [[nodiscard]] static QString normalizedHost(const QString& host);

    /// Script text as file bytes: PowerShell = UTF-8 BOM + CRLF (5.1 reads BOM-less
    /// files as ANSI), bash = LF.
    [[nodiscard]] static QByteArray generate(Kind kind, const Params& params);

    /// Target of this OS
    [[nodiscard]] static Target nativeTarget();
    [[nodiscard]] static Kind kindOf(Target target);
    /// Dropdown text, e.g. "macOS (.command)"
    [[nodiscard]] static QString targetName(Target target);
    /// GUI executable to start: the AppImage, else this binary
    [[nodiscard]] static QString currentAppPath();
    /// Suggested file name, e.g. "eMule Qt core example.org.command"
    [[nodiscard]] static QString suggestedFileName(const QString& host, Target target);
    /// Save dialog filter list for @p target, first = default
    [[nodiscard]] static QString fileFilter(Target target);

private:
    [[nodiscard]] static QString bash(const Params& params);
    [[nodiscard]] static QString powerShell(const Params& params);
    /// '…' literal for bash
    [[nodiscard]] static QString bashQuote(const QString& value);
    /// '…' literal for PowerShell
    [[nodiscard]] static QString psQuote(const QString& value);
};

} // namespace eMule
