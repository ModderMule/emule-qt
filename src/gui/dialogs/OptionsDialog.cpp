#include "pch.h"
#include "dialogs/OptionsDialog.h"
#include "dialogs/FirstStartWizard.h"

#include "app/AppConfig.h"
#include "app/AutoStart.h"
#include "app/UiState.h"
#include "app/Ed2kSchemeHandler.h"
#include "app/IpcClient.h"
#include "panels/StatisticsPanel.h"
#include "prefs/Preferences.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCborArray>
#include <QApplication>
#include <QCoreApplication>

#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QFontDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QFormLayout>
#include <QMenu>
#include <QTimeEdit>
#include <QListWidget>
#include <QRadioButton>
#include <QScrollArea>
#include <QSoundEffect>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QLocale>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QVBoxLayout>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

OptionsDialog::OptionsDialog(IpcClient* ipc, StatisticsPanel* statsPanel,
                             QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
    , m_statsPanel(statsPanel)
{
    setWindowTitle(tr("Options"));
    resize(800, 640);

    auto* mainLayout = new QHBoxLayout;

    // Left sidebar
    m_sidebar = new QListWidget(this);
    setupSidebar();
    m_sidebar->setFixedWidth(200);
    m_sidebar->setIconSize(QSize(16, 16));
    mainLayout->addWidget(m_sidebar);

    // Right side: header + pages + buttons
    auto* rightLayout = new QVBoxLayout;

    // Page header (icon + title on blue background)
    m_pageHeader = new QLabel(this);
    m_pageHeader->setAutoFillBackground(true);
    auto pal = m_pageHeader->palette();
    pal.setColor(QPalette::Window, QColor(0x33, 0x66, 0xCC));
    pal.setColor(QPalette::WindowText, Qt::white);
    m_pageHeader->setPalette(pal);
    m_pageHeader->setFont([this] {
        auto f = font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 2);
        return f;
    }());
    m_pageHeader->setContentsMargins(8, 4, 8, 4);
    rightLayout->addWidget(m_pageHeader);

    // Stacked pages
    m_pages = new QStackedWidget(this);
    setupPages();
    rightLayout->addWidget(m_pages, 1);

    // Buttons (matching MFC: OK, Cancel, Apply, Help)
    setupButtons();
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    auto* okBtn = new QPushButton(tr("OK"), this);
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    m_applyBtn = new QPushButton(tr("Apply"), this);
    auto* helpBtn = new QPushButton(tr("Help"), this);
    connect(helpBtn, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://emule-qt.org")));
    });
    okBtn->setDefault(true);
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(m_applyBtn);
    btnLayout->addWidget(helpBtn);
    rightLayout->addLayout(btnLayout);

    mainLayout->addLayout(rightLayout, 1);
    setLayout(mainLayout);

    // Connections
    connect(m_sidebar, &QListWidget::currentRowChanged,
            this, &OptionsDialog::onPageChanged);
    connect(okBtn, &QPushButton::clicked, this, &OptionsDialog::onOk);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_applyBtn, &QPushButton::clicked, this, &OptionsDialog::onApply);

    // Restore last selected page
    m_sidebar->setCurrentRow(theUiState.optionsLastPage());

    // Load current settings into controls (before wiring change signals
    // so that loading doesn't immediately mark the dialog dirty).
    loadSettings();

    // Apply starts disabled — enabled only when a setting changes.
    m_applyBtn->setEnabled(false);

    // Wire change signals from all editable controls to markDirty.
    connect(m_nickEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_promptOnExitCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_startMinimizedCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_versionCheckBox, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_versionCheckDaysSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_bringToFrontCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_coreAddressEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_corePortSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_coreTokenEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);

    // Display page
    connect(m_depth3DSlider, &QSlider::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_tooltipDelaySpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_minimizeToTrayCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_transferDoubleClickCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_showDwlPercentageCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_showRatesInTitleCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_showCatTabInfosCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_autoRemoveFinishedCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_showTransToolbarCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_storeSearchesCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_disableKnownClientListCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_disableQueueListCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_useAutoCompletionCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_useOriginalIconsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);

    // Connection page
    connect(m_capacityDownloadSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_capacityUploadSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_downloadLimitCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_uploadLimitCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_downloadLimitSlider, &QSlider::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_uploadLimitSlider, &QSlider::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_tcpPortSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_udpPortSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_udpDisableCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_upnpCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_maxSourcesSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_maxConnectionsSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_autoConnectCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_reconnectCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_overheadCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_kadEnabledCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_ed2kEnabledCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);

    // Server page
    connect(m_addServersFromServerCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_addServersFromClientsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_safeServerConnectCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_autoConnectStaticOnlyCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_useServerPrioritiesCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_deadServerRetriesSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_autoUpdateServerListCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_smartLowIdCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_manualHighPrioCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);

    // Proxy page
    connect(m_proxyEnableCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_proxyTypeCombo, &QComboBox::currentIndexChanged, this, &OptionsDialog::markDirty);
    connect(m_proxyHostEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_proxyPortSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_proxyAuthCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_proxyUserEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_proxyPasswordEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);

    // Directories page
    connect(m_incomingDirEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_tempDirEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_sharedDirsModel, &QAbstractItemModel::dataChanged, this, &OptionsDialog::markDirty);

    // Files page
    connect(m_addFilesPausedCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_autoSharedFilesPrioCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_autoDownloadPrioCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_autoCleanupFilenamesCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_transferFullChunksCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_previewPrioCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_watchClipboardCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_advancedCalcRemainingCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_startNextPausedCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_preferSameCatCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_onlySameCatCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_rememberDownloadedCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_rememberCancelledCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_videoPlayerCmdEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_videoPlayerArgsEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_createBackupToPreviewCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);

    // Notifications page
    connect(m_noSoundRadio, &QRadioButton::toggled, this, &OptionsDialog::markDirty);
    connect(m_playSoundRadio, &QRadioButton::toggled, this, &OptionsDialog::markDirty);
    connect(m_soundFileEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_notifyLogCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_notifyChatCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_notifyChatMsgCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_notifyDownloadAddedCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_notifyDownloadFinishedCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_notifyNewVersionCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_notifyUrgentCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_emailEnabledCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_emailRecipientEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_emailSenderEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);

    // Messages and Comments page
    connect(m_messageFilterEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_msgFriendsOnlyCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_advancedSpamFilterCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_requireCaptchaCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_showSmileysCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_commentFilterEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_indicateRatingsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);

    // Security page
    connect(m_filterServersByIPCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_ipFilterLevelSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_viewSharedGroup, &QButtonGroup::idToggled, this, &OptionsDialog::markDirty);
    connect(m_cryptLayerRequestedCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_cryptLayerRequiredCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_cryptLayerDisableCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_useSecureIdentCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_enableSearchResultFilterCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_warnUntrustedFilesCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_ipFilterUpdateUrlEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);

    // Extended page
    connect(m_maxConPerFiveSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_maxHalfOpenSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_serverKeepAliveSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_useCreditSystemCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_filterLANIPsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_showExtControlsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_a4afSaveCpuCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_disableArchPreviewCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_ed2kHostnameEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_checkDiskspaceCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_minFreeDiskSpaceSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_commitFilesGroup, &QButtonGroup::idToggled, this, &OptionsDialog::markDirty);
    connect(m_extractMetaDataGroup, &QButtonGroup::idToggled, this, &OptionsDialog::markDirty);
    connect(m_logToDiskCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_verboseCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logLevelSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_verboseLogToDiskCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logSourceExchangeCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logBannedClientsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logRatingDescCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logSecureIdentCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logFilteredIPsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logFileSavingCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logA4AFCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logUlDlEventsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logRawSocketPacketsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_enableIpcLogCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_startCoreWithConsoleCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_closeUPnPCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_skipWANIPCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_skipWANPPPCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_fileBufferSlider, &QSlider::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_queueSizeSlider, &QSlider::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_dynUpEnabledCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_dynUpPingToleranceSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_dynUpPingToleranceMsSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_dynUpRadioPercent, &QRadioButton::toggled, this, &OptionsDialog::markDirty);
    connect(m_dynUpRadioMs, &QRadioButton::toggled, this, &OptionsDialog::markDirty);
    connect(m_dynUpGoingUpSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_dynUpGoingDownSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_dynUpNumPingsSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
#ifdef Q_OS_WIN
    connect(m_autotakeEd2kCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_winFirewallCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_sparsePartFilesCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_allocFullFileCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_resolveShellLinksCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_multiUserSharingGroup, &QButtonGroup::idToggled, this, &OptionsDialog::markDirty);
#endif

    // IP filter reload button
    connect(m_reloadIPFilterBtn, &QPushButton::clicked, this, [this]() {
        if (m_ipc && m_ipc->isConnected()) {
            m_reloadIPFilterBtn->setEnabled(false);
            Ipc::IpcMessage req(Ipc::IpcMsgType::ReloadIPFilter);
            m_ipc->sendRequest(std::move(req), [this](const Ipc::IpcMessage& resp) {
                m_reloadIPFilterBtn->setEnabled(true);
                if (resp.fieldBool(0)) {
                    auto count = resp.fieldInt(1);
                    QMessageBox::information(this, tr("IP Filter"),
                        tr("IP filter reloaded: %1 entries.").arg(count));
                }
            });
        }
    });

    // Protocol obfuscation interactive logic
    connect(m_cryptLayerDisableCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            m_cryptLayerRequestedCheck->setChecked(false);
            m_cryptLayerRequiredCheck->setChecked(false);
        }
        m_cryptLayerRequestedCheck->setEnabled(!on);
        m_cryptLayerRequiredCheck->setEnabled(!on && m_cryptLayerRequestedCheck->isChecked());
    });
    connect(m_cryptLayerRequestedCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!on)
            m_cryptLayerRequiredCheck->setChecked(false);
        m_cryptLayerRequiredCheck->setEnabled(on);
    });

    // Proxy enable/disable logic
    connect(m_proxyEnableCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_proxyTypeCombo->setEnabled(on);
        m_proxyHostEdit->setEnabled(on);
        m_proxyPortSpin->setEnabled(on);
        m_proxyAuthCheck->setEnabled(on);
        m_proxyUserEdit->setEnabled(on && m_proxyAuthCheck->isChecked());
        m_proxyPasswordEdit->setEnabled(on && m_proxyAuthCheck->isChecked());
    });
    connect(m_proxyAuthCheck, &QCheckBox::toggled, this, [this](bool on) {
        bool proxyOn = m_proxyEnableCheck->isChecked();
        m_proxyUserEdit->setEnabled(proxyOn && on);
        m_proxyPasswordEdit->setEnabled(proxyOn && on);
    });
}

OptionsDialog::~OptionsDialog() = default;

void OptionsDialog::selectPage(int page)
{
    if (page >= 0 && page < PageCount)
        m_sidebar->setCurrentRow(page);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void OptionsDialog::onPageChanged(int row)
{
    if (row >= 0 && row < PageCount) {
        m_pages->setCurrentIndex(row);
        auto* item = m_sidebar->item(row);
        if (item)
            m_pageHeader->setText(QStringLiteral("  %1").arg(item->text()));
    }
}

void OptionsDialog::onOk()
{
    theUiState.setOptionsLastPage(m_sidebar->currentRow());
    saveSettings();
    accept();
}

void OptionsDialog::onApply()
{
    theUiState.setOptionsLastPage(m_sidebar->currentRow());
    saveSettings();
    m_applyBtn->setEnabled(false);
}

void OptionsDialog::markDirty()
{
    if (m_loading)
        return;
    m_applyBtn->setEnabled(true);
}

// ---------------------------------------------------------------------------
// Setup: sidebar
// ---------------------------------------------------------------------------

void OptionsDialog::setupSidebar()
{
    struct PageDef {
        const char* label;
        QStyle::StandardPixmap icon;
        const char* originalIcon;  // resource path when using original eMule icons
    };

    static constexpr PageDef pages[] = {
        {"General",               QStyle::SP_FileDialogDetailedView, "Preferences.ico"},
        {"Display",               QStyle::SP_DesktopIcon,            "Display.ico"},
        {"Connection",            QStyle::SP_DriveNetIcon,           "Connection.ico"},
        {"Proxy",                 QStyle::SP_BrowserReload,          "Proxy.ico"},
        {"Server",                QStyle::SP_ComputerIcon,           "Server.ico"},
        {"Directories",           QStyle::SP_DirIcon,                "Folders.ico"},
        {"Files",                 QStyle::SP_FileIcon,               "FileTypeAny.ico"},
        {"Notifications",         QStyle::SP_MessageBoxInformation,  "Notifications.ico"},
        {"Statistics",            QStyle::SP_DialogHelpButton,       "Statistics.ico"},
        {"IRC",                   QStyle::SP_DialogApplyButton,      "IRC.ico"},
        {"Messages and Comments", QStyle::SP_MessageBoxQuestion,     "Chat.ico"},
        {"Security",              QStyle::SP_CustomBase,             "Security.ico"},
        {"Scheduler",             QStyle::SP_DialogResetButton,      "Scheduler.ico"},
        {"Web Interface",         QStyle::SP_DriveNetIcon,           "Web.ico"},
        {"Extended",              QStyle::SP_DialogCancelButton,     "Tweak.ico"},
    };

    const bool useOriginal = thePrefs.useOriginalIcons();

    for (const auto& [label, icon, resIcon] : pages) {
        QIcon qicon;
        if (useOriginal) {
            qicon = QIcon(QStringLiteral(":/icons/") + QLatin1String(resIcon));
        }
        else if (icon == QStyle::SP_CustomBase)
            qicon = makePadlockIcon();
        else
            qicon = style()->standardIcon(icon);
        auto* item = new QListWidgetItem(qicon, tr(label));
        m_sidebar->addItem(item);
    }
}

// ---------------------------------------------------------------------------
// Setup: pages
// ---------------------------------------------------------------------------

void OptionsDialog::setupPages()
{
    // General — fully implemented
    m_pages->addWidget(createGeneralPage());

    // Display — fully implemented
    m_pages->addWidget(createDisplayPage());

    // Connection — fully implemented
    m_pages->addWidget(createConnectionPage());

    // Proxy — fully implemented
    m_pages->addWidget(createProxyPage());

    // Server — fully implemented
    m_pages->addWidget(createServerPage());

    // Directories — fully implemented
    m_pages->addWidget(createDirectoriesPage());

    // Files — fully implemented
    m_pages->addWidget(createFilesPage());

    // Notifications — fully implemented
    m_pages->addWidget(createNotificationsPage());

    // Statistics — fully implemented
    m_pages->addWidget(createStatisticsPage());

    // IRC — fully implemented
    m_pages->addWidget(createIRCPage());

    // Messages and Comments — fully implemented
    m_pages->addWidget(createMessagesPage());

    // Security — fully implemented
    m_pages->addWidget(createSecurityPage());

    // Scheduler — fully implemented
    m_pages->addWidget(createSchedulerPage());

    // Web Interface — fully implemented
    m_pages->addWidget(createWebInterfacePage());

    // Extended — fully implemented
    m_pages->addWidget(createExtendedPage());
}

void OptionsDialog::setupButtons()
{
    // Buttons are set up in the constructor layout section.
}

// ---------------------------------------------------------------------------
// General page — matches MFC "Options General.png"
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createGeneralPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    // --- User Name group ---
    auto* nameGroup = new QGroupBox(tr("User Name"), page);
    auto* nameLayout = new QHBoxLayout(nameGroup);
    m_nickEdit = new QLineEdit(nameGroup);
    m_nickEdit->setMaxLength(50);
    nameLayout->addWidget(m_nickEdit);
    layout->addWidget(nameGroup);

    // --- Language group ---
    auto* langGroup = new QGroupBox(tr("Language"), page);
    auto* langLayout = new QHBoxLayout(langGroup);
    m_langCombo = new QComboBox(langGroup);
    m_langCombo->addItem(tr("System Default"), QString{});
    m_langCombo->addItem(QStringLiteral("English (United States)"), QStringLiteral("en_US"));

    // Discover available translations from .qm files
    const QStringList langSearchPaths = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/lang"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/lang"),
#ifdef EMULE_DEV_BUILD
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../../lang"),
#endif
    };
    QSet<QString> foundLocales;
    for (const auto& dir : langSearchPaths) {
        QDirIterator it(dir, {QStringLiteral("emuleqt_*.qm")}, QDir::Files);
        while (it.hasNext()) {
            it.next();
            // Extract locale code from "emuleqt_xx_YY.qm"
            QString name = it.fileName();
            name.remove(0, 8);           // strip "emuleqt_"
            name.chop(3);                // strip ".qm"
            if (!name.isEmpty() && name != QStringLiteral("en"))
                foundLocales.insert(name);
        }
    }
    // Add each found locale with its native language name
    QList<std::pair<QString, QString>> available;
    for (const auto& code : foundLocales) {
        QLocale loc(code);
        QString label = loc.nativeLanguageName();
        if (!loc.nativeTerritoryName().isEmpty())
            label += QStringLiteral(" (") + loc.nativeTerritoryName() + u')';
        available.emplaceBack(label, code);
    }
    std::ranges::sort(available, {}, &std::pair<QString, QString>::first);
    for (const auto& [label, code] : available)
        m_langCombo->addItem(label, code);

    langLayout->addWidget(m_langCombo);
    layout->addWidget(langGroup);

    // --- Miscellaneous group ---
    auto* miscGroup = new QGroupBox(tr("Miscellaneous"), page);
    auto* miscLayout = new QVBoxLayout(miscGroup);

    m_bringToFrontCheck = new QCheckBox(tr("Bring to front on link click"), miscGroup);
    miscLayout->addWidget(m_bringToFrontCheck);

    m_promptOnExitCheck = new QCheckBox(tr("Prompt on exit"), miscGroup);
    miscLayout->addWidget(m_promptOnExitCheck);

    m_enableOnlineSigCheck = new QCheckBox(tr("Enable online signature"), miscGroup);
    miscLayout->addWidget(m_enableOnlineSigCheck);

#ifdef Q_OS_WIN
    m_enableMiniMuleCheck = new QCheckBox(tr("Enable MiniMule"), miscGroup);
    miscLayout->addWidget(m_enableMiniMuleCheck);
#endif

    m_preventStandbyCheck = new QCheckBox(tr("Prevent standby mode while running"), miscGroup);
    miscLayout->addWidget(m_preventStandbyCheck);

    // Button row
    auto* miscBtnLayout = new QHBoxLayout;
    auto* webServicesBtn = new QPushButton(tr("Edit Web Services..."), miscGroup);
    connect(webServicesBtn, &QPushButton::clicked, this, [] {
        QString tmplPath = thePrefs.webServerTemplatePath();
        if (tmplPath.isEmpty())
            tmplPath = AppConfig::configDir() + QStringLiteral("/eMule.tmpl");
        if (QFile::exists(tmplPath))
            QDesktopServices::openUrl(QUrl::fromLocalFile(tmplPath));
    });
    auto* ed2kLinksBtn = new QPushButton(tr("Handle eD2K Links"), miscGroup);
    connect(ed2kLinksBtn, &QPushButton::clicked, this, [] {
        eMule::registerEd2kUrlScheme();
    });
    miscBtnLayout->addWidget(webServicesBtn);
    miscBtnLayout->addWidget(ed2kLinksBtn);
    miscBtnLayout->addStretch();
    miscLayout->addLayout(miscBtnLayout);

    layout->addWidget(miscGroup);

    // --- Startup group ---
    auto* startupGroup = new QGroupBox(tr("Startup"), page);
    auto* startupLayout = new QVBoxLayout(startupGroup);

    auto* versionCheckRow = new QHBoxLayout;
    m_versionCheckBox = new QCheckBox(tr("Check for new version"), startupGroup);
    versionCheckRow->addWidget(m_versionCheckBox);
    m_versionCheckDaysSpin = new QSpinBox(startupGroup);
    m_versionCheckDaysSpin->setRange(1, 14);
    m_versionCheckDaysSpin->setSuffix(tr(" Days"));
    versionCheckRow->addWidget(m_versionCheckDaysSpin);
    versionCheckRow->addStretch();
    connect(m_versionCheckBox, &QCheckBox::toggled,
            m_versionCheckDaysSpin, &QWidget::setEnabled);
    startupLayout->addLayout(versionCheckRow);

    m_showSplashCheck = new QCheckBox(tr("Show splash screen"), startupGroup);
    startupLayout->addWidget(m_showSplashCheck);

    m_startMinimizedCheck = new QCheckBox(tr("Start minimized"), startupGroup);
    startupLayout->addWidget(m_startMinimizedCheck);

    m_startWithOSCheck = new QCheckBox(
#ifdef Q_OS_MACOS
        tr("Start with macOS")
#elif defined(Q_OS_WIN)
        tr("Start with Windows")
#else
        tr("Start with system")
#endif
        , startupGroup);
    startupLayout->addWidget(m_startWithOSCheck);

    layout->addWidget(startupGroup);

    // --- Core group ---
    auto* coreGroup = new QGroupBox(tr("Core"), page);
    auto* coreLayout = new QFormLayout(coreGroup);

    m_coreAddressEdit = new QLineEdit(coreGroup);
    m_coreAddressEdit->setPlaceholderText(QStringLiteral("127.0.0.1"));
    coreLayout->addRow(tr("Address:"), m_coreAddressEdit);

    m_corePortSpin = new QSpinBox(coreGroup);
    m_corePortSpin->setRange(1, 65535);
    coreLayout->addRow(tr("Port:"), m_corePortSpin);

    m_coreTokenEdit = new QLineEdit(coreGroup);
    m_coreTokenEdit->setPlaceholderText(tr("authentication token"));
    coreLayout->addRow(tr("Token:"), m_coreTokenEdit);

    auto* coreNote = new QLabel(tr("Changes require a restart to take effect."), coreGroup);
    coreNote->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
    coreLayout->addRow(coreNote);

    m_shutdownCoreBtn = new QPushButton(tr("Shutdown eMule Core"), coreGroup);
    auto* shutdownBtnLayout = new QHBoxLayout;
    shutdownBtnLayout->addWidget(m_shutdownCoreBtn);
    shutdownBtnLayout->addStretch();
    coreLayout->addRow(shutdownBtnLayout);

    connect(m_shutdownCoreBtn, &QPushButton::clicked, this, [this] {
        auto result = QMessageBox::warning(
            this,
            tr("Shutdown eMule Core"),
            tr("This will shut down both the eMule Core and the GUI.\n\n"
               "Are you sure you want to continue?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (result != QMessageBox::Yes)
            return;
        if (m_ipc) {
            m_ipc->sendShutdown();
            QApplication::quit();
        }
    });

    layout->addWidget(coreGroup);

    layout->addStretch();
    return page;
}

// ---------------------------------------------------------------------------
// Display page — matches MFC "Options Display.png"
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createDisplayPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    // --- Progressbar style row ---
    auto* progressRow = new QHBoxLayout;
    progressRow->addWidget(new QLabel(tr("Progressbar style"), page));
    // Preview placeholder (small colored rectangle)
    auto* preview = new QLabel(page);
    preview->setFixedSize(60, 16);
    preview->setFrameShape(QLabel::Box);
    preview->setAutoFillBackground(true);
    auto previewPal = preview->palette();
    previewPal.setColor(QPalette::Window, QColor(0x99, 0x99, 0xCC));
    preview->setPalette(previewPal);
    progressRow->addWidget(preview);
    progressRow->addWidget(new QLabel(tr("flat"), page));
    m_depth3DSlider = new QSlider(Qt::Horizontal, page);
    m_depth3DSlider->setRange(0, 5);
    m_depth3DSlider->setTickPosition(QSlider::TicksBelow);
    m_depth3DSlider->setTickInterval(1);
    progressRow->addWidget(m_depth3DSlider);
    progressRow->addWidget(new QLabel(tr("round"), page));
    layout->addLayout(progressRow);

    // --- Tooltip delay row ---
    auto* tooltipRow = new QHBoxLayout;
    tooltipRow->addStretch();
    tooltipRow->addWidget(new QLabel(tr("Tooltip delay time [sec.]"), page));
    m_tooltipDelaySpin = new QSpinBox(page);
    m_tooltipDelaySpin->setRange(0, 32);
    tooltipRow->addWidget(m_tooltipDelaySpin);
    layout->addLayout(tooltipRow);

    // --- 8 checkboxes (no group box) ---
    m_minimizeToTrayCheck = new QCheckBox(tr("Minimize to system tray"), page);
    layout->addWidget(m_minimizeToTrayCheck);

    m_transferDoubleClickCheck = new QCheckBox(tr("Download list double-click to expand"), page);
    layout->addWidget(m_transferDoubleClickCheck);

    m_showDwlPercentageCheck = new QCheckBox(tr("Show percentage of download completion in progressbar"), page);
    layout->addWidget(m_showDwlPercentageCheck);

    m_showRatesInTitleCheck = new QCheckBox(tr("Show transfer rates on title"), page);
    layout->addWidget(m_showRatesInTitleCheck);

    m_showCatTabInfosCheck = new QCheckBox(tr("Show download info on category tabs"), page);
    layout->addWidget(m_showCatTabInfosCheck);

    m_autoRemoveFinishedCheck = new QCheckBox(tr("Auto clear completed downloads"), page);
    layout->addWidget(m_autoRemoveFinishedCheck);

    m_showTransToolbarCheck = new QCheckBox(tr("Show additional toolbar on Transfers window"), page);
    layout->addWidget(m_showTransToolbarCheck);

    m_storeSearchesCheck = new QCheckBox(tr("Remember open searches between restarts"), page);
    layout->addWidget(m_storeSearchesCheck);

    m_useOriginalIconsCheck = new QCheckBox(tr("Use original eMule icons"), page);
    layout->addWidget(m_useOriginalIconsCheck);

    // --- Save CPU & Memory Usage group ---
    auto* cpuGroup = new QGroupBox(tr("Save CPU && Memory Usage"), page);
    auto* cpuLayout = new QVBoxLayout(cpuGroup);
    m_disableKnownClientListCheck = new QCheckBox(tr("Disable Known Clients list"), cpuGroup);
    cpuLayout->addWidget(m_disableKnownClientListCheck);
    m_disableQueueListCheck = new QCheckBox(tr("Disable Queue list"), cpuGroup);
    cpuLayout->addWidget(m_disableQueueListCheck);
    layout->addWidget(cpuGroup);

    // --- Font group ---
    auto* fontGroup = new QGroupBox(tr("Font for Server-, Message- and IRC-Window"), page);
    auto* fontLayout = new QHBoxLayout(fontGroup);
    m_selectFontBtn = new QPushButton(tr("Select Font..."), fontGroup);
    fontLayout->addWidget(m_selectFontBtn);
    m_fontPreviewLabel = new QLabel(fontGroup);
    m_fontPreviewLabel->setStyleSheet(QStringLiteral("color: gray;"));
    fontLayout->addWidget(m_fontPreviewLabel);
    fontLayout->addStretch();
    connect(m_selectFontBtn, &QPushButton::clicked, this, [this] {
        QFont initial;
        if (!m_currentLogFont.isEmpty())
            initial.fromString(m_currentLogFont);
        bool ok = false;
        QFont font = QFontDialog::getFont(&ok, initial, this, tr("Select Font"));
        if (ok) {
            m_currentLogFont = font.toString();
            m_fontPreviewLabel->setText(QStringLiteral("%1, %2pt").arg(font.family()).arg(font.pointSize()));
            markDirty();
        }
    });
    layout->addWidget(fontGroup);

    // --- Auto completion group ---
    auto* autoCompGroup = new QGroupBox(tr("Auto completion (history function)"), page);
    auto* autoCompLayout = new QHBoxLayout(autoCompGroup);
    m_useAutoCompletionCheck = new QCheckBox(tr("Enabled"), autoCompGroup);
    autoCompLayout->addWidget(m_useAutoCompletionCheck);
    auto* resetBtn = new QPushButton(tr("Reset"), autoCompGroup);
    connect(resetBtn, &QPushButton::clicked, this, [resetBtn] {
        QSettings settings;
        settings.remove(QStringLiteral("search/history"));
        resetBtn->setEnabled(false);
    });
    autoCompLayout->addWidget(resetBtn);
    autoCompLayout->addStretch();
    layout->addWidget(autoCompGroup);

    layout->addStretch();
    return page;
}

