#include "pch.h"
#include <QUrl>
#include "utils/FileAssociation.h"
#include "panels/UsenetPanel.h"
#include "app/MainWindow.h"
/// @file CommandLineExec.cpp
/// @brief Command-line parsing and execution for the GUI application.

#include "CommandLineExec.h"
#include "ExternalLinkHandler.h"
#include "dialogs/OptionsDialog.h"
#include "panels/KadPanel.h"
#include "panels/TransferPanel.h"
#include "utils/Log.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QEventLoop>
#include <QPixmap>
#include <QTimer>

namespace eMule {

void CommandLineExec::parse(QApplication& app)
{
    m_parser.setApplicationDescription(QStringLiteral(
        "eMule Qt — P2P file sharing client with Qt GUI."));
    m_parser.addHelpOption();
    m_parser.addVersionOption();

    m_parser.addOption(m_screenshotOption);
    m_parser.addOption(m_tabOption);
    m_parser.addOption(m_subtabOption);
    m_parser.addOption(m_subtabTopOption);
    m_parser.addOption(m_delayOption);
    m_parser.addOption(m_optionsOption);
    m_parser.addOption(m_configOption);
    m_parser.addOption(m_registerTypesOption);
    m_parser.addOption(m_unregisterTypesOption);

    m_parser.addPositionalArgument(
        QStringLiteral("files"),
        QStringLiteral("ed2k:// links, and .nzb files to queue."),
        QStringLiteral("[ed2k://... | file.nzb]"));

    m_parser.process(app);

    // Cache parsed values
    m_screenshotMode = m_parser.isSet(m_screenshotOption);
    m_screenshotPath = m_parser.value(m_screenshotOption);
    m_screenshotDelay = m_parser.value(m_delayOption).toInt();
    m_positionalArgs = m_parser.positionalArguments();

    // Parse --tab
    if (m_parser.isSet(m_tabOption)) {
        m_hasTab = true;
        const QString tabArg = m_parser.value(m_tabOption).toLower();
        static const std::pair<QString, MainWindow::Tab> tabNames[] = {
            {QStringLiteral("kad"),        MainWindow::TabKad},
            {QStringLiteral("servers"),    MainWindow::TabServers},
            {QStringLiteral("transfers"),  MainWindow::TabTransfers},
            {QStringLiteral("search"),     MainWindow::TabSearch},
            {QStringLiteral("shared"),     MainWindow::TabSharedFiles},
            {QStringLiteral("messages"),   MainWindow::TabMessages},
            {QStringLiteral("irc"),        MainWindow::TabIRC},
            {QStringLiteral("statistics"), MainWindow::TabStatistics},
            {QStringLiteral("usenet"),     MainWindow::TabUsenet},
        };
        m_activeTab = static_cast<MainWindow::Tab>(tabArg.toInt());
        for (const auto& [name, value] : tabNames) {
            if (tabArg == name) {
                m_activeTab = value;
                break;
            }
        }
    }

    // Parse --subtab
    if (m_parser.isSet(m_subtabOption)) {
        m_hasSubtab = true;
        m_subtab = m_parser.value(m_subtabOption).toInt();
    }

    // Parse --subtab-top
    if (m_parser.isSet(m_subtabTopOption)) {
        m_hasSubtabTop = true;
        m_subtabTop = m_parser.value(m_subtabTopOption).toInt();
    }

    // Parse --options
    if (m_parser.isSet(m_optionsOption)) {
        const QString optArg = m_parser.value(m_optionsOption).toLower();
        static const std::pair<QString, int> pageNames[] = {
            {QStringLiteral("general"),      OptionsDialog::PageGeneral},
            {QStringLiteral("display"),      OptionsDialog::PageDisplay},
            {QStringLiteral("connection"),   OptionsDialog::PageConnection},
            {QStringLiteral("proxy"),        OptionsDialog::PageProxy},
            {QStringLiteral("server"),       OptionsDialog::PageServer},
            {QStringLiteral("directories"),  OptionsDialog::PageDirectories},
            {QStringLiteral("files"),        OptionsDialog::PageFiles},
            {QStringLiteral("notifications"),OptionsDialog::PageNotifications},
            {QStringLiteral("statistics"),   OptionsDialog::PageStatistics},
            {QStringLiteral("irc"),          OptionsDialog::PageIRC},
            {QStringLiteral("messages"),     OptionsDialog::PageMessages},
            {QStringLiteral("security"),     OptionsDialog::PageSecurity},
            {QStringLiteral("scheduler"),    OptionsDialog::PageScheduler},
            {QStringLiteral("webinterface"), OptionsDialog::PageWebInterface},
            {QStringLiteral("usenet"),       OptionsDialog::PageUsenet},
            {QStringLiteral("indexers"),     OptionsDialog::PageIndexers},
            {QStringLiteral("feeds"),        OptionsDialog::PageFeeds},
            {QStringLiteral("extended"),     OptionsDialog::PageExtended},
        };
        m_optionsPage = optArg.toInt(); // fallback: numeric index
        for (const auto& [name, value] : pageNames) {
            if (optArg == name) {
                m_optionsPage = value;
                break;
            }
        }
    }
}

void CommandLineExec::applyTabArgs(MainWindow& mainWindow) const
{
    if (!m_hasTab)
        return;

    mainWindow.switchToTab(m_activeTab);

    if (m_hasSubtab) {
        if (m_activeTab == MainWindow::TabKad)
            mainWindow.kadPanel()->switchToSubTab(m_subtab);
        else if (m_activeTab == MainWindow::TabTransfers)
            mainWindow.transferPanel()->switchToSubTab(m_subtab);
    }

    // After --subtab: the two Transfers panes cannot show the same list, and the top
    // pane wins that clash — so asking for both lands --subtab-top exactly as given.
    if (m_hasSubtabTop && m_activeTab == MainWindow::TabTransfers)
        mainWindow.transferPanel()->switchToTopView(m_subtabTop);
}

void CommandLineExec::setupScreenshotTimer(QApplication& app, MainWindow& mainWindow) const
{
    if (m_screenshotMode) {
        const QString path = m_screenshotPath;
        const int optPage = m_optionsPage;
        QTimer::singleShot(m_screenshotDelay, &app, [&mainWindow, path, &app, optPage]() {
            QPixmap pixmap;
            if (optPage >= 0) {
                // The live client, not nullptr: the dialog's daemon-owned lists
                // (news servers, indexers) load over IPC, and without one every
                // screenshot of those pages shows an empty table.
                OptionsDialog dlg(mainWindow.ipcClient(), mainWindow.statisticsPanel(),
                                  &mainWindow);
                dlg.selectPage(optPage);
                dlg.show();

                // Let the daemon answer before grabbing. The dialog's
                // daemon-owned lists (news servers, indexers) load over IPC and
                // fill in from a *callback*; a single processEvents() returns
                // long before the round trip completes, and the page shoots
                // empty however much is configured.
                QEventLoop settle;
                QTimer::singleShot(750, &settle, &QEventLoop::quit);
                settle.exec();

                dlg.repaint();
                QApplication::processEvents();
                pixmap = dlg.grab();
            } else {
                // A dialog is its own top-level window, so grabbing the main window
                // would miss it and the shot would look like nothing happened.
                // Prefer whatever is blocking input, then whatever has focus — the
                // detail dialogs are modeless, so only the latter finds them.
                QWidget* target = QApplication::activeModalWidget();
                if (!target)
                    target = QApplication::activeWindow();
                if (!target)
                    target = &mainWindow;
                target->repaint();
                QApplication::processEvents();
                pixmap = target->grab();
            }
            // grab() returns by value, so the dialog above may already be gone.
            app.exit(saveScreenshot(pixmap, path) ? 0 : 1);
        });
    } else if (m_optionsPage >= 0) {
        mainWindow.showOptionsDialog(m_optionsPage);
    }
}

void CommandLineExec::handleOpenArguments(ExternalLinkHandler& linkHandler,
                                          MainWindow& mainWindow) const
{
    bool linkTaken = false;

    for (const QString& arg : m_positionalArgs) {
        if (arg.startsWith(QStringLiteral("ed2k:"), Qt::CaseInsensitive)) {
            // Unchanged: still the first link only.
            if (!linkTaken) {
                linkHandler.open(arg);
                linkTaken = true;
            }
            continue;
        }

        // A desktop launcher runs `Exec=... %U`, so the same argument arrives as
        // a file:// URL from one caller and a plain path from another.
        QString path = arg;
        if (path.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive))
            path = QUrl(path).toLocalFile();

        if (!path.endsWith(QStringLiteral(".nzb"), Qt::CaseInsensitive))
            continue;
        if (!QFileInfo(path).isFile())
            continue;

        // Every .nzb, not just the first: opening a selection of them is an
        // ordinary thing to do from a file manager.
        if (auto* panel = mainWindow.usenetPanel())
            panel->addNzbFile(path);
    }
}

