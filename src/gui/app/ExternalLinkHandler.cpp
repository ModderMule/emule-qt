#include "pch.h"
#include <QFileInfo>
#ifdef EMULE_HAVE_BITTORRENT
#include "panels/TorrentPanel.h"
#endif
#include "panels/UsenetPanel.h"
#include "panels/IrcPanel.h"
#include "app/ExternalLinkHandler.h"

#include "app/AppConfig.h"
#include "app/Ed3kNameOpener.h"
#include "dialogs/Ed3kSetsDialog.h"
#include "app/IpcClient.h"
#include "app/MainWindow.h"
#include "prefs/Preferences.h"
#include "utils/Ed2kLinkImporter.h"
#include "utils/Log.h"
#include "utils/TorrentLinks.h"

#include <QDesktopServices>
#include <QEvent>
#include <QFileOpenEvent>
#include <QMessageBox>
#include <QTimer>
#include <QPushButton>
#include <QUrl>
#include <QUrlQuery>

#include <utility>

namespace eMule {

ExternalLinkHandler::ExternalLinkHandler(QObject* parent)
    : QObject(parent)
{
}

void ExternalLinkHandler::setMainWindow(MainWindow* mainWindow)
{
    m_mainWindow = mainWindow;
    if (m_names)
        m_names->setDialogParent(mainWindow);
    flushPending();
}

void ExternalLinkHandler::setIpcClient(IpcClient* ipc)
{
    m_ipc = ipc;
    if (ipc) {
        // Also fires on every later reconnect, which costs nothing: with the queue empty
        // flushPending() returns immediately.
        connect(ipc, &IpcClient::connected, this, &ExternalLinkHandler::flushPending,
                Qt::UniqueConnection);
        connect(ipc, &IpcClient::ed3kBootstrapAsk, this, &ExternalLinkHandler::askBootstrapSource,
                Qt::UniqueConnection);
        // ed3k sets: a website that is none (name.md §6.1), a new version to take (file-set.md §5.3)
        // member slot: UniqueConnection asserts on a lambda
        connect(ipc, &IpcClient::ed3kSetOffered, this, &ExternalLinkHandler::onEd3kSetOffered,
                Qt::UniqueConnection);
        connect(ipc, &IpcClient::ed3kSetVersion, this, &ExternalLinkHandler::tellEd3kSetVersion, Qt::UniqueConnection);
        // ed3k:name links and names typed in the browser (name.md); its links come back here
        if (!m_names) {
            m_names = new Ed3kNameOpener(ipc, [this](const QString& link) { open(link); }, this);
            m_names->setDialogParent(m_mainWindow);
        }
    }
    flushPending();
}

void ExternalLinkHandler::open(const QString& link)
{
    if (link.isEmpty())
        return;

    // Only an import needs the daemon. The version-check sentinel and plain web links go
    // to the browser and must not wait behind a connection that may never come up.
    const bool needsDaemon = link.startsWith(QStringLiteral("ed2k:"), Qt::CaseInsensitive)
                             || link.startsWith(QStringLiteral("ed3k:"), Qt::CaseInsensitive)
                             || link.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive);
    if (needsDaemon && !canOpenNow()) {
        queueForLater({link, false});
        return;
    }

    openNow(link);
}

void ExternalLinkHandler::openFile(const QString& path)
{
    if (path.isEmpty())
        return;

    // No needsDaemon test, unlike open(): the file's bytes go to the daemon, so there
    // is nothing useful to do with one without it.
    if (!canOpenNow()) {
        queueForLater({path, true});
        return;
    }

    openFileNow(path);
}

