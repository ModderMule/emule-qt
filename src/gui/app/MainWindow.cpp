#include "app/MainWindow.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "IpcProtocol.h"
#include "dialogs/NetworkInfoDialog.h"
#include "dialogs/OptionsDialog.h"
#include "panels/IrcPanel.h"
#include "panels/KadPanel.h"
#include "panels/MessagesPanel.h"
#include "panels/SearchPanel.h"
#include "panels/ServerPanel.h"
#include "panels/SharedFilesPanel.h"
#include "panels/TransferPanel.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "protocol/ED2KLink.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPainterPath>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace eMule {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("eMule Qt v%1").arg(QApplication::applicationVersion()));

    setupPages();
    setupToolbar();
    setupStatusBar();

    switchToTab(TabKad);

    // Restore saved window size (default 900×620 if no saved state).
    theUiState.bindMainWindow(this);

    // Clipboard monitoring — prompt user when ed2k file links are copied (MFC SearchClipboard)
    connect(QApplication::clipboard(), &QClipboard::dataChanged,
            this, &MainWindow::onClipboardChanged);
}

MainWindow::~MainWindow() = default;

void MainWindow::switchToTab(Tab tab)
{
    if (tab < 0 || tab >= TabCount)
        return;
    m_pages->setCurrentIndex(tab);

    // Update the toolbar button to match
    for (auto* action : m_tabGroup->actions()) {
        if (action->data().toInt() == tab) {
            action->setChecked(true);
            break;
        }
    }
}

void MainWindow::onToolbarAction(QAction* action)
{
    const int idx = action->data().toInt();
    if (idx >= 0 && idx < TabCount)
        m_pages->setCurrentIndex(idx);
}

void MainWindow::onConnectToggle()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    const bool anyConnected = m_ed2kConnected || m_kadRunning;

    if (anyConnected) {
        // Disconnect only what is actually connected
        if (m_ed2kConnected) {
            Ipc::IpcMessage req(Ipc::IpcMsgType::DisconnectFromServer);
            m_ipc->sendRequest(std::move(req));
        }
        if (m_kadRunning) {
            Ipc::IpcMessage req(Ipc::IpcMsgType::DisconnectKad);
            m_ipc->sendRequest(std::move(req));
        }
    } else {
        // Connect both eD2K and Kad
        Ipc::IpcMessage reqEd2k(Ipc::IpcMsgType::ConnectToServer);
        m_ipc->sendRequest(std::move(reqEd2k));

        Ipc::IpcMessage reqKad(Ipc::IpcMsgType::BootstrapKad);
        m_ipc->sendRequest(std::move(reqKad));
    }
}

void MainWindow::showOptionsDialog(int page)
{
    OptionsDialog dlg(m_ipc, this);
    if (page >= 0 && page < OptionsDialog::PageCount)
        dlg.selectPage(page);
    dlg.exec();
}

void MainWindow::onOptionsClicked()
{
    showOptionsDialog();
}

void MainWindow::setEd2kStatus(bool connected, bool connecting, bool firewalled)
{
    m_ed2kConnected = connected;
    m_ed2kFirewalled = firewalled;

    if (connected) {
        const QString label = firewalled
            ? tr("eD2K: Connected (LowID)")
            : tr("eD2K: Connected");
        m_statusEd2k->setText(label);
        m_statusEd2k->setStyleSheet(QStringLiteral("color: green;"));
        m_connStatus->setEd2kState(firewalled
            ? ConnectionStatusWidget::Firewalled
            : ConnectionStatusWidget::Connected);
    } else if (connecting) {
        m_statusEd2k->setText(tr("eD2K: Connecting..."));
        m_statusEd2k->setStyleSheet(QStringLiteral("color: orange;"));
        m_connStatus->setEd2kState(ConnectionStatusWidget::Firewalled);
    } else {
        m_statusEd2k->setText(tr("eD2K: Disconnected"));
        m_statusEd2k->setStyleSheet(QString{});
        m_connStatus->setEd2kState(ConnectionStatusWidget::Disconnected);
    }
    updateConnectButton();
}