// ---------------------------------------------------------------------------
// Connection page — matches MFC "Options Connection.png"
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createConnectionPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    // === Row 1: Capacities | Limits ===
    auto* row1 = new QHBoxLayout;

    // --- Capacities group ---
    auto* capGroup = new QGroupBox(tr("Capacities"), page);
    auto* capLayout = new QGridLayout(capGroup);

    capLayout->addWidget(new QLabel(tr("Download"), capGroup), 0, 0);
    m_capacityDownloadSpin = new QSpinBox(capGroup);
    m_capacityDownloadSpin->setRange(1, 100000);
    m_capacityDownloadSpin->setSuffix(tr(" KB/s"));
    capLayout->addWidget(m_capacityDownloadSpin, 0, 1);

    capLayout->addWidget(new QLabel(tr("Upload"), capGroup), 1, 0);
    m_capacityUploadSpin = new QSpinBox(capGroup);
    m_capacityUploadSpin->setRange(1, 100000);
    m_capacityUploadSpin->setSuffix(tr(" KB/s"));
    capLayout->addWidget(m_capacityUploadSpin, 1, 1);

    row1->addWidget(capGroup);

    // --- Limits group ---
    auto* limGroup = new QGroupBox(tr("Limits"), page);
    auto* limLayout = new QGridLayout(limGroup);

    m_downloadLimitCheck = new QCheckBox(tr("Download limit"), limGroup);
    limLayout->addWidget(m_downloadLimitCheck, 0, 0);
    m_downloadLimitLabel = new QLabel(QStringLiteral("0 KB/s"), limGroup);
    m_downloadLimitLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_downloadLimitLabel->setMinimumWidth(60);
    limLayout->addWidget(m_downloadLimitLabel, 0, 1);
    m_downloadLimitSlider = new QSlider(Qt::Horizontal, limGroup);
    m_downloadLimitSlider->setRange(1, 500);
    limLayout->addWidget(m_downloadLimitSlider, 1, 0, 1, 2);

    m_uploadLimitCheck = new QCheckBox(tr("Upload limit"), limGroup);
    limLayout->addWidget(m_uploadLimitCheck, 2, 0);
    m_uploadLimitLabel = new QLabel(QStringLiteral("0 KB/s"), limGroup);
    m_uploadLimitLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_uploadLimitLabel->setMinimumWidth(60);
    limLayout->addWidget(m_uploadLimitLabel, 2, 1);
    m_uploadLimitSlider = new QSlider(Qt::Horizontal, limGroup);
    m_uploadLimitSlider->setRange(1, 250);
    limLayout->addWidget(m_uploadLimitSlider, 3, 0, 1, 2);

    row1->addWidget(limGroup);
    layout->addLayout(row1);

    // === Row 2: Client Port group (full width) ===
    auto* portGroup = new QGroupBox(tr("Client Port"), page);
    auto* portLayout = new QGridLayout(portGroup);

    portLayout->addWidget(new QLabel(tr("TCP"), portGroup), 0, 0);
    m_tcpPortSpin = new QSpinBox(portGroup);
    m_tcpPortSpin->setRange(1, 65535);
    portLayout->addWidget(m_tcpPortSpin, 0, 1);

    portLayout->addWidget(new QLabel(tr("UDP"), portGroup), 1, 0);
    m_udpPortSpin = new QSpinBox(portGroup);
    m_udpPortSpin->setRange(1, 65535);
    portLayout->addWidget(m_udpPortSpin, 1, 1);
    m_udpDisableCheck = new QCheckBox(tr("Disable"), portGroup);
    portLayout->addWidget(m_udpDisableCheck, 1, 2);

    auto* testPortsBtn = new QPushButton(tr("Test Ports"), portGroup);
    connect(testPortsBtn, &QPushButton::clicked, this, [this] {
        int tcp = m_tcpPortSpin->value();
        int udp = m_udpDisableCheck->isChecked() ? 0 : m_udpPortSpin->value();
        QDesktopServices::openUrl(QUrl(
            QStringLiteral("https://porttest.emule-project.net/connectiontest.php?tcpport=%1&udpport=%2")
                .arg(tcp).arg(udp)));
    });
    portLayout->addWidget(testPortsBtn, 1, 3);

    m_upnpCheck = new QCheckBox(tr("Use UPnP to Setup Ports"), portGroup);
    portLayout->addWidget(m_upnpCheck, 2, 0, 1, 4);

    layout->addWidget(portGroup);

    // === Row 3: Max. Sources/File | Connection Limits ===
    auto* row3 = new QHBoxLayout;

    auto* srcGroup = new QGroupBox(tr("Max. Sources/File"), page);
    auto* srcLayout = new QHBoxLayout(srcGroup);
    srcLayout->addWidget(new QLabel(tr("Hard limit"), srcGroup));
    m_maxSourcesSpin = new QSpinBox(srcGroup);
    m_maxSourcesSpin->setRange(1, 5000);
    srcLayout->addWidget(m_maxSourcesSpin);
    row3->addWidget(srcGroup);

    auto* connGroup = new QGroupBox(tr("Connection Limits"), page);
    auto* connLayout = new QHBoxLayout(connGroup);
    connLayout->addWidget(new QLabel(tr("Max. connections"), connGroup));
    m_maxConnectionsSpin = new QSpinBox(connGroup);
    m_maxConnectionsSpin->setRange(1, 10000);
    connLayout->addWidget(m_maxConnectionsSpin);
    row3->addWidget(connGroup);

    layout->addLayout(row3);

    // === Row 4: Checkboxes left | Network group right ===
    auto* row4 = new QHBoxLayout;

    auto* checkLayout = new QVBoxLayout;
    m_autoConnectCheck = new QCheckBox(tr("Autoconnect on startup"), page);
    checkLayout->addWidget(m_autoConnectCheck);
    m_reconnectCheck = new QCheckBox(tr("Reconnect on loss"), page);
    checkLayout->addWidget(m_reconnectCheck);
    m_overheadCheck = new QCheckBox(tr("Show overhead bandwidth"), page);
    checkLayout->addWidget(m_overheadCheck);
    auto* wizardBtn = new QPushButton(tr("Wizard..."), page);
    wizardBtn->setFixedWidth(100);
    connect(wizardBtn, &QPushButton::clicked, this, [this]() {
        auto* wizard = new FirstStartWizard(m_ipc, this);
        wizard->setAttribute(Qt::WA_DeleteOnClose);
        wizard->show();
    });
    checkLayout->addWidget(wizardBtn);
    checkLayout->addStretch();
    row4->addLayout(checkLayout);

    auto* netGroup = new QGroupBox(tr("Network"), page);
    auto* netLayout = new QVBoxLayout(netGroup);
    m_kadEnabledCheck = new QCheckBox(tr("Kad"), netGroup);
    netLayout->addWidget(m_kadEnabledCheck);
    m_ed2kEnabledCheck = new QCheckBox(tr("eD2K"), netGroup);
    netLayout->addWidget(m_ed2kEnabledCheck);
    row4->addWidget(netGroup);

    layout->addLayout(row4);

    layout->addStretch();

    // --- Wire slider ↔ label sync ---
    connect(m_downloadLimitSlider, &QSlider::valueChanged, this, [this](int val) {
        m_downloadLimitLabel->setText(QStringLiteral("%1 KB/s").arg(val));
    });
    connect(m_uploadLimitSlider, &QSlider::valueChanged, this, [this](int val) {
        m_uploadLimitLabel->setText(QStringLiteral("%1 KB/s").arg(val));
    });

    // --- Wire capacity spin → slider max ---
    connect(m_capacityDownloadSpin, &QSpinBox::valueChanged, this, [this](int val) {
        m_downloadLimitSlider->setMaximum(val);
    });
    connect(m_capacityUploadSpin, &QSpinBox::valueChanged, this, [this](int val) {
        m_uploadLimitSlider->setMaximum(val);
    });

    // --- Wire limit checkbox → slider enable/disable ---
    connect(m_downloadLimitCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_downloadLimitSlider->setEnabled(checked);
        m_downloadLimitLabel->setEnabled(checked);
    });
    connect(m_uploadLimitCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_uploadLimitSlider->setEnabled(checked);
        m_uploadLimitLabel->setEnabled(checked);
    });

    // --- Wire UDP disable checkbox → port spin ---
    connect(m_udpDisableCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_udpPortSpin->setEnabled(!checked);
    });

    return page;
}

// ---------------------------------------------------------------------------
// Proxy page — matches MFC PPgProxy layout
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createProxyPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // --- General group ---
    auto* generalGroup = new QGroupBox(tr("General"), page);
    auto* generalLayout = new QVBoxLayout(generalGroup);

    m_proxyEnableCheck = new QCheckBox(tr("Enable proxy"), generalGroup);
    generalLayout->addWidget(m_proxyEnableCheck);

    auto* generalForm = new QHBoxLayout;
    auto* labelCol = new QVBoxLayout;
    auto* editCol = new QVBoxLayout;

    labelCol->addWidget(new QLabel(tr("Proxy type:"), generalGroup));
    m_proxyTypeCombo = new QComboBox(generalGroup);
    m_proxyTypeCombo->addItem(tr("No Proxy"));    // 0
    m_proxyTypeCombo->addItem(tr("SOCKS4"));       // 1
    m_proxyTypeCombo->addItem(tr("SOCKS4a"));      // 2
    m_proxyTypeCombo->addItem(tr("SOCKS5"));       // 3
    m_proxyTypeCombo->addItem(tr("HTTP/1.0"));     // 4
    m_proxyTypeCombo->addItem(tr("HTTP/1.1"));     // 5
    editCol->addWidget(m_proxyTypeCombo);

    labelCol->addWidget(new QLabel(tr("Proxy host:"), generalGroup));
    m_proxyHostEdit = new QLineEdit(generalGroup);
    editCol->addWidget(m_proxyHostEdit);

    labelCol->addWidget(new QLabel(tr("Proxy port:"), generalGroup));
    m_proxyPortSpin = new QSpinBox(generalGroup);
    m_proxyPortSpin->setRange(1, 65535);
    m_proxyPortSpin->setValue(1080);
    editCol->addWidget(m_proxyPortSpin);

    generalForm->addLayout(labelCol);
    generalForm->addLayout(editCol, 1);
    generalLayout->addLayout(generalForm);
    layout->addWidget(generalGroup);

    // --- Authentication group ---
    auto* authGroup = new QGroupBox(tr("Authentication"), page);
    auto* authLayout = new QVBoxLayout(authGroup);

    m_proxyAuthCheck = new QCheckBox(tr("Enable authentication"), authGroup);
    authLayout->addWidget(m_proxyAuthCheck);

    auto* authForm = new QHBoxLayout;
    auto* authLabelCol = new QVBoxLayout;
    auto* authEditCol = new QVBoxLayout;

    authLabelCol->addWidget(new QLabel(tr("Name:"), authGroup));
    m_proxyUserEdit = new QLineEdit(authGroup);
    authEditCol->addWidget(m_proxyUserEdit);

    authLabelCol->addWidget(new QLabel(tr("Password:"), authGroup));
    m_proxyPasswordEdit = new QLineEdit(authGroup);
    m_proxyPasswordEdit->setEchoMode(QLineEdit::Password);
    authEditCol->addWidget(m_proxyPasswordEdit);

    authForm->addLayout(authLabelCol);
    authForm->addLayout(authEditCol, 1);
    authLayout->addLayout(authForm);
    layout->addWidget(authGroup);

    layout->addStretch();

    // Set initial enabled states (all disabled until proxy is enabled)
    m_proxyTypeCombo->setEnabled(false);
    m_proxyHostEdit->setEnabled(false);
    m_proxyPortSpin->setEnabled(false);
    m_proxyAuthCheck->setEnabled(false);
    m_proxyUserEdit->setEnabled(false);
    m_proxyPasswordEdit->setEnabled(false);

    return page;
}

// ---------------------------------------------------------------------------
// Server page
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createServerPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // --- Update group ---
    auto* updateGroup = new QGroupBox(tr("Update"), page);
    auto* updateLayout = new QVBoxLayout(updateGroup);

    // "Remove dead servers after [N] retries"
    auto* retriesRow = new QHBoxLayout;
    auto* retriesLabel = new QLabel(tr("Remove dead servers after"), updateGroup);
    m_deadServerRetriesSpin = new QSpinBox(updateGroup);
    m_deadServerRetriesSpin->setRange(1, 99);
    m_deadServerRetriesSpin->setValue(20);
    m_deadServerRetriesSpin->setMinimumWidth(50);
    auto* retriesSuffix = new QLabel(tr("retries"), updateGroup);
    retriesRow->addSpacing(20);
    retriesRow->addWidget(retriesLabel);
    retriesRow->addWidget(m_deadServerRetriesSpin);
    retriesRow->addWidget(retriesSuffix);
    retriesRow->addStretch();
    updateLayout->addLayout(retriesRow);

    // "Auto-update server list at startup" + "List..." button
    auto* autoUpdateRow = new QHBoxLayout;
    m_autoUpdateServerListCheck = new QCheckBox(tr("Auto-update server list at startup"), updateGroup);
    m_listUrlBtn = new QPushButton(tr("List..."), updateGroup);
    autoUpdateRow->addWidget(m_autoUpdateServerListCheck);
    autoUpdateRow->addStretch();
    autoUpdateRow->addWidget(m_listUrlBtn);
    updateLayout->addLayout(autoUpdateRow);

    connect(m_listUrlBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        QString url = QInputDialog::getText(this, tr("Server List URL"),
                                            tr("Enter the URL for server.met download:"),
                                            QLineEdit::Normal, m_serverListURLValue, &ok);
        if (ok) {
            m_serverListURLValue = url;
            markDirty();
        }
    });

    // "Update server list when connecting to a server"
    m_addServersFromServerCheck = new QCheckBox(tr("Update server list when connecting to a server"), updateGroup);
    updateLayout->addWidget(m_addServersFromServerCheck);

    // "Update server list when a client connects"
    m_addServersFromClientsCheck = new QCheckBox(tr("Update server list when a client connects"), updateGroup);
    updateLayout->addWidget(m_addServersFromClientsCheck);

    layout->addWidget(updateGroup);

    // --- Miscellaneous group ---
    auto* miscGroup = new QGroupBox(tr("Miscellaneous"), page);
    auto* miscLayout = new QVBoxLayout(miscGroup);

    // "Use smart LowID check on connect"
    m_smartLowIdCheck = new QCheckBox(tr("Use smart LowID check on connect"), miscGroup);
    miscLayout->addWidget(m_smartLowIdCheck);

    // "Safe Connect"
    m_safeServerConnectCheck = new QCheckBox(tr("Safe Connect"), miscGroup);
    miscLayout->addWidget(m_safeServerConnectCheck);

    // "Autoconnect to servers in static list only"
    m_autoConnectStaticOnlyCheck = new QCheckBox(tr("Autoconnect to servers in static list only"), miscGroup);
    miscLayout->addWidget(m_autoConnectStaticOnlyCheck);

    // "Use priority system"
    m_useServerPrioritiesCheck = new QCheckBox(tr("Use priority system"), miscGroup);
    miscLayout->addWidget(m_useServerPrioritiesCheck);

    // "Set manually added servers to high priority"
    m_manualHighPrioCheck = new QCheckBox(tr("Set manually added servers to high priority"), miscGroup);
    miscLayout->addWidget(m_manualHighPrioCheck);

    layout->addWidget(miscGroup);

    layout->addStretch();
    return page;
}

// ---------------------------------------------------------------------------
// CheckableFileSystemModel — QFileSystemModel with checkboxes for directories
// ---------------------------------------------------------------------------

namespace {

class CheckableFileSystemModel : public QFileSystemModel {
public:
    explicit CheckableFileSystemModel(QObject* parent = nullptr)
        : QFileSystemModel(parent) {}

    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        auto f = QFileSystemModel::flags(index);
        if (index.column() == 0)
            f |= Qt::ItemIsUserCheckable;
        return f;
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (role == Qt::CheckStateRole && index.column() == 0) {
            const QString path = filePath(index);
            return m_checked.contains(path) ? Qt::Checked : Qt::Unchecked;
        }
        return QFileSystemModel::data(index, role);
    }

    bool setData(const QModelIndex& index, const QVariant& value, int role) override
    {
        if (role == Qt::CheckStateRole && index.column() == 0) {
            const QString path = filePath(index);
            if (value.toInt() == Qt::Checked)
                m_checked.insert(path);
            else
                m_checked.remove(path);
            emit dataChanged(index, index, {Qt::CheckStateRole});
            return true;
        }
        return QFileSystemModel::setData(index, value, role);
    }

    void setCheckedPaths(const QStringList& paths)
    {
        m_checked.clear();
        for (const auto& p : paths)
            m_checked.insert(p);
        // Notify the view that check states may have changed everywhere
        emit layoutChanged();
    }

    [[nodiscard]] QStringList checkedPaths() const
    {
        return QStringList(m_checked.begin(), m_checked.end());
    }

private:
    QSet<QString> m_checked;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Directories page — matches MFC "Options Directories.png"
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createDirectoriesPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    // --- Incoming Files group ---
    auto* incomingGroup = new QGroupBox(tr("Incoming Files"), page);
    auto* incomingLayout = new QHBoxLayout(incomingGroup);
    m_incomingDirEdit = new QLineEdit(incomingGroup);
    m_incomingDirEdit->setReadOnly(true);
    incomingLayout->addWidget(m_incomingDirEdit);
    auto* incomingBrowseBtn = new QPushButton(incomingGroup);
    incomingBrowseBtn->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    incomingBrowseBtn->setFixedSize(28, 28);
    incomingLayout->addWidget(incomingBrowseBtn);
    layout->addWidget(incomingGroup);

    connect(incomingBrowseBtn, &QPushButton::clicked, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select Incoming Directory"), m_incomingDirEdit->text());
        if (!dir.isEmpty())
            m_incomingDirEdit->setText(dir);
    });

    // --- Temporary Files group ---
    auto* tempGroup = new QGroupBox(tr("Temporary Files"), page);
    auto* tempLayout = new QHBoxLayout(tempGroup);
    m_tempDirEdit = new QLineEdit(tempGroup);
    m_tempDirEdit->setReadOnly(true);
    tempLayout->addWidget(m_tempDirEdit);
    auto* tempBrowseBtn = new QPushButton(tempGroup);
    tempBrowseBtn->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    tempBrowseBtn->setFixedSize(28, 28);
    tempLayout->addWidget(tempBrowseBtn);
    layout->addWidget(tempGroup);

    connect(tempBrowseBtn, &QPushButton::clicked, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select Temporary Directory"), m_tempDirEdit->text());
        if (!dir.isEmpty())
            m_tempDirEdit->setText(dir);
    });

    // --- Shared Directories group ---
    auto* sharedGroup = new QGroupBox(tr("Shared Directories (Ctrl+Click includes subdirectories)"), page);
    auto* sharedLayout = new QVBoxLayout(sharedGroup);

    auto* fsModel = new CheckableFileSystemModel(this);
    fsModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
    fsModel->setRootPath(QDir::homePath());
    m_sharedDirsModel = fsModel;

    m_sharedDirsTree = new QTreeView(sharedGroup);
    m_sharedDirsTree->setModel(fsModel);
    m_sharedDirsTree->setRootIndex(fsModel->index(QDir::homePath()));
    // Show only the Name column
    for (int i = 1; i < fsModel->columnCount(); ++i)
        m_sharedDirsTree->setColumnHidden(i, true);
    m_sharedDirsTree->setHeaderHidden(true);

    sharedLayout->addWidget(m_sharedDirsTree);
    layout->addWidget(sharedGroup, 1);  // stretch factor for the tree

    // --- Add UNC share button (disabled, not applicable on macOS) ---
    auto* uncBtn = new QPushButton(tr("Add UNC share"), page);
    uncBtn->setEnabled(false);  // ToDo: implement UNC share support
    layout->addWidget(uncBtn, 0, Qt::AlignLeft);

    return page;
}

// ---------------------------------------------------------------------------
// Files page — matches MFC "Options Files.png"
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createFilesPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    // === Initializations group ===
    auto* initGroup = new QGroupBox(tr("Initializations"), page);
    auto* initLayout = new QVBoxLayout(initGroup);

    m_addFilesPausedCheck = new QCheckBox(tr("Add files to download in paused mode"), initGroup);
    initLayout->addWidget(m_addFilesPausedCheck);

    m_autoSharedFilesPrioCheck = new QCheckBox(tr("Add new shared files with auto priority"), initGroup);
    initLayout->addWidget(m_autoSharedFilesPrioCheck);

    m_autoDownloadPrioCheck = new QCheckBox(tr("Add new downloads with auto priority"), initGroup);
    initLayout->addWidget(m_autoDownloadPrioCheck);

    auto* cleanupRow = new QHBoxLayout;
    m_autoCleanupFilenamesCheck = new QCheckBox(tr("Auto cleanup file names of new downloads"), initGroup);
    cleanupRow->addWidget(m_autoCleanupFilenamesCheck);
    cleanupRow->addStretch();
    auto* editCleanupBtn = new QPushButton(tr("Edit..."), initGroup);
    connect(editCleanupBtn, &QPushButton::clicked, this, [this]() {
        auto* dlg = new QDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle(tr("Filename Cleanup Rules"));
        dlg->setMinimumSize(500, 350);
        auto* layout = new QVBoxLayout(dlg);
        layout->addWidget(new QLabel(
            tr("Define patterns to automatically clean up filenames of new downloads.\n"
               "Each rule replaces a regex pattern with a replacement string.")));

        auto* table = new QTreeWidget(dlg);
        table->setHeaderLabels({tr("Pattern"), tr("Replacement"), tr("Enabled")});
        table->setRootIsDecorated(false);
        table->setColumnCount(3);
        table->header()->setStretchLastSection(false);
        table->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        layout->addWidget(table);

        // Default cleanup rules (common in eMule)
        auto addRule = [table](const QString& pattern, const QString& replacement, bool enabled) {
            auto* item = new QTreeWidgetItem(table);
            item->setText(0, pattern);
            item->setText(1, replacement);
            item->setCheckState(2, enabled ? Qt::Checked : Qt::Unchecked);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        };
        addRule(QStringLiteral("\\[www\\..*?\\]"), QString(), true);
        addRule(QStringLiteral("_"), QStringLiteral(" "), true);

        auto* btnLayout = new QHBoxLayout;
        auto* addBtn = new QPushButton(tr("Add"), dlg);
        connect(addBtn, &QPushButton::clicked, dlg, [table, addRule]() {
            addRule(QString(), QString(), true);
        });
        auto* removeBtn = new QPushButton(tr("Remove"), dlg);
        connect(removeBtn, &QPushButton::clicked, dlg, [table]() {
            delete table->currentItem();
        });
        btnLayout->addWidget(addBtn);
        btnLayout->addWidget(removeBtn);
        btnLayout->addStretch();
        layout->addLayout(btnLayout);

        auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
        connect(btnBox, &QDialogButtonBox::rejected, dlg, &QDialog::close);
        layout->addWidget(btnBox);
        dlg->show();
    });
    cleanupRow->addWidget(editCleanupBtn);
    initLayout->addLayout(cleanupRow);

    layout->addWidget(initGroup);

    // === Miscellaneous group ===
    auto* miscGroup = new QGroupBox(tr("Miscellaneous"), page);
    auto* miscLayout = new QVBoxLayout(miscGroup);

    m_transferFullChunksCheck = new QCheckBox(tr("Try to transfer full chunks to all uploads"), miscGroup);
    miscLayout->addWidget(m_transferFullChunksCheck);

    m_previewPrioCheck = new QCheckBox(tr("Try to download preview chunks first"), miscGroup);
    miscLayout->addWidget(m_previewPrioCheck);

    m_watchClipboardCheck = new QCheckBox(tr("Watch clipboard for eD2K file links"), miscGroup);
    miscLayout->addWidget(m_watchClipboardCheck);

    m_advancedCalcRemainingCheck = new QCheckBox(tr("Use advanced calculation method for remaining time"), miscGroup);
    miscLayout->addWidget(m_advancedCalcRemainingCheck);

    m_startNextPausedCheck = new QCheckBox(tr("Start next paused file when a file completes"), miscGroup);
    miscLayout->addWidget(m_startNextPausedCheck);

    // Indented sub-checkboxes for same-category preferences
    auto* sameCatLayout = new QVBoxLayout;
    sameCatLayout->setContentsMargins(30, 0, 0, 0);
    m_preferSameCatCheck = new QCheckBox(tr("Prefer same category"), miscGroup);
    m_preferSameCatCheck->setEnabled(false);
    sameCatLayout->addWidget(m_preferSameCatCheck);
    m_onlySameCatCheck = new QCheckBox(tr("Only in same category"), miscGroup);
    m_onlySameCatCheck->setEnabled(false);
    sameCatLayout->addWidget(m_onlySameCatCheck);
    miscLayout->addLayout(sameCatLayout);

    // Wire start-next-paused toggle → enable/disable sub-checkboxes
    connect(m_startNextPausedCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_preferSameCatCheck->setEnabled(on);
        m_onlySameCatCheck->setEnabled(on);
    });

    m_rememberDownloadedCheck = new QCheckBox(tr("Remember downloaded files"), miscGroup);
    miscLayout->addWidget(m_rememberDownloadedCheck);

    m_rememberCancelledCheck = new QCheckBox(tr("Remember cancelled files"), miscGroup);
    miscLayout->addWidget(m_rememberCancelledCheck);

    layout->addWidget(miscGroup);

    // === Video Player group ===
    auto* videoGroup = new QGroupBox(tr("Video Player"), page);
    auto* videoLayout = new QVBoxLayout(videoGroup);

    videoLayout->addWidget(new QLabel(tr("Command"), videoGroup));
    auto* cmdRow = new QHBoxLayout;
    m_videoPlayerCmdEdit = new QLineEdit(videoGroup);
    cmdRow->addWidget(m_videoPlayerCmdEdit);
    auto* browseVideoBtn = new QPushButton(videoGroup);
    browseVideoBtn->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    browseVideoBtn->setFixedSize(28, 28);
    cmdRow->addWidget(browseVideoBtn);
    videoLayout->addLayout(cmdRow);

    connect(browseVideoBtn, &QPushButton::clicked, this, [this] {
        QString file = QFileDialog::getOpenFileName(
            this, tr("Select Video Player"), m_videoPlayerCmdEdit->text());
        if (!file.isEmpty())
            m_videoPlayerCmdEdit->setText(file);
    });

    videoLayout->addWidget(new QLabel(tr("Arguments"), videoGroup));
    m_videoPlayerArgsEdit = new QLineEdit(videoGroup);
    videoLayout->addWidget(m_videoPlayerArgsEdit);

    m_createBackupToPreviewCheck = new QCheckBox(tr("Create backup to preview"), videoGroup);
    videoLayout->addWidget(m_createBackupToPreviewCheck);

    layout->addWidget(videoGroup);

    layout->addStretch();
    return page;
}