bool ExternalLinkHandler::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FileOpen) {
        // The link is in file(), not url() — Ed2kLinkImporter::linkFromFileOpenEvent()
        // documents which and why. A dropped file yields nothing here and falls through.
        const QString link =
            Ed2kLinkImporter::linkFromFileOpenEvent(*static_cast<QFileOpenEvent*>(event));
        if (!link.isEmpty()) {
            // Off the Apple Event's call stack before anything modal can open.
            QTimer::singleShot(0, this, [this, link] { open(link); });
            return true;
        }

        // The fall-through the importer's header documents, which until now had
        // no consumer: a .nzb double-clicked in Finder arrives as a path here.
        // file(), never url() — an ed2k link comes back empty through url(), and
        // this code path has been bitten by that before.
        const QString path = static_cast<QFileOpenEvent*>(event)->file();
        if ((path.endsWith(QStringLiteral(".nzb"), Qt::CaseInsensitive)
             || (gui::kBitTorrentBuilt && gui::isTorrentFilePath(path)))
            && QFileInfo(path).isFile()) {
            // Off the Apple Event's call stack, then through the same queue a link
            // uses. Acting here directly dropped the file when this arrived during
            // the splash screen, with no main window yet.
            QTimer::singleShot(0, this, [this, path] { openFile(path); });
            return true;
        }
    }

#ifdef Q_OS_MACOS
    // Clicking the dock icon when the window is hidden should restore it. Elsewhere
    // any window of ours taking focus (MiniMule, a tray menu) would do the same.
    if (event->type() == QEvent::ApplicationActivate && m_mainWindow
        && !m_mainWindow->isVisible()) {
        m_mainWindow->restoreWindow();
    }
#endif

    return QObject::eventFilter(watched, event);
}

void ExternalLinkHandler::openNow(const QString& link)
{
    if (link == QStringLiteral("emuleqt:versioncheck")) {
        // The reference opens the version-check page in a browser rather than checking
        // in-place (CServerWnd::OnEnLinkServerBox — srchybrid/ServerWnd.cpp:682-695).
        // Tools -> Links -> Version Check is where the in-app manifest check lives.
        QDesktopServices::openUrl(QUrl(QString(kWebsiteUrl)));
        return;
    }

    const bool ed3k = link.startsWith(QStringLiteral("ed3k:"), Qt::CaseInsensitive);
    if (!ed3k && !link.startsWith(QStringLiteral("ed2k:"), Qt::CaseInsensitive)
        && !link.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)) {
        QDesktopServices::openUrl(QUrl::fromUserInput(link));
        return;
    }

#ifdef EMULE_HAVE_BITTORRENT
    // A BitTorrent magnet (btih/btmh, no eD2K hash) goes to the Torrents tab;
    // eD2K magnets, hybrid ones included, stay with the importer below.
    // Without BitTorrent the importer reports it as invalid.
    if (gui::isBitTorrentMagnet(link)) {
        TorrentPanel* panel = m_mainWindow ? m_mainWindow->torrentPanel() : nullptr;
        if (!panel) {
            logError(QStringLiteral("No Torrents panel to add a magnet link to"));
            return;
        }
        m_mainWindow->switchToTab(MainWindow::TabTorrents);
        if (thePrefs.bringToFrontOnLinkClick())
            m_mainWindow->restoreWindow();
        panel->addMagnets({link});
        return;
    }