bool CommandLineExec::handleFileTypeRegistration() const
{
    const bool wantRegister = m_parser.isSet(m_registerTypesOption);
    const bool wantUnregister = m_parser.isSet(m_unregisterTypesOption);
    if (!wantRegister && !wantUnregister)
        return false;

    QString error;
    const bool ok = wantRegister ? gui::FileAssociation::registerNzbFileType(error)
                                 : gui::FileAssociation::unregisterNzbFileType(error);
    if (ok) {
        logInfo(wantRegister ? QStringLiteral("Registered .nzb with the desktop")
                             : QStringLiteral("Removed the .nzb file association"));
    } else {
        logError(QStringLiteral("File-type registration failed: %1").arg(error));
    }
    return true;
}

QString CommandLineExec::configOverride() const
{
    if (m_parser.isSet(m_configOption))
        return m_parser.value(m_configOption);
    return {};
}

bool CommandLineExec::saveScreenshot(const QPixmap& pixmap, const QString& path)
{
    if (pixmap.isNull()) {
        logError(QStringLiteral("Screenshot: nothing captured, not writing %1").arg(path));
        return false;
    }

    const QString dir = QFileInfo(path).absolutePath();
    if (!dir.isEmpty() && !QDir().mkpath(dir)) {
        logError(QStringLiteral("Screenshot: cannot create directory %1").arg(dir));
        return false;
    }

    // save() gives no reason, so name the two likely ones.
    if (!pixmap.save(path)) {
        logError(QStringLiteral("Screenshot: failed to write %1 "
                                "(unsupported extension or path not writable)").arg(path));
        return false;
    }

    logInfo(QStringLiteral("Screenshot saved to %1").arg(path));
    return true;
}

} // namespace eMule