void MainWindow::setKadStatus(bool running, bool kadConnected, bool firewalled)
{
    m_kadRunning = running;
    m_kadConnected = kadConnected;
    m_kadFirewalled = firewalled;

    if (kadConnected && !firewalled) {
        m_statusKad->setText(tr("Kad: Connected"));
        m_statusKad->setStyleSheet(QStringLiteral("color: green;"));
        m_connStatus->setKadState(ConnectionStatusWidget::Connected);
    } else if (kadConnected && firewalled) {
        m_statusKad->setText(tr("Kad: Connected (Firewalled)"));
        m_statusKad->setStyleSheet(QStringLiteral("color: orange;"));
        m_connStatus->setKadState(ConnectionStatusWidget::Firewalled);
    } else if (running) {
        m_statusKad->setText(tr("Kad: Connecting..."));
        m_statusKad->setStyleSheet(QStringLiteral("color: orange;"));
        m_connStatus->setKadState(ConnectionStatusWidget::Firewalled);
    } else {
        m_statusKad->setText(tr("Kad: Disconnected"));
        m_statusKad->setStyleSheet(QString{});
        m_connStatus->setKadState(ConnectionStatusWidget::Disconnected);
    }
    updateConnectButton();
}

void MainWindow::setNetworkStats(quint32 users, quint32 files)
{
    m_statusUsersLabel->setText(tr("Users: %1 | Files: %2")
        .arg(QLocale().toString(users))
        .arg(QLocale().toString(files)));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    theUiState.captureMainWindow(this);
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (obj == m_statusEd2k || obj == m_statusKad || obj == m_statusUsersWidget) {
            showNetworkInfo();
            return true;
        }
        if (obj == m_statusUpDownWidget) {
            switchToTab(TabStatistics);
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// ---------------------------------------------------------------------------
// Clipboard monitoring — prompt user when ed2k file links are copied
// ---------------------------------------------------------------------------

void MainWindow::onClipboardChanged()
{
    const QString text = QApplication::clipboard()->text().trimmed();
    if (text.isEmpty() || text == m_lastClipboardContents)
        return;
    m_lastClipboardContents = text;

    // Check if any line contains an ed2k file link
    if (!text.contains(QStringLiteral("ed2k://|file|"), Qt::CaseInsensitive))
        return;

    if (!m_ipc || !m_ipc->isConnected())
        return;

    // Build preview of file links for the prompt
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QStringList fileNames;
    for (const QString& line : lines) {
        auto parsed = parseED2KLink(line.trimmed());
        if (!parsed)
            continue;
        if (auto* fileLink = std::get_if<ED2KFileLink>(&*parsed))
            fileNames << fileLink->name;
    }
    if (fileNames.isEmpty())
        return;

    const QString preview = fileNames.join(QLatin1Char('\n'));
    const auto result = QMessageBox::question(
        this, tr("eD2K Link"),
        tr("Do you want to download the following file(s)?\n\n%1").arg(preview),
        QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes)
        return;

    // Download each file link via IPC
    for (const QString& line : lines) {
        auto parsed = parseED2KLink(line.trimmed());
        if (!parsed)
            continue;
        auto* fileLink = std::get_if<ED2KFileLink>(&*parsed);
        if (!fileLink)
            continue;

        QString hashHex;
        for (uint8 b : fileLink->hash)
            hashHex += QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0'));

        Ipc::IpcMessage msg(Ipc::IpcMsgType::DownloadSearchFile);
        msg.append(hashHex);
        msg.append(fileLink->name);
        msg.append(static_cast<int64_t>(fileLink->size));
        m_ipc->sendRequest(std::move(msg));
    }

    switchToTab(TabTransfers);
}

// ---------------------------------------------------------------------------
// Private setup helpers
// ---------------------------------------------------------------------------

void MainWindow::setupToolbar()
{
    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName(QStringLiteral("MainToolbar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    toolbar->setIconSize(QSize(32, 32));

    // Connect/Disconnect button (not part of tab group)
    m_connectAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_MediaStop),
        tr("Disconnect"));
    connect(m_connectAction, &QAction::triggered, this, &MainWindow::onConnectToggle);

    toolbar->addSeparator();

    // Tab buttons
    m_tabGroup = new QActionGroup(this);
    m_tabGroup->setExclusive(true);
    connect(m_tabGroup, &QActionGroup::triggered, this, &MainWindow::onToolbarAction);

    struct TabDef {
        const char* label;
        QStyle::StandardPixmap icon;
        Tab tab;
    };

    // ToDo: Replace StandardPixmap icons with proper eMule icons (Module 27)
    static constexpr TabDef tabs[] = {
        {"Kad",          QStyle::SP_DriveNetIcon,       TabKad},
        {"Servers",      QStyle::SP_ComputerIcon,       TabServers},
        {"Transfers",    QStyle::SP_ArrowDown,          TabTransfers},
        {"Search",       QStyle::SP_FileDialogContentsView, TabSearch},
        {"Shared Files", QStyle::SP_DirOpenIcon,        TabSharedFiles},
        {"Messages",     QStyle::SP_MessageBoxInformation, TabMessages},
        {"IRC",          QStyle::SP_DialogApplyButton,  TabIRC},
        {"Statistics",   QStyle::SP_DialogHelpButton,   TabStatistics},
    };

    for (const auto& [label, icon, tab] : tabs) {
        auto* action = toolbar->addAction(style()->standardIcon(icon),
                                          tr(label));
        action->setCheckable(true);
        action->setData(static_cast<int>(tab));
        m_tabGroup->addAction(action);
        if (tab == TabKad)
            action->setChecked(true);
    }

    toolbar->addSeparator();

    // Options, Tools, Help (non-tab actions)
    auto* optionsAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView),
        tr("Options"));
    connect(optionsAction, &QAction::triggered, this, &MainWindow::onOptionsClicked);

    auto* toolsAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_FileDialogListView),
        tr("Tools"));
    Q_UNUSED(toolsAction);
    // ToDo: Open tools menu

    auto* helpAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_TitleBarContextHelpButton),
        tr("Help"));
    Q_UNUSED(helpAction);
    // ToDo: Open help
}