#endif

    if (ed3k && link.startsWith(QStringLiteral("ed3k:nodes?"), Qt::CaseInsensitive)) {
        // a node list source (bootstrap.md §3): the daemon asks back (askBootstrapSource)
        if (!m_ipc) {
            logWarning(QStringLiteral("Cannot open the ed3k link, no core connection: %1").arg(link));
            return;
        }
        Ipc::IpcMessage msg(Ipc::IpcMsgType::OpenEd3kNodesLink);
        msg.append(link);
        m_ipc->sendRequest(std::move(msg), [link](const Ipc::IpcMessage& reply) {
            if (reply.type() == Ipc::IpcMsgType::Error)
                logWarning(QStringLiteral("ed3k node list link not taken (%1): %2").arg(reply.fieldString(1), link));
        });
        return;
    }
    if (ed3k && link.startsWith(QStringLiteral("ed3k:name?"), Qt::CaseInsensitive)) {
        // a readable name (name.md §1.3): resolved by the core, shown, then opened
        if (m_names)
            m_names->open(link);
        else
            logWarning(QStringLiteral("Cannot open the ed3k link, no core connection: %1").arg(link));
        return;
    }
    if (ed3k && link.startsWith(QStringLiteral("ed3k:room?"), Qt::CaseInsensitive)) {
        // a room (room.md §1.3): the IRC panel asks, joins and shows it. A link with s is a
        // credential: never logged
        IrcPanel* panel = m_mainWindow ? m_mainWindow->ircPanel() : nullptr;
        if (!panel) {
            logWarning(QStringLiteral("No IRC panel to open an ed3k room link in"));
            return;
        }
        m_mainWindow->switchToTab(MainWindow::TabIRC);
        if (thePrefs.bringToFrontOnLinkClick())
            m_mainWindow->restoreWindow();
        panel->openEd3kRoomLink(link);
        return;
    }
    if (ed3k && link.startsWith(QStringLiteral("ed3k:file?"), Qt::CaseInsensitive)) {
        // a plain file (the bt= ones went to the Torrents tab above): the daemon parses it
        // (parseFileLink) and downloads it (Ed3kDownloads), shown in Transfers
        if (!m_ipc) {
            logWarning(QStringLiteral("Cannot open the ed3k link, no core connection: %1").arg(link));
            return;
        }
        MainWindow* window = m_mainWindow;
        m_ipc->addEd3kDownloadLink(link, 0, thePrefs.addNewFilesPaused(), [window, link](const Ipc::IpcMessage& reply) {
            if (reply.type() != Ipc::IpcMsgType::Result || !reply.fieldBool(0)) {
                const QString why = reply.fieldMap(1).value(QStringLiteral("error")).toString();
                logWarning(QStringLiteral("ed3k file link not taken (%1): %2").arg(why, link));
                if (window)
                    QMessageBox::warning(window, tr("ed3k link"), why.isEmpty() ? tr("The ed3k link could not be added.") : why);
                return;
            }
            if (window) {
                window->switchToTab(MainWindow::TabTransfers);
                if (thePrefs.bringToFrontOnLinkClick())
                    window->restoreWindow();
            }
        });
        return;
    }
    if (ed3k && link.startsWith(QStringLiteral("ed3k:set?"), Qt::CaseInsensitive)) {
        // a set (file-set.md §5.4): asked first; a link with s is a credential: never logged
        openEd3kSet(link, false);
        return;
    }
    if (ed3k && link.startsWith(QStringLiteral("ed3k:live?"), Qt::CaseInsensitive)) {
        // a live channel (live.md §1.3): watched, played from the core's loopback
        watchEd3kLive(link);
        return;
    }
    if (ed3k) {
        // ToDo: open the other ed3k links (friend, nodes by hand, ...); was: room (done: IrcPanel);
        // was: file without bt= (done: Ed3kDownloads above); was: set and live (done: Ed3kSetsDialog,
        // openEd3kSet, watchEd3kLive)
        logWarning(QStringLiteral("Cannot open this ed3k link yet: %1").arg(link));
        return;
    }

    // Manual: the user clicked or typed this link, so a completed or cancelled file is a
    // genuine re-download request and is left alone.
    MainWindow* window = m_mainWindow;
    Ed2kLinkImporter::importLinks(
        link, m_ipc, window,
        Ed2kLinkImporter::Source::Manual,
        Ed2kLinkImporter::Prompt::Ask,
        [window](const Ed2kLinkImporter::Result& result) {
            if (window && result.added > 0)
                window->switchToTab(MainWindow::TabTransfers);
        },
        [window] {
            if (window && thePrefs.bringToFrontOnLinkClick()) {
                window->restoreWindow();
            }
        });
}