// ---------------------------------------------------------------------------
// Notifications page — matches MFC "Options Notifications.png"
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createNotificationsPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    // === Pop-up Message group ===
    auto* popupGroup = new QGroupBox(tr("Pop-up Message"), page);
    auto* popupLayout = new QVBoxLayout(popupGroup);

    m_soundGroup = new QButtonGroup(this);

    // "No sound" row with "Test" button right-aligned
    auto* noSoundRow = new QHBoxLayout;
    m_noSoundRadio = new QRadioButton(tr("No sound"), popupGroup);
    noSoundRow->addWidget(m_noSoundRadio);
    noSoundRow->addStretch();
    m_testSoundBtn = new QPushButton(tr("Test"), popupGroup);
    m_testSoundBtn->setFixedWidth(80);
    noSoundRow->addWidget(m_testSoundBtn);
    popupLayout->addLayout(noSoundRow);

    // "Play sound" radio
    m_playSoundRadio = new QRadioButton(tr("Play sound"), popupGroup);
    popupLayout->addWidget(m_playSoundRadio);

    // Indented sound file row
    auto* soundFileRow = new QHBoxLayout;
    soundFileRow->setContentsMargins(30, 0, 0, 0);
    m_soundFileEdit = new QLineEdit(popupGroup);
    soundFileRow->addWidget(m_soundFileEdit);
    m_soundBrowseBtn = new QPushButton(popupGroup);
    m_soundBrowseBtn->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    m_soundBrowseBtn->setFixedSize(28, 28);
    soundFileRow->addWidget(m_soundBrowseBtn);
    popupLayout->addLayout(soundFileRow);

    // "Speak notification message" radio (disabled — no QTextToSpeech)
    m_speakRadio = new QRadioButton(tr("Speak notification message"), popupGroup);
    m_speakRadio->setEnabled(false);
    popupLayout->addWidget(m_speakRadio);

    m_soundGroup->addButton(m_noSoundRadio, 0);
    m_soundGroup->addButton(m_playSoundRadio, 1);
    m_soundGroup->addButton(m_speakRadio, 2);
    m_noSoundRadio->setChecked(true);

    layout->addWidget(popupGroup);

    // Wire sound radio → enable/disable file controls
    auto updateSoundControls = [this]() {
        bool playSoundOn = m_playSoundRadio->isChecked();
        m_soundFileEdit->setEnabled(playSoundOn);
        m_soundBrowseBtn->setEnabled(playSoundOn);
    };
    connect(m_noSoundRadio, &QRadioButton::toggled, this, updateSoundControls);
    connect(m_playSoundRadio, &QRadioButton::toggled, this, updateSoundControls);
    updateSoundControls();

    // Browse for sound file
    connect(m_soundBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(
            this, tr("Select Sound File"), m_soundFileEdit->text(),
            tr("Sound Files (*.wav *.mp3 *.ogg);;All Files (*)"));
        if (!file.isEmpty())
            m_soundFileEdit->setText(file);
    });

    // Test button — play selected sound
    connect(m_testSoundBtn, &QPushButton::clicked, this, [this]() {
        if (m_playSoundRadio->isChecked() && !m_soundFileEdit->text().isEmpty()) {
            auto* effect = new QSoundEffect(this);
            effect->setSource(QUrl::fromLocalFile(m_soundFileEdit->text()));
            effect->setVolume(1.0f);
            effect->play();
            connect(effect, &QSoundEffect::playingChanged, effect, [effect]() {
                if (!effect->isPlaying())
                    effect->deleteLater();
            });
        }
    });

    // === Pop-up when group ===
    auto* whenGroup = new QGroupBox(tr("Pop-up when"), page);
    auto* whenLayout = new QVBoxLayout(whenGroup);

    m_notifyLogCheck = new QCheckBox(tr("Log entry added"), whenGroup);
    whenLayout->addWidget(m_notifyLogCheck);

    m_notifyChatCheck = new QCheckBox(tr("Chat session started"), whenGroup);
    whenLayout->addWidget(m_notifyChatCheck);

    // Indented "Chat message received"
    auto* chatMsgLayout = new QVBoxLayout;
    chatMsgLayout->setContentsMargins(30, 0, 0, 0);
    m_notifyChatMsgCheck = new QCheckBox(tr("Chat message received"), whenGroup);
    m_notifyChatMsgCheck->setEnabled(false);
    chatMsgLayout->addWidget(m_notifyChatMsgCheck);
    whenLayout->addLayout(chatMsgLayout);

    m_notifyDownloadAddedCheck = new QCheckBox(tr("Download added"), whenGroup);
    whenLayout->addWidget(m_notifyDownloadAddedCheck);

    m_notifyDownloadFinishedCheck = new QCheckBox(tr("Download finished (*)"), whenGroup);
    whenLayout->addWidget(m_notifyDownloadFinishedCheck);

    m_notifyNewVersionCheck = new QCheckBox(tr("New eMule version detected"), whenGroup);
    whenLayout->addWidget(m_notifyNewVersionCheck);

    m_notifyUrgentCheck = new QCheckBox(tr("Urgent: out of disk space, server connection lost (*)"), whenGroup);
    whenLayout->addWidget(m_notifyUrgentCheck);

    layout->addWidget(whenGroup);

    // Wire Chat started → Chat message received enable
    connect(m_notifyChatCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_notifyChatMsgCheck->setEnabled(on);
        if (!on) m_notifyChatMsgCheck->setChecked(false);
    });

    // === (*) Email Notifications group ===
    auto* emailGroup = new QGroupBox(tr("(*) Email Notifications"), page);
    auto* emailLayout = new QVBoxLayout(emailGroup);

    m_emailEnabledCheck = new QCheckBox(tr("Enable email notifications"), emailGroup);
    emailLayout->addWidget(m_emailEnabledCheck);

    // SMTP server button (centered)
    auto* smtpRow = new QHBoxLayout;
    smtpRow->addStretch();
    m_smtpServerBtn = new QPushButton(tr("SMTP server..."), emailGroup);
    m_smtpServerBtn->setEnabled(false);
    smtpRow->addWidget(m_smtpServerBtn);
    smtpRow->addStretch();
    emailLayout->addLayout(smtpRow);

    // Recipient address row
    auto* recipientRow = new QHBoxLayout;
    auto* recipientLabel = new QLabel(tr("Recipient address:"), emailGroup);
    recipientLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    recipientLabel->setMinimumWidth(120);
    recipientRow->addWidget(recipientLabel);
    m_emailRecipientEdit = new QLineEdit(emailGroup);
    m_emailRecipientEdit->setEnabled(false);
    recipientRow->addWidget(m_emailRecipientEdit);
    emailLayout->addLayout(recipientRow);

    // Sender address row
    auto* senderRow = new QHBoxLayout;
    auto* senderLabel = new QLabel(tr("Sender address:"), emailGroup);
    senderLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    senderLabel->setMinimumWidth(120);
    senderRow->addWidget(senderLabel);
    m_emailSenderEdit = new QLineEdit(emailGroup);
    m_emailSenderEdit->setEnabled(false);
    senderRow->addWidget(m_emailSenderEdit);
    emailLayout->addLayout(senderRow);

    layout->addWidget(emailGroup);

    // Wire email enabled → SMTP/recipient/sender
    connect(m_emailEnabledCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_smtpServerBtn->setEnabled(on);
        m_emailRecipientEdit->setEnabled(on);
        m_emailSenderEdit->setEnabled(on);
    });

    // SMTP server dialog
    connect(m_smtpServerBtn, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("SMTP Server Settings"));
        auto* form = new QFormLayout(&dlg);

        auto* serverEdit = new QLineEdit(m_smtpServer, &dlg);
        form->addRow(tr("Server:"), serverEdit);

        auto* portSpin = new QSpinBox(&dlg);
        portSpin->setRange(1, 65535);
        portSpin->setValue(m_smtpPort);
        form->addRow(tr("Port:"), portSpin);

        auto* authCombo = new QComboBox(&dlg);
        authCombo->addItem(tr("None"));    // 0
        authCombo->addItem(tr("Plain"));   // 1
        authCombo->setCurrentIndex(m_smtpAuth);
        form->addRow(tr("Authentication:"), authCombo);

        auto* tlsCheck = new QCheckBox(tr("Use TLS/STARTTLS"), &dlg);
        tlsCheck->setChecked(m_smtpTls);
        form->addRow(tlsCheck);

        auto* userEdit = new QLineEdit(m_smtpUser, &dlg);
        form->addRow(tr("Username:"), userEdit);

        auto* passEdit = new QLineEdit(m_smtpPassword, &dlg);
        passEdit->setEchoMode(QLineEdit::Password);
        form->addRow(tr("Password:"), passEdit);

        auto* btnLayout = new QHBoxLayout;
        auto* okBtn = new QPushButton(tr("OK"), &dlg);
        auto* cancelBtn = new QPushButton(tr("Cancel"), &dlg);
        btnLayout->addStretch();
        btnLayout->addWidget(okBtn);
        btnLayout->addWidget(cancelBtn);
        form->addRow(btnLayout);

        connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

        if (dlg.exec() == QDialog::Accepted) {
            m_smtpServer = serverEdit->text();
            m_smtpPort = portSpin->value();
            m_smtpAuth = authCombo->currentIndex();
            m_smtpTls = tlsCheck->isChecked();
            m_smtpUser = userEdit->text();
            m_smtpPassword = passEdit->text();
            markDirty();
        }
    });

    layout->addStretch();
    return page;
}

// ---------------------------------------------------------------------------
// IRC page — matches MFC "Options IRC.png"
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createIRCPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    // --- Server group ---
    auto* serverGroup = new QGroupBox(tr("Server"), page);
    auto* serverLayout = new QVBoxLayout(serverGroup);
    m_ircServerEdit = new QLineEdit(serverGroup);
    m_ircServerEdit->setPlaceholderText(QStringLiteral("irc.mindforge.org:6667"));
    serverLayout->addWidget(m_ircServerEdit);
    layout->addWidget(serverGroup);

    // --- Nick group ---
    auto* nickGroup = new QGroupBox(tr("Nick"), page);
    auto* nickLayout = new QVBoxLayout(nickGroup);
    m_ircNickEdit = new QLineEdit(nickGroup);
    m_ircNickEdit->setMaxLength(25);
    nickLayout->addWidget(m_ircNickEdit);
    layout->addWidget(nickGroup);

    // --- Channels group ---
    auto* channelsGroup = new QGroupBox(tr("Channels"), page);
    auto* channelsLayout = new QVBoxLayout(channelsGroup);
    m_ircUseChannelFilterCheck = new QCheckBox(tr("Use channel list filter"), channelsGroup);
    channelsLayout->addWidget(m_ircUseChannelFilterCheck);

    auto* filterRow = new QHBoxLayout;
    auto* nameLabel = new QLabel(tr("Name"), channelsGroup);
    m_ircChannelFilterNameEdit = new QLineEdit(channelsGroup);
    auto* usersLabel = new QLabel(tr("Users"), channelsGroup);
    m_ircChannelFilterUsersSpin = new QSpinBox(channelsGroup);
    m_ircChannelFilterUsersSpin->setRange(0, 99999);
    m_ircChannelFilterUsersSpin->setValue(0);
    filterRow->addWidget(nameLabel);
    filterRow->addWidget(m_ircChannelFilterNameEdit, 1);
    filterRow->addWidget(usersLabel);
    filterRow->addWidget(m_ircChannelFilterUsersSpin);
    channelsLayout->addLayout(filterRow);

    m_ircChannelFilterNameEdit->setEnabled(false);
    m_ircChannelFilterUsersSpin->setEnabled(false);
    connect(m_ircUseChannelFilterCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_ircChannelFilterNameEdit->setEnabled(on);
        m_ircChannelFilterUsersSpin->setEnabled(on);
    });
    layout->addWidget(channelsGroup);

    // --- Perform group ---
    auto* performGroup = new QGroupBox(tr("Perform"), page);
    auto* performLayout = new QVBoxLayout(performGroup);
    m_ircUsePerformCheck = new QCheckBox(tr("Use perform string on connect"), performGroup);
    performLayout->addWidget(m_ircUsePerformCheck);
    m_ircPerformEdit = new QLineEdit(performGroup);
    m_ircPerformEdit->setEnabled(false);
    performLayout->addWidget(m_ircPerformEdit);
    connect(m_ircUsePerformCheck, &QCheckBox::toggled, m_ircPerformEdit, &QLineEdit::setEnabled);
    layout->addWidget(performGroup);

    // --- Miscellaneous group ---
    auto* miscGroup = new QGroupBox(tr("Miscellaneous"), page);
    auto* miscLayout = new QVBoxLayout(miscGroup);
    m_ircMiscTree = new QTreeWidget(miscGroup);
    m_ircMiscTree->setHeaderHidden(true);
    m_ircMiscTree->setRootIsDecorated(true);
    m_ircMiscTree->setIndentation(20);

    // Top-level checkable items
    auto* helpItem = new QTreeWidgetItem(m_ircMiscTree);
    helpItem->setText(0, tr("Connect to help channel"));
    helpItem->setFlags(helpItem->flags() | Qt::ItemIsUserCheckable);
    helpItem->setCheckState(0, Qt::Checked);

    auto* loadListItem = new QTreeWidgetItem(m_ircMiscTree);
    loadListItem->setText(0, tr("Load server channel list on connect"));
    loadListItem->setFlags(loadListItem->flags() | Qt::ItemIsUserCheckable);
    loadListItem->setCheckState(0, Qt::Checked);

    auto* timestampItem = new QTreeWidgetItem(m_ircMiscTree);
    timestampItem->setText(0, tr("Add timestamp to messages"));
    timestampItem->setFlags(timestampItem->flags() | Qt::ItemIsUserCheckable);
    timestampItem->setCheckState(0, Qt::Checked);

    // "Ignore info messages" parent with auto-tristate
    auto* ignoreParent = new QTreeWidgetItem(m_ircMiscTree);
    ignoreParent->setText(0, tr("Ignore info messages"));
    ignoreParent->setFlags(ignoreParent->flags() | Qt::ItemIsAutoTristate | Qt::ItemIsUserCheckable);

    auto* ignoreMisc = new QTreeWidgetItem(ignoreParent);
    ignoreMisc->setText(0, tr("Ignore misc. info messages"));
    ignoreMisc->setFlags(ignoreMisc->flags() | Qt::ItemIsUserCheckable);
    ignoreMisc->setCheckState(0, Qt::Unchecked);

    auto* ignoreJoin = new QTreeWidgetItem(ignoreParent);
    ignoreJoin->setText(0, tr("Ignore Join info messages"));
    ignoreJoin->setFlags(ignoreJoin->flags() | Qt::ItemIsUserCheckable);
    ignoreJoin->setCheckState(0, Qt::Checked);

    auto* ignorePart = new QTreeWidgetItem(ignoreParent);
    ignorePart->setText(0, tr("Ignore Part info messages"));
    ignorePart->setFlags(ignorePart->flags() | Qt::ItemIsUserCheckable);
    ignorePart->setCheckState(0, Qt::Checked);

    auto* ignoreQuit = new QTreeWidgetItem(ignoreParent);
    ignoreQuit->setText(0, tr("Ignore Quit info messages"));
    ignoreQuit->setFlags(ignoreQuit->flags() | Qt::ItemIsUserCheckable);
    ignoreQuit->setCheckState(0, Qt::Checked);

    m_ircMiscTree->expandAll();
    miscLayout->addWidget(m_ircMiscTree);
    layout->addWidget(miscGroup);

    layout->addStretch();

    // --- markDirty connections ---
    connect(m_ircServerEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_ircNickEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_ircUseChannelFilterCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_ircChannelFilterNameEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_ircChannelFilterUsersSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_ircUsePerformCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_ircPerformEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_ircMiscTree, &QTreeWidget::itemChanged, this, [this] { markDirty(); });

    return page;
}

// ---------------------------------------------------------------------------
// Messages and Comments page — matches MFC "Options Messages and Comments.png"
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createMessagesPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    // --- Messages group ---
    auto* msgGroup = new QGroupBox(tr("Messages"), page);
    auto* msgLayout = new QVBoxLayout(msgGroup);

    msgLayout->addWidget(new QLabel(tr("Filter messages containing: (Separator | )"), msgGroup));
    m_messageFilterEdit = new QLineEdit(msgGroup);
    msgLayout->addWidget(m_messageFilterEdit);

    m_msgFriendsOnlyCheck = new QCheckBox(tr("Accept from friends only"), msgGroup);
    msgLayout->addWidget(m_msgFriendsOnlyCheck);

    m_advancedSpamFilterCheck = new QCheckBox(tr("Advanced spam filter"), msgGroup);
    msgLayout->addWidget(m_advancedSpamFilterCheck);

    // Indented captcha checkbox
    auto* captchaLayout = new QHBoxLayout;
    captchaLayout->setContentsMargins(20, 0, 0, 0);
    m_requireCaptchaCheck = new QCheckBox(tr("Require captcha authentication"), msgGroup);
    captchaLayout->addWidget(m_requireCaptchaCheck);
    captchaLayout->addStretch();
    msgLayout->addLayout(captchaLayout);

    m_showSmileysCheck = new QCheckBox(tr("Show smileys"), msgGroup);
    msgLayout->addWidget(m_showSmileysCheck);

    layout->addWidget(msgGroup);

    // --- Comments group ---
    auto* cmtGroup = new QGroupBox(tr("Comments"), page);
    auto* cmtLayout = new QVBoxLayout(cmtGroup);

    cmtLayout->addWidget(new QLabel(tr("Ignore comments containing: (Separator | )"), cmtGroup));
    m_commentFilterEdit = new QLineEdit(cmtGroup);
    cmtLayout->addWidget(m_commentFilterEdit);

    m_indicateRatingsCheck = new QCheckBox(tr("Indicate downloads with comments/rating by icon"), cmtGroup);
    cmtLayout->addWidget(m_indicateRatingsCheck);

    layout->addWidget(cmtGroup);
    layout->addStretch();

    // Wire captcha enabled state to spam filter checkbox
    connect(m_advancedSpamFilterCheck, &QCheckBox::toggled,
            m_requireCaptchaCheck, &QCheckBox::setEnabled);

    return page;
}

