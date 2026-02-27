#include "app/MainWindow.h"

#include "app/UiState.h"
#include "panels/KadPanel.h"
#include "panels/ServerPanel.h"
#include "panels/TransferPanel.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QLabel>
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
}

MainWindow::~MainWindow() = default;

void MainWindow::switchToTab(Tab tab)
{
    if (tab >= 0 && tab < TabCount)
        m_pages->setCurrentIndex(tab);
}

void MainWindow::onToolbarAction(QAction* action)
{
    const int idx = action->data().toInt();
    if (idx >= 0 && idx < TabCount)
        m_pages->setCurrentIndex(idx);
}

void MainWindow::onConnectToggle()
{
    // ToDo: Toggle eD2K/Kad connection
}

void MainWindow::setEd2kStatus(bool connected, bool connecting, bool firewalled)
{
    if (connected) {
        const QString label = firewalled
            ? tr("eD2K: Connected (LowID)")
            : tr("eD2K: Connected");
        m_statusEd2k->setText(label);
        m_statusEd2k->setStyleSheet(QStringLiteral("color: green;"));
    } else if (connecting) {
        m_statusEd2k->setText(tr("eD2K: Connecting..."));
        m_statusEd2k->setStyleSheet(QStringLiteral("color: orange;"));
    } else {
        m_statusEd2k->setText(tr("eD2K: Disconnected"));
        m_statusEd2k->setStyleSheet(QString{});
    }
}

void MainWindow::setKadStatus(bool running, bool kadConnected)
{
    if (kadConnected) {
        m_statusKad->setText(tr("Kad: Connected"));
        m_statusKad->setStyleSheet(QStringLiteral("color: green;"));
    } else if (running) {
        m_statusKad->setText(tr("Kad: Connecting..."));
        m_statusKad->setStyleSheet(QStringLiteral("color: orange;"));
    } else {
        m_statusKad->setText(tr("Kad: Disconnected"));
        m_statusKad->setStyleSheet(QString{});
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    theUiState.captureMainWindow(this);
    QMainWindow::closeEvent(event);
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
    Q_UNUSED(optionsAction);
    // ToDo: Open options dialog

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

void MainWindow::setupStatusBar()
{
    auto* sb = statusBar();

    m_statusMsg = new QLabel(tr("Ready"), this);
    sb->addWidget(m_statusMsg, 1);

    m_statusUsers = new QLabel(tr("Users: 0 | Files: 0"), this);
    sb->addPermanentWidget(m_statusUsers);

    m_statusUpDown = new QLabel(tr("Up: 0.0 | Down: 0.0"), this);
    sb->addPermanentWidget(m_statusUpDown);

    m_statusEd2k = new QLabel(tr("eD2K: Disconnected"), this);
    sb->addPermanentWidget(m_statusEd2k);

    m_statusKad = new QLabel(tr("Kad: Disconnected"), this);
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

    // Placeholder widgets for unimplemented tabs
    for (int i = TabSearch; i < TabCount; ++i) {
        auto* placeholder = new QWidget(this);
        auto* layout = new QVBoxLayout(placeholder);
        auto* label = new QLabel(tr("Not implemented yet"), placeholder);
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
        m_pages->addWidget(placeholder);
    }
}

} // namespace eMule
