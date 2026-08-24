#include "pch.h"
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

namespace {

/// How many unopened links to keep. A cold start delivers one; anything beyond a handful
/// means something is feeding us links faster than the daemon can start, and stacking
/// confirmation dialogs behind each other helps nobody.
constexpr qsizetype kMaxPending = 16;

} // namespace

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
    if (needsDaemon && (!m_mainWindow || !m_ipc || !m_ipc->isConnected())) {
        if (m_pending.size() >= kMaxPending) {
            logWarning(QStringLiteral("ExternalLinkHandler: queue full, dropping link"));
            return;
        }
        // Deliberately not deduplicated: clicking the same link twice while the daemon
        // starts is a repeat request, and the importer's own known-file filter is what
        // decides whether it turns into anything.
        m_pending << link;
        logInfo(QStringLiteral("Link received before the daemon was ready — queued (%1)")
                    .arg(m_pending.size()));
        return;
    }

    openNow(link);
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

void ExternalLinkHandler::flushPending()
{
    if (m_pending.isEmpty() || !m_mainWindow || !m_ipc || !m_ipc->isConnected())
        return;

    // Taken before the loop: openNow() can re-enter through the event loop, and a link
    // released twice would prompt twice.
    const QStringList links = std::exchange(m_pending, {});
    for (const QString& link : links)
        openNow(link);
}

} // namespace eMule