// ---------------------------------------------------------------------------
// Security page — matches MFC "Options Security.png"
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createSecurityPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    // --- IP Filter group ---
    auto* ipFilterGroup = new QGroupBox(tr("IP Filter"), page);
    auto* ipFilterLayout = new QVBoxLayout(ipFilterGroup);

    m_filterServersByIPCheck = new QCheckBox(tr("Filter servers too"), ipFilterGroup);
    ipFilterLayout->addWidget(m_filterServersByIPCheck);

    // Filter level row: label "Filter level:  <", spin (0-255), Reload button, Edit button
    auto* filterLevelRow = new QHBoxLayout;
    filterLevelRow->addSpacing(20);
    filterLevelRow->addWidget(new QLabel(tr("Filter level:   <"), ipFilterGroup));
    m_ipFilterLevelSpin = new QSpinBox(ipFilterGroup);
    m_ipFilterLevelSpin->setRange(0, 255);
    m_ipFilterLevelSpin->setValue(127);
    m_ipFilterLevelSpin->setFixedWidth(70);
    filterLevelRow->addWidget(m_ipFilterLevelSpin);
    filterLevelRow->addSpacing(10);
    m_reloadIPFilterBtn = new QPushButton(tr("Reload"), ipFilterGroup);
    filterLevelRow->addWidget(m_reloadIPFilterBtn);
    auto* editBtn = new QPushButton(tr("Edit..."), ipFilterGroup);
    connect(editBtn, &QPushButton::clicked, this, []() {
        const QString path = QDir(thePrefs.configDir()).filePath(QStringLiteral("ipfilter.dat"));
        if (!QFileInfo::exists(path)) {
            // Create empty file so the editor can open it
            QFile f(path);
            f.open(QIODevice::WriteOnly);
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    filterLevelRow->addWidget(editBtn);
    filterLevelRow->addStretch();
    ipFilterLayout->addLayout(filterLevelRow);

    // Update from URL row
    ipFilterLayout->addWidget(new QLabel(
        tr("Update from URL: (filter.dat- or PeerGuardian-format)"), ipFilterGroup));
    auto* urlRow = new QHBoxLayout;
    m_ipFilterUpdateUrlEdit = new QLineEdit(ipFilterGroup);
    m_ipFilterUpdateUrlEdit->setPlaceholderText(tr("http://example.com/ipfilter.dat"));
    urlRow->addWidget(m_ipFilterUpdateUrlEdit);
    auto* loadBtn = new QPushButton(tr("Load"), ipFilterGroup);
    connect(loadBtn, &QPushButton::clicked, this, [this, loadBtn]() {
        const QString url = m_ipFilterUpdateUrlEdit->text().trimmed();
        if (url.isEmpty())
            return;
        loadBtn->setEnabled(false);
        loadBtn->setText(tr("Loading..."));
        auto* nam = new QNetworkAccessManager(this);
        auto* reply = nam->get(QNetworkRequest(QUrl(url)));
        connect(reply, &QNetworkReply::finished, this, [this, reply, loadBtn, nam]() {
            reply->deleteLater();
            nam->deleteLater();
            loadBtn->setEnabled(true);
            loadBtn->setText(tr("Load"));
            if (reply->error() != QNetworkReply::NoError) {
                QMessageBox::warning(this, tr("IP Filter"),
                    tr("Failed to download IP filter: %1").arg(reply->errorString()));
                return;
            }
            const QString path = QDir(thePrefs.configDir()).filePath(QStringLiteral("ipfilter.dat"));
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(reply->readAll());
                f.close();
                if (m_ipc && m_ipc->isConnected()) {
                    Ipc::IpcMessage msg(Ipc::IpcMsgType::ReloadIPFilter);
                    m_ipc->sendRequest(std::move(msg));
                }
                QMessageBox::information(this, tr("IP Filter"), tr("IP filter updated and reloaded."));
            }
        });
    });
    urlRow->addWidget(loadBtn);
    ipFilterLayout->addLayout(urlRow);

    layout->addWidget(ipFilterGroup);

    // --- See My Shared Files/Directories group ---
    auto* sharedGroup = new QGroupBox(tr("See My Shared Files/Directories"), page);
    auto* sharedLayout = new QHBoxLayout(sharedGroup);
    m_viewSharedGroup = new QButtonGroup(this);

    auto* everybodyRadio = new QRadioButton(tr("Everybody"), sharedGroup);
    auto* friendsRadio = new QRadioButton(tr("Friends only"), sharedGroup);
    auto* nobodyRadio = new QRadioButton(tr("Nobody"), sharedGroup);

    m_viewSharedGroup->addButton(everybodyRadio, 2);
    m_viewSharedGroup->addButton(friendsRadio, 1);
    m_viewSharedGroup->addButton(nobodyRadio, 0);

    sharedLayout->addWidget(everybodyRadio);
    sharedLayout->addWidget(friendsRadio);
    sharedLayout->addWidget(nobodyRadio);
    sharedLayout->addStretch();

    layout->addWidget(sharedGroup);

    // --- Protocol Obfuscation group ---
    auto* obfuscGroup = new QGroupBox(tr("Protocol Obfuscation"), page);
    auto* obfuscLayout = new QVBoxLayout(obfuscGroup);

    m_cryptLayerRequestedCheck = new QCheckBox(tr("Enable protocol obfuscation"), obfuscGroup);
    obfuscLayout->addWidget(m_cryptLayerRequestedCheck);

    m_cryptLayerRequiredCheck = new QCheckBox(
        tr("Allow obfuscated connections only (not recommended)"), obfuscGroup);
    obfuscLayout->addWidget(m_cryptLayerRequiredCheck);

    m_cryptLayerDisableCheck = new QCheckBox(
        tr("Disable support for obfuscated connections"), obfuscGroup);
    obfuscLayout->addWidget(m_cryptLayerDisableCheck);

    layout->addWidget(obfuscGroup);

    // --- Miscellaneous group ---
    auto* miscGroup = new QGroupBox(tr("Miscellaneous"), page);
    auto* miscLayout = new QVBoxLayout(miscGroup);

    m_useSecureIdentCheck = new QCheckBox(tr("Use secure identification"), miscGroup);
    miscLayout->addWidget(m_useSecureIdentCheck);

    auto* unprivCheck = new QCheckBox(tr("Run eMule as unprivileged user"), miscGroup);
    unprivCheck->setEnabled(false); // not applicable to Qt
    miscLayout->addWidget(unprivCheck);

    m_enableSearchResultFilterCheck = new QCheckBox(
        tr("Enable spam filter for search results"), miscGroup);
    miscLayout->addWidget(m_enableSearchResultFilterCheck);

    m_warnUntrustedFilesCheck = new QCheckBox(
        tr("Warn when opening untrusted files"), miscGroup);
    miscLayout->addWidget(m_warnUntrustedFilesCheck);

    layout->addWidget(miscGroup);
    layout->addStretch();

    return page;
}

// ---------------------------------------------------------------------------
// Statistics page
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createStatisticsPage()
{
    auto* page = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(page);

    // Initialize default MFC colors
    m_statsColors = {{
        QColor(0, 0, 64),       //  0 Background
        QColor(192, 192, 255),   //  1 Grid
        QColor(128, 255, 128),   //  2 DL current
        QColor(0, 210, 0),       //  3 DL average
        QColor(0, 128, 0),       //  4 DL session
        QColor(255, 128, 128),   //  5 UL current
        QColor(200, 0, 0),       //  6 UL average
        QColor(140, 0, 0),       //  7 UL session
        QColor(150, 150, 255),   //  8 Active connections
        QColor(192, 0, 192),     //  9 Total uploads
        QColor(255, 255, 128),   // 10 Active uploads
        QColor(0, 0, 0),         // 11 Icon bar (unused)
        QColor(255, 255, 255),   // 12 Active downloads
        QColor(255, 255, 255),   // 13 UL friend slots
        QColor(255, 190, 190),   // 14 UL slots no overhead
    }};

    // --- Graphs group ---
    auto* graphsGroup = new QGroupBox(tr("Graphs"), page);
    auto* graphsLayout = new QVBoxLayout(graphsGroup);

    // Graph update delay slider
    m_statsGraphUpdateLabel = new QLabel(tr("Update delay: 3 sec"), graphsGroup);
    m_statsGraphUpdateSlider = new QSlider(Qt::Horizontal, graphsGroup);
    m_statsGraphUpdateSlider->setRange(0, 200);
    m_statsGraphUpdateSlider->setValue(3);
    m_statsGraphUpdateSlider->setTickInterval(10);
    m_statsGraphUpdateSlider->setTickPosition(QSlider::TicksBelow);
    connect(m_statsGraphUpdateSlider, &QSlider::valueChanged, this, [this](int val) {
        m_statsGraphUpdateLabel->setText(val > 0
            ? tr("Update delay: %1 sec").arg(val)
            : tr("Update delay: disabled"));
        markDirty();
    });
    graphsLayout->addWidget(m_statsGraphUpdateLabel);
    graphsLayout->addWidget(m_statsGraphUpdateSlider);

    // Average time slider
    m_statsAvgTimeLabel = new QLabel(tr("Time for average graph: 5 mins"), graphsGroup);
    m_statsAvgTimeSlider = new QSlider(Qt::Horizontal, graphsGroup);
    m_statsAvgTimeSlider->setRange(1, 100);
    m_statsAvgTimeSlider->setValue(5);
    m_statsAvgTimeSlider->setTickInterval(5);
    m_statsAvgTimeSlider->setTickPosition(QSlider::TicksBelow);
    connect(m_statsAvgTimeSlider, &QSlider::valueChanged, this, [this](int val) {
        m_statsAvgTimeLabel->setText(tr("Time for average graph: %1 mins").arg(val));
        markDirty();
    });
    graphsLayout->addWidget(m_statsAvgTimeLabel);
    graphsLayout->addWidget(m_statsAvgTimeSlider);

    // Colors sub-group
    auto* colorsGroup = new QGroupBox(tr("Colors"), graphsGroup);
    auto* colorsLayout = new QVBoxLayout(colorsGroup);

    // Color selector row
    auto* colorRow = new QHBoxLayout;
    m_statsColorSelector = new QComboBox(colorsGroup);
    m_statsColorSelector->addItems({
        tr("Background"), tr("Grid"),
        tr("Download Current"), tr("Download Average"), tr("Download Session"),
        tr("Upload Current"), tr("Upload Average"), tr("Upload Session"),
        tr("Active Connections"), tr("Total Uploads"), tr("Active Uploads"),
        tr("Icon Bar"), tr("Active Downloads"),
        tr("Upload Friend Slots"), tr("Upload Slots (no overhead)")
    });
    m_statsColorBtn = new QPushButton(colorsGroup);
    m_statsColorBtn->setFixedSize(48, 24);
    m_statsColorBtn->setFlat(true);
    m_statsColorBtn->setAutoFillBackground(true);

    auto updateColorBtn = [this]() {
        int idx = m_statsColorSelector->currentIndex();
        if (idx >= 0 && idx < 15) {
            QPalette pal = m_statsColorBtn->palette();
            pal.setColor(QPalette::Button, m_statsColors[static_cast<size_t>(idx)]);
            m_statsColorBtn->setPalette(pal);
            m_statsColorBtn->setStyleSheet(
                QStringLiteral("background-color: %1; border: 1px solid gray;")
                    .arg(m_statsColors[static_cast<size_t>(idx)].name()));
        }
    };
    connect(m_statsColorSelector, &QComboBox::currentIndexChanged, this, updateColorBtn);
    connect(m_statsColorBtn, &QPushButton::clicked, this, [this, updateColorBtn]() {
        int idx = m_statsColorSelector->currentIndex();
        if (idx < 0 || idx >= 15) return;
        QColor chosen = QColorDialog::getColor(
            m_statsColors[static_cast<size_t>(idx)], this, tr("Select Color"));
        if (chosen.isValid()) {
            m_statsColors[static_cast<size_t>(idx)] = chosen;
            updateColorBtn();
            markDirty();
        }
    });
    colorRow->addWidget(m_statsColorSelector, 1);
    colorRow->addWidget(m_statsColorBtn);
    colorsLayout->addLayout(colorRow);

    // Fill graphs checkbox
    m_statsFillGraphsCheck = new QCheckBox(tr("Draw filled graphs"), colorsGroup);
    connect(m_statsFillGraphsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    colorsLayout->addWidget(m_statsFillGraphsCheck);

    // Connections Y-axis scale
    auto* yScaleRow = new QHBoxLayout;
    yScaleRow->addStretch();
    yScaleRow->addWidget(new QLabel(tr("Connections statistics Y-axis scale:"), colorsGroup));
    m_statsYScaleSpin = new QSpinBox(colorsGroup);
    m_statsYScaleSpin->setRange(0, 1000);
    m_statsYScaleSpin->setValue(100);
    connect(m_statsYScaleSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    yScaleRow->addWidget(m_statsYScaleSpin);
    colorsLayout->addLayout(yScaleRow);

    // Active connections ratio
    auto* ratioRow = new QHBoxLayout;
    ratioRow->addStretch();
    ratioRow->addWidget(new QLabel(tr("Active connections ratio:"), colorsGroup));
    m_statsRatioCombo = new QComboBox(colorsGroup);
    m_statsRatioCombo->addItems({
        QStringLiteral("1:1"), QStringLiteral("1:2"), QStringLiteral("1:3"),
        QStringLiteral("1:4"), QStringLiteral("1:5"), QStringLiteral("1:10"),
        QStringLiteral("1:20")
    });
    connect(m_statsRatioCombo, &QComboBox::currentIndexChanged, this, &OptionsDialog::markDirty);
    ratioRow->addWidget(m_statsRatioCombo);
    colorsLayout->addLayout(ratioRow);

    graphsLayout->addWidget(colorsGroup);
    mainLayout->addWidget(graphsGroup);

    // --- Statistics Tree group ---
    auto* treeGroup = new QGroupBox(tr("Statistics Tree"), page);
    auto* treeLayout = new QVBoxLayout(treeGroup);

    m_statsTreeUpdateLabel = new QLabel(tr("Update delay: 5 sec"), treeGroup);
    m_statsTreeUpdateSlider = new QSlider(Qt::Horizontal, treeGroup);
    m_statsTreeUpdateSlider->setRange(0, 200);
    m_statsTreeUpdateSlider->setValue(5);
    m_statsTreeUpdateSlider->setTickInterval(10);
    m_statsTreeUpdateSlider->setTickPosition(QSlider::TicksBelow);
    connect(m_statsTreeUpdateSlider, &QSlider::valueChanged, this, [this](int val) {
        m_statsTreeUpdateLabel->setText(val > 0
            ? tr("Update delay: %1 sec").arg(val)
            : tr("Update delay: disabled"));
        markDirty();
    });
    treeLayout->addWidget(m_statsTreeUpdateLabel);
    treeLayout->addWidget(m_statsTreeUpdateSlider);

    mainLayout->addWidget(treeGroup);
    mainLayout->addStretch();

    // Set initial color button
    updateColorBtn();

    return page;
}

// ---------------------------------------------------------------------------
// Web Interface page — matches MFC "Options Web Interface.png"
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createWebInterfacePage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    // --- General group ---
    auto* generalGroup = new QGroupBox(tr("General"));
    auto* generalLayout = new QVBoxLayout(generalGroup);

    m_webEnabledCheck = new QCheckBox(tr("Enabled"));
    generalLayout->addWidget(m_webEnabledCheck);

    m_webRestApiCheck = new QCheckBox(tr("Enable REST API"));
    generalLayout->addWidget(m_webRestApiCheck);

    m_webGzipCheck = new QCheckBox(tr("Gzip compression"));
    generalLayout->addWidget(m_webGzipCheck);

    m_webUPnPCheck = new QCheckBox(tr("Include port into UPnP setup"));
    generalLayout->addWidget(m_webUPnPCheck);

    // Port row
    auto* portLayout = new QHBoxLayout;
    portLayout->addWidget(new QLabel(tr("Port:")));
    m_webPortSpin = new QSpinBox;
    m_webPortSpin->setRange(1, 65535);
    m_webPortSpin->setValue(4711);
    portLayout->addWidget(m_webPortSpin);
    portLayout->addStretch();
    generalLayout->addLayout(portLayout);

    // Template row
    auto* tmplLayout = new QHBoxLayout;
    tmplLayout->addWidget(new QLabel(tr("Template:")));
    m_webTemplateEdit = new QLineEdit;
    tmplLayout->addWidget(m_webTemplateEdit, 1);
    m_webTemplateBrowseBtn = new QPushButton(tr("..."));
    m_webTemplateBrowseBtn->setFixedWidth(30);
    tmplLayout->addWidget(m_webTemplateBrowseBtn);
    generalLayout->addLayout(tmplLayout);

    // Reload button (right-aligned)
    auto* reloadLayout = new QHBoxLayout;
    reloadLayout->addStretch();
    m_webTemplateReloadBtn = new QPushButton(tr("Reload"));
    reloadLayout->addWidget(m_webTemplateReloadBtn);
    generalLayout->addLayout(reloadLayout);

    // Session timeout row
    auto* sessionLayout = new QHBoxLayout;
    sessionLayout->addWidget(new QLabel(tr("Session Time out:")));
    m_webSessionTimeoutSpin = new QSpinBox;
    m_webSessionTimeoutSpin->setRange(1, 60);
    m_webSessionTimeoutSpin->setValue(5);
    sessionLayout->addWidget(m_webSessionTimeoutSpin);
    sessionLayout->addWidget(new QLabel(tr("minutes")));
    sessionLayout->addStretch();
    generalLayout->addLayout(sessionLayout);

    // HTTPS row
    auto* httpsLayout = new QHBoxLayout;
    m_webHttpsCheck = new QCheckBox(tr("Use HTTPS"));
    httpsLayout->addWidget(m_webHttpsCheck);
    m_webCreateCertBtn = new QPushButton(tr("Create new certificate"));
    httpsLayout->addWidget(m_webCreateCertBtn);
    httpsLayout->addStretch();
    generalLayout->addLayout(httpsLayout);

    // Certificate row
    auto* certLayout = new QHBoxLayout;
    certLayout->addWidget(new QLabel(tr("Certificate:")));
    m_webCertEdit = new QLineEdit;
    certLayout->addWidget(m_webCertEdit, 1);
    m_webCertBrowseBtn = new QPushButton(tr("..."));
    m_webCertBrowseBtn->setFixedWidth(30);
    certLayout->addWidget(m_webCertBrowseBtn);
    generalLayout->addLayout(certLayout);

    // Key row
    auto* keyLayout = new QHBoxLayout;
    keyLayout->addWidget(new QLabel(tr("Key:")));
    m_webKeyEdit = new QLineEdit;
    keyLayout->addWidget(m_webKeyEdit, 1);
    m_webKeyBrowseBtn = new QPushButton(tr("..."));
    m_webKeyBrowseBtn->setFixedWidth(30);
    keyLayout->addWidget(m_webKeyBrowseBtn);
    generalLayout->addLayout(keyLayout);

    // REST API Key row
    auto* apiKeyLayout = new QHBoxLayout;
    apiKeyLayout->addWidget(new QLabel(tr("REST API Key:")));
    m_webApiKeyEdit = new QLineEdit;
    apiKeyLayout->addWidget(m_webApiKeyEdit, 1);
    generalLayout->addLayout(apiKeyLayout);

    layout->addWidget(generalGroup);

    // --- Administrator group ---
    auto* adminGroup = new QGroupBox(tr("Administrator"));
    auto* adminLayout = new QVBoxLayout(adminGroup);

    auto* adminPwLayout = new QHBoxLayout;
    adminPwLayout->addWidget(new QLabel(tr("Password:")));
    m_webAdminPasswordEdit = new QLineEdit;
    m_webAdminPasswordEdit->setEchoMode(QLineEdit::Password);
    adminPwLayout->addWidget(m_webAdminPasswordEdit, 1);
    adminLayout->addLayout(adminPwLayout);

    m_webAdminHiLevCheck = new QCheckBox(tr("Allow exit eMule, reboot and shutdown"));
    adminLayout->addWidget(m_webAdminHiLevCheck);

    layout->addWidget(adminGroup);

    // --- Guest group ---
    auto* guestGroup = new QGroupBox(tr("Guest"));
    auto* guestLayout = new QVBoxLayout(guestGroup);

    m_webGuestEnabledCheck = new QCheckBox(tr("Enabled"));
    guestLayout->addWidget(m_webGuestEnabledCheck);

    auto* guestPwLayout = new QHBoxLayout;
    guestPwLayout->addWidget(new QLabel(tr("Password:")));
    m_webGuestPasswordEdit = new QLineEdit;
    m_webGuestPasswordEdit->setEchoMode(QLineEdit::Password);
    guestPwLayout->addWidget(m_webGuestPasswordEdit, 1);
    guestLayout->addLayout(guestPwLayout);

    layout->addWidget(guestGroup);
    layout->addStretch();

    // --- Enable/disable logic ---
    auto updateWebEnabled = [this] {
        bool on = m_webEnabledCheck->isChecked();
        m_webRestApiCheck->setEnabled(on);
        m_webGzipCheck->setEnabled(on);
        m_webUPnPCheck->setEnabled(on);
        m_webPortSpin->setEnabled(on);
        m_webTemplateEdit->setEnabled(on);
        m_webTemplateBrowseBtn->setEnabled(on);
        m_webTemplateReloadBtn->setEnabled(on);
        m_webSessionTimeoutSpin->setEnabled(on);
        m_webHttpsCheck->setEnabled(on);
        m_webCreateCertBtn->setEnabled(on && m_webHttpsCheck->isChecked());
        m_webCertEdit->setEnabled(on && m_webHttpsCheck->isChecked());
        m_webCertBrowseBtn->setEnabled(on && m_webHttpsCheck->isChecked());
        m_webKeyEdit->setEnabled(on && m_webHttpsCheck->isChecked());
        m_webKeyBrowseBtn->setEnabled(on && m_webHttpsCheck->isChecked());
        m_webApiKeyEdit->setEnabled(on);
        m_webAdminPasswordEdit->setEnabled(on);
        m_webAdminHiLevCheck->setEnabled(on);
        m_webGuestEnabledCheck->setEnabled(on);
        m_webGuestPasswordEdit->setEnabled(on && m_webGuestEnabledCheck->isChecked());
    };

    connect(m_webEnabledCheck, &QCheckBox::toggled, this, [updateWebEnabled, this] {
        updateWebEnabled();
        markDirty();
    });
    connect(m_webHttpsCheck, &QCheckBox::toggled, this, [updateWebEnabled, this] {
        updateWebEnabled();
        markDirty();
    });
    connect(m_webGuestEnabledCheck, &QCheckBox::toggled, this, [updateWebEnabled, this] {
        updateWebEnabled();
        markDirty();
    });

    // Browse buttons
    connect(m_webTemplateBrowseBtn, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getOpenFileName(this, tr("Select Template File"),
            m_webTemplateEdit->text(), tr("Template files (*.tmpl);;All files (*)"));
        if (!path.isEmpty()) {
            m_webTemplateEdit->setText(path);
            markDirty();
        }
    });
    connect(m_webCertBrowseBtn, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getOpenFileName(this, tr("Select Certificate File"),
            m_webCertEdit->text(), tr("PEM files (*.pem *.crt);;All files (*)"));
        if (!path.isEmpty()) {
            m_webCertEdit->setText(path);
            markDirty();
        }
    });
    connect(m_webKeyBrowseBtn, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getOpenFileName(this, tr("Select Key File"),
            m_webKeyEdit->text(), tr("PEM files (*.pem *.key);;All files (*)"));
        if (!path.isEmpty()) {
            m_webKeyEdit->setText(path);
            markDirty();
        }
    });

    // Create certificate button
    connect(m_webCreateCertBtn, &QPushButton::clicked, this, [this] {
        // Generate a self-signed cert using OpenSSL CLI
        QString certPath = QFileDialog::getSaveFileName(this, tr("Save Certificate"),
            QString(), tr("PEM files (*.pem)"));
        if (certPath.isEmpty())
            return;
        QString keyPath = certPath;
        keyPath.replace(QStringLiteral(".pem"), QStringLiteral("_key.pem"));
        if (keyPath == certPath)
            keyPath += QStringLiteral("_key.pem");

        QProcess proc;
        proc.start(QStringLiteral("openssl"), {
            QStringLiteral("req"), QStringLiteral("-x509"),
            QStringLiteral("-newkey"), QStringLiteral("rsa:2048"),
            QStringLiteral("-keyout"), keyPath,
            QStringLiteral("-out"), certPath,
            QStringLiteral("-days"), QStringLiteral("3650"),
            QStringLiteral("-nodes"),
            QStringLiteral("-subj"), QStringLiteral("/CN=eMule Web Server")
        });
        proc.waitForFinished(10000);
        if (proc.exitCode() == 0) {
            m_webCertEdit->setText(certPath);
            m_webKeyEdit->setText(keyPath);
            markDirty();
        }
    });

    // Mark dirty for all editable controls
    for (auto* cb : {m_webRestApiCheck, m_webGzipCheck, m_webUPnPCheck, m_webAdminHiLevCheck})
        connect(cb, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    for (auto* le : {m_webTemplateEdit, m_webCertEdit, m_webKeyEdit, m_webApiKeyEdit,
                     m_webAdminPasswordEdit, m_webGuestPasswordEdit})
        connect(le, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    for (auto* sb : {m_webPortSpin, m_webSessionTimeoutSpin})
        connect(sb, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);

    // Initial state
    updateWebEnabled();

    return page;
}

// ---------------------------------------------------------------------------
// Extended page — matches MFC "Options Extended*.png" (PPgTweaks)
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createExtendedPage()
{
    auto* page = new QWidget(this);
    auto* outerLayout = new QVBoxLayout(page);
    outerLayout->setContentsMargins(4, 4, 4, 4);

    // --- Warning text ---
    auto* warning = new QLabel(
        tr("Warning: Do not change these settings unless you know what you "
           "are doing. Otherwise you can easily make things worse for "
           "yourself. eMule will run fine without adjusting any of these "
           "settings."), page);
    warning->setWordWrap(true);
    auto warnPal = warning->palette();
    warnPal.setColor(QPalette::WindowText, QColor(0x80, 0x00, 0x00));
    warning->setPalette(warnPal);
    outerLayout->addWidget(warning);

    // --- Scrollable area ---
    auto* scrollWidget = new QWidget(page);
    auto* scrollLayout = new QVBoxLayout(scrollWidget);
    scrollLayout->setContentsMargins(0, 0, 0, 0);

    auto* scrollArea = new QScrollArea(page);
    scrollArea->setWidget(scrollWidget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::Box);

    // --- TCP/IP connections group ---
    auto* tcpGroup = new QGroupBox(tr("TCP/IP connections"), scrollWidget);
    auto* tcpLayout = new QVBoxLayout(tcpGroup);
    tcpLayout->setContentsMargins(20, 4, 4, 4);

    auto* tcpRow1 = new QHBoxLayout;
    tcpRow1->addWidget(new QLabel(tr("Max. new connections / 5 secs.:"), tcpGroup));
    m_maxConPerFiveSpin = new QSpinBox(tcpGroup);
    m_maxConPerFiveSpin->setRange(1, 100);
    tcpRow1->addWidget(m_maxConPerFiveSpin);
    tcpRow1->addStretch();
    tcpLayout->addLayout(tcpRow1);

    auto* tcpRow2 = new QHBoxLayout;
    tcpRow2->addWidget(new QLabel(tr("Max. half-open connections:"), tcpGroup));
    m_maxHalfOpenSpin = new QSpinBox(tcpGroup);
    m_maxHalfOpenSpin->setRange(1, 100);
    tcpRow2->addWidget(m_maxHalfOpenSpin);
    tcpRow2->addStretch();
    tcpLayout->addLayout(tcpRow2);

    auto* tcpRow3 = new QHBoxLayout;
    tcpRow3->addWidget(new QLabel(tr("Server connection refresh interval [min.]:"), tcpGroup));
    m_serverKeepAliveSpin = new QSpinBox(tcpGroup);
    m_serverKeepAliveSpin->setRange(0, 60);
    m_serverKeepAliveSpin->setSpecialValueText(tr("Disabled"));
    tcpRow3->addWidget(m_serverKeepAliveSpin);
    tcpRow3->addStretch();
    tcpLayout->addLayout(tcpRow3);

    scrollLayout->addWidget(tcpGroup);

#ifdef Q_OS_WIN
    m_autotakeEd2kCheck = new QCheckBox(tr("Autotake eD2K links only during runtime"), scrollWidget);
    scrollLayout->addWidget(m_autotakeEd2kCheck);
#endif

    // --- Ungrouped checkboxes ---
    m_useCreditSystemCheck = new QCheckBox(tr("Use credit system (reward uploaders)"), scrollWidget);
    scrollLayout->addWidget(m_useCreditSystemCheck);

#ifdef Q_OS_WIN
    m_winFirewallCheck = new QCheckBox(
        tr("Open/close ports on WinXP firewall when starting/exiting eMule"), scrollWidget);
    scrollLayout->addWidget(m_winFirewallCheck);
#endif

    m_filterLANIPsCheck = new QCheckBox(tr("Filter server and client LAN IPs"), scrollWidget);
    scrollLayout->addWidget(m_filterLANIPsCheck);

    m_showExtControlsCheck = new QCheckBox(tr("Show more controls (advanced mode controls)"), scrollWidget);
    scrollLayout->addWidget(m_showExtControlsCheck);

    m_a4afSaveCpuCheck = new QCheckBox(tr("Disable A4AF checks to save CPU"), scrollWidget);
    scrollLayout->addWidget(m_a4afSaveCpuCheck);

    m_disableArchPreviewCheck = new QCheckBox(
        tr("Disable automatic archive preview start in file details"), scrollWidget);
    scrollLayout->addWidget(m_disableArchPreviewCheck);

    auto* hostnameRow = new QHBoxLayout;
    hostnameRow->addWidget(new QLabel(tr("Host name for own eD2K links:"), scrollWidget));
    m_ed2kHostnameEdit = new QLineEdit(scrollWidget);
    hostnameRow->addWidget(m_ed2kHostnameEdit);
    hostnameRow->addStretch();
    scrollLayout->addLayout(hostnameRow);

#ifdef Q_OS_WIN
    m_sparsePartFilesCheck = new QCheckBox(
        tr("Create new part files as 'sparse' (NTFS only)"), scrollWidget);
    scrollLayout->addWidget(m_sparsePartFilesCheck);

    m_allocFullFileCheck = new QCheckBox(
        tr("Allocate full file size for non-sparse part files"), scrollWidget);
    scrollLayout->addWidget(m_allocFullFileCheck);
#endif

    // --- Check disk space ---
    m_checkDiskspaceCheck = new QCheckBox(tr("Check disk space"), scrollWidget);
    scrollLayout->addWidget(m_checkDiskspaceCheck);

    auto* diskSpaceRow = new QHBoxLayout;
    diskSpaceRow->setContentsMargins(20, 0, 0, 0);
    m_minFreeDiskSpaceSpin = new QSpinBox(scrollWidget);
    m_minFreeDiskSpaceSpin->setRange(0, 999999);
    diskSpaceRow->addWidget(new QLabel(tr("Min. free disk space [MB]:"), scrollWidget));
    diskSpaceRow->addWidget(m_minFreeDiskSpaceSpin);
    diskSpaceRow->addStretch();
    scrollLayout->addLayout(diskSpaceRow);

    // --- Safe .met/.dat file writing ---
    auto* commitGroup = new QGroupBox(tr("Safe .met/.dat file writing"), scrollWidget);
    auto* commitLayout = new QVBoxLayout(commitGroup);
    commitLayout->setContentsMargins(20, 4, 4, 4);
    m_commitFilesGroup = new QButtonGroup(this);
    auto* commitNever = new QRadioButton(tr("Never"), commitGroup);
    auto* commitShutdown = new QRadioButton(tr("On shutdown"), commitGroup);
    auto* commitAlways = new QRadioButton(tr("Always"), commitGroup);
    m_commitFilesGroup->addButton(commitNever, 0);
    m_commitFilesGroup->addButton(commitShutdown, 1);
    m_commitFilesGroup->addButton(commitAlways, 2);
    commitLayout->addWidget(commitNever);
    commitLayout->addWidget(commitShutdown);
    commitLayout->addWidget(commitAlways);
    scrollLayout->addWidget(commitGroup);

    auto* metaGroup = new QGroupBox(tr("Extract meta data"), scrollWidget);
    auto* metaLayout = new QVBoxLayout(metaGroup);
    metaLayout->setContentsMargins(20, 4, 4, 4);
    m_extractMetaDataGroup = new QButtonGroup(this);
    auto* metaNever = new QRadioButton(tr("Never"), metaGroup);
    auto* metaLibrary = new QRadioButton(tr("MediaInfo Library"), metaGroup);
    m_extractMetaDataGroup->addButton(metaNever, 0);
    m_extractMetaDataGroup->addButton(metaLibrary, 1);
    metaLayout->addWidget(metaNever);
    metaLayout->addWidget(metaLibrary);
    scrollLayout->addWidget(metaGroup);

#ifdef Q_OS_WIN
    m_resolveShellLinksCheck = new QCheckBox(
        tr("Resolve shell links in shared directories"), scrollWidget);
    scrollLayout->addWidget(m_resolveShellLinksCheck);
#endif

    // --- Save log to disk ---
    m_logToDiskCheck = new QCheckBox(tr("Save log to disk"), scrollWidget);
    scrollLayout->addWidget(m_logToDiskCheck);

    // --- Verbose group ---
    auto* verboseGroup = new QGroupBox(tr("Verbose (additional program feedback)"), scrollWidget);
    auto* verboseLayout = new QVBoxLayout(verboseGroup);
    verboseLayout->setContentsMargins(20, 4, 4, 4);

    m_verboseCheck = new QCheckBox(tr("Enabled"), verboseGroup);
    verboseLayout->addWidget(m_verboseCheck);

    auto* logLevelRow = new QHBoxLayout;
    logLevelRow->addWidget(new QLabel(tr("Log level:"), verboseGroup));
    m_logLevelSpin = new QSpinBox(verboseGroup);
    m_logLevelSpin->setRange(0, 5);
    logLevelRow->addWidget(m_logLevelSpin);
    logLevelRow->addStretch();
    verboseLayout->addLayout(logLevelRow);

    m_verboseLogToDiskCheck = new QCheckBox(tr("Save log to disk"), verboseGroup);
    verboseLayout->addWidget(m_verboseLogToDiskCheck);

    m_logSourceExchangeCheck = new QCheckBox(
        tr("Log client source exchange and server source queries/answers"), verboseGroup);
    verboseLayout->addWidget(m_logSourceExchangeCheck);

    m_logBannedClientsCheck = new QCheckBox(tr("Log banned clients"), verboseGroup);
    verboseLayout->addWidget(m_logBannedClientsCheck);

    m_logRatingDescCheck = new QCheckBox(
        tr("Log received file descriptions and ratings"), verboseGroup);
    verboseLayout->addWidget(m_logRatingDescCheck);

    m_logSecureIdentCheck = new QCheckBox(tr("Log secure ident"), verboseGroup);
    verboseLayout->addWidget(m_logSecureIdentCheck);

    m_logFilteredIPsCheck = new QCheckBox(
        tr("Log filtered and/or ignored IPs"), verboseGroup);
    verboseLayout->addWidget(m_logFilteredIPsCheck);

    m_logFileSavingCheck = new QCheckBox(tr("Log file save actions"), verboseGroup);
    verboseLayout->addWidget(m_logFileSavingCheck);

    m_logA4AFCheck = new QCheckBox(tr("Log A4AF actions"), verboseGroup);
    verboseLayout->addWidget(m_logA4AFCheck);

    m_logUlDlEventsCheck = new QCheckBox(tr("Log upload/download events"), verboseGroup);
    verboseLayout->addWidget(m_logUlDlEventsCheck);

    m_logRawSocketPacketsCheck = new QCheckBox(tr("Log raw socket packets"), verboseGroup);
    verboseLayout->addWidget(m_logRawSocketPacketsCheck);

    m_enableIpcLogCheck = new QCheckBox(tr("Enable IPC log tab"), verboseGroup);
    verboseLayout->addWidget(m_enableIpcLogCheck);

    m_startCoreWithConsoleCheck = new QCheckBox(tr("Start core with console (debug)"), verboseGroup);
    verboseLayout->addWidget(m_startCoreWithConsoleCheck);

    scrollLayout->addWidget(verboseGroup);

    // --- Upload SpeedSense group ---
    auto* ussGroup = new QGroupBox(tr("Upload SpeedSense (not recommended)"), scrollWidget);
    auto* ussLayout = new QVBoxLayout(ussGroup);
    ussLayout->setContentsMargins(20, 4, 4, 4);

    m_dynUpEnabledCheck = new QCheckBox(tr("Find best upload limit automatically"), ussGroup);
    ussLayout->addWidget(m_dynUpEnabledCheck);

    auto* pingTolRow = new QHBoxLayout;
    pingTolRow->addWidget(new QLabel(tr("Ping tolerance (% of lowest ping):"), ussGroup));
    m_dynUpPingToleranceSpin = new QSpinBox(ussGroup);
    m_dynUpPingToleranceSpin->setRange(100, 5000);
    m_dynUpPingToleranceSpin->setSuffix(QStringLiteral("%"));
    pingTolRow->addWidget(m_dynUpPingToleranceSpin);
    pingTolRow->addStretch();
    ussLayout->addLayout(pingTolRow);

    auto* pingTolMsRow = new QHBoxLayout;
    pingTolMsRow->addWidget(new QLabel(tr("Ping tolerance (ms):"), ussGroup));
    m_dynUpPingToleranceMsSpin = new QSpinBox(ussGroup);
    m_dynUpPingToleranceMsSpin->setRange(1, 5000);
    m_dynUpPingToleranceMsSpin->setSuffix(tr(" ms"));
    pingTolMsRow->addWidget(m_dynUpPingToleranceMsSpin);
    pingTolMsRow->addStretch();
    ussLayout->addLayout(pingTolMsRow);

    auto* methodRow = new QHBoxLayout;
    methodRow->addWidget(new QLabel(tr("Method for ping tolerance:"), ussGroup));
    m_dynUpRadioPercent = new QRadioButton(tr("Percent (%)"), ussGroup);
    m_dynUpRadioMs = new QRadioButton(tr("Milliseconds (ms)"), ussGroup);
    methodRow->addWidget(m_dynUpRadioPercent);
    methodRow->addWidget(m_dynUpRadioMs);
    methodRow->addStretch();
    ussLayout->addLayout(methodRow);

    auto* goingUpRow = new QHBoxLayout;
    goingUpRow->addWidget(new QLabel(tr("Going up slowness:"), ussGroup));
    m_dynUpGoingUpSpin = new QSpinBox(ussGroup);
    m_dynUpGoingUpSpin->setRange(1, 100000);
    goingUpRow->addWidget(m_dynUpGoingUpSpin);
    goingUpRow->addStretch();
    ussLayout->addLayout(goingUpRow);

    auto* goingDownRow = new QHBoxLayout;
    goingDownRow->addWidget(new QLabel(tr("Going down slowness:"), ussGroup));
    m_dynUpGoingDownSpin = new QSpinBox(ussGroup);
    m_dynUpGoingDownSpin->setRange(1, 100000);
    goingDownRow->addWidget(m_dynUpGoingDownSpin);
    goingDownRow->addStretch();
    ussLayout->addLayout(goingDownRow);

    auto* numPingsRow = new QHBoxLayout;
    numPingsRow->addWidget(new QLabel(tr("Max number of pings for average:"), ussGroup));
    m_dynUpNumPingsSpin = new QSpinBox(ussGroup);
    m_dynUpNumPingsSpin->setRange(1, 100);
    numPingsRow->addWidget(m_dynUpNumPingsSpin);
    numPingsRow->addStretch();
    ussLayout->addLayout(numPingsRow);

    // Enable/disable child controls based on USS checkbox
    auto updateUssControls = [this](bool on) {
        m_dynUpPingToleranceSpin->setEnabled(on);
        m_dynUpPingToleranceMsSpin->setEnabled(on);
        m_dynUpRadioPercent->setEnabled(on);
        m_dynUpRadioMs->setEnabled(on);
        m_dynUpGoingUpSpin->setEnabled(on);
        m_dynUpGoingDownSpin->setEnabled(on);
        m_dynUpNumPingsSpin->setEnabled(on);
    };
    connect(m_dynUpEnabledCheck, &QCheckBox::toggled, this, updateUssControls);

    scrollLayout->addWidget(ussGroup);

    // --- UPnP group ---
    auto* upnpGroup = new QGroupBox(tr("UPnP"), scrollWidget);
    auto* upnpLayout = new QVBoxLayout(upnpGroup);
    upnpLayout->setContentsMargins(20, 4, 4, 4);

    m_closeUPnPCheck = new QCheckBox(tr("Remove UPnP port forwarding on exit"), upnpGroup);
    upnpLayout->addWidget(m_closeUPnPCheck);

    m_skipWANIPCheck = new QCheckBox(tr("Skip WAN IP setup"), upnpGroup);
    upnpLayout->addWidget(m_skipWANIPCheck);

    m_skipWANPPPCheck = new QCheckBox(tr("Skip WAN PPP setup"), upnpGroup);
    upnpLayout->addWidget(m_skipWANPPPCheck);

    scrollLayout->addWidget(upnpGroup);

#ifdef Q_OS_WIN
    // --- Sharing eMule with other computer users ---
    auto* multiUserGroup = new QGroupBox(
        tr("Sharing eMule with other computer users"), scrollWidget);
    auto* muLayout = new QVBoxLayout(multiUserGroup);
    muLayout->setContentsMargins(20, 4, 4, 4);
    m_multiUserSharingGroup = new QButtonGroup(this);
    auto* muPerUser = new QRadioButton(
        tr("Each user has its own configuration and downloads"), multiUserGroup);
    auto* muShared = new QRadioButton(
        tr("Everyone has the same configuration and downloads"), multiUserGroup);
    auto* muProgDir = new QRadioButton(
        tr("Store config and downloads in the program directory"), multiUserGroup);
    m_multiUserSharingGroup->addButton(muPerUser, 0);
    m_multiUserSharingGroup->addButton(muShared, 1);
    m_multiUserSharingGroup->addButton(muProgDir, 2);
    muLayout->addWidget(muPerUser);
    muLayout->addWidget(muShared);
    muLayout->addWidget(muProgDir);
    scrollLayout->addWidget(multiUserGroup);
#endif

    scrollLayout->addStretch();
    outerLayout->addWidget(scrollArea, 1);

    // --- Bottom fixed area: File buffer size slider ---
    auto* bufferRow = new QHBoxLayout;
    m_fileBufferLabel = new QLabel(page);
    bufferRow->addWidget(m_fileBufferLabel);
    bufferRow->addStretch();
    outerLayout->addLayout(bufferRow);

    m_fileBufferSlider = new QSlider(Qt::Horizontal, page);
    // Range: 16 KB to 64 MB, step 16 KB → slider values 1..4096
    m_fileBufferSlider->setRange(1, 4096);
    m_fileBufferSlider->setTickPosition(QSlider::TicksBelow);
    m_fileBufferSlider->setTickInterval(256);
    outerLayout->addWidget(m_fileBufferSlider);

    connect(m_fileBufferSlider, &QSlider::valueChanged, this, [this](int v) {
        auto bytes = static_cast<uint32>(v) * 16384u;
        m_fileBufferLabel->setText(
            tr("File buffer size: %1 MB").arg(
                static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2));
    });

    // --- Queue size slider ---
    auto* queueRow = new QHBoxLayout;
    m_queueSizeLabel = new QLabel(page);
    queueRow->addWidget(m_queueSizeLabel);
    queueRow->addStretch();
    outerLayout->addLayout(queueRow);

    m_queueSizeSlider = new QSlider(Qt::Horizontal, page);
    // Range: 2000 to 30000, step 100
    m_queueSizeSlider->setRange(5, 300);
    m_queueSizeSlider->setTickPosition(QSlider::TicksBelow);
    m_queueSizeSlider->setTickInterval(50);
    outerLayout->addWidget(m_queueSizeSlider);

    connect(m_queueSizeSlider, &QSlider::valueChanged, this, [this](int v) {
        auto size = static_cast<uint32>(v) * 100u;
        m_queueSizeLabel->setText(
            tr("Queue size: %1").arg(QLocale().toString(size)));
    });

    // --- Enable/disable logic ---
    // Verbose sub-controls depend on verbose checkbox
    connect(m_verboseCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_logLevelSpin->setEnabled(on);
        m_verboseLogToDiskCheck->setEnabled(on);
        m_logSourceExchangeCheck->setEnabled(on);
        m_logBannedClientsCheck->setEnabled(on);
        m_logRatingDescCheck->setEnabled(on);
        m_logSecureIdentCheck->setEnabled(on);
        m_logFilteredIPsCheck->setEnabled(on);
        m_logFileSavingCheck->setEnabled(on);
        m_logA4AFCheck->setEnabled(on);
        m_logUlDlEventsCheck->setEnabled(on);
        m_logRawSocketPacketsCheck->setEnabled(on);
    });

    // Min free disk space depends on check disk space
    connect(m_checkDiskspaceCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_minFreeDiskSpaceSpin->setEnabled(on);
    });

    return page;
}

