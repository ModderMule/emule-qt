/// @file CommandLineExec.cpp
/// @brief Command-line parsing and execution for the GUI application.

#include "CommandLineExec.h"
#include "IpcClient.h"
#include "dialogs/OptionsDialog.h"
#include "panels/KadPanel.h"
#include "panels/TransferPanel.h"
#include "utils/Log.h"

#include "IpcMessage.h"
#include "protocol/ED2KLink.h"

#include <QApplication>
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
    m_parser.addOption(m_delayOption);
    m_parser.addOption(m_optionsOption);

    m_parser.addPositionalArgument(
        QStringLiteral("links"),
        QStringLiteral("ed2k:// links to download."),
        QStringLiteral("[ed2k://...]"));

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
}

void CommandLineExec::setupScreenshotTimer(QApplication& app, MainWindow& mainWindow) const
{
    if (m_screenshotMode) {
        const QString path = m_screenshotPath;
        const int optPage = m_optionsPage;
        QTimer::singleShot(m_screenshotDelay, &app, [&mainWindow, path, &app, optPage]() {
            if (optPage >= 0) {
                OptionsDialog dlg(nullptr, mainWindow.statisticsPanel(), &mainWindow);
                dlg.selectPage(optPage);
                dlg.show();
                dlg.repaint();
                QApplication::processEvents();
                QPixmap pixmap = dlg.grab();
                pixmap.save(path);
            } else {
                mainWindow.repaint();
                QApplication::processEvents();
                QPixmap pixmap = mainWindow.grab();
                pixmap.save(path);
            }
            logInfo(QStringLiteral("Screenshot saved to %1").arg(path));
            app.quit();
        });
    } else if (m_optionsPage >= 0) {
        mainWindow.showOptionsDialog(m_optionsPage);
    }
}

void CommandLineExec::handleEd2kLinks(MainWindow& mainWindow, IpcClient& ipcClient) const
{
    for (const QString& arg : m_positionalArgs) {
        if (arg.startsWith(QStringLiteral("ed2k://"), Qt::CaseInsensitive)) {
            QTimer::singleShot(3000, &mainWindow, [arg, &mainWindow, &ipcClient]() {
                // Inline the ed2k handling — same as handleEd2kUrl in main.cpp
                if (!ipcClient.isConnected())
                    return;

                auto parsed = parseED2KLink(arg.trimmed());
                if (!parsed) return;
                auto* fl = std::get_if<ED2KFileLink>(&*parsed);
                if (!fl) return;

                QString hashHex;
                for (uint8_t b : fl->hash)
                    hashHex += QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0'));

                Ipc::IpcMessage msg(Ipc::IpcMsgType::DownloadSearchFile);
                msg.append(hashHex);
                msg.append(fl->name);
                msg.append(static_cast<int64_t>(fl->size));
                ipcClient.sendRequest(std::move(msg));
                mainWindow.switchToTab(MainWindow::TabTransfers);
            });
            break;
        }
    }
}

} // namespace eMule
