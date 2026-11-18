#include "pch.h"
/// @file CoreConnectScript.cpp
/// @brief Connect-to-core script for PowerShell and bash.

#include "utils/CoreConnectScript.h"

#include "app/AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>

namespace eMule {

namespace {

// bash 3.2 safe (stock macOS): no [[ ]], ${x,,}, arrays, mapfile, &>>, read -s.
// Kept to POSIX sh constructs, so dash/busybox run it too.
constexpr auto kBashTemplate = R"SCRIPT(#!/usr/bin/env bash
# eMule Qt: connect to the core at @ADDRESS@
# The token gives full control of that core: keep this file private.
# macOS: double-click a .command file. Linux: run it, or "Run as program".

CORE_HOST=@HOST@
CORE_PORT=@PORT@
CORE_TOKEN=@TOKEN@
# eMule Qt when this script was made; searched for if it moved
APP=@APP@

fail() {
    printf '%s\n' "$1" >&2
    if [ -t 0 ]; then
        printf 'Press Enter to close. ' >&2
        read -r _ || true
    fi
    exit 1
}

find_app() {
    if [ -n "$APP" ] && [ -x "$APP" ] && [ ! -d "$APP" ]; then
        return 0
    fi
    case "$(uname -s)" in
        Darwin)
            for c in "/Applications/eMule Qt.app/Contents/MacOS/emuleqt" \
                     "$HOME/Applications/eMule Qt.app/Contents/MacOS/emuleqt"; do
                if [ -x "$c" ]; then APP=$c; return 0; fi
            done
            ;;
        *)
            c=$(command -v emuleqt 2>/dev/null)
            if [ -n "$c" ]; then APP=$c; return 0; fi
            for c in "$HOME"/Applications/eMuleQt*.AppImage "$HOME"/Downloads/eMuleQt*.AppImage \
                     "$HOME"/eMuleQt*.AppImage /opt/emuleqt/emuleqt /usr/local/bin/emuleqt; do
                if [ -x "$c" ] && [ ! -d "$c" ]; then APP=$c; return 0; fi
            done
            ;;
    esac
    return 1
}

ask_token() {
    prompt="Token for the eMule Qt core at $CORE_HOST"
    if [ -t 0 ]; then
        printf '%s: ' "$prompt" >&2
        trap 'stty echo 2>/dev/null' EXIT
        trap 'stty echo 2>/dev/null; exit 130' INT TERM
        stty -echo 2>/dev/null
        IFS= read -r CORE_TOKEN || CORE_TOKEN=
        stty echo 2>/dev/null
        trap - EXIT INT TERM
        printf '\n' >&2
    elif [ "$(uname -s)" = Darwin ]; then
        CORE_TOKEN=$(osascript -e 'on run argv' \
            -e 'return text returned of (display dialog (item 1 of argv & ":") default answer "" with hidden answer)' \
            -e 'end run' "$prompt" 2>/dev/null)
    elif command -v zenity >/dev/null 2>&1; then
        CORE_TOKEN=$(zenity --password --title="$prompt" 2>/dev/null)
    elif command -v kdialog >/dev/null 2>&1; then
        CORE_TOKEN=$(kdialog --password "$prompt:" 2>/dev/null)
    fi
    # trim spaces from a paste
    CORE_TOKEN=$(printf '%s' "$CORE_TOKEN" | tr -d ' \t\r\n')
}

if [ -z "$CORE_TOKEN" ]; then
    ask_token
fi
if [ -z "$CORE_TOKEN" ]; then
    fail "No token given."
fi
if ! find_app; then
    fail "eMule Qt not found. Set APP= in this script to its program file."
fi

EMULEQT_CORE_TOKEN=$CORE_TOKEN
export EMULEQT_CORE_TOKEN
# Ignored before the fork: a terminal closing as this script ends hangs up the
# child before nohup has run
trap '' HUP
nohup "$APP" --core-host "$CORE_HOST" --core-port "$CORE_PORT" >/dev/null 2>&1 &
exit 0
)SCRIPT";

constexpr auto kPowerShellTemplate = R"SCRIPT(# eMule Qt: connect to the core at @ADDRESS@
# The token gives full control of that core: keep this file private.
# Start it with right-click > Run with PowerShell.

$CoreHost  = @HOST@
$CorePort  = @PORT@
$CoreToken = @TOKEN@
# eMule Qt when this script was made; searched for if it moved
$App       = @APP@

function Fail([string]$Message) {
    Write-Host $Message -ForegroundColor Red
    Read-Host 'Press Enter to close' | Out-Null
    exit 1
}

if (-not $App -or -not (Test-Path -LiteralPath $App -PathType Leaf)) {
    $App = $null
    foreach ($dir in @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:ProgramW6432)) {
        if (-not $dir) { continue }
        $candidate = Join-Path $dir 'eMule Qt\emuleqt.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $App = $candidate; break }
    }
    if (-not $App -and $env:LOCALAPPDATA) {
        $candidate = Join-Path $env:LOCALAPPDATA 'Programs\eMule Qt\emuleqt.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $App = $candidate }
    }
    if (-not $App) {
        $command = Get-Command 'emuleqt.exe' -ErrorAction SilentlyContinue
        if ($command) { $App = $command.Source }
    }
    if (-not $App) { Fail 'eMule Qt not found. Set $App in this script to emuleqt.exe.' }
}