// ---------------------------------------------------------------------------
// Scheduler page — matches MFC "Options Scheduler.png"
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createSchedulerPage()
{
    auto* page = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(page);

    // Top: Enabled checkbox
    m_schedEnabledCheck = new QCheckBox(tr("Enabled"), page);
    mainLayout->addWidget(m_schedEnabledCheck);

    // Buttons row: Remove + New (right-aligned)
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_schedRemoveBtn = new QPushButton(tr("Remove"), page);
    m_schedNewBtn = new QPushButton(tr("New"), page);
    btnRow->addWidget(m_schedRemoveBtn);
    btnRow->addWidget(m_schedNewBtn);
    mainLayout->addLayout(btnRow);

    // Schedule table: Title | Days | Start Time
    m_schedTable = new QTreeWidget(page);
    m_schedTable->setHeaderLabels({tr("Title"), tr("Days"), tr("Start Time")});
    m_schedTable->setRootIsDecorated(false);
    m_schedTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_schedTable->setColumnCount(3);
    m_schedTable->header()->setStretchLastSection(true);
    mainLayout->addWidget(m_schedTable);

    // Details group box
    auto* detailsGroup = new QGroupBox(tr("Details"), page);
    auto* detailsLayout = new QVBoxLayout(detailsGroup);

    // Entry enabled checkbox
    m_schedEntryEnabledCheck = new QCheckBox(tr("Enabled"), detailsGroup);
    detailsLayout->addWidget(m_schedEntryEnabledCheck);

    // Title
    auto* titleRow = new QHBoxLayout;
    titleRow->addWidget(new QLabel(tr("Title"), detailsGroup));
    m_schedTitleEdit = new QLineEdit(detailsGroup);
    titleRow->addWidget(m_schedTitleEdit);
    detailsLayout->addLayout(titleRow);

    // Time row: Day combo + start time - end time + "No end time"
    auto* timeRow = new QHBoxLayout;
    timeRow->addWidget(new QLabel(tr("Time"), detailsGroup));
    m_schedDayCombo = new QComboBox(detailsGroup);
    m_schedDayCombo->addItems({
        tr("Daily"), tr("Monday"), tr("Tuesday"), tr("Wednesday"),
        tr("Thursday"), tr("Friday"), tr("Saturday"), tr("Sunday"),
        tr("Mon-Fri"), tr("Mon-Sat"), tr("Sat-Sun")
    });
    timeRow->addWidget(m_schedDayCombo);
    detailsLayout->addLayout(timeRow);

    auto* timePickRow = new QHBoxLayout;
    timePickRow->addSpacing(40); // indent
    m_schedStartTime = new QTimeEdit(detailsGroup);
    m_schedStartTime->setDisplayFormat(QStringLiteral("HH:mm"));
    timePickRow->addWidget(m_schedStartTime);
    timePickRow->addWidget(new QLabel(QStringLiteral("-"), detailsGroup));
    m_schedEndTime = new QTimeEdit(detailsGroup);
    m_schedEndTime->setDisplayFormat(QStringLiteral("HH:mm"));
    timePickRow->addWidget(m_schedEndTime);
    m_schedNoEndTimeCheck = new QCheckBox(tr("No end time"), detailsGroup);
    timePickRow->addWidget(m_schedNoEndTimeCheck);
    timePickRow->addStretch();
    detailsLayout->addLayout(timePickRow);

    // Action group
    auto* actionGroup = new QGroupBox(tr("Action"), detailsGroup);
    auto* actionLayout = new QVBoxLayout(actionGroup);
    m_schedActionsTable = new QTreeWidget(actionGroup);
    m_schedActionsTable->setHeaderLabels({tr("Action"), tr("Value")});
    m_schedActionsTable->setRootIsDecorated(false);
    m_schedActionsTable->setColumnCount(2);
    m_schedActionsTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_schedActionsTable->header()->setStretchLastSection(true);
    actionLayout->addWidget(m_schedActionsTable);
    detailsLayout->addWidget(actionGroup);

    // Apply button (right-aligned)
    auto* applyRow = new QHBoxLayout;
    applyRow->addStretch();
    m_schedApplyBtn = new QPushButton(tr("Apply"), detailsGroup);
    applyRow->addWidget(m_schedApplyBtn);
    detailsLayout->addLayout(applyRow);

    mainLayout->addWidget(detailsGroup);

    // Wire signals
    connect(m_schedTable, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            int idx = current ? m_schedTable->indexOfTopLevelItem(current) : -1;
            populateScheduleDetails(idx);
        });

    connect(m_schedNewBtn, &QPushButton::clicked, this, [this]() {
        SchedUiEntry entry;
        entry.title = tr("New Schedule");
        entry.enabled = true;
        auto now = QTime::currentTime();
        QDateTime dt = QDateTime::currentDateTime();
        dt.setTime(now);
        entry.startTime = dt.toSecsSinceEpoch();
        entry.endTime = entry.startTime;
        m_schedEntries.push_back(std::move(entry));
        refreshScheduleTable();
        m_schedTable->setCurrentItem(
            m_schedTable->topLevelItem(static_cast<int>(m_schedEntries.size()) - 1));
        markDirty();
    });

    connect(m_schedRemoveBtn, &QPushButton::clicked, this, [this]() {
        if (m_schedSelectedIndex >= 0
            && m_schedSelectedIndex < static_cast<int>(m_schedEntries.size())) {
            m_schedEntries.erase(m_schedEntries.begin() + m_schedSelectedIndex);
            refreshScheduleTable();
            m_schedSelectedIndex = -1;
            populateScheduleDetails(-1);
            markDirty();
        }
    });

    connect(m_schedApplyBtn, &QPushButton::clicked, this, [this]() {
        applyScheduleDetails();
        markDirty();
    });

    connect(m_schedNoEndTimeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_schedEndTime->setEnabled(!checked);
    });

    connect(m_schedActionsTable, &QTreeWidget::customContextMenuRequested,
            this, &OptionsDialog::showScheduleActionsMenu);

    connect(m_schedEnabledCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);

    return page;
}

// ---------------------------------------------------------------------------
// Scheduler helpers
// ---------------------------------------------------------------------------

void OptionsDialog::refreshScheduleTable()
{
    m_schedTable->clear();
    static const char* dayNames[] = {
        "Daily", "Monday", "Tuesday", "Wednesday", "Thursday",
        "Friday", "Saturday", "Sunday", "Mon-Fri", "Mon-Sat", "Sat-Sun"
    };

    for (const auto& entry : m_schedEntries) {
        auto* item = new QTreeWidgetItem(m_schedTable);
        item->setText(0, entry.title);
        int dayIdx = entry.day;
        item->setText(1, (dayIdx >= 0 && dayIdx <= 10) ? tr(dayNames[dayIdx]) : tr("Daily"));
        QDateTime dt = QDateTime::fromSecsSinceEpoch(entry.startTime);
        item->setText(2, dt.time().toString(QStringLiteral("HH:mm")));
    }
}

void OptionsDialog::populateScheduleDetails(int index)
{
    m_schedSelectedIndex = index;
    bool valid = index >= 0 && index < static_cast<int>(m_schedEntries.size());

    m_schedEntryEnabledCheck->setEnabled(valid);
    m_schedTitleEdit->setEnabled(valid);
    m_schedDayCombo->setEnabled(valid);
    m_schedStartTime->setEnabled(valid);
    m_schedEndTime->setEnabled(valid);
    m_schedNoEndTimeCheck->setEnabled(valid);
    m_schedApplyBtn->setEnabled(valid);

    if (!valid) {
        m_schedEntryEnabledCheck->setChecked(false);
        m_schedTitleEdit->clear();
        m_schedDayCombo->setCurrentIndex(0);
        m_schedActionsTable->clear();
        return;
    }

    const auto& entry = m_schedEntries[static_cast<size_t>(index)];
    m_schedEntryEnabledCheck->setChecked(entry.enabled);
    m_schedTitleEdit->setText(entry.title);
    m_schedDayCombo->setCurrentIndex(std::clamp(entry.day, 0, 10));

    QDateTime startDt = QDateTime::fromSecsSinceEpoch(entry.startTime);
    m_schedStartTime->setTime(startDt.time());

    bool noEnd = (entry.endTime == 0 || entry.endTime == entry.startTime);
    m_schedNoEndTimeCheck->setChecked(noEnd);
    m_schedEndTime->setEnabled(!noEnd);
    if (!noEnd) {
        QDateTime endDt = QDateTime::fromSecsSinceEpoch(entry.endTime);
        m_schedEndTime->setTime(endDt.time());
    } else {
        m_schedEndTime->setTime(startDt.time());
    }

    // Populate actions
    m_schedActionsTable->clear();
    static const char* actionNames[] = {
        "None", "Upload Limit", "Download Limit", "Source Limit",
        "Con/5sec Limit", "Max Connections", "Stop Category", "Resume Category"
    };
    for (const auto& act : entry.actions) {
        if (act.type <= 0 || act.type > 7) continue;
        auto* item = new QTreeWidgetItem(m_schedActionsTable);
        item->setText(0, tr(actionNames[act.type]));
        item->setText(1, act.value);
        item->setData(0, Qt::UserRole, act.type);
    }
}

void OptionsDialog::applyScheduleDetails()
{
    if (m_schedSelectedIndex < 0
        || m_schedSelectedIndex >= static_cast<int>(m_schedEntries.size()))
        return;

    auto& entry = m_schedEntries[static_cast<size_t>(m_schedSelectedIndex)];
    entry.enabled = m_schedEntryEnabledCheck->isChecked();
    entry.title = m_schedTitleEdit->text();
    entry.day = m_schedDayCombo->currentIndex();

    // Build start/end times: use today's date as base, set time from pickers
    QDateTime baseDt = QDateTime::currentDateTime();
    baseDt.setTime(m_schedStartTime->time());
    entry.startTime = baseDt.toSecsSinceEpoch();

    if (m_schedNoEndTimeCheck->isChecked()) {
        entry.endTime = 0;
    } else {
        QDateTime endDt = QDateTime::currentDateTime();
        endDt.setTime(m_schedEndTime->time());
        entry.endTime = endDt.toSecsSinceEpoch();
    }

    // Collect actions from table
    entry.actions.clear();
    for (int i = 0; i < m_schedActionsTable->topLevelItemCount(); ++i) {
        auto* item = m_schedActionsTable->topLevelItem(i);
        SchedUiEntry::Action act;
        act.type = item->data(0, Qt::UserRole).toInt();
        act.value = item->text(1);
        entry.actions.push_back(act);
    }

    refreshScheduleTable();
    m_schedTable->setCurrentItem(m_schedTable->topLevelItem(m_schedSelectedIndex));
}

void OptionsDialog::showScheduleActionsMenu(const QPoint& pos)
{
    QMenu menu;

    // Add submenu with action types
    auto* addMenu = menu.addMenu(tr("Add"));
    static const char* actionNames[] = {
        nullptr, "Upload Limit", "Download Limit", "Source Limit",
        "Con/5sec Limit", "Max Connections", "Stop Category", "Resume Category"
    };
    for (int i = 1; i <= 7; ++i) {
        addMenu->addAction(tr(actionNames[i]), this, [this, i]() {
            bool ok = false;
            QString value = QInputDialog::getText(this, tr("Action Value"),
                tr("Enter value:"), QLineEdit::Normal, QString(), &ok);
            if (!ok) return;

            auto* item = new QTreeWidgetItem(m_schedActionsTable);
            item->setText(0, tr(actionNames[i]));
            item->setText(1, value);
            item->setData(0, Qt::UserRole, i);
        });
    }

    auto* current = m_schedActionsTable->currentItem();
    if (current) {
        menu.addAction(tr("Edit Value"), this, [this, current]() {
            bool ok = false;
            QString value = QInputDialog::getText(this, tr("Edit Value"),
                tr("Enter value:"), QLineEdit::Normal, current->text(1), &ok);
            if (ok)
                current->setText(1, value);
        });
        menu.addAction(tr("Remove"), this, [current]() {
            delete current;
        });
    }

    menu.exec(m_schedActionsTable->viewport()->mapToGlobal(pos));
}

void OptionsDialog::loadSchedulerData()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    Ipc::IpcMessage req(Ipc::IpcMsgType::GetSchedules);
    m_ipc->sendRequest(std::move(req), [this](const Ipc::IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;
        const QCborMap data = resp.fieldMap(1);

        m_schedEnabledCheck->setChecked(data.value(QStringLiteral("enabled")).toBool());

        m_schedEntries.clear();
        const QCborArray schedArr = data.value(QStringLiteral("schedules")).toArray();
        for (const auto& item : schedArr) {
            const QCborMap m = item.toMap();
            SchedUiEntry entry;
            entry.title = m.value(QStringLiteral("title")).toString();
            entry.startTime = static_cast<time_t>(m.value(QStringLiteral("startTime")).toInteger());
            entry.endTime = static_cast<time_t>(m.value(QStringLiteral("endTime")).toInteger());
            entry.day = static_cast<int>(m.value(QStringLiteral("day")).toInteger());
            entry.enabled = m.value(QStringLiteral("enabled")).toBool();

            const QCborArray actArr = m.value(QStringLiteral("actions")).toArray();
            for (const auto& actItem : actArr) {
                const QCborMap actMap = actItem.toMap();
                SchedUiEntry::Action act;
                act.type = static_cast<int>(actMap.value(QStringLiteral("action")).toInteger());
                act.value = actMap.value(QStringLiteral("value")).toString();
                entry.actions.push_back(act);
            }
            m_schedEntries.push_back(std::move(entry));
        }

        refreshScheduleTable();
        if (!m_schedEntries.empty())
            m_schedTable->setCurrentItem(m_schedTable->topLevelItem(0));
    });
}

void OptionsDialog::saveSchedulerData()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    Ipc::IpcMessage req(Ipc::IpcMsgType::SaveSchedules);
    req.append(m_schedEnabledCheck->isChecked());

    QCborArray schedArr;
    for (const auto& entry : m_schedEntries) {
        QCborMap m;
        m.insert(QStringLiteral("title"), entry.title);
        m.insert(QStringLiteral("startTime"), static_cast<qint64>(entry.startTime));
        m.insert(QStringLiteral("endTime"), static_cast<qint64>(entry.endTime));
        m.insert(QStringLiteral("day"), entry.day);
        m.insert(QStringLiteral("enabled"), entry.enabled);

        QCborArray actArr;
        for (const auto& act : entry.actions) {
            QCborMap actMap;
            actMap.insert(QStringLiteral("action"), act.type);
            actMap.insert(QStringLiteral("value"), act.value);
            actArr.append(actMap);
        }
        m.insert(QStringLiteral("actions"), actArr);
        schedArr.append(m);
    }
    req.append(schedArr);
    m_ipc->sendRequest(std::move(req));
}

// ---------------------------------------------------------------------------
// Placeholder page for unimplemented categories
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createPlaceholderPage(const QString& title)
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* label = new QLabel(tr("The %1 settings page is not yet implemented.").arg(title), page);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    layout->addWidget(label);
    return page;
}

// ---------------------------------------------------------------------------
// Load settings into controls
// ---------------------------------------------------------------------------

