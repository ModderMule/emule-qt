#include "pch.h"
#include <QUrl>
#include "utils/FileAssociation.h"
#include "app/Ed2kSchemeHandler.h"
#include "app/MainWindow.h"
/// @file CommandLineExec.cpp
/// @brief Command-line parsing and execution for the GUI application.

#include "CommandLineExec.h"
#include "ExternalLinkHandler.h"
#include "app/IpcClient.h"
#include "dialogs/DetailDialog.h"
#include "dialogs/OptionsDialog.h"
#include "panels/KadPanel.h"
#include "panels/TransferPanel.h"
#ifdef EMULE_HAVE_BITTORRENT
#include "panels/TorrentPanel.h"
#endif
#include "utils/CommandLine.h"
#include "utils/Log.h"
#include "utils/TorrentLinks.h"

#include <QApplication>
#include <QDialog>
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
    m_parser.addOption(m_clientDetailsOption);
    m_parser.addOption(m_configOption);
    m_parser.addOption(m_newInstanceOption);
    m_afterUpdateOption.setFlags(QCommandLineOption::HiddenFromHelp);
    m_parser.addOption(m_afterUpdateOption);
    m_parser.addOption(m_registerTypesOption);
    m_parser.addOption(m_unregisterTypesOption);

    m_parser.addPositionalArgument(
        QStringLiteral("files"),
        QStringLiteral("ed2k://, ed3k: and magnet: links, .nzb and .torrent files to queue."),
        QStringLiteral("[ed2k://... | ed3k:... | magnet:... | file.nzb | file.torrent]"));

    processCommandLine(m_parser, app);

    // Cache parsed values
    m_screenshotMode = m_parser.isSet(m_screenshotOption);
    m_screenshotPath = m_parser.value(m_screenshotOption);
    m_newInstance = m_parser.isSet(m_newInstanceOption);
    m_afterUpdate = m_parser.isSet(m_afterUpdateOption);
    m_screenshotDelay = m_parser.value(m_delayOption).toInt();
    m_clientDetailsHash = m_parser.value(m_clientDetailsOption);
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
#ifdef EMULE_HAVE_BITTORRENT
            {QStringLiteral("torrents"),   MainWindow::TabTorrents},
#endif
#ifdef EMULE_HAVE_HOSTER_JD
            {QStringLiteral("hoster"),     MainWindow::TabHoster},
#endif
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
#ifdef EMULE_HAVE_BITTORRENT
            {QStringLiteral("bittorrent"),   OptionsDialog::PageBitTorrent},
            {QStringLiteral("btconnection"), OptionsDialog::PageBtConnection},
            {QStringLiteral("btbandwidth"),  OptionsDialog::PageBtConnection},
#endif
            {QStringLiteral("ed3k"),           OptionsDialog::PageEd3k},
            {QStringLiteral("ed3kconnection"), OptionsDialog::PageEd3kConnection},
            // {QStringLiteral("ed3kcredits"),    OptionsDialog::PageEd3kCredits},   // ToDo: page hidden for now
            {QStringLiteral("ed3khoster"),     OptionsDialog::PageEd3kHoster},
            {QStringLiteral("ed3knames"),      OptionsDialog::PageEd3kNames},
            {QStringLiteral("ed3kfilters"),    OptionsDialog::PageEd3kFilters},
            {QStringLiteral("ed3kcredits"),    OptionsDialog::PageEd3kAutoCredits},
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
#ifdef EMULE_HAVE_BITTORRENT
        else if (m_activeTab == MainWindow::TabTorrents && mainWindow.torrentPanel())
            mainWindow.torrentPanel()->switchToSubTab(m_subtab);
#endif
    }

    // After --subtab: the two Transfers panes cannot show the same list, and the top
    // pane wins that clash — so asking for both lands --subtab-top exactly as given.
    if (m_hasSubtabTop && m_activeTab == MainWindow::TabTransfers)
        mainWindow.transferPanel()->switchToTopView(m_subtabTop);
}

void CommandLineExec::setupScreenshotTimer(QApplication& app, MainWindow& mainWindow) const
{
    if (!m_clientDetailsHash.isEmpty()) {
        // Connect is async: open on the first connect (or now, when already up)
        IpcClient* ipc = mainWindow.ipcClient();
        const QString hash = m_clientDetailsHash;
        auto open = [&mainWindow, ipc, hash] { showClientDetails(&mainWindow, ipc, hash); };
        if (ipc && ipc->isConnected())
            open();
        else if (ipc)
            QObject::connect(ipc, &IpcClient::connected, &mainWindow, open,
                             Qt::SingleShotConnection);
    }
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
                // a GUI started from a shell is not frontmost: no active window, so
                // take a visible modeless dialog over the main window
                if (!target) {
                    for (QWidget* w : QApplication::topLevelWidgets())
                        if (w->isVisible() && qobject_cast<QDialog*>(w))
                            target = w;
                }
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

void CommandLineExec::handleOpenArguments(ExternalLinkHandler& linkHandler) const
{
    openArguments(m_positionalArgs, linkHandler);
}

void CommandLineExec::openArguments(const QStringList& args, ExternalLinkHandler& linkHandler)
{
    bool linkTaken = false;

    for (const QString& arg : args) {
        if (arg.startsWith(QStringLiteral("ed2k:"), Qt::CaseInsensitive)) {
            // Unchanged: still the first link only.
            if (!linkTaken) {
                linkHandler.open(arg);
                linkTaken = true;
            }
            continue;
        }

        // Every BitTorrent magnet and ed3k link; the handler routes a torrent one to
        // the Torrents tab (or, built without BitTorrent, reports it invalid)
        if (gui::isBitTorrentMagnet(arg) || arg.startsWith(QStringLiteral("ed3k:"), Qt::CaseInsensitive)) {
            linkHandler.open(arg);
            continue;
        }

        // A desktop launcher runs `Exec=... %U`, so the same argument arrives as
        // a file:// URL from one caller and a plain path from another.
        QString path = arg;
        if (path.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive))
            path = QUrl(path).toLocalFile();

        const bool isTorrent = gui::kBitTorrentBuilt && gui::isTorrentFilePath(path);
        if (!path.endsWith(QStringLiteral(".nzb"), Qt::CaseInsensitive) && !isTorrent)
            continue;
        if (!QFileInfo(path).isFile())
            continue;

        // Every .nzb / .torrent, not just the first: opening a selection of them is an
        // ordinary thing to do from a file manager.
        linkHandler.openFile(path);
    }
}

bool CommandLineExec::handleFileTypeRegistration() const
{
    const bool wantRegister = m_parser.isSet(m_registerTypesOption);
    const bool wantUnregister = m_parser.isSet(m_unregisterTypesOption);
    if (!wantRegister && !wantUnregister)
        return false;

    QString error;
    // ed2k/ed3k as they are: they have their own switch (Options → General)
    const bool ok = wantRegister ? gui::FileAssociation::applyFileTypes({true, true, isEd2kSchemeRegistered()}, error)
                                 : gui::FileAssociation::unregisterFileTypes(error);
    if (ok) {
        logInfo(wantRegister ? QStringLiteral("Registered .nzb, .torrent and magnet: with the desktop")
                             : QStringLiteral("Removed the file and link associations"));
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
