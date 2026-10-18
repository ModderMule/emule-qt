#pragma once

/// @file CommandLineExec.h
/// @brief Command-line parsing and execution for the GUI application.

#include "MainWindow.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QString>

class QApplication;
class QPixmap;

namespace eMule {

class ExternalLinkHandler;

class CommandLineExec {
public:
    /// Parse command-line arguments.
    void parse(QApplication& app);

    /// Whether --screenshot was given.
    [[nodiscard]] bool screenshotMode() const { return m_screenshotMode; }

    /// Screenshot output path from --screenshot, or empty if not given.
    [[nodiscard]] const QString& screenshotPath() const { return m_screenshotPath; }

    /// Screenshot delay in milliseconds (default: 3000).
    [[nodiscard]] int screenshotDelay() const { return m_screenshotDelay; }

    /// Options page index, or -1 if not set.
    [[nodiscard]] int optionsPage() const { return m_optionsPage; }

    /// Whether --new-instance was given: skip the one-GUI-per-profile guard.
    [[nodiscard]] bool newInstance() const { return m_newInstance; }

    /// The ed2k links and .nzb files given on the command line, as typed.
    [[nodiscard]] const QStringList& positionalArgs() const { return m_positionalArgs; }

    /// Config directory override from --config, or empty if not set.
    [[nodiscard]] QString configOverride() const;

    /// Apply --tab and --subtab to the main window.
    void applyTabArgs(MainWindow& mainWindow) const;

    /// Schedule screenshot capture (if --screenshot is set).
    void setupScreenshotTimer(QApplication& app, MainWindow& mainWindow) const;

    /// Import the ed2k:// positional arguments and queue the .nzb ones.
    ///
    /// Handing them to @p linkHandler rather than acting here is what makes the wait for
    /// the daemon correct: an argument given on the command line always arrives before
    /// the IPC connection is up — main() calls the *asynchronous* connectToDaemon() and
    /// then this, on the next line — and the handler holds it until there is a daemon
    /// instead of guessing at a delay. That is as true of a .nzb, whose contents travel
    /// over IPC, as of a link; adding one here directly raised a modal "Not connected"
    /// over a window the user had not seen yet.
    /// It is not called handleEd2kLinks any more because a desktop launcher passes `%U`,
    /// so what arrives here is whatever the user double-clicked.
    void handleOpenArguments(ExternalLinkHandler& linkHandler) const;

    /// What handleOpenArguments() does, for @p args from anywhere — also the ones a
    /// second start of the same profile forwards to this one.
    static void openArguments(const QStringList& args, ExternalLinkHandler& linkHandler);

    /// --register-file-types / --unregister-file-types, for a packager's
    /// post-install script or a user who turned the setting off. Returns true
    /// when one was given and handled, and the caller should then exit.
    [[nodiscard]] bool handleFileTypeRegistration() const;

private:
    /// mkpath the parent dir, write @p pixmap to @p path, log the outcome.
    /// Returns false (and logs why) on any failure.
    [[nodiscard]] static bool saveScreenshot(const QPixmap& pixmap, const QString& path);

    QCommandLineParser m_parser;

    QCommandLineOption m_screenshotOption{
        QStringLiteral("screenshot"),
        QStringLiteral("Take a screenshot to <path> and exit (development aid)."),
        QStringLiteral("path")};

    QCommandLineOption m_tabOption{
        QStringLiteral("tab"),
        QStringLiteral("Switch to tab on startup (kad, servers, transfers, search, shared, messages, irc, statistics, usenet)."),
        QStringLiteral("name")};

    QCommandLineOption m_subtabOption{
        QStringLiteral("subtab"),
        QStringLiteral("Sub-tab index within the active panel."),
        QStringLiteral("index")};

    QCommandLineOption m_subtabTopOption{
        QStringLiteral("subtab-top"),
        QStringLiteral("Transfers upper-pane list (0=downloads, 1=uploading, "
                       "2=downloading, 3=on queue, 4=known clients)."),
        QStringLiteral("index")};

    QCommandLineOption m_delayOption{
        QStringLiteral("delay"),
        QStringLiteral("Screenshot delay in milliseconds (default: 3000)."),
        QStringLiteral("ms"),
        QStringLiteral("3000")};

    QCommandLineOption m_optionsOption{
        QStringLiteral("options"),
        QStringLiteral("Open Options dialog at page (general, display, connection, ...)."),
        QStringLiteral("page")};

    QCommandLineOption m_configOption{
        QStringLiteral("config"),
        QStringLiteral("Override config directory (default: platform-specific)."),
        QStringLiteral("path")};

    QCommandLineOption m_newInstanceOption{
        QStringLiteral("new-instance"),
        QStringLiteral("Open another window even if one already runs for this "
                       "install and config dir.")};

    QCommandLineOption m_registerTypesOption{
        QStringLiteral("register-file-types"),
        QStringLiteral("Claim .nzb files for this executable, then exit.")};

    QCommandLineOption m_unregisterTypesOption{
        QStringLiteral("unregister-file-types"),
        QStringLiteral("Give up the .nzb file association, then exit.")};

    // Cached parsed values
    bool m_screenshotMode = false;
    bool m_newInstance = false;
    QString m_screenshotPath;
    int m_screenshotDelay = 3000;
    int m_optionsPage = -1;
    MainWindow::Tab m_activeTab = MainWindow::TabKad;
    bool m_hasTab = false;
    bool m_hasSubtab = false;
    int m_subtab = 0;
    bool m_hasSubtabTop = false;
    int m_subtabTop = 0;
    QStringList m_positionalArgs;
};

} // namespace eMule