if (-not $CoreToken) {
    $secure = Read-Host -AsSecureString "Token for the eMule Qt core at $CoreHost"
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try { $CoreToken = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
    if ($CoreToken) { $CoreToken = $CoreToken.Trim() }
}
if (-not $CoreToken) { Fail 'No token given.' }

# Inherited by eMule Qt only; never on its command line
$env:EMULEQT_CORE_TOKEN = $CoreToken
Start-Process -FilePath $App -WorkingDirectory (Split-Path -Parent $App) `
    -ArgumentList @('--core-host', $CoreHost, '--core-port', "$CorePort")
)SCRIPT";

QString fill(const QString& text, const CoreConnectScript::Params& p, const QString& host,
             const QString& token, const QString& app)
{
    const bool v6 = p.host.contains(u':');
    const QString address = (v6 ? u'[' + p.host + u']' : p.host) + u':' + QString::number(p.port);
    // One pass: a token or path may hold "@…@" itself
    static const QRegularExpression re(QStringLiteral("@(ADDRESS|HOST|PORT|TOKEN|APP)@"));
    QString out;
    qsizetype last = 0;
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        out += QStringView(text).mid(last, m.capturedStart() - last);
        const QStringView key = m.capturedView(1);
        if (key == u"ADDRESS")    out += address;
        else if (key == u"HOST")  out += host;
        else if (key == u"PORT")  out += QString::number(p.port);
        else if (key == u"TOKEN") out += token;
        else                      out += app;
        last = m.capturedEnd();
    }
    out += QStringView(text).mid(last);
    return out;
}

} // namespace

bool CoreConnectScript::isValidHost(const QString& host)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9._:%-]+$"));
    const QString h = normalizedHost(host);
    return !h.isEmpty() && h.size() <= 253 && re.match(h).hasMatch() && !h.startsWith(u'-');
}

QString CoreConnectScript::normalizedHost(const QString& host)
{
    QString h = host.trimmed();
    if (h.startsWith(u'[') && h.endsWith(u']'))
        h = h.mid(1, h.size() - 2);
    return h;
}

QByteArray CoreConnectScript::generate(Kind kind, const Params& params)
{
    Params p = params;
    p.host = normalizedHost(p.host);
    p.token = p.token.trimmed();
    if (kind == Kind::Bash)
        return bash(p).toUtf8();
    QString text = powerShell(p);
    text.replace(u'\n', QStringLiteral("\r\n"));
    return QByteArray("\xEF\xBB\xBF") + text.toUtf8();
}

CoreConnectScript::Target CoreConnectScript::nativeTarget()
{
#ifdef Q_OS_WIN
    return Target::Windows;
#elif defined(Q_OS_MACOS)
    return Target::MacOS;
#else
    return Target::Linux;
#endif
}

CoreConnectScript::Kind CoreConnectScript::kindOf(Target target)
{
    return target == Target::Windows ? Kind::PowerShell : Kind::Bash;
}

QString CoreConnectScript::targetName(Target target)
{
    switch (target) {
    case Target::Windows:
        return QCoreApplication::translate("CoreConnectScript", "Windows (PowerShell .ps1)");
    case Target::MacOS:
        return QCoreApplication::translate("CoreConnectScript", "macOS (Terminal .command)");
    case Target::Linux:
        break;
    }
    return QCoreApplication::translate("CoreConnectScript", "Linux (shell .sh)");
}

QString CoreConnectScript::currentAppPath()
{
    if (const QString image = AppConfig::appImagePath(); !image.isEmpty())
        return image;
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

QString CoreConnectScript::suggestedFileName(const QString& host, Target target)
{
    QString name = normalizedHost(host);
    name.replace(u':', u'-');   // not allowed in Windows file names
    name.replace(u'%', u'-');
    const auto ext = target == Target::Windows ? QStringLiteral(".ps1")
                   : target == Target::MacOS   ? QStringLiteral(".command")
                                               : QStringLiteral(".sh");
    return QStringLiteral("eMule Qt core ") + name + ext;
}

QString CoreConnectScript::fileFilter(Target target)
{
    switch (target) {
    case Target::Windows:
        return QCoreApplication::translate("CoreConnectScript", "PowerShell script (*.ps1)");
    case Target::MacOS:
        // .command opens in Terminal on double-click; .sh opens an editor
        return QCoreApplication::translate("CoreConnectScript", "Terminal script (*.command)")
             + QStringLiteral(";;")
             + QCoreApplication::translate("CoreConnectScript", "Shell script (*.sh)");
    case Target::Linux:
        break;
    }
    return QCoreApplication::translate("CoreConnectScript", "Shell script (*.sh)");
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

QString CoreConnectScript::bash(const Params& p)
{
    return fill(QString::fromUtf8(kBashTemplate), p, bashQuote(p.host), bashQuote(p.token),
                bashQuote(p.appPath));
}

QString CoreConnectScript::powerShell(const Params& p)
{
    return fill(QString::fromUtf8(kPowerShellTemplate), p, psQuote(p.host), psQuote(p.token),
                psQuote(p.appPath));
}

QString CoreConnectScript::bashQuote(const QString& value)
{
    QString v = value;
    v.replace(u'\'', QStringLiteral("'\\''"));
    return u'\'' + v + u'\'';
}

QString CoreConnectScript::psQuote(const QString& value)
{
    // PowerShell also closes '…' on the typographic single quotes
    QString v = value;
    for (const QChar q : {QChar(u'\''), QChar(0x2018), QChar(0x2019), QChar(0x201A), QChar(0x201B)})
        v.replace(q, QString(q) + q);
    return u'\'' + v + u'\'';
}

} // namespace eMule