void ExternalLinkHandler::openFileNow(const QString& path)
{
#ifndef EMULE_HAVE_BITTORRENT
    if (gui::isTorrentFilePath(path)) {
        logError(QStringLiteral("Built without BitTorrent support, cannot add %1").arg(path));
        return;
    }
#else
    if (gui::isTorrentFilePath(path)) {
        TorrentPanel* torrents = m_mainWindow ? m_mainWindow->torrentPanel() : nullptr;
        if (!torrents) {
            logError(QStringLiteral("No Torrents panel to add %1 to").arg(path));
            return;
        }
        m_mainWindow->switchToTab(MainWindow::TabTorrents);
        torrents->addTorrentFiles({path});
        return;
    }
#endif

    UsenetPanel* panel = m_mainWindow ? m_mainWindow->usenetPanel() : nullptr;
    if (!panel) {
        logError(QStringLiteral("No Usenet panel to add %1 to").arg(path));
        return;
    }

    // Before the add, not after: the add is asynchronous and may come back asking
    // "download it again?", and that question wants the queue behind it.
    m_mainWindow->switchToTab(MainWindow::TabUsenet);
    panel->addNzbFile(path);
}

void ExternalLinkHandler::flushPending()
{
    if (m_pending.isEmpty() || !canOpenNow())
        return;

    m_pending.release([this](const PendingOpen& item) {
        if (item.isFile)
            openFileNow(item.value);
        else
            openNow(item.value);
    });
}

bool ExternalLinkHandler::canOpenNow() const
{
    return m_mainWindow && m_ipc && m_ipc->isConnected();
}

void ExternalLinkHandler::queueForLater(PendingOpen item)
{
    const QString what = item.isFile ? QStringLiteral("File") : QStringLiteral("Link");
    if (!m_pending.push(std::move(item))) {
        logWarning(QStringLiteral("ExternalLinkHandler: queue full, dropping %1")
                       .arg(what.toLower()));
        return;
    }
    logInfo(QStringLiteral("%1 received before the daemon was ready — queued (%2)")
                .arg(what).arg(m_pending.size()));
}

