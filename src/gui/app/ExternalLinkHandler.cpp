#include "pch.h"
#include <QFileInfo>
#include "panels/UsenetPanel.h"
#include "app/ExternalLinkHandler.h"

#include "app/AppConfig.h"
#include "app/IpcClient.h"
#include "app/MainWindow.h"
#include "prefs/Preferences.h"
#include "utils/Ed2kLinkImporter.h"
#include "utils/Log.h"

#include <QDesktopServices>
#include <QEvent>
#include <QFileOpenEvent>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace eMule {

ExternalLinkHandler::ExternalLinkHandler(QObject* parent)
    : QObject(parent)
{
}

void ExternalLinkHandler::setMainWindow(MainWindow* mainWindow)
{
    m_mainWindow = mainWindow;
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
        if (path.endsWith(QStringLiteral(".nzb"), Qt::CaseInsensitive)
            && QFileInfo(path).isFile()) {
            // Off the Apple Event's call stack, then through the same queue a link
            // uses. Acting here directly dropped the file when this arrived during
            // the splash screen, with no main window yet.
            QTimer::singleShot(0, this, [this, path] { openFile(path); });
            return true;
        }
    }

    // macOS: clicking the dock icon when the window is hidden should restore it.
    if (event->type() == QEvent::ApplicationActivate && m_mainWindow
        && !m_mainWindow->isVisible()) {
        m_mainWindow->showNormal();
        m_mainWindow->raise();
        m_mainWindow->activateWindow();
    }

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

    if (!link.startsWith(QStringLiteral("ed2k:"), Qt::CaseInsensitive)
        && !link.startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive)) {
        QDesktopServices::openUrl(QUrl::fromUserInput(link));
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
                window->showNormal();
                window->raise();
                window->activateWindow();
            }
        });
}

void ExternalLinkHandler::openFileNow(const QString& path)
{
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

} // namespace eMule
