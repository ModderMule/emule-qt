#pragma once

/// @file CommandLineExec.h
/// @brief Command-line parsing and execution for the GUI application.

#include "MainWindow.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QString>

class QApplication;

namespace eMule {

class IpcClient;

class CommandLineExec {
public:
    /// Parse command-line arguments.
    void parse(QApplication& app);

    /// Whether --screenshot was given.
    [[nodiscard]] bool screenshotMode() const { return m_screenshotMode; }

    /// Screenshot output path (default: /tmp/emuleqt_screenshot.png).
    [[nodiscard]] const QString& screenshotPath() const { return m_screenshotPath; }

    /// Screenshot delay in milliseconds (default: 3000).
    [[nodiscard]] int screenshotDelay() const { return m_screenshotDelay; }

    /// Options page index, or -1 if not set.
    [[nodiscard]] int optionsPage() const { return m_optionsPage; }

    /// Apply --tab and --subtab to the main window.
    void applyTabArgs(MainWindow& mainWindow) const;

    /// Schedule screenshot capture (if --screenshot is set).
    void setupScreenshotTimer(QApplication& app, MainWindow& mainWindow) const;

    /// Handle ed2k:// positional arguments (delayed to allow IPC connection).
    void handleEd2kLinks(MainWindow& mainWindow, IpcClient& ipcClient) const;

private:
    QCommandLineParser m_parser;

    QCommandLineOption m_screenshotOption{
        QStringLiteral("screenshot"),
        QStringLiteral("Take a screenshot and exit (development aid)."),
        QStringLiteral("path"),
        QStringLiteral("/tmp/emuleqt_screenshot.png")};

    QCommandLineOption m_tabOption{
        QStringLiteral("tab"),
        QStringLiteral("Switch to tab on startup (kad, servers, transfers, search, shared, messages, irc, statistics)."),
        QStringLiteral("name")};

    QCommandLineOption m_subtabOption{
        QStringLiteral("subtab"),
        QStringLiteral("Sub-tab index within the active panel."),
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

    // Cached parsed values
    bool m_screenshotMode = false;
    QString m_screenshotPath;
    int m_screenshotDelay = 3000;
    int m_optionsPage = -1;
    MainWindow::Tab m_activeTab = MainWindow::TabKad;
    bool m_hasTab = false;
    bool m_hasSubtab = false;
    int m_subtab = 0;
    QStringList m_positionalArgs;
};

} // namespace eMule