void OptionsDialog::loadSettings()
{


    // GUI-only settings from local preferences
    m_promptOnExitCheck->setChecked(thePrefs.promptOnExit());
    m_startMinimizedCheck->setChecked(thePrefs.startMinimized());
    m_showSplashCheck->setChecked(thePrefs.showSplashScreen());
    m_enableOnlineSigCheck->setChecked(thePrefs.enableOnlineSignature());
#ifdef Q_OS_WIN
    m_enableMiniMuleCheck->setChecked(thePrefs.enableMiniMule());
#endif
    m_preventStandbyCheck->setChecked(thePrefs.preventStandby());
    m_startWithOSCheck->setChecked(thePrefs.startWithOS());
    m_versionCheckBox->setChecked(thePrefs.versionCheckEnabled());
    m_versionCheckDaysSpin->setValue(thePrefs.versionCheckDays());
    m_versionCheckDaysSpin->setEnabled(thePrefs.versionCheckEnabled());
    m_bringToFrontCheck->setChecked(thePrefs.bringToFrontOnLinkClick());
    {
        const QString lang = thePrefs.language();
        int langIdx = m_langCombo->findData(lang);
        m_langCombo->setCurrentIndex(langIdx >= 0 ? langIdx : 0);
    }

    // Core settings
    m_coreAddressEdit->setText(thePrefs.ipcListenAddress());
    m_corePortSpin->setValue(thePrefs.ipcPort());
    if (!thePrefs.ipcTokens().isEmpty())
        m_coreTokenEdit->setText(thePrefs.ipcTokens().first());

    // Display page
    m_depth3DSlider->setValue(thePrefs.depth3D());
    m_tooltipDelaySpin->setValue(thePrefs.tooltipDelay());
    m_minimizeToTrayCheck->setChecked(thePrefs.minimizeToTray());
    m_transferDoubleClickCheck->setChecked(thePrefs.transferDoubleClick());
    m_showDwlPercentageCheck->setChecked(thePrefs.showDwlPercentage());
    m_showRatesInTitleCheck->setChecked(thePrefs.showRatesInTitle());
    m_showCatTabInfosCheck->setChecked(thePrefs.showCatTabInfos());
    m_autoRemoveFinishedCheck->setChecked(thePrefs.autoRemoveFinishedDownloads());
    m_showTransToolbarCheck->setChecked(thePrefs.showTransToolbar());
    m_storeSearchesCheck->setChecked(thePrefs.storeSearches());
    m_disableKnownClientListCheck->setChecked(thePrefs.disableKnownClientList());
    m_disableQueueListCheck->setChecked(thePrefs.disableQueueList());
    m_useAutoCompletionCheck->setChecked(thePrefs.useAutoCompletion());
    m_useOriginalIconsCheck->setChecked(thePrefs.useOriginalIcons());
    m_initialUseOriginalIcons = thePrefs.useOriginalIcons();

    // Display - font
    m_currentLogFont = thePrefs.logFont();
    if (!m_currentLogFont.isEmpty()) {
        QFont f;
        f.fromString(m_currentLogFont);
        m_fontPreviewLabel->setText(QStringLiteral("%1, %2pt").arg(f.family()).arg(f.pointSize()));
    }

    // Files page (GUI-only)
    m_watchClipboardCheck->setChecked(thePrefs.watchClipboard4ED2KLinks());
    m_advancedCalcRemainingCheck->setChecked(thePrefs.useAdvancedCalcRemainingTime());
    m_videoPlayerCmdEdit->setText(thePrefs.videoPlayerCommand());
    m_videoPlayerArgsEdit->setText(thePrefs.videoPlayerArgs());
    m_createBackupToPreviewCheck->setChecked(thePrefs.createBackupToPreview());
    m_autoCleanupFilenamesCheck->setChecked(thePrefs.autoCleanupFilenames());

    // Notifications page (GUI-side)
    m_soundGroup->button(thePrefs.notifySoundType())->setChecked(true);
    m_soundFileEdit->setText(thePrefs.notifySoundFile());
    m_soundFileEdit->setEnabled(thePrefs.notifySoundType() == 1);
    m_soundBrowseBtn->setEnabled(thePrefs.notifySoundType() == 1);

    // IRC page (GUI-local)
    m_ircServerEdit->setText(thePrefs.ircServer());
    m_ircNickEdit->setText(thePrefs.ircNick());
    m_ircUseChannelFilterCheck->setChecked(thePrefs.ircUseChannelFilter());
    {
        const QString filter = thePrefs.ircChannelFilter();
        const auto parts = filter.split(QLatin1Char('|'));
        m_ircChannelFilterNameEdit->setText(parts.value(0));
        m_ircChannelFilterUsersSpin->setValue(parts.value(1).toInt());
    }
    m_ircChannelFilterNameEdit->setEnabled(thePrefs.ircUseChannelFilter());
    m_ircChannelFilterUsersSpin->setEnabled(thePrefs.ircUseChannelFilter());
    m_ircUsePerformCheck->setChecked(thePrefs.ircUsePerform());
    m_ircPerformEdit->setText(thePrefs.ircPerformString());
    m_ircPerformEdit->setEnabled(thePrefs.ircUsePerform());

    // Misc tree items: 0=help, 1=loadList, 2=timestamp, 3=ignoreParent->(0=misc,1=join,2=part,3=quit)
    auto* root = m_ircMiscTree->invisibleRootItem();
    root->child(0)->setCheckState(0, thePrefs.ircConnectHelpChannel() ? Qt::Checked : Qt::Unchecked);
    root->child(1)->setCheckState(0, thePrefs.ircLoadChannelList() ? Qt::Checked : Qt::Unchecked);
    root->child(2)->setCheckState(0, thePrefs.ircAddTimestamp() ? Qt::Checked : Qt::Unchecked);
    auto* ignoreParent = root->child(3);
    ignoreParent->child(0)->setCheckState(0, thePrefs.ircIgnoreMiscInfoMessages() ? Qt::Checked : Qt::Unchecked);
    ignoreParent->child(1)->setCheckState(0, thePrefs.ircIgnoreJoinMessages() ? Qt::Checked : Qt::Unchecked);
    ignoreParent->child(2)->setCheckState(0, thePrefs.ircIgnorePartMessages() ? Qt::Checked : Qt::Unchecked);
    ignoreParent->child(3)->setCheckState(0, thePrefs.ircIgnoreQuitMessages() ? Qt::Checked : Qt::Unchecked);

    // Messages page (GUI-only)
    m_showSmileysCheck->setChecked(thePrefs.showSmileys());
    m_indicateRatingsCheck->setChecked(thePrefs.indicateRatings());

    // Always populate daemon settings synchronously from thePrefs first
    // (provides immediate correct display with no visible flash).
    // If IPC is connected, the async callback overrides with live daemon values.
    m_loading = true;
    {
        // Synchronous fill from local thePrefs
        m_nickEdit->setText(thePrefs.nick());

        // Connection page
        m_capacityDownloadSpin->setValue(static_cast<int>(thePrefs.maxGraphDownloadRate()));
        m_capacityUploadSpin->setValue(static_cast<int>(thePrefs.maxGraphUploadRate()));

        auto maxDown = static_cast<int>(thePrefs.maxDownload());
        auto maxUp   = static_cast<int>(thePrefs.maxUpload());
        m_downloadLimitCheck->setChecked(maxDown > 0);
        m_downloadLimitSlider->setEnabled(maxDown > 0);
        m_downloadLimitLabel->setEnabled(maxDown > 0);
        if (maxDown > 0)
            m_downloadLimitSlider->setValue(maxDown);
        m_uploadLimitCheck->setChecked(maxUp > 0);
        m_uploadLimitSlider->setEnabled(maxUp > 0);
        m_uploadLimitLabel->setEnabled(maxUp > 0);
        if (maxUp > 0)
            m_uploadLimitSlider->setValue(maxUp);

        m_tcpPortSpin->setValue(thePrefs.port());
        auto udpPort = thePrefs.udpPort();
        if (udpPort == 0) {
            m_udpDisableCheck->setChecked(true);
            m_udpPortSpin->setValue(5672);
            m_udpPortSpin->setEnabled(false);
        } else {
            m_udpDisableCheck->setChecked(false);
            m_udpPortSpin->setValue(udpPort);
        }

        m_upnpCheck->setChecked(thePrefs.enableUPnP());
        m_maxSourcesSpin->setValue(thePrefs.maxSourcesPerFile());
        m_maxConnectionsSpin->setValue(thePrefs.maxConnections());
        m_autoConnectCheck->setChecked(thePrefs.autoConnect());
        m_reconnectCheck->setChecked(thePrefs.reconnect());
        m_overheadCheck->setChecked(thePrefs.showOverhead());
        m_kadEnabledCheck->setChecked(thePrefs.kadEnabled());
        m_ed2kEnabledCheck->setChecked(thePrefs.networkED2K());

        // Server page
        m_safeServerConnectCheck->setChecked(thePrefs.safeServerConnect());
        m_autoConnectStaticOnlyCheck->setChecked(thePrefs.autoConnectStaticOnly());
        m_useServerPrioritiesCheck->setChecked(thePrefs.useServerPriorities());
        m_addServersFromServerCheck->setChecked(thePrefs.addServersFromServer());
        m_addServersFromClientsCheck->setChecked(thePrefs.addServersFromClients());
        m_deadServerRetriesSpin->setValue(static_cast<int>(thePrefs.deadServerRetries()));
        m_autoUpdateServerListCheck->setChecked(thePrefs.autoUpdateServerList());
        m_serverListURLValue = thePrefs.serverListURL();
        m_smartLowIdCheck->setChecked(thePrefs.smartLowIdCheck());
        m_manualHighPrioCheck->setChecked(thePrefs.manualServerHighPriority());

        // Proxy page
        int proxyType = thePrefs.proxyType();
        bool proxyOn = (proxyType != 0);
        m_proxyEnableCheck->setChecked(proxyOn);
        m_proxyTypeCombo->setCurrentIndex(proxyType);
        m_proxyHostEdit->setText(thePrefs.proxyHost());
        m_proxyPortSpin->setValue(thePrefs.proxyPort());
        bool authOn = thePrefs.proxyEnablePassword();
        m_proxyAuthCheck->setChecked(authOn);
        m_proxyUserEdit->setText(thePrefs.proxyUser());
        m_proxyPasswordEdit->setText(thePrefs.proxyPassword());

        m_proxyTypeCombo->setEnabled(proxyOn);
        m_proxyHostEdit->setEnabled(proxyOn);
        m_proxyPortSpin->setEnabled(proxyOn);
        m_proxyAuthCheck->setEnabled(proxyOn);
        m_proxyUserEdit->setEnabled(proxyOn && authOn);
        m_proxyPasswordEdit->setEnabled(proxyOn && authOn);

        // Directories page
        m_incomingDirEdit->setText(thePrefs.incomingDir());
        auto tempDirs = thePrefs.tempDirs();
        if (!tempDirs.isEmpty())
            m_tempDirEdit->setText(tempDirs.first());
        static_cast<CheckableFileSystemModel*>(m_sharedDirsModel)->setCheckedPaths(thePrefs.sharedDirs());

        // Files page (daemon-side)
        m_addFilesPausedCheck->setChecked(thePrefs.addNewFilesPaused());
        m_autoSharedFilesPrioCheck->setChecked(thePrefs.autoSharedFilesPriority());
        m_autoDownloadPrioCheck->setChecked(thePrefs.autoDownloadPriority());
        m_transferFullChunksCheck->setChecked(thePrefs.transferFullChunks());
        m_previewPrioCheck->setChecked(thePrefs.previewPrio());
        bool startNext = thePrefs.startNextPausedFile();
        m_startNextPausedCheck->setChecked(startNext);
        m_preferSameCatCheck->setEnabled(startNext);
        m_onlySameCatCheck->setEnabled(startNext);
        m_preferSameCatCheck->setChecked(thePrefs.startNextPausedFileSameCat());
        m_onlySameCatCheck->setChecked(thePrefs.startNextPausedFileOnlySameCat());
        m_rememberDownloadedCheck->setChecked(thePrefs.rememberDownloadedFiles());
        m_rememberCancelledCheck->setChecked(thePrefs.rememberCancelledFiles());

        // Notifications page (daemon-side)
        m_notifyLogCheck->setChecked(thePrefs.notifyOnLog());
        m_notifyChatCheck->setChecked(thePrefs.notifyOnChat());
        m_notifyChatMsgCheck->setChecked(thePrefs.notifyOnChatMsg());
        m_notifyChatMsgCheck->setEnabled(m_notifyChatCheck->isChecked());
        m_notifyDownloadAddedCheck->setChecked(thePrefs.notifyOnDownloadAdded());
        m_notifyDownloadFinishedCheck->setChecked(thePrefs.notifyOnDownloadFinished());
        m_notifyNewVersionCheck->setChecked(thePrefs.notifyOnNewVersion());
        m_notifyUrgentCheck->setChecked(thePrefs.notifyOnUrgent());
        m_emailEnabledCheck->setChecked(thePrefs.notifyEmailEnabled());
        m_smtpServer = thePrefs.notifyEmailSmtpServer();
        m_smtpPort = thePrefs.notifyEmailSmtpPort();
        m_smtpAuth = thePrefs.notifyEmailSmtpAuth();
        m_smtpTls = thePrefs.notifyEmailSmtpTls();
        m_smtpUser = thePrefs.notifyEmailSmtpUser();
        m_smtpPassword = thePrefs.notifyEmailSmtpPassword();
        m_emailRecipientEdit->setText(thePrefs.notifyEmailRecipient());
        m_emailSenderEdit->setText(thePrefs.notifyEmailSender());
        bool emailOn = m_emailEnabledCheck->isChecked();
        m_smtpServerBtn->setEnabled(emailOn);
        m_emailRecipientEdit->setEnabled(emailOn);
        m_emailSenderEdit->setEnabled(emailOn);

        // Messages and Comments page (daemon-side)
        m_msgFriendsOnlyCheck->setChecked(thePrefs.msgOnlyFriends());
        bool spamOn = thePrefs.enableSpamFilter();
        m_advancedSpamFilterCheck->setChecked(spamOn);
        m_requireCaptchaCheck->setChecked(thePrefs.useChatCaptchas());
        m_requireCaptchaCheck->setEnabled(spamOn);
        m_messageFilterEdit->setText(thePrefs.messageFilter());
        m_commentFilterEdit->setText(thePrefs.commentFilter());

        // Security page (daemon-side)
        m_filterServersByIPCheck->setChecked(thePrefs.filterServerByIP());
        m_ipFilterLevelSpin->setValue(static_cast<int>(thePrefs.ipFilterLevel()));
        m_viewSharedGroup->button(thePrefs.viewSharedFilesAccess())->setChecked(true);
        bool cryptSupported = thePrefs.cryptLayerSupported();
        bool cryptRequested = thePrefs.cryptLayerRequested();
        bool cryptRequired  = thePrefs.cryptLayerRequired();
        m_cryptLayerDisableCheck->setChecked(!cryptSupported);
        m_cryptLayerRequestedCheck->setChecked(cryptRequested);
        m_cryptLayerRequestedCheck->setEnabled(cryptSupported);
        m_cryptLayerRequiredCheck->setChecked(cryptRequired);
        m_cryptLayerRequiredCheck->setEnabled(cryptSupported && cryptRequested);
        m_useSecureIdentCheck->setChecked(thePrefs.useSecureIdent());
        m_enableSearchResultFilterCheck->setChecked(thePrefs.enableSearchResultFilter());
        m_warnUntrustedFilesCheck->setChecked(thePrefs.warnUntrustedFiles());
        m_ipFilterUpdateUrlEdit->setText(thePrefs.ipFilterUpdateUrl());

        // Statistics page
        m_statsGraphUpdateSlider->setValue(static_cast<int>(thePrefs.graphsUpdateSec()));
        m_statsAvgTimeSlider->setValue(static_cast<int>(thePrefs.statsAverageMinutes()));
        m_statsFillGraphsCheck->setChecked(thePrefs.fillGraphs());
        m_statsYScaleSpin->setValue(static_cast<int>(thePrefs.statsConnectionsMax()));
        {
            auto ratio = thePrefs.statsConnectionsRatio();
            static constexpr int ratioValues[] = {1, 2, 3, 4, 5, 10, 20};
            int ratioIdx = 2;
            for (int ri = 0; ri < 7; ++ri) {
                if (static_cast<uint32_t>(ratioValues[ri]) == ratio) { ratioIdx = ri; break; }
            }
            m_statsRatioCombo->setCurrentIndex(ratioIdx);
        }
        m_statsTreeUpdateSlider->setValue(static_cast<int>(thePrefs.statsUpdateSec()));

        // Extended page
        m_maxConPerFiveSpin->setValue(thePrefs.maxConsPerFive());
        m_maxHalfOpenSpin->setValue(thePrefs.maxHalfConnections());
        m_serverKeepAliveSpin->setValue(static_cast<int>(thePrefs.serverKeepAliveTimeout()) / 60000);
        m_useCreditSystemCheck->setChecked(thePrefs.useCreditSystem());
        m_filterLANIPsCheck->setChecked(thePrefs.filterLANIPs());
        m_showExtControlsCheck->setChecked(thePrefs.showExtControls());
        m_a4afSaveCpuCheck->setChecked(thePrefs.a4afSaveCpu());
        m_disableArchPreviewCheck->setChecked(!thePrefs.autoArchivePreviewStart());
        m_ed2kHostnameEdit->setText(thePrefs.ed2kHostname());
        bool diskCheck = thePrefs.checkDiskspace();
        m_checkDiskspaceCheck->setChecked(diskCheck);
        m_minFreeDiskSpaceSpin->setValue(static_cast<int>(thePrefs.minFreeDiskSpace() / (1024 * 1024)));
        m_minFreeDiskSpaceSpin->setEnabled(diskCheck);
        if (auto* btn = m_commitFilesGroup->button(thePrefs.commitFiles()))
            btn->setChecked(true);
        if (auto* btn = m_extractMetaDataGroup->button(thePrefs.extractMetaData()))
            btn->setChecked(true);
        m_logToDiskCheck->setChecked(thePrefs.logToDisk());
        bool verboseOn = thePrefs.verbose();
        m_verboseCheck->setChecked(verboseOn);
        m_logLevelSpin->setValue(thePrefs.logLevel());
        m_logLevelSpin->setEnabled(verboseOn);
        m_verboseLogToDiskCheck->setChecked(thePrefs.verboseLogToDisk());
        m_verboseLogToDiskCheck->setEnabled(verboseOn);
        m_logSourceExchangeCheck->setChecked(thePrefs.logSourceExchange());
        m_logSourceExchangeCheck->setEnabled(verboseOn);
        m_logBannedClientsCheck->setChecked(thePrefs.logBannedClients());
        m_logBannedClientsCheck->setEnabled(verboseOn);
        m_logRatingDescCheck->setChecked(thePrefs.logRatingDescReceived());
        m_logRatingDescCheck->setEnabled(verboseOn);
        m_logSecureIdentCheck->setChecked(thePrefs.logSecureIdent());
        m_logSecureIdentCheck->setEnabled(verboseOn);
        m_logFilteredIPsCheck->setChecked(thePrefs.logFilteredIPs());
        m_logFilteredIPsCheck->setEnabled(verboseOn);
        m_logFileSavingCheck->setChecked(thePrefs.logFileSaving());
        m_logFileSavingCheck->setEnabled(verboseOn);
        m_logA4AFCheck->setChecked(thePrefs.logA4AF());
        m_logA4AFCheck->setEnabled(verboseOn);
        m_logUlDlEventsCheck->setChecked(thePrefs.logUlDlEvents());
        m_logUlDlEventsCheck->setEnabled(verboseOn);
        m_logRawSocketPacketsCheck->setChecked(thePrefs.logRawSocketPackets());
        m_logRawSocketPacketsCheck->setEnabled(verboseOn);
        m_enableIpcLogCheck->setChecked(thePrefs.enableIpcLog());
        m_startCoreWithConsoleCheck->setChecked(thePrefs.startCoreWithConsole());
        // USS
        bool ussOn = thePrefs.dynUpEnabled();
        m_dynUpEnabledCheck->setChecked(ussOn);
        m_dynUpPingToleranceSpin->setValue(thePrefs.dynUpPingTolerance());
        m_dynUpPingToleranceSpin->setEnabled(ussOn);
        m_dynUpPingToleranceMsSpin->setValue(thePrefs.dynUpPingToleranceMs());
        m_dynUpPingToleranceMsSpin->setEnabled(ussOn);
        bool useMs = thePrefs.dynUpUseMillisecondPingTolerance();
        m_dynUpRadioMs->setChecked(useMs);
        m_dynUpRadioPercent->setChecked(!useMs);
        m_dynUpRadioPercent->setEnabled(ussOn);
        m_dynUpRadioMs->setEnabled(ussOn);
        m_dynUpGoingUpSpin->setValue(thePrefs.dynUpGoingUpDivider());
        m_dynUpGoingUpSpin->setEnabled(ussOn);
        m_dynUpGoingDownSpin->setValue(thePrefs.dynUpGoingDownDivider());
        m_dynUpGoingDownSpin->setEnabled(ussOn);
        m_dynUpNumPingsSpin->setValue(thePrefs.dynUpNumberOfPings());
        m_dynUpNumPingsSpin->setEnabled(ussOn);
        m_closeUPnPCheck->setChecked(thePrefs.closeUPnPOnExit());
        m_skipWANIPCheck->setChecked(thePrefs.skipWANIPSetup());
        m_skipWANPPPCheck->setChecked(thePrefs.skipWANPPPSetup());
        m_fileBufferSlider->setValue(static_cast<int>(thePrefs.fileBufferSize()) / 16384);
        m_queueSizeSlider->setValue(static_cast<int>(thePrefs.queueSize()) / 100);

#ifdef Q_OS_WIN
        m_autotakeEd2kCheck->setChecked(thePrefs.autotakeEd2kLinks());
        m_winFirewallCheck->setChecked(thePrefs.openPortsOnWinFirewall());
        m_sparsePartFilesCheck->setChecked(thePrefs.sparsePartFiles());
        m_allocFullFileCheck->setChecked(thePrefs.allocFullFile());
        m_resolveShellLinksCheck->setChecked(thePrefs.resolveShellLinks());
        if (auto* btn = m_multiUserSharingGroup->button(thePrefs.multiUserSharing()))
            btn->setChecked(true);
#endif
    }
    m_loading = false;
    m_daemonSettingsLoaded = true;
    m_applyBtn->setEnabled(false);

    // Warn if thePrefs has clearly invalid port values (IPC never connected yet)
    if (thePrefs.port() <= 1 || thePrefs.udpPort() <= 1) {
        qWarning() << "Options: preferences not loaded — port values invalid"
                   << thePrefs.port() << "/" << thePrefs.udpPort();
        QMessageBox::warning(this,
            tr("Settings"),
            tr("Failed to load settings: connection to the daemon has not been established yet.\n"
               "Please wait a moment and reopen Options."));
    }

    // Additionally update from IPC if connected (live values override thePrefs)
    if (m_ipc && m_ipc->isConnected()) {
        Ipc::IpcMessage req(Ipc::IpcMsgType::GetPreferences);
        m_ipc->sendRequest(std::move(req), [this](const Ipc::IpcMessage& resp) {
            if (!resp.fieldBool(0)) {
                return;
            }
            m_loading = true;
            const QCborMap prefs = resp.fieldMap(1);

            // General page
            m_nickEdit->setText(prefs.value(QStringLiteral("nick")).toString());

            // Connection page
            auto capDown = static_cast<int>(prefs.value(QStringLiteral("maxGraphDownloadRate")).toInteger(100));
            auto capUp   = static_cast<int>(prefs.value(QStringLiteral("maxGraphUploadRate")).toInteger(100));
            m_capacityDownloadSpin->setValue(capDown);
            m_capacityUploadSpin->setValue(capUp);

            auto maxDown = static_cast<int>(prefs.value(QStringLiteral("maxDownload")).toInteger(0));
            auto maxUp   = static_cast<int>(prefs.value(QStringLiteral("maxUpload")).toInteger(0));
            m_downloadLimitCheck->setChecked(maxDown > 0);
            m_downloadLimitSlider->setEnabled(maxDown > 0);
            m_downloadLimitLabel->setEnabled(maxDown > 0);
            if (maxDown > 0) {
                m_downloadLimitSlider->setValue(maxDown);
            }
            m_uploadLimitCheck->setChecked(maxUp > 0);
            m_uploadLimitSlider->setEnabled(maxUp > 0);
            m_uploadLimitLabel->setEnabled(maxUp > 0);
            if (maxUp > 0) {
                m_uploadLimitSlider->setValue(maxUp);
            }

            auto tcpPort = static_cast<int>(prefs.value(QStringLiteral("port")).toInteger(5662));
            auto udpPort = static_cast<int>(prefs.value(QStringLiteral("udpPort")).toInteger(5672));
            m_tcpPortSpin->setValue(tcpPort);
            if (udpPort == 0) {
                m_udpDisableCheck->setChecked(true);
                m_udpPortSpin->setValue(5672);
                m_udpPortSpin->setEnabled(false);
            } else {
                m_udpDisableCheck->setChecked(false);
                m_udpPortSpin->setValue(udpPort);
            }

            m_upnpCheck->setChecked(prefs.value(QStringLiteral("enableUPnP")).toBool());
            m_maxSourcesSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("maxSourcesPerFile")).toInteger(400)));
            m_maxConnectionsSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("maxConnections")).toInteger(500)));
            m_autoConnectCheck->setChecked(prefs.value(QStringLiteral("autoConnect")).toBool());
            m_reconnectCheck->setChecked(prefs.value(QStringLiteral("reconnect")).toBool());
            m_overheadCheck->setChecked(prefs.value(QStringLiteral("showOverhead")).toBool());
            m_kadEnabledCheck->setChecked(prefs.value(QStringLiteral("kadEnabled")).toBool());
            m_ed2kEnabledCheck->setChecked(prefs.value(QStringLiteral("networkED2K")).toBool());

            // Server page
            m_safeServerConnectCheck->setChecked(prefs.value(QStringLiteral("safeServerConnect")).toBool());
            m_autoConnectStaticOnlyCheck->setChecked(prefs.value(QStringLiteral("autoConnectStaticOnly")).toBool());
            m_useServerPrioritiesCheck->setChecked(prefs.value(QStringLiteral("useServerPriorities")).toBool());
            m_addServersFromServerCheck->setChecked(prefs.value(QStringLiteral("addServersFromServer")).toBool());
            m_addServersFromClientsCheck->setChecked(prefs.value(QStringLiteral("addServersFromClients")).toBool());
            m_deadServerRetriesSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("deadServerRetries")).toInteger(1)));
            m_autoUpdateServerListCheck->setChecked(prefs.value(QStringLiteral("autoUpdateServerList")).toBool());
            m_serverListURLValue = prefs.value(QStringLiteral("serverListURL")).toString();
            m_smartLowIdCheck->setChecked(prefs.value(QStringLiteral("smartLowIdCheck")).toBool(true));
            m_manualHighPrioCheck->setChecked(prefs.value(QStringLiteral("manualServerHighPriority")).toBool());

            // Proxy page
            auto proxyType = static_cast<int>(prefs.value(QStringLiteral("proxyType")).toInteger(0));
            bool proxyOn = (proxyType != 0);
            m_proxyEnableCheck->setChecked(proxyOn);
            m_proxyTypeCombo->setCurrentIndex(proxyType);
            m_proxyHostEdit->setText(prefs.value(QStringLiteral("proxyHost")).toString());
            m_proxyPortSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("proxyPort")).toInteger(1080)));
            bool authOn = prefs.value(QStringLiteral("proxyEnablePassword")).toBool();
            m_proxyAuthCheck->setChecked(authOn);
            m_proxyUserEdit->setText(prefs.value(QStringLiteral("proxyUser")).toString());
            m_proxyPasswordEdit->setText(prefs.value(QStringLiteral("proxyPassword")).toString());

            m_proxyTypeCombo->setEnabled(proxyOn);
            m_proxyHostEdit->setEnabled(proxyOn);
            m_proxyPortSpin->setEnabled(proxyOn);
            m_proxyAuthCheck->setEnabled(proxyOn);
            m_proxyUserEdit->setEnabled(proxyOn && authOn);
            m_proxyPasswordEdit->setEnabled(proxyOn && authOn);

            // Directories page
            m_incomingDirEdit->setText(prefs.value(QStringLiteral("incomingDir")).toString());
            auto tempDirsArr = prefs.value(QStringLiteral("tempDirs")).toArray();
            if (!tempDirsArr.isEmpty())
                m_tempDirEdit->setText(tempDirsArr.first().toString());
            QStringList sharedPaths;
            for (const auto& item : prefs.value(QStringLiteral("sharedDirs")).toArray())
                sharedPaths.append(item.toString());
            static_cast<CheckableFileSystemModel*>(m_sharedDirsModel)->setCheckedPaths(sharedPaths);

            // Files page (daemon-side)
            m_addFilesPausedCheck->setChecked(prefs.value(QStringLiteral("addNewFilesPaused")).toBool());
            m_autoSharedFilesPrioCheck->setChecked(prefs.value(QStringLiteral("autoSharedFilesPriority")).toBool(true));
            m_autoDownloadPrioCheck->setChecked(prefs.value(QStringLiteral("autoDownloadPriority")).toBool(true));
            m_transferFullChunksCheck->setChecked(prefs.value(QStringLiteral("transferFullChunks")).toBool(true));
            m_previewPrioCheck->setChecked(prefs.value(QStringLiteral("previewPrio")).toBool());
            bool startNext = prefs.value(QStringLiteral("startNextPausedFile")).toBool();
            m_startNextPausedCheck->setChecked(startNext);
            m_preferSameCatCheck->setEnabled(startNext);
            m_onlySameCatCheck->setEnabled(startNext);
            m_preferSameCatCheck->setChecked(prefs.value(QStringLiteral("startNextPausedFileSameCat")).toBool());
            m_onlySameCatCheck->setChecked(prefs.value(QStringLiteral("startNextPausedFileOnlySameCat")).toBool());
            m_rememberDownloadedCheck->setChecked(prefs.value(QStringLiteral("rememberDownloadedFiles")).toBool(true));
            m_rememberCancelledCheck->setChecked(prefs.value(QStringLiteral("rememberCancelledFiles")).toBool(true));

            // Notifications page (daemon-side)
            m_notifyLogCheck->setChecked(prefs.value(QStringLiteral("notifyOnLog")).toBool());
            m_notifyChatCheck->setChecked(prefs.value(QStringLiteral("notifyOnChat")).toBool());
            m_notifyChatMsgCheck->setChecked(prefs.value(QStringLiteral("notifyOnChatMsg")).toBool());
            m_notifyChatMsgCheck->setEnabled(m_notifyChatCheck->isChecked());
            m_notifyDownloadAddedCheck->setChecked(prefs.value(QStringLiteral("notifyOnDownloadAdded")).toBool());
            m_notifyDownloadFinishedCheck->setChecked(prefs.value(QStringLiteral("notifyOnDownloadFinished")).toBool());
            m_notifyNewVersionCheck->setChecked(prefs.value(QStringLiteral("notifyOnNewVersion")).toBool());
            m_notifyUrgentCheck->setChecked(prefs.value(QStringLiteral("notifyOnUrgent")).toBool());
            m_emailEnabledCheck->setChecked(prefs.value(QStringLiteral("notifyEmailEnabled")).toBool());
            m_smtpServer = prefs.value(QStringLiteral("notifyEmailSmtpServer")).toString();
            m_smtpPort = static_cast<int>(prefs.value(QStringLiteral("notifyEmailSmtpPort")).toInteger(25));
            m_smtpAuth = static_cast<int>(prefs.value(QStringLiteral("notifyEmailSmtpAuth")).toInteger(0));
            m_smtpTls = prefs.value(QStringLiteral("notifyEmailSmtpTls")).toBool();
            m_smtpUser = prefs.value(QStringLiteral("notifyEmailSmtpUser")).toString();
            m_smtpPassword = prefs.value(QStringLiteral("notifyEmailSmtpPassword")).toString();
            m_emailRecipientEdit->setText(prefs.value(QStringLiteral("notifyEmailRecipient")).toString());
            m_emailSenderEdit->setText(prefs.value(QStringLiteral("notifyEmailSender")).toString());
            bool emailOn = m_emailEnabledCheck->isChecked();
            m_smtpServerBtn->setEnabled(emailOn);
            m_emailRecipientEdit->setEnabled(emailOn);
            m_emailSenderEdit->setEnabled(emailOn);

            // Messages and Comments page (daemon-side)
            m_msgFriendsOnlyCheck->setChecked(prefs.value(QStringLiteral("msgOnlyFriends")).toBool());
            bool spamOn = prefs.value(QStringLiteral("enableSpamFilter")).toBool();
            m_advancedSpamFilterCheck->setChecked(spamOn);
            m_requireCaptchaCheck->setChecked(prefs.value(QStringLiteral("useChatCaptchas")).toBool());
            m_requireCaptchaCheck->setEnabled(spamOn);
            m_messageFilterEdit->setText(prefs.value(QStringLiteral("messageFilter")).toString());
            m_commentFilterEdit->setText(prefs.value(QStringLiteral("commentFilter")).toString());

            // Security page (daemon-side)
            m_filterServersByIPCheck->setChecked(prefs.value(QStringLiteral("filterServerByIP")).toBool());
            m_ipFilterLevelSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("ipFilterLevel")).toInteger(127)));
            m_viewSharedGroup->button(static_cast<int>(prefs.value(QStringLiteral("viewSharedFilesAccess")).toInteger(1)))->setChecked(true);
            bool cryptSupported = prefs.value(QStringLiteral("cryptLayerSupported")).toBool(true);
            bool cryptRequested = prefs.value(QStringLiteral("cryptLayerRequested")).toBool(true);
            bool cryptRequired  = prefs.value(QStringLiteral("cryptLayerRequired")).toBool();
            m_cryptLayerDisableCheck->setChecked(!cryptSupported);
            m_cryptLayerRequestedCheck->setChecked(cryptRequested);
            m_cryptLayerRequestedCheck->setEnabled(cryptSupported);
            m_cryptLayerRequiredCheck->setChecked(cryptRequired);
            m_cryptLayerRequiredCheck->setEnabled(cryptSupported && cryptRequested);
            m_useSecureIdentCheck->setChecked(prefs.value(QStringLiteral("useSecureIdent")).toBool(true));
            m_enableSearchResultFilterCheck->setChecked(prefs.value(QStringLiteral("enableSearchResultFilter")).toBool(true));
            m_warnUntrustedFilesCheck->setChecked(prefs.value(QStringLiteral("warnUntrustedFiles")).toBool(true));
            m_ipFilterUpdateUrlEdit->setText(prefs.value(QStringLiteral("ipFilterUpdateUrl")).toString());

            // Web Interface page
            m_webEnabledCheck->setChecked(prefs.value(QStringLiteral("webServerEnabled")).toBool());
            m_webRestApiCheck->setChecked(prefs.value(QStringLiteral("webServerRestApiEnabled")).toBool());
            m_webGzipCheck->setChecked(prefs.value(QStringLiteral("webServerGzipEnabled")).toBool(true));
            m_webUPnPCheck->setChecked(prefs.value(QStringLiteral("webServerUPnP")).toBool());
            m_webPortSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("webServerPort")).toInteger(4711)));
            m_webTemplateEdit->setText(prefs.value(QStringLiteral("webServerTemplatePath")).toString());
            if (m_webTemplateEdit->text().isEmpty())
                m_webTemplateEdit->setPlaceholderText(AppConfig::configDir() + QStringLiteral("/eMule.tmpl"));
            m_webSessionTimeoutSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("webServerSessionTimeout")).toInteger(5)));
            m_webHttpsCheck->setChecked(prefs.value(QStringLiteral("webServerHttpsEnabled")).toBool());
            m_webCertEdit->setText(prefs.value(QStringLiteral("webServerCertPath")).toString());
            m_webKeyEdit->setText(prefs.value(QStringLiteral("webServerKeyPath")).toString());
            m_webApiKeyEdit->setText(prefs.value(QStringLiteral("webServerApiKey")).toString());
            // Password fields: don't show the hash — leave blank; user types new password to change
            m_webAdminHiLevCheck->setChecked(prefs.value(QStringLiteral("webServerAdminAllowHiLevFunc")).toBool());
            m_webGuestEnabledCheck->setChecked(prefs.value(QStringLiteral("webServerGuestEnabled")).toBool());
            {
                bool webOn = m_webEnabledCheck->isChecked();
                m_webRestApiCheck->setEnabled(webOn);
                m_webGzipCheck->setEnabled(webOn);
                m_webUPnPCheck->setEnabled(webOn);
                m_webPortSpin->setEnabled(webOn);
                m_webTemplateEdit->setEnabled(webOn);
                m_webTemplateBrowseBtn->setEnabled(webOn);
                m_webTemplateReloadBtn->setEnabled(webOn);
                m_webSessionTimeoutSpin->setEnabled(webOn);
                m_webHttpsCheck->setEnabled(webOn);
                bool httpsOn = webOn && m_webHttpsCheck->isChecked();
                m_webCreateCertBtn->setEnabled(httpsOn);
                m_webCertEdit->setEnabled(httpsOn);
                m_webCertBrowseBtn->setEnabled(httpsOn);
                m_webKeyEdit->setEnabled(httpsOn);
                m_webKeyBrowseBtn->setEnabled(httpsOn);
                m_webApiKeyEdit->setEnabled(webOn);
                m_webAdminPasswordEdit->setEnabled(webOn);
                m_webAdminHiLevCheck->setEnabled(webOn);
                m_webGuestEnabledCheck->setEnabled(webOn);
                m_webGuestPasswordEdit->setEnabled(webOn && m_webGuestEnabledCheck->isChecked());
            }

            // Statistics page
            auto graphsSec = static_cast<int>(prefs.value(QStringLiteral("graphsUpdateSec")).toInteger(3));
            m_statsGraphUpdateSlider->setValue(graphsSec);
            auto avgMin = static_cast<int>(prefs.value(QStringLiteral("statsAverageMinutes")).toInteger(5));
            m_statsAvgTimeSlider->setValue(avgMin);
            m_statsFillGraphsCheck->setChecked(prefs.value(QStringLiteral("fillGraphs")).toBool());
            m_statsYScaleSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("statsConnectionsMax")).toInteger(100)));
            auto ratio = static_cast<uint32_t>(prefs.value(QStringLiteral("statsConnectionsRatio")).toInteger(3));
            // Map ratio value to combo index: 1→0, 2→1, 3→2, 4→3, 5→4, 10→5, 20→6
            static constexpr int ratioValues[] = {1, 2, 3, 4, 5, 10, 20};
            int ratioIdx = 2; // default 1:3
            for (int ri = 0; ri < 7; ++ri) {
                if (static_cast<uint32_t>(ratioValues[ri]) == ratio) { ratioIdx = ri; break; }
            }
            m_statsRatioCombo->setCurrentIndex(ratioIdx);
            auto statsSec = static_cast<int>(prefs.value(QStringLiteral("statsUpdateSec")).toInteger(5));
            m_statsTreeUpdateSlider->setValue(statsSec);

            // Extended page
            m_maxConPerFiveSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("maxConsPerFive")).toInteger(20)));
            m_maxHalfOpenSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("maxHalfConnections")).toInteger(9)));
            auto keepAlive = static_cast<int>(prefs.value(QStringLiteral("serverKeepAliveTimeout")).toInteger(0));
            m_serverKeepAliveSpin->setValue(keepAlive / 60000); // stored in ms, shown in min
            m_useCreditSystemCheck->setChecked(prefs.value(QStringLiteral("useCreditSystem")).toBool(true));
            m_filterLANIPsCheck->setChecked(prefs.value(QStringLiteral("filterLANIPs")).toBool(true));
            m_showExtControlsCheck->setChecked(prefs.value(QStringLiteral("showExtControls")).toBool());
            m_a4afSaveCpuCheck->setChecked(prefs.value(QStringLiteral("a4afSaveCpu")).toBool());
            m_disableArchPreviewCheck->setChecked(!prefs.value(QStringLiteral("autoArchivePreviewStart")).toBool(true));
            m_ed2kHostnameEdit->setText(prefs.value(QStringLiteral("ed2kHostname")).toString());
            bool diskCheck = prefs.value(QStringLiteral("checkDiskspace")).toBool();
            m_checkDiskspaceCheck->setChecked(diskCheck);
            auto minFree = static_cast<int>(prefs.value(QStringLiteral("minFreeDiskSpace")).toInteger(20971520));
            m_minFreeDiskSpaceSpin->setValue(minFree / (1024 * 1024)); // bytes to MB
            m_minFreeDiskSpaceSpin->setEnabled(diskCheck);
            int commit = static_cast<int>(prefs.value(QStringLiteral("commitFiles")).toInteger(1));
            if (auto* btn = m_commitFilesGroup->button(commit))
                btn->setChecked(true);
            int metaMode = static_cast<int>(prefs.value(QStringLiteral("extractMetaData")).toInteger(1));
            if (auto* btn = m_extractMetaDataGroup->button(metaMode))
                btn->setChecked(true);
            m_logToDiskCheck->setChecked(prefs.value(QStringLiteral("logToDisk")).toBool());
            bool verboseOn = prefs.value(QStringLiteral("verbose")).toBool(true);
            m_verboseCheck->setChecked(verboseOn);
            m_logLevelSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("logLevel")).toInteger(5)));
            m_logLevelSpin->setEnabled(verboseOn);
            m_verboseLogToDiskCheck->setChecked(prefs.value(QStringLiteral("verboseLogToDisk")).toBool());
            m_verboseLogToDiskCheck->setEnabled(verboseOn);
            m_logSourceExchangeCheck->setChecked(prefs.value(QStringLiteral("logSourceExchange")).toBool());
            m_logSourceExchangeCheck->setEnabled(verboseOn);
            m_logBannedClientsCheck->setChecked(prefs.value(QStringLiteral("logBannedClients")).toBool(true));
            m_logBannedClientsCheck->setEnabled(verboseOn);
            m_logRatingDescCheck->setChecked(prefs.value(QStringLiteral("logRatingDescReceived")).toBool(true));
            m_logRatingDescCheck->setEnabled(verboseOn);
            m_logSecureIdentCheck->setChecked(prefs.value(QStringLiteral("logSecureIdent")).toBool(true));
            m_logSecureIdentCheck->setEnabled(verboseOn);
            m_logFilteredIPsCheck->setChecked(prefs.value(QStringLiteral("logFilteredIPs")).toBool(true));
            m_logFilteredIPsCheck->setEnabled(verboseOn);
            m_logFileSavingCheck->setChecked(prefs.value(QStringLiteral("logFileSaving")).toBool());
            m_logFileSavingCheck->setEnabled(verboseOn);
            m_logA4AFCheck->setChecked(prefs.value(QStringLiteral("logA4AF")).toBool());
            m_logA4AFCheck->setEnabled(verboseOn);
            m_logUlDlEventsCheck->setChecked(prefs.value(QStringLiteral("logUlDlEvents")).toBool(true));
            m_logUlDlEventsCheck->setEnabled(verboseOn);
            m_logRawSocketPacketsCheck->setChecked(prefs.value(QStringLiteral("logRawSocketPackets")).toBool());
            m_logRawSocketPacketsCheck->setEnabled(verboseOn);
            // USS
            bool ussOn = prefs.value(QStringLiteral("dynUpEnabled")).toBool();
            m_dynUpEnabledCheck->setChecked(ussOn);
            m_dynUpPingToleranceSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("dynUpPingTolerance")).toInteger(500)));
            m_dynUpPingToleranceSpin->setEnabled(ussOn);
            m_dynUpPingToleranceMsSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("dynUpPingToleranceMs")).toInteger(200)));
            m_dynUpPingToleranceMsSpin->setEnabled(ussOn);
            bool useMs = prefs.value(QStringLiteral("dynUpUseMillisecondPingTolerance")).toBool();
            m_dynUpRadioMs->setChecked(useMs);
            m_dynUpRadioPercent->setChecked(!useMs);
            m_dynUpRadioPercent->setEnabled(ussOn);
            m_dynUpRadioMs->setEnabled(ussOn);
            m_dynUpGoingUpSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("dynUpGoingUpDivider")).toInteger(1000)));
            m_dynUpGoingUpSpin->setEnabled(ussOn);
            m_dynUpGoingDownSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("dynUpGoingDownDivider")).toInteger(1000)));
            m_dynUpGoingDownSpin->setEnabled(ussOn);
            m_dynUpNumPingsSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("dynUpNumberOfPings")).toInteger(1)));
            m_dynUpNumPingsSpin->setEnabled(ussOn);
            m_closeUPnPCheck->setChecked(prefs.value(QStringLiteral("closeUPnPOnExit")).toBool(true));
            m_skipWANIPCheck->setChecked(prefs.value(QStringLiteral("skipWANIPSetup")).toBool());
            m_skipWANPPPCheck->setChecked(prefs.value(QStringLiteral("skipWANPPPSetup")).toBool());
            auto bufSize = static_cast<int>(prefs.value(QStringLiteral("fileBufferSize")).toInteger(245760));
            m_fileBufferSlider->setValue(bufSize / 16384); // bytes to slider units (16KB each)
            auto qSize = static_cast<int>(prefs.value(QStringLiteral("queueSize")).toInteger(5000));
            m_queueSizeSlider->setValue(qSize / 100); // to slider units (100 each)