void MainWindow::updateConnectButton()
{
    const bool anyConnected = m_ed2kConnected || m_kadRunning;
    if (anyConnected) {
        m_connectAction->setText(tr("Disconnect"));
        m_connectAction->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    } else {
        m_connectAction->setText(tr("Connect"));
        m_connectAction->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    }
}

void MainWindow::showNetworkInfo()
{
    NetworkInfoDialog dlg(m_ipc, this);
    dlg.exec();
}

void MainWindow::setupStatusBar()
{
    auto* sb = statusBar();

    m_statusMsg = new QLabel(tr("Ready"), this);
    sb->addWidget(m_statusMsg, 1);

    // Users/Files indicator with green person icon (matching MFC status bar)
    m_statusUsersWidget = new QWidget(this);
    auto* usersLayout = new QHBoxLayout(m_statusUsersWidget);
    usersLayout->setContentsMargins(0, 0, 0, 0);
    usersLayout->setSpacing(2);

    auto* usersIcon = new QLabel;
    usersIcon->setFixedSize(16, 16);
    usersIcon->setScaledContents(true);
    usersIcon->setPixmap(QIcon(QStringLiteral(":/icons/User.ico")).pixmap(16, 16));
    usersLayout->addWidget(usersIcon);

    m_statusUsersLabel = new QLabel(tr("Users: 0 | Files: 0"));
    usersLayout->addWidget(m_statusUsersLabel);

    m_statusUsersWidget->installEventFilter(this);
    sb->addPermanentWidget(m_statusUsersWidget);

    // Up/Down speed indicator with red/green arrow icons (matching MFC status bar)
    m_statusUpDownWidget = new QWidget(this);
    auto* udLayout = new QHBoxLayout(m_statusUpDownWidget);
    udLayout->setContentsMargins(0, 0, 0, 0);
    udLayout->setSpacing(2);

    auto* upIcon = new QLabel;
    upIcon->setFixedSize(16, 16);
    upIcon->setScaledContents(true);
    upIcon->setPixmap(QIcon(QStringLiteral(":/icons/Upload.ico")).pixmap(16, 16));
    udLayout->addWidget(upIcon);

    m_statusUpLabel = new QLabel(tr("Up: 0.0"));
    udLayout->addWidget(m_statusUpLabel);

    udLayout->addWidget(new QLabel(QStringLiteral("|")));

    auto* downIcon = new QLabel;
    downIcon->setFixedSize(16, 16);
    downIcon->setScaledContents(true);
    downIcon->setPixmap(QIcon(QStringLiteral(":/icons/Download.ico")).pixmap(16, 16));
    udLayout->addWidget(downIcon);

    m_statusDownLabel = new QLabel(tr("Down: 0.0"));
    udLayout->addWidget(m_statusDownLabel);

    m_statusUpDownWidget->installEventFilter(this);
    sb->addPermanentWidget(m_statusUpDownWidget);

    // World icon with eD2K/Kad connection arrows
    m_connStatus = new ConnectionStatusWidget(this);
    m_connStatus->setToolTip(tr("Double-click for Network Information"));
    connect(m_connStatus, &ConnectionStatusWidget::doubleClicked,
            this, &MainWindow::showNetworkInfo);
    sb->addPermanentWidget(m_connStatus);

    m_statusEd2k = new QLabel(tr("eD2K: Disconnected"), this);
    m_statusEd2k->installEventFilter(this);
    sb->addPermanentWidget(m_statusEd2k);

    m_statusKad = new QLabel(tr("Kad: Disconnected"), this);
    m_statusKad->installEventFilter(this);
    sb->addPermanentWidget(m_statusKad);
}