void ExternalLinkHandler::askBootstrapSource(const Ipc::IpcMessage& msg)
{
    const qint64 ask = msg.fieldInt(0);
    const QString host = msg.fieldString(1);
    // bootstrap.md §3: the user decides, shown the host name of the URL
    const auto answer = QMessageBox::question(
        m_mainWindow, tr("ed3k node list"),
        tr("A link wants to add a source of ed3k node lists:\n\n%1\n\n"
           "Node lists are used only to find the first ed3k nodes. Add this source?")
            .arg(host),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (!m_ipc)
        return;
    Ipc::IpcMessage reply(Ipc::IpcMsgType::AnswerEd3kBootstrapAsk);
    reply.append(ask);
    reply.append(answer == QMessageBox::Yes);
    m_ipc->sendRequest(std::move(reply));
}

void ExternalLinkHandler::openEd3kSet(const QString& link, bool noWebsite)
{
    if (!m_ipc) {
        logWarning(QStringLiteral("Cannot open the ed3k set link, no core connection"));
        return;
    }
    QPointer<MainWindow> window = m_mainWindow;
    const QString name = QUrlQuery(link.mid(link.indexOf(QLatin1Char('?')) + 1)).queryItemValue(QStringLiteral("dn"), QUrl::FullyDecoded);
    // file-set.md §2.3: nothing downloads before the user says so
    QMessageBox box(QMessageBox::Question, tr("ed3k set"),
                    noWebsite ? tr("This ed3k set is no website. Follow it and download its files?")
                              : tr("Follow this ed3k set%1?\n\nIts files go to the incoming folder, below a folder of the set's own; "
                                   "new versions are downloaded as they come.")
                                    .arg(name.isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(name)),
                    QMessageBox::NoButton, window);
    QPushButton* follow = box.addButton(tr("Follow and Download"), QMessageBox::AcceptRole);
    QPushButton* choose = box.addButton(tr("Choose Files..."), QMessageBox::ActionRole);
    QPushButton* site = noWebsite ? nullptr : box.addButton(tr("Open as Website"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(follow);
    box.exec();
    if (!m_ipc)
        return;
    if (site && box.clickedButton() == site) {
        // name.md §6.1: the website server follows it for the user; no website: it offers it back
        m_ipc->requestEd3kSetSite(link, [](const Ipc::IpcMessage& reply) {
            const QString url = reply.fieldMap(1).value(QStringLiteral("url")).toString();
            if (reply.fieldBool(0) && !url.isEmpty())
                QDesktopServices::openUrl(QUrl(url));
            else
                logWarning(QStringLiteral("ed3k: no website server to open the set in"));
        });
        return;
    }
    if (box.clickedButton() != follow && box.clickedButton() != choose)
        return;
    // choose: "ask" keeps the version for the dialog's selection
    const int policy = box.clickedButton() == follow ? 1 : 0;
    IpcClient* ipc = m_ipc;
    m_ipc->followEd3kSet(link, policy, [window, ipc, policy](const Ipc::IpcMessage& reply) {
        if (!reply.fieldBool(0)) {
            const QString why = reply.fieldMap(1).value(QStringLiteral("error")).toString();
            if (window)
                QMessageBox::warning(window, tr("ed3k set"), why.isEmpty() ? tr("The set could not be followed.") : why);
            return;
        }
        if (!window)
            return;
        if (thePrefs.bringToFrontOnLinkClick())
            window->restoreWindow();
        if (policy == 0)
            Ed3kSetsDialog::showDialog(window, ipc)->selectSet(reply.fieldMap(1).value(QStringLiteral("id")).toInteger());
        else
            window->switchToTab(MainWindow::TabTransfers);
    });
}

void ExternalLinkHandler::watchEd3kLive(const QString& link)
{
    if (!m_ipc) {
        logWarning(QStringLiteral("Cannot open the ed3k live link, no core connection"));
        return;
    }
    QPointer<MainWindow> window = m_mainWindow;
    IpcClient* ipc = m_ipc;
    m_ipc->watchEd3kLive(link, [window, ipc](const Ipc::IpcMessage& reply) {
        if (!reply.fieldBool(0)) {
            const QString why = reply.fieldMap(1).value(QStringLiteral("error")).toString();
            if (window)
                QMessageBox::warning(window, tr("ed3k live stream"), why.isEmpty() ? tr("The stream could not be watched.") : why);
            return;
        }
        // the stream is served on the core's loopback: a remote GUI only lists it
        const QString url = reply.fieldMap(1).value(QStringLiteral("url")).toString();
        if (!url.isEmpty() && ipc && ipc->isLocalConnection())
            QDesktopServices::openUrl(QUrl(url));
        if (window)
            Ed3kSetsDialog::showDialog(window, ipc, Ed3kSetsDialog::TabLive);
    });
}

void ExternalLinkHandler::onEd3kSetOffered(const Ipc::IpcMessage& msg)
{
    openEd3kSet(msg.fieldString(0), true);
}

void ExternalLinkHandler::tellEd3kSetVersion(const Ipc::IpcMessage& msg)
{
    const qint64 id = msg.fieldInt(0);
    const QString title = msg.fieldString(1);
    const qint64 seq = msg.fieldInt(2);
    const auto added = msg.fieldArray(3).size();
    const auto changed = msg.fieldArray(4).size();
    // non-modal: a new version is no reason to stop what the user does
    auto* box = new QMessageBox(QMessageBox::Information, tr("ed3k set"),
                                tr("Version %1 of the ed3k set \"%2\" is here: %3 new and %4 changed file(s).")
                                    .arg(seq)
                                    .arg(title)
                                    .arg(added)
                                    .arg(changed),
                                QMessageBox::NoButton, m_mainWindow);
    box->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton* show = box->addButton(tr("Show"), QMessageBox::AcceptRole);
    box->addButton(tr("Later"), QMessageBox::RejectRole);
    QPointer<MainWindow> window = m_mainWindow;
    IpcClient* ipc = m_ipc;
    connect(box, &QMessageBox::buttonClicked, this, [window, ipc, show, id](QAbstractButton* b) {
        if (b == show && window && ipc)
            Ed3kSetsDialog::showDialog(window, ipc)->selectSet(id);
    });
    box->open();
}

} // namespace eMule