#ifdef Q_OS_WIN
            m_autotakeEd2kCheck->setChecked(prefs.value(QStringLiteral("autotakeEd2kLinks")).toBool(true));
            m_winFirewallCheck->setChecked(prefs.value(QStringLiteral("openPortsOnWinFirewall")).toBool());
            m_sparsePartFilesCheck->setChecked(prefs.value(QStringLiteral("sparsePartFiles")).toBool());
            m_allocFullFileCheck->setChecked(prefs.value(QStringLiteral("allocFullFile")).toBool());
            m_resolveShellLinksCheck->setChecked(prefs.value(QStringLiteral("resolveShellLinks")).toBool());
            int muSharing = static_cast<int>(prefs.value(QStringLiteral("multiUserSharing")).toInteger(2));
            if (auto* btn = m_multiUserSharingGroup->button(muSharing))
                btn->setChecked(true);
#endif

            m_loading = false;
            m_daemonSettingsLoaded = true;
            m_applyBtn->setEnabled(false);
        });
    }

    loadSchedulerData();
}

// ---------------------------------------------------------------------------
// Save settings from controls
// ---------------------------------------------------------------------------

void OptionsDialog::saveSettings()
{
    // Detect proxy changes and warn the user
    int newProxyType = m_proxyEnableCheck->isChecked() ? m_proxyTypeCombo->currentIndex() : 0;
    if (newProxyType != thePrefs.proxyType()
        || m_proxyHostEdit->text() != thePrefs.proxyHost()
        || static_cast<uint16>(m_proxyPortSpin->value()) != thePrefs.proxyPort()
        || m_proxyAuthCheck->isChecked() != thePrefs.proxyEnablePassword()
        || m_proxyUserEdit->text() != thePrefs.proxyUser()
        || m_proxyPasswordEdit->text() != thePrefs.proxyPassword()) {
        QMessageBox::information(this, tr("Proxy"),
            tr("Proxy settings will only apply to new connections.\n"
               "Restart eMule for all connections to use the new proxy settings."));
    }

    // GUI-only settings — save locally
    thePrefs.setPromptOnExit(m_promptOnExitCheck->isChecked());
    thePrefs.setStartMinimized(m_startMinimizedCheck->isChecked());
    thePrefs.setShowSplashScreen(m_showSplashCheck->isChecked());
    thePrefs.setEnableOnlineSignature(m_enableOnlineSigCheck->isChecked());
#ifdef Q_OS_WIN
    thePrefs.setEnableMiniMule(m_enableMiniMuleCheck->isChecked());
#endif
    thePrefs.setPreventStandby(m_preventStandbyCheck->isChecked());
    thePrefs.setVersionCheckEnabled(m_versionCheckBox->isChecked());
    thePrefs.setVersionCheckDays(m_versionCheckDaysSpin->value());
    thePrefs.setBringToFrontOnLinkClick(m_bringToFrontCheck->isChecked());

    // Language change requires restart
    {
        const QString newLang = m_langCombo->currentData().toString();
        if (newLang != thePrefs.language()) {
            thePrefs.setLanguage(newLang);
            QMessageBox::information(this, tr("Language"),
                tr("The language change will take effect after restarting the application."));
        }
    }

    // Start with OS: register/unregister autostart
    if (m_startWithOSCheck->isChecked() != thePrefs.startWithOS()) {
        thePrefs.setStartWithOS(m_startWithOSCheck->isChecked());
        eMule::setAutoStart(m_startWithOSCheck->isChecked());
    }

    // Core settings — changes require restart
    {
        bool coreChanged = false;
        if (m_coreAddressEdit->text().trimmed() != thePrefs.ipcListenAddress()) {
            thePrefs.setIpcListenAddress(m_coreAddressEdit->text().trimmed());
            coreChanged = true;
        }
        if (static_cast<uint16_t>(m_corePortSpin->value()) != thePrefs.ipcPort()) {
            thePrefs.setIpcPort(static_cast<uint16_t>(m_corePortSpin->value()));
            coreChanged = true;
        }
        const QString newToken = m_coreTokenEdit->text().trimmed();
        const QString oldToken = thePrefs.ipcTokens().isEmpty() ? QString{} : thePrefs.ipcTokens().first();
        if (newToken != oldToken) {
            thePrefs.setIpcTokens(newToken.isEmpty() ? QStringList{} : QStringList{newToken});
            coreChanged = true;
        }
        if (coreChanged) {
            QMessageBox::information(this, tr("Core"),
                tr("Core connection settings will take effect after restarting the application."));
        }
    }

    // Display page
    thePrefs.setDepth3D(m_depth3DSlider->value());
    thePrefs.setTooltipDelay(m_tooltipDelaySpin->value());
    thePrefs.setMinimizeToTray(m_minimizeToTrayCheck->isChecked());
    thePrefs.setTransferDoubleClick(m_transferDoubleClickCheck->isChecked());
    thePrefs.setShowDwlPercentage(m_showDwlPercentageCheck->isChecked());
    thePrefs.setShowRatesInTitle(m_showRatesInTitleCheck->isChecked());
    thePrefs.setShowCatTabInfos(m_showCatTabInfosCheck->isChecked());
    thePrefs.setAutoRemoveFinishedDownloads(m_autoRemoveFinishedCheck->isChecked());
    thePrefs.setShowTransToolbar(m_showTransToolbarCheck->isChecked());
    thePrefs.setStoreSearches(m_storeSearchesCheck->isChecked());
    thePrefs.setDisableKnownClientList(m_disableKnownClientListCheck->isChecked());
    thePrefs.setDisableQueueList(m_disableQueueListCheck->isChecked());
    thePrefs.setUseAutoCompletion(m_useAutoCompletionCheck->isChecked());
    thePrefs.setUseOriginalIcons(m_useOriginalIconsCheck->isChecked());
    thePrefs.setEnableIpcLog(m_enableIpcLogCheck->isChecked());
    thePrefs.setStartCoreWithConsole(m_startCoreWithConsoleCheck->isChecked());
    if (m_useOriginalIconsCheck->isChecked() != m_initialUseOriginalIcons) {
        QMessageBox::information(this, tr("Icons"),
            tr("The icon change will take effect after restarting the application."));
        m_initialUseOriginalIcons = m_useOriginalIconsCheck->isChecked();
    }

    // Display - font
    thePrefs.setLogFont(m_currentLogFont);

    // Connection page (GUI-only display setting)
    thePrefs.setShowOverhead(m_overheadCheck->isChecked());

    // Files page (GUI-only)
    thePrefs.setWatchClipboard4ED2KLinks(m_watchClipboardCheck->isChecked());
    thePrefs.setUseAdvancedCalcRemainingTime(m_advancedCalcRemainingCheck->isChecked());
    thePrefs.setVideoPlayerCommand(m_videoPlayerCmdEdit->text());
    thePrefs.setVideoPlayerArgs(m_videoPlayerArgsEdit->text());
    thePrefs.setCreateBackupToPreview(m_createBackupToPreviewCheck->isChecked());
    thePrefs.setAutoCleanupFilenames(m_autoCleanupFilenamesCheck->isChecked());

    // Notifications page (GUI-side)
    thePrefs.setNotifySoundType(m_soundGroup->checkedId());
    thePrefs.setNotifySoundFile(m_soundFileEdit->text());

    // IRC page (GUI-local)
    thePrefs.setIrcServer(m_ircServerEdit->text());
    thePrefs.setIrcNick(m_ircNickEdit->text());
    thePrefs.setIrcUseChannelFilter(m_ircUseChannelFilterCheck->isChecked());
    thePrefs.setIrcChannelFilter(
        m_ircChannelFilterNameEdit->text() + QLatin1Char('|')
        + QString::number(m_ircChannelFilterUsersSpin->value()));
    thePrefs.setIrcUsePerform(m_ircUsePerformCheck->isChecked());
    thePrefs.setIrcPerformString(m_ircPerformEdit->text());

    auto* root = m_ircMiscTree->invisibleRootItem();
    thePrefs.setIrcConnectHelpChannel(root->child(0)->checkState(0) == Qt::Checked);
    thePrefs.setIrcLoadChannelList(root->child(1)->checkState(0) == Qt::Checked);
    thePrefs.setIrcAddTimestamp(root->child(2)->checkState(0) == Qt::Checked);
    auto* ignoreParent = root->child(3);
    thePrefs.setIrcIgnoreMiscInfoMessages(ignoreParent->child(0)->checkState(0) == Qt::Checked);
    thePrefs.setIrcIgnoreJoinMessages(ignoreParent->child(1)->checkState(0) == Qt::Checked);
    thePrefs.setIrcIgnorePartMessages(ignoreParent->child(2)->checkState(0) == Qt::Checked);
    thePrefs.setIrcIgnoreQuitMessages(ignoreParent->child(3)->checkState(0) == Qt::Checked);

    // Messages page (GUI-only)
    thePrefs.setShowSmileys(m_showSmileysCheck->isChecked());
    thePrefs.setIndicateRatings(m_indicateRatingsCheck->isChecked());
    thePrefs.setShowExtControls(m_showExtControlsCheck->isChecked());

    // Statistics page — always save locally for immediate GUI effect
    thePrefs.setGraphsUpdateSec(static_cast<uint32>(m_statsGraphUpdateSlider->value()));
    thePrefs.setStatsAverageMinutes(static_cast<uint32>(m_statsAvgTimeSlider->value()));
    thePrefs.setStatsUpdateSec(static_cast<uint32>(m_statsTreeUpdateSlider->value()));
    thePrefs.setFillGraphs(m_statsFillGraphsCheck->isChecked());
    thePrefs.setStatsConnectionsMax(static_cast<uint32>(m_statsYScaleSpin->value()));
    {
        static constexpr int ratioValues[] = {1, 2, 3, 4, 5, 10, 20};
        int ri = m_statsRatioCombo->currentIndex();
        thePrefs.setStatsConnectionsRatio(static_cast<uint32>(ri >= 0 && ri < 7 ? ratioValues[ri] : 3));
    }

    thePrefs.save();

    // Daemon settings — send via IPC (only if we successfully loaded them first)
    if (m_daemonSettingsLoaded && m_ipc && m_ipc->isConnected()) {
        Ipc::IpcMessage req(Ipc::IpcMsgType::SetPreferences);
        req.append(QStringLiteral("nick"));
        req.append(m_nickEdit->text());

        // Connection page
        req.append(QStringLiteral("maxGraphDownloadRate"));
        req.append(static_cast<qint64>(m_capacityDownloadSpin->value()));
        req.append(QStringLiteral("maxGraphUploadRate"));
        req.append(static_cast<qint64>(m_capacityUploadSpin->value()));
        req.append(QStringLiteral("maxDownload"));
        req.append(static_cast<qint64>(m_downloadLimitCheck->isChecked() ? m_downloadLimitSlider->value() : 0));
        req.append(QStringLiteral("maxUpload"));
        req.append(static_cast<qint64>(m_uploadLimitCheck->isChecked() ? m_uploadLimitSlider->value() : 0));
        req.append(QStringLiteral("port"));
        req.append(static_cast<qint64>(m_tcpPortSpin->value()));
        req.append(QStringLiteral("udpPort"));
        req.append(static_cast<qint64>(m_udpDisableCheck->isChecked() ? 0 : m_udpPortSpin->value()));
        req.append(QStringLiteral("enableUPnP"));
        req.append(m_upnpCheck->isChecked());
        req.append(QStringLiteral("maxSourcesPerFile"));
        req.append(static_cast<qint64>(m_maxSourcesSpin->value()));
        req.append(QStringLiteral("maxConnections"));
        req.append(static_cast<qint64>(m_maxConnectionsSpin->value()));
        req.append(QStringLiteral("autoConnect"));
        req.append(m_autoConnectCheck->isChecked());
        req.append(QStringLiteral("reconnect"));
        req.append(m_reconnectCheck->isChecked());
        req.append(QStringLiteral("showOverhead"));
        req.append(m_overheadCheck->isChecked());
        req.append(QStringLiteral("kadEnabled"));
        req.append(m_kadEnabledCheck->isChecked());
        req.append(QStringLiteral("schedulerEnabled"));
        req.append(m_schedEnabledCheck->isChecked());
        req.append(QStringLiteral("networkED2K"));
        req.append(m_ed2kEnabledCheck->isChecked());

        // Server page
        req.append(QStringLiteral("safeServerConnect"));
        req.append(m_safeServerConnectCheck->isChecked());
        req.append(QStringLiteral("autoConnectStaticOnly"));
        req.append(m_autoConnectStaticOnlyCheck->isChecked());
        req.append(QStringLiteral("useServerPriorities"));
        req.append(m_useServerPrioritiesCheck->isChecked());
        req.append(QStringLiteral("addServersFromServer"));
        req.append(m_addServersFromServerCheck->isChecked());
        req.append(QStringLiteral("addServersFromClients"));
        req.append(m_addServersFromClientsCheck->isChecked());
        req.append(QStringLiteral("deadServerRetries"));
        req.append(static_cast<qint64>(m_deadServerRetriesSpin->value()));
        req.append(QStringLiteral("autoUpdateServerList"));
        req.append(m_autoUpdateServerListCheck->isChecked());
        req.append(QStringLiteral("serverListURL"));
        req.append(m_serverListURLValue);
        req.append(QStringLiteral("smartLowIdCheck"));
        req.append(m_smartLowIdCheck->isChecked());
        req.append(QStringLiteral("manualServerHighPriority"));
        req.append(m_manualHighPrioCheck->isChecked());

        // Proxy page
        req.append(QStringLiteral("proxyType"));
        req.append(static_cast<qint64>(m_proxyEnableCheck->isChecked() ? m_proxyTypeCombo->currentIndex() : 0));
        req.append(QStringLiteral("proxyHost"));
        req.append(m_proxyHostEdit->text());
        req.append(QStringLiteral("proxyPort"));
        req.append(static_cast<qint64>(m_proxyPortSpin->value()));
        req.append(QStringLiteral("proxyEnablePassword"));
        req.append(m_proxyAuthCheck->isChecked());
        req.append(QStringLiteral("proxyUser"));
        req.append(m_proxyUserEdit->text());
        req.append(QStringLiteral("proxyPassword"));
        req.append(m_proxyPasswordEdit->text());

        // Directories page
        req.append(QStringLiteral("incomingDir"));
        req.append(m_incomingDirEdit->text());
        req.append(QStringLiteral("tempDirs"));
        QCborArray tempArr;
        tempArr.append(m_tempDirEdit->text());
        req.append(tempArr);
        req.append(QStringLiteral("sharedDirs"));
        QCborArray sharedArr;
        for (const auto& p : static_cast<CheckableFileSystemModel*>(m_sharedDirsModel)->checkedPaths())
            sharedArr.append(p);
        req.append(sharedArr);

        // Files page (daemon-side)
        req.append(QStringLiteral("addNewFilesPaused"));
        req.append(m_addFilesPausedCheck->isChecked());
        req.append(QStringLiteral("autoDownloadPriority"));
        req.append(m_autoDownloadPrioCheck->isChecked());
        req.append(QStringLiteral("autoSharedFilesPriority"));
        req.append(m_autoSharedFilesPrioCheck->isChecked());
        req.append(QStringLiteral("transferFullChunks"));
        req.append(m_transferFullChunksCheck->isChecked());
        req.append(QStringLiteral("previewPrio"));
        req.append(m_previewPrioCheck->isChecked());
        req.append(QStringLiteral("startNextPausedFile"));
        req.append(m_startNextPausedCheck->isChecked());
        req.append(QStringLiteral("startNextPausedFileSameCat"));
        req.append(m_preferSameCatCheck->isChecked());
        req.append(QStringLiteral("startNextPausedFileOnlySameCat"));
        req.append(m_onlySameCatCheck->isChecked());
        req.append(QStringLiteral("rememberDownloadedFiles"));
        req.append(m_rememberDownloadedCheck->isChecked());
        req.append(QStringLiteral("rememberCancelledFiles"));
        req.append(m_rememberCancelledCheck->isChecked());

        // Notifications page (daemon-side)
        req.append(QStringLiteral("notifyOnLog"));
        req.append(m_notifyLogCheck->isChecked());
        req.append(QStringLiteral("notifyOnChat"));
        req.append(m_notifyChatCheck->isChecked());
        req.append(QStringLiteral("notifyOnChatMsg"));
        req.append(m_notifyChatMsgCheck->isChecked());
        req.append(QStringLiteral("notifyOnDownloadAdded"));
        req.append(m_notifyDownloadAddedCheck->isChecked());
        req.append(QStringLiteral("notifyOnDownloadFinished"));
        req.append(m_notifyDownloadFinishedCheck->isChecked());
        req.append(QStringLiteral("notifyOnNewVersion"));
        req.append(m_notifyNewVersionCheck->isChecked());
        req.append(QStringLiteral("notifyOnUrgent"));
        req.append(m_notifyUrgentCheck->isChecked());
        req.append(QStringLiteral("notifyEmailEnabled"));
        req.append(m_emailEnabledCheck->isChecked());
        req.append(QStringLiteral("notifyEmailSmtpServer"));
        req.append(m_smtpServer);
        req.append(QStringLiteral("notifyEmailSmtpPort"));
        req.append(static_cast<qint64>(m_smtpPort));
        req.append(QStringLiteral("notifyEmailSmtpAuth"));
        req.append(static_cast<qint64>(m_smtpAuth));
        req.append(QStringLiteral("notifyEmailSmtpTls"));
        req.append(m_smtpTls);
        req.append(QStringLiteral("notifyEmailSmtpUser"));
        req.append(m_smtpUser);
        req.append(QStringLiteral("notifyEmailSmtpPassword"));
        req.append(m_smtpPassword);
        req.append(QStringLiteral("notifyEmailRecipient"));
        req.append(m_emailRecipientEdit->text());
        req.append(QStringLiteral("notifyEmailSender"));
        req.append(m_emailSenderEdit->text());

        // Messages and Comments page (daemon-side)
        req.append(QStringLiteral("msgOnlyFriends"));
        req.append(m_msgFriendsOnlyCheck->isChecked());
        req.append(QStringLiteral("enableSpamFilter"));
        req.append(m_advancedSpamFilterCheck->isChecked());
        req.append(QStringLiteral("useChatCaptchas"));
        req.append(m_requireCaptchaCheck->isChecked());
        req.append(QStringLiteral("messageFilter"));
        req.append(m_messageFilterEdit->text());
        req.append(QStringLiteral("commentFilter"));
        req.append(m_commentFilterEdit->text());

        // Security page (daemon-side)
        req.append(QStringLiteral("filterServerByIP"));
        req.append(m_filterServersByIPCheck->isChecked());
        req.append(QStringLiteral("ipFilterLevel"));
        req.append(static_cast<qint64>(m_ipFilterLevelSpin->value()));
        req.append(QStringLiteral("viewSharedFilesAccess"));
        req.append(static_cast<qint64>(m_viewSharedGroup->checkedId()));
        req.append(QStringLiteral("cryptLayerSupported"));
        req.append(!m_cryptLayerDisableCheck->isChecked());
        req.append(QStringLiteral("cryptLayerRequested"));
        req.append(m_cryptLayerRequestedCheck->isChecked());
        req.append(QStringLiteral("cryptLayerRequired"));
        req.append(m_cryptLayerRequiredCheck->isChecked());
        req.append(QStringLiteral("useSecureIdent"));
        req.append(m_useSecureIdentCheck->isChecked());
        req.append(QStringLiteral("enableSearchResultFilter"));
        req.append(m_enableSearchResultFilterCheck->isChecked());
        req.append(QStringLiteral("warnUntrustedFiles"));
        req.append(m_warnUntrustedFilesCheck->isChecked());
        req.append(QStringLiteral("ipFilterUpdateUrl"));
        req.append(m_ipFilterUpdateUrlEdit->text().trimmed());

        // Web Interface page
        req.append(QStringLiteral("webServerEnabled"));
        req.append(m_webEnabledCheck->isChecked());
        req.append(QStringLiteral("webServerRestApiEnabled"));
        req.append(m_webRestApiCheck->isChecked());
        req.append(QStringLiteral("webServerGzipEnabled"));
        req.append(m_webGzipCheck->isChecked());
        req.append(QStringLiteral("webServerUPnP"));
        req.append(m_webUPnPCheck->isChecked());
        req.append(QStringLiteral("webServerPort"));
        req.append(static_cast<qint64>(m_webPortSpin->value()));
        req.append(QStringLiteral("webServerTemplatePath"));
        req.append(m_webTemplateEdit->text());
        req.append(QStringLiteral("webServerSessionTimeout"));
        req.append(static_cast<qint64>(m_webSessionTimeoutSpin->value()));
        req.append(QStringLiteral("webServerHttpsEnabled"));
        req.append(m_webHttpsCheck->isChecked());
        req.append(QStringLiteral("webServerCertPath"));
        req.append(m_webCertEdit->text());
        req.append(QStringLiteral("webServerKeyPath"));
        req.append(m_webKeyEdit->text());
        req.append(QStringLiteral("webServerApiKey"));
        req.append(m_webApiKeyEdit->text());
        // Only send password if user typed something (hash it SHA-256 before sending)
        if (!m_webAdminPasswordEdit->text().isEmpty()) {
            QByteArray hash = QCryptographicHash::hash(
                m_webAdminPasswordEdit->text().toUtf8(), QCryptographicHash::Sha256);
            req.append(QStringLiteral("webServerAdminPassword"));
            req.append(QString::fromLatin1(hash.toHex()));
        }
        req.append(QStringLiteral("webServerAdminAllowHiLevFunc"));
        req.append(m_webAdminHiLevCheck->isChecked());
        req.append(QStringLiteral("webServerGuestEnabled"));
        req.append(m_webGuestEnabledCheck->isChecked());
        if (!m_webGuestPasswordEdit->text().isEmpty()) {
            QByteArray hash = QCryptographicHash::hash(
                m_webGuestPasswordEdit->text().toUtf8(), QCryptographicHash::Sha256);
            req.append(QStringLiteral("webServerGuestPassword"));
            req.append(QString::fromLatin1(hash.toHex()));
        }

        // Mirror web server settings into local thePrefs so save() doesn't overwrite with defaults
        thePrefs.setWebServerEnabled(m_webEnabledCheck->isChecked());
        thePrefs.setWebServerRestApiEnabled(m_webRestApiCheck->isChecked());
        thePrefs.setWebServerGzipEnabled(m_webGzipCheck->isChecked());
        thePrefs.setWebServerUPnP(m_webUPnPCheck->isChecked());
        thePrefs.setWebServerPort(static_cast<uint16>(m_webPortSpin->value()));
        thePrefs.setWebServerTemplatePath(m_webTemplateEdit->text());
        thePrefs.setWebServerSessionTimeout(m_webSessionTimeoutSpin->value());
        thePrefs.setWebServerHttpsEnabled(m_webHttpsCheck->isChecked());
        thePrefs.setWebServerCertPath(m_webCertEdit->text());
        thePrefs.setWebServerKeyPath(m_webKeyEdit->text());
        thePrefs.setWebServerApiKey(m_webApiKeyEdit->text());
        thePrefs.setWebServerAdminAllowHiLevFunc(m_webAdminHiLevCheck->isChecked());
        thePrefs.setWebServerGuestEnabled(m_webGuestEnabledCheck->isChecked());

        // Statistics page
        req.append(QStringLiteral("graphsUpdateSec"));
        req.append(static_cast<qint64>(m_statsGraphUpdateSlider->value()));
        req.append(QStringLiteral("statsAverageMinutes"));
        req.append(static_cast<qint64>(m_statsAvgTimeSlider->value()));
        req.append(QStringLiteral("statsUpdateSec"));
        req.append(static_cast<qint64>(m_statsTreeUpdateSlider->value()));
        req.append(QStringLiteral("fillGraphs"));
        req.append(m_statsFillGraphsCheck->isChecked());
        req.append(QStringLiteral("statsConnectionsMax"));
        req.append(static_cast<qint64>(m_statsYScaleSpin->value()));
        {
            static constexpr int ratioValues[] = {1, 2, 3, 4, 5, 10, 20};
            int ri = m_statsRatioCombo->currentIndex();
            req.append(QStringLiteral("statsConnectionsRatio"));
            req.append(static_cast<qint64>(ri >= 0 && ri < 7 ? ratioValues[ri] : 3));
        }

        // Extended page
        req.append(QStringLiteral("maxConsPerFive"));
        req.append(static_cast<qint64>(m_maxConPerFiveSpin->value()));
        req.append(QStringLiteral("maxHalfConnections"));
        req.append(static_cast<qint64>(m_maxHalfOpenSpin->value()));
        req.append(QStringLiteral("serverKeepAliveTimeout"));
        req.append(static_cast<qint64>(m_serverKeepAliveSpin->value()) * 60000); // min to ms
        req.append(QStringLiteral("filterLANIPs"));
        req.append(m_filterLANIPsCheck->isChecked());
        req.append(QStringLiteral("checkDiskspace"));
        req.append(m_checkDiskspaceCheck->isChecked());
        req.append(QStringLiteral("minFreeDiskSpace"));
        req.append(static_cast<qint64>(m_minFreeDiskSpaceSpin->value()) * 1024 * 1024); // MB to bytes
        req.append(QStringLiteral("logToDisk"));
        req.append(m_logToDiskCheck->isChecked());
        req.append(QStringLiteral("verbose"));
        req.append(m_verboseCheck->isChecked());
        req.append(QStringLiteral("closeUPnPOnExit"));
        req.append(m_closeUPnPCheck->isChecked());
        req.append(QStringLiteral("skipWANIPSetup"));
        req.append(m_skipWANIPCheck->isChecked());
        req.append(QStringLiteral("skipWANPPPSetup"));
        req.append(m_skipWANPPPCheck->isChecked());
        req.append(QStringLiteral("fileBufferSize"));
        req.append(static_cast<qint64>(m_fileBufferSlider->value()) * 16384); // slider to bytes
        req.append(QStringLiteral("useCreditSystem"));
        req.append(m_useCreditSystemCheck->isChecked());
        req.append(QStringLiteral("a4afSaveCpu"));
        req.append(m_a4afSaveCpuCheck->isChecked());
        req.append(QStringLiteral("autoArchivePreviewStart"));
        req.append(!m_disableArchPreviewCheck->isChecked());
        req.append(QStringLiteral("ed2kHostname"));
        req.append(m_ed2kHostnameEdit->text());
        req.append(QStringLiteral("showExtControls"));
        req.append(m_showExtControlsCheck->isChecked());
        req.append(QStringLiteral("commitFiles"));
        req.append(static_cast<qint64>(m_commitFilesGroup->checkedId()));
        req.append(QStringLiteral("extractMetaData"));
        req.append(static_cast<qint64>(m_extractMetaDataGroup->checkedId()));
        req.append(QStringLiteral("logLevel"));
        req.append(static_cast<qint64>(m_logLevelSpin->value()));
        req.append(QStringLiteral("verboseLogToDisk"));
        req.append(m_verboseLogToDiskCheck->isChecked());
        req.append(QStringLiteral("logSourceExchange"));
        req.append(m_logSourceExchangeCheck->isChecked());
        req.append(QStringLiteral("logBannedClients"));
        req.append(m_logBannedClientsCheck->isChecked());
        req.append(QStringLiteral("logRatingDescReceived"));
        req.append(m_logRatingDescCheck->isChecked());
        req.append(QStringLiteral("logSecureIdent"));
        req.append(m_logSecureIdentCheck->isChecked());
        req.append(QStringLiteral("logFilteredIPs"));
        req.append(m_logFilteredIPsCheck->isChecked());
        req.append(QStringLiteral("logFileSaving"));
        req.append(m_logFileSavingCheck->isChecked());
        req.append(QStringLiteral("logA4AF"));
        req.append(m_logA4AFCheck->isChecked());
        req.append(QStringLiteral("logUlDlEvents"));
        req.append(m_logUlDlEventsCheck->isChecked());
        req.append(QStringLiteral("logRawSocketPackets"));
        req.append(m_logRawSocketPacketsCheck->isChecked());
        req.append(QStringLiteral("enableIpcLog"));
        req.append(m_enableIpcLogCheck->isChecked());
        req.append(QStringLiteral("startCoreWithConsole"));
        req.append(m_startCoreWithConsoleCheck->isChecked());
        // USS
        req.append(QStringLiteral("dynUpEnabled"));
        req.append(m_dynUpEnabledCheck->isChecked());
        req.append(QStringLiteral("dynUpPingTolerance"));
        req.append(static_cast<qint64>(m_dynUpPingToleranceSpin->value()));
        req.append(QStringLiteral("dynUpPingToleranceMs"));
        req.append(static_cast<qint64>(m_dynUpPingToleranceMsSpin->value()));
        req.append(QStringLiteral("dynUpUseMillisecondPingTolerance"));
        req.append(m_dynUpRadioMs->isChecked());
        req.append(QStringLiteral("dynUpGoingUpDivider"));
        req.append(static_cast<qint64>(m_dynUpGoingUpSpin->value()));
        req.append(QStringLiteral("dynUpGoingDownDivider"));
        req.append(static_cast<qint64>(m_dynUpGoingDownSpin->value()));
        req.append(QStringLiteral("dynUpNumberOfPings"));
        req.append(static_cast<qint64>(m_dynUpNumPingsSpin->value()));
        req.append(QStringLiteral("queueSize"));
        req.append(static_cast<qint64>(m_queueSizeSlider->value()) * 100); // slider to count

#ifdef Q_OS_WIN
        req.append(QStringLiteral("autotakeEd2kLinks"));
        req.append(m_autotakeEd2kCheck->isChecked());
        req.append(QStringLiteral("openPortsOnWinFirewall"));
        req.append(m_winFirewallCheck->isChecked());
        req.append(QStringLiteral("sparsePartFiles"));
        req.append(m_sparsePartFilesCheck->isChecked());
        req.append(QStringLiteral("allocFullFile"));
        req.append(m_allocFullFileCheck->isChecked());
        req.append(QStringLiteral("resolveShellLinks"));
        req.append(m_resolveShellLinksCheck->isChecked());
        req.append(QStringLiteral("multiUserSharing"));
        req.append(static_cast<qint64>(m_multiUserSharingGroup->checkedId()));
#endif

        m_ipc->sendRequest(std::move(req));
    } else {
        // Fallback: save locally
        thePrefs.setNick(m_nickEdit->text());
        thePrefs.setMaxGraphDownloadRate(static_cast<uint32>(m_capacityDownloadSpin->value()));
        thePrefs.setMaxGraphUploadRate(static_cast<uint32>(m_capacityUploadSpin->value()));
        thePrefs.setMaxDownload(m_downloadLimitCheck->isChecked() ? static_cast<uint32>(m_downloadLimitSlider->value()) : 0);
        thePrefs.setMaxUpload(m_uploadLimitCheck->isChecked() ? static_cast<uint32>(m_uploadLimitSlider->value()) : 0);
        thePrefs.setPort(static_cast<uint16>(m_tcpPortSpin->value()));
        thePrefs.setUdpPort(m_udpDisableCheck->isChecked() ? uint16(0) : static_cast<uint16>(m_udpPortSpin->value()));
        thePrefs.setEnableUPnP(m_upnpCheck->isChecked());
        thePrefs.setMaxSourcesPerFile(static_cast<uint16>(m_maxSourcesSpin->value()));
        thePrefs.setMaxConnections(static_cast<uint16>(m_maxConnectionsSpin->value()));
        thePrefs.setAutoConnect(m_autoConnectCheck->isChecked());
        thePrefs.setReconnect(m_reconnectCheck->isChecked());
        thePrefs.setShowOverhead(m_overheadCheck->isChecked());
        thePrefs.setKadEnabled(m_kadEnabledCheck->isChecked());
        thePrefs.setNetworkED2K(m_ed2kEnabledCheck->isChecked());

        // Server page fallback
        thePrefs.setSafeServerConnect(m_safeServerConnectCheck->isChecked());
        thePrefs.setAutoConnectStaticOnly(m_autoConnectStaticOnlyCheck->isChecked());
        thePrefs.setUseServerPriorities(m_useServerPrioritiesCheck->isChecked());
        thePrefs.setAddServersFromServer(m_addServersFromServerCheck->isChecked());
        thePrefs.setAddServersFromClients(m_addServersFromClientsCheck->isChecked());
        thePrefs.setDeadServerRetries(static_cast<uint32>(m_deadServerRetriesSpin->value()));
        thePrefs.setAutoUpdateServerList(m_autoUpdateServerListCheck->isChecked());
        thePrefs.setServerListURL(m_serverListURLValue);
        thePrefs.setSmartLowIdCheck(m_smartLowIdCheck->isChecked());
        thePrefs.setManualServerHighPriority(m_manualHighPrioCheck->isChecked());

        // Proxy page fallback
        thePrefs.setProxyType(m_proxyEnableCheck->isChecked() ? m_proxyTypeCombo->currentIndex() : 0);
        thePrefs.setProxyHost(m_proxyHostEdit->text());
        thePrefs.setProxyPort(static_cast<uint16>(m_proxyPortSpin->value()));
        thePrefs.setProxyEnablePassword(m_proxyAuthCheck->isChecked());
        thePrefs.setProxyUser(m_proxyUserEdit->text());
        thePrefs.setProxyPassword(m_proxyPasswordEdit->text());

        // Directories page fallback
        thePrefs.setIncomingDir(m_incomingDirEdit->text());
        thePrefs.setTempDirs({m_tempDirEdit->text()});
        thePrefs.setSharedDirs(static_cast<CheckableFileSystemModel*>(m_sharedDirsModel)->checkedPaths());

        // Files page (daemon-side) fallback
        thePrefs.setAddNewFilesPaused(m_addFilesPausedCheck->isChecked());
        thePrefs.setAutoDownloadPriority(m_autoDownloadPrioCheck->isChecked());
        thePrefs.setAutoSharedFilesPriority(m_autoSharedFilesPrioCheck->isChecked());
        thePrefs.setTransferFullChunks(m_transferFullChunksCheck->isChecked());
        thePrefs.setPreviewPrio(m_previewPrioCheck->isChecked());
        thePrefs.setStartNextPausedFile(m_startNextPausedCheck->isChecked());
        thePrefs.setStartNextPausedFileSameCat(m_preferSameCatCheck->isChecked());
        thePrefs.setStartNextPausedFileOnlySameCat(m_onlySameCatCheck->isChecked());
        thePrefs.setRememberDownloadedFiles(m_rememberDownloadedCheck->isChecked());
        thePrefs.setRememberCancelledFiles(m_rememberCancelledCheck->isChecked());

        // Notifications page (daemon-side) fallback
        thePrefs.setNotifyOnLog(m_notifyLogCheck->isChecked());
        thePrefs.setNotifyOnChat(m_notifyChatCheck->isChecked());
        thePrefs.setNotifyOnChatMsg(m_notifyChatMsgCheck->isChecked());
        thePrefs.setNotifyOnDownloadAdded(m_notifyDownloadAddedCheck->isChecked());
        thePrefs.setNotifyOnDownloadFinished(m_notifyDownloadFinishedCheck->isChecked());
        thePrefs.setNotifyOnNewVersion(m_notifyNewVersionCheck->isChecked());
        thePrefs.setNotifyOnUrgent(m_notifyUrgentCheck->isChecked());
        thePrefs.setNotifyEmailEnabled(m_emailEnabledCheck->isChecked());
        thePrefs.setNotifyEmailSmtpServer(m_smtpServer);
        thePrefs.setNotifyEmailSmtpPort(static_cast<uint16>(m_smtpPort));
        thePrefs.setNotifyEmailSmtpAuth(m_smtpAuth);
        thePrefs.setNotifyEmailSmtpTls(m_smtpTls);
        thePrefs.setNotifyEmailSmtpUser(m_smtpUser);
        thePrefs.setNotifyEmailSmtpPassword(m_smtpPassword);
        thePrefs.setNotifyEmailRecipient(m_emailRecipientEdit->text());
        thePrefs.setNotifyEmailSender(m_emailSenderEdit->text());

        // Messages and Comments page (daemon-side) fallback
        thePrefs.setMsgOnlyFriends(m_msgFriendsOnlyCheck->isChecked());
        thePrefs.setEnableSpamFilter(m_advancedSpamFilterCheck->isChecked());
        thePrefs.setUseChatCaptchas(m_requireCaptchaCheck->isChecked());
        thePrefs.setMessageFilter(m_messageFilterEdit->text());
        thePrefs.setCommentFilter(m_commentFilterEdit->text());

        // Security page (daemon-side) fallback
        thePrefs.setFilterServerByIP(m_filterServersByIPCheck->isChecked());
        thePrefs.setIpFilterLevel(static_cast<uint32>(m_ipFilterLevelSpin->value()));
        thePrefs.setViewSharedFilesAccess(m_viewSharedGroup->checkedId());
        thePrefs.setCryptLayerSupported(!m_cryptLayerDisableCheck->isChecked());
        thePrefs.setCryptLayerRequested(m_cryptLayerRequestedCheck->isChecked());
        thePrefs.setCryptLayerRequired(m_cryptLayerRequiredCheck->isChecked());
        thePrefs.setUseSecureIdent(m_useSecureIdentCheck->isChecked());
        thePrefs.setEnableSearchResultFilter(m_enableSearchResultFilterCheck->isChecked());
        thePrefs.setWarnUntrustedFiles(m_warnUntrustedFilesCheck->isChecked());
        thePrefs.setIpFilterUpdateUrl(m_ipFilterUpdateUrlEdit->text().trimmed());

        // Statistics page fallback
        thePrefs.setGraphsUpdateSec(static_cast<uint32>(m_statsGraphUpdateSlider->value()));
        thePrefs.setStatsAverageMinutes(static_cast<uint32>(m_statsAvgTimeSlider->value()));
        thePrefs.setStatsUpdateSec(static_cast<uint32>(m_statsTreeUpdateSlider->value()));
        thePrefs.setFillGraphs(m_statsFillGraphsCheck->isChecked());
        thePrefs.setStatsConnectionsMax(static_cast<uint32>(m_statsYScaleSpin->value()));
        {
            static constexpr int ratioValues[] = {1, 2, 3, 4, 5, 10, 20};
            int ri = m_statsRatioCombo->currentIndex();
            thePrefs.setStatsConnectionsRatio(static_cast<uint32>(ri >= 0 && ri < 7 ? ratioValues[ri] : 3));
        }

        // Extended page fallback
        thePrefs.setMaxConsPerFive(static_cast<uint16>(m_maxConPerFiveSpin->value()));
        thePrefs.setMaxHalfConnections(static_cast<uint16>(m_maxHalfOpenSpin->value()));
        thePrefs.setServerKeepAliveTimeout(static_cast<uint32>(m_serverKeepAliveSpin->value()) * 60000);
        thePrefs.setFilterLANIPs(m_filterLANIPsCheck->isChecked());
        thePrefs.setCheckDiskspace(m_checkDiskspaceCheck->isChecked());
        thePrefs.setMinFreeDiskSpace(static_cast<uint64>(m_minFreeDiskSpaceSpin->value()) * 1024 * 1024);
        thePrefs.setLogToDisk(m_logToDiskCheck->isChecked());
        thePrefs.setVerbose(m_verboseCheck->isChecked());
        thePrefs.setCloseUPnPOnExit(m_closeUPnPCheck->isChecked());
        thePrefs.setSkipWANIPSetup(m_skipWANIPCheck->isChecked());
        thePrefs.setSkipWANPPPSetup(m_skipWANPPPCheck->isChecked());
        thePrefs.setFileBufferSize(static_cast<uint32>(m_fileBufferSlider->value()) * 16384);
        thePrefs.setUseCreditSystem(m_useCreditSystemCheck->isChecked());
        thePrefs.setA4afSaveCpu(m_a4afSaveCpuCheck->isChecked());
        thePrefs.setAutoArchivePreviewStart(!m_disableArchPreviewCheck->isChecked());
        thePrefs.setEd2kHostname(m_ed2kHostnameEdit->text());
        thePrefs.setCommitFiles(m_commitFilesGroup->checkedId());
        thePrefs.setExtractMetaData(m_extractMetaDataGroup->checkedId());
        thePrefs.setLogLevel(m_logLevelSpin->value());
        thePrefs.setVerboseLogToDisk(m_verboseLogToDiskCheck->isChecked());
        thePrefs.setLogSourceExchange(m_logSourceExchangeCheck->isChecked());
        thePrefs.setLogBannedClients(m_logBannedClientsCheck->isChecked());
        thePrefs.setLogRatingDescReceived(m_logRatingDescCheck->isChecked());
        thePrefs.setLogSecureIdent(m_logSecureIdentCheck->isChecked());
        thePrefs.setLogFilteredIPs(m_logFilteredIPsCheck->isChecked());
        thePrefs.setLogFileSaving(m_logFileSavingCheck->isChecked());
        thePrefs.setLogA4AF(m_logA4AFCheck->isChecked());
        thePrefs.setLogUlDlEvents(m_logUlDlEventsCheck->isChecked());
        thePrefs.setLogRawSocketPackets(m_logRawSocketPacketsCheck->isChecked());
        // USS
        thePrefs.setDynUpEnabled(m_dynUpEnabledCheck->isChecked());
        thePrefs.setDynUpPingTolerance(m_dynUpPingToleranceSpin->value());
        thePrefs.setDynUpPingToleranceMs(m_dynUpPingToleranceMsSpin->value());
        thePrefs.setDynUpUseMillisecondPingTolerance(m_dynUpRadioMs->isChecked());
        thePrefs.setDynUpGoingUpDivider(m_dynUpGoingUpSpin->value());
        thePrefs.setDynUpGoingDownDivider(m_dynUpGoingDownSpin->value());
        thePrefs.setDynUpNumberOfPings(m_dynUpNumPingsSpin->value());
        thePrefs.setQueueSize(static_cast<uint32>(m_queueSizeSlider->value()) * 100);

#ifdef Q_OS_WIN
        thePrefs.setAutotakeEd2kLinks(m_autotakeEd2kCheck->isChecked());
        thePrefs.setOpenPortsOnWinFirewall(m_winFirewallCheck->isChecked());
        thePrefs.setSparsePartFiles(m_sparsePartFilesCheck->isChecked());
        thePrefs.setAllocFullFile(m_allocFullFileCheck->isChecked());
        thePrefs.setResolveShellLinks(m_resolveShellLinksCheck->isChecked());
        thePrefs.setMultiUserSharing(m_multiUserSharingGroup->checkedId());
#endif

        thePrefs.save();
    }

    saveSchedulerData();

    // Apply settings to live statistics panel
    if (m_statsPanel)
        m_statsPanel->applySettings();
}

// ---------------------------------------------------------------------------
// Private: padlock icon for Security (matches MFC yellow lock)
// ---------------------------------------------------------------------------

QIcon OptionsDialog::makePadlockIcon()
{
    QPixmap pix(24, 24);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Shackle (arc)
    QPen shacklePen(QColor(0xB8, 0x86, 0x0B), 2.5);  // dark goldenrod
    p.setPen(shacklePen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(6, 2, 12, 12), 0, 180 * 16);

    // Body (rounded rect)
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xDA, 0xA5, 0x20));  // goldenrod
    p.drawRoundedRect(QRectF(4, 11, 16, 11), 2, 2);

    // Keyhole
    p.setBrush(QColor(0x8B, 0x6D, 0x14));  // darker gold
    p.drawEllipse(QPointF(12, 15), 2, 2);
    p.drawRect(QRectF(11, 16, 2, 3));

    p.end();
    return QIcon(pix);
}

} // namespace eMule
