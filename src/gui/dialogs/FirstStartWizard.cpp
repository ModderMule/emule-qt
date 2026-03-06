#include "pch.h"
#include "dialogs/FirstStartWizard.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "prefs/Preferences.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMovie>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace eMule {

FirstStartWizard::FirstStartWizard(IpcClient* ipc, QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
{
    setWindowTitle(tr("eMule First Runtime Wizard"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/Wizard.ico")));
    setFixedSize(530, 460);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 8);
    mainLayout->setSpacing(0);

    setupHeader();
    setupPortSection();
    setupNetworkSection();

    mainLayout->addStretch();

    setupButtons();
}

// ---------------------------------------------------------------------------
// Header banner — white bar with bold title + donkey mascot
// ---------------------------------------------------------------------------

void FirstStartWizard::setupHeader()
{
    auto* header = new QWidget(this);
    header->setAutoFillBackground(true);
    auto pal = header->palette();
    pal.setColor(QPalette::Window, Qt::white);
    header->setPalette(pal);
    header->setFixedHeight(62);

    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 8, 8, 8);

    auto* textLayout = new QVBoxLayout;
    auto* titleLabel = new QLabel(tr("Ports and Connection"), header);
    auto titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleLabel->setFont(titleFont);

    auto* subtitleLabel = new QLabel(tr("Connection"), header);
    auto subFont = subtitleLabel->font();
    subFont.setPointSize(subFont.pointSize());
    subtitleLabel->setFont(subFont);
    subtitleLabel->setContentsMargins(16, 0, 0, 0);

    textLayout->addWidget(titleLabel);
    textLayout->addWidget(subtitleLabel);
    textLayout->addStretch();
    headerLayout->addLayout(textLayout, 1);

    // Donkey mascot GIF
    auto* mascotLabel = new QLabel(header);
    auto* movie = new QMovie(QStringLiteral(":/images/mule_wiz_hdr.gif"), {}, mascotLabel);
    mascotLabel->setMovie(movie);
    movie->start();
    headerLayout->addWidget(mascotLabel, 0, Qt::AlignRight | Qt::AlignVCenter);

    static_cast<QVBoxLayout*>(layout())->addWidget(header);

    // Separator line below header
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    static_cast<QVBoxLayout*>(layout())->addWidget(line);
}

// ---------------------------------------------------------------------------
// Port section — TCP/UDP spin boxes + UPnP button
// ---------------------------------------------------------------------------

void FirstStartWizard::setupPortSection()
{
    auto* container = new QWidget(this);
    auto* vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(16, 12, 16, 4);

    auto* descLabel = new QLabel(
        tr("eMule uses two ports for communication with servers and clients. "
           "These ports must be free and available for remote clients. "
           "The TCP port must be available to ensure the main functionality of eMule. "
           "The UDP port is used for Kad (serverless network) and to reduce "
           "network usage (Overhead)."),
        container);
    descLabel->setWordWrap(true);
    vbox->addWidget(descLabel);

    vbox->addSpacing(4);

    auto* changeLabel = new QLabel(
        tr("You can change the ports here while no network activities have started."),
        container);
    changeLabel->setWordWrap(true);
    vbox->addWidget(changeLabel);

    vbox->addSpacing(8);

    // Port row: TCP + UDP + UPnP button
    auto* portRow = new QHBoxLayout;

    auto* tcpLabel = new QLabel(tr("TCP:"), container);
    m_tcpPortSpin = new QSpinBox(container);
    m_tcpPortSpin->setRange(1, 65535);
    auto tcpPort = thePrefs.port();
    m_tcpPortSpin->setValue(tcpPort > 0 ? tcpPort : Preferences::randomTCPPort());

    auto* udpLabel = new QLabel(tr("UDP:"), container);
    m_udpPortSpin = new QSpinBox(container);
    m_udpPortSpin->setRange(1, 65535);
    auto udpPort = thePrefs.udpPort();
    m_udpPortSpin->setValue(udpPort > 0 ? udpPort : Preferences::randomUDPPort());

    m_upnpBtn = new QPushButton(tr("Use UPnP to Setup Ports"), container);

    portRow->addWidget(tcpLabel);
    portRow->addWidget(m_tcpPortSpin);
    portRow->addSpacing(12);
    portRow->addWidget(udpLabel);
    portRow->addWidget(m_udpPortSpin);
    portRow->addSpacing(16);
    portRow->addWidget(m_upnpBtn);
    portRow->addStretch();
    vbox->addLayout(portRow);

    // UPnP progress bar (hidden by default)
    m_upnpProgress = new QProgressBar(container);
    m_upnpProgress->setRange(0, 0); // indeterminate
    m_upnpProgress->setFixedHeight(16);
    m_upnpProgress->setVisible(false);
    vbox->addWidget(m_upnpProgress);

    connect(m_upnpBtn, &QPushButton::clicked, this, &FirstStartWizard::onUPnPSetup);

    static_cast<QVBoxLayout*>(layout())->addWidget(container);
}

// ---------------------------------------------------------------------------
// Network section — Kad / eD2K checkboxes
// ---------------------------------------------------------------------------

void FirstStartWizard::setupNetworkSection()
{
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    static_cast<QVBoxLayout*>(layout())->addWidget(separator);

    auto* group = new QGroupBox(tr("Choose which Network(s) you want to use"), this);
    auto* groupLayout = new QHBoxLayout(group);
    groupLayout->setContentsMargins(16, 8, 16, 8);

    m_kadCheck = new QCheckBox(tr("Kad"), group);
    m_kadCheck->setChecked(thePrefs.kadEnabled());

    m_ed2kCheck = new QCheckBox(tr("eD2K"), group);
    m_ed2kCheck->setChecked(thePrefs.networkED2K());

    groupLayout->addWidget(m_kadCheck);
    groupLayout->addSpacing(40);
    groupLayout->addWidget(m_ed2kCheck);
    groupLayout->addStretch();

    auto* wrapper = new QWidget(this);
    auto* wrapperLayout = new QVBoxLayout(wrapper);
    wrapperLayout->setContentsMargins(16, 8, 16, 0);
    wrapperLayout->addWidget(group);

    static_cast<QVBoxLayout*>(layout())->addWidget(wrapper);
}

// ---------------------------------------------------------------------------
// Button row — Back (disabled), Finish, Cancel, Help
// ---------------------------------------------------------------------------

void FirstStartWizard::setupButtons()
{
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    static_cast<QVBoxLayout*>(layout())->addWidget(separator);

    auto* btnLayout = new QHBoxLayout;
    btnLayout->setContentsMargins(16, 8, 16, 0);

    m_backBtn = new QPushButton(tr("< Back"), this);
    m_backBtn->setEnabled(false);

    m_finishBtn = new QPushButton(tr("Finish"), this);
    m_finishBtn->setDefault(true);

    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    auto* helpBtn = new QPushButton(tr("Help"), this);

    btnLayout->addStretch();
    btnLayout->addWidget(m_backBtn);
    btnLayout->addWidget(m_finishBtn);
    btnLayout->addSpacing(24);
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(helpBtn);

    connect(m_finishBtn, &QPushButton::clicked, this, &FirstStartWizard::onFinish);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(helpBtn, &QPushButton::clicked, this, &FirstStartWizard::onHelp);

    static_cast<QVBoxLayout*>(layout())->addLayout(btnLayout);
}

// ---------------------------------------------------------------------------
// Finish — save settings locally and via IPC
// ---------------------------------------------------------------------------

void FirstStartWizard::onFinish()
{
    const auto tcpPort = static_cast<uint16_t>(m_tcpPortSpin->value());
    const auto udpPort = static_cast<uint16_t>(m_udpPortSpin->value());
    const bool kadEnabled = m_kadCheck->isChecked();
    const bool ed2kEnabled = m_ed2kCheck->isChecked();

    if (!kadEnabled && !ed2kEnabled) {
        QMessageBox::warning(this, tr("Network"),
            tr("You must enable at least one network (Kad or eD2K)."));
        return;
    }

    // Save locally
    thePrefs.setPort(tcpPort);
    thePrefs.setUdpPort(udpPort);
    thePrefs.setKadEnabled(kadEnabled);
    thePrefs.setNetworkED2K(ed2kEnabled);
    thePrefs.save();

    // Send to daemon via IPC
    if (m_ipc && m_ipc->isConnected()) {
        Ipc::IpcMessage req(Ipc::IpcMsgType::SetPreferences);
        req.append(QStringLiteral("port"));
        req.append(static_cast<qint64>(tcpPort));
        req.append(QStringLiteral("udpPort"));
        req.append(static_cast<qint64>(udpPort));
        req.append(QStringLiteral("kadEnabled"));
        req.append(kadEnabled);
        req.append(QStringLiteral("networkED2K"));
        req.append(ed2kEnabled);
        m_ipc->sendRequest(std::move(req));
    }

    accept();
}

// ---------------------------------------------------------------------------
// UPnP — enable UPnP and let the daemon handle port mapping
// ---------------------------------------------------------------------------

void FirstStartWizard::onUPnPSetup()
{
    m_upnpBtn->setEnabled(false);
    m_upnpProgress->setVisible(true);

    thePrefs.setEnableUPnP(true);
    thePrefs.save();

    if (m_ipc && m_ipc->isConnected()) {
        Ipc::IpcMessage req(Ipc::IpcMsgType::SetPreferences);
        req.append(QStringLiteral("enableUPnP"));
        req.append(true);
        req.append(QStringLiteral("port"));
        req.append(static_cast<qint64>(m_tcpPortSpin->value()));
        req.append(QStringLiteral("udpPort"));
        req.append(static_cast<qint64>(m_udpPortSpin->value()));
        m_ipc->sendRequest(std::move(req));
    }

    // Timeout after 30 seconds — treat as failure
    m_upnpTimer = new QTimer(this);
    m_upnpTimer->setSingleShot(true);
    connect(m_upnpTimer, &QTimer::timeout, this, &FirstStartWizard::onUPnPTimeout);
    m_upnpTimer->start(30000);
}

void FirstStartWizard::onUPnPTimeout()
{
    m_upnpProgress->setVisible(false);
    m_upnpBtn->setEnabled(true);
    QMessageBox::warning(this, tr("UPnP"),
        tr("UPnP port mapping timed out. Your router may not support UPnP, "
           "or it may be disabled. You can set up port forwarding manually."));
}

// ---------------------------------------------------------------------------
// Help — open project website
// ---------------------------------------------------------------------------

void FirstStartWizard::onHelp()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://emule-qt.org")));
}

} // namespace eMule