void MainWindow::setupPages()
{
    m_pages = new QStackedWidget(this);
    setCentralWidget(m_pages);

    // Tab 0: Kad
    m_kadPanel = new KadPanel(this);
    m_pages->addWidget(m_kadPanel);

    // Tab 1: Servers
    m_serverPanel = new ServerPanel(this);
    m_pages->addWidget(m_serverPanel);

    // Tab 2: Transfers
    m_transferPanel = new TransferPanel(this);
    m_pages->addWidget(m_transferPanel);

    // Tab 3: Search
    m_searchPanel = new SearchPanel(this);
    m_pages->addWidget(m_searchPanel);

    // Tab 4: Shared Files
    m_sharedFilesPanel = new SharedFilesPanel(this);
    m_pages->addWidget(m_sharedFilesPanel);

    // Tab 5: Messages
    m_messagesPanel = new MessagesPanel(this);
    m_pages->addWidget(m_messagesPanel);

    // Tab 6: IRC
    m_ircPanel = new IrcPanel(this);
    m_pages->addWidget(m_ircPanel);

    // Placeholder widget for unimplemented Statistics tab
    {
        auto* placeholder = new QWidget(this);
        auto* layout = new QVBoxLayout(placeholder);
        auto* label = new QLabel(tr("Not implemented yet"), placeholder);
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
        m_pages->addWidget(placeholder);
    }
}

// ---------------------------------------------------------------------------
// ConnectionStatusWidget — world icon with eD2K/Kad connection arrows
// ---------------------------------------------------------------------------

ConnectionStatusWidget::ConnectionStatusWidget(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(36, 18);
}

void ConnectionStatusWidget::setEd2kState(State s)
{
    if (m_ed2kState != s) {
        m_ed2kState = s;
        update();
    }
}

void ConnectionStatusWidget::setKadState(State s)
{
    if (m_kadState != s) {
        m_kadState = s;
        update();
    }
}

QColor ConnectionStatusWidget::colorForState(State s)
{
    switch (s) {
    case Connected:    return QColor(0x00, 0xAA, 0x00); // green
    case Firewalled:   return QColor(0xDD, 0xAA, 0x00); // yellow/amber
    case Disconnected: return QColor(0xCC, 0x22, 0x22); // red
    }
    return QColor(0xCC, 0x22, 0x22);
}

void ConnectionStatusWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int w = width();
    const int h = height();
    const int cx = w / 2;
    const int cy = h / 2;
    const int r = qMin(w, h) / 2 - 2;

    // Draw world globe (simple circle with grid lines)
    p.setPen(QPen(QColor(0x55, 0x77, 0xBB), 1.2));
    p.setBrush(QColor(0xCC, 0xDD, 0xEE));
    p.drawEllipse(QPoint(cx, cy), r, r);

    // Horizontal and vertical grid lines
    p.setPen(QPen(QColor(0x88, 0xAA, 0xCC), 0.6));
    p.drawLine(cx - r, cy, cx + r, cy);
    p.drawLine(cx, cy - r, cx, cy + r);

    // Draw ellipse arcs for the globe effect
    p.drawEllipse(QRectF(cx - r * 0.4, cy - r, r * 0.8, r * 2.0));

    // Left arrow (eD2K) — pointing down-left
    const QColor ed2kColor = colorForState(m_ed2kState);
    p.setPen(QPen(ed2kColor, 2.0, Qt::SolidLine, Qt::RoundCap));
    // Arrow shaft going down from top-left of globe
    const int ax1 = cx - r - 3;
    const int ay1 = cy - 3;
    const int ax2 = cx - r - 3;
    const int ay2 = cy + 5;
    p.drawLine(ax1, ay1, ax2, ay2);
    // Arrowhead (pointing down)
    p.drawLine(ax2, ay2, ax2 - 2, ay2 - 3);
    p.drawLine(ax2, ay2, ax2 + 2, ay2 - 3);

    // Right arrow (Kad) — pointing up from bottom-right of globe
    const QColor kadColor = colorForState(m_kadState);
    p.setPen(QPen(kadColor, 2.0, Qt::SolidLine, Qt::RoundCap));
    const int bx1 = cx + r + 3;
    const int by1 = cy + 3;
    const int bx2 = cx + r + 3;
    const int by2 = cy - 5;
    p.drawLine(bx1, by1, bx2, by2);
    // Arrowhead (pointing up)
    p.drawLine(bx2, by2, bx2 - 2, by2 + 3);
    p.drawLine(bx2, by2, bx2 + 2, by2 + 3);
}

void ConnectionStatusWidget::mouseDoubleClickEvent(QMouseEvent* /*event*/)
{
    emit doubleClicked();
}

} // namespace eMule
