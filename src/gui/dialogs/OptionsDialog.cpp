#include "pch.h"
#include "utils/FileAssociation.h"
#include "dialogs/OptionsDialog.h"
#include "dialogs/FirstStartWizard.h"

#include "app/AppConfig.h"
#include "app/AutoStart.h"
#include "app/UiState.h"
#include "app/Ed2kSchemeHandler.h"
#include "app/IpcClient.h"
#include "controls/AbstractListView.h"
#include "controls/AccordionSidebar.h"
#include "controls/ContentScrollArea.h"
#include "panels/StatisticsPanel.h"
#include "net/HttpFileDownload.h"
#include "prefs/Preferences.h"
#include "utils/DialogSizing.h"
#include "utils/StatusBarNotifier.h"
#include "utils/WebServices.h"
#include "utils/StringUtils.h"

#include "IpcMessage.h"

#include <iterator>   // std::size, for the sidebar table asserts

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
#include <QRadioButton>
#include <QScrollArea>
#include <QSoundEffect>
#include <QMessageBox>
#include <QDate>
#include <QLocale>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrlQuery>
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

    // A feed can finish a poll while this dialog is open — from its own
    // schedule, not only from Check now — so the table updates in place rather
    // than going stale until the next time the page is opened.
    if (m_ipc) {
        connect(m_ipc, &IpcClient::indexerFeedStatus, this,
                [this](const Ipc::IpcMessage& msg) {
            applyFeedStatus(msg.fieldMap(0));
        });
    }

    auto* mainLayout = new QHBoxLayout;

    // Left sidebar. No setFixedWidth: 200 px was already tight for "Messages and
    // Comments" with a 16 px icon, and the accordion adds bold group captions on top --
    // AccordionSidebar::sizeHint() measures them instead.
    m_sidebar = new AccordionSidebar(this);
    setupSidebar();
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
    connect(m_sidebar, &AccordionSidebar::currentItemChanged,
            this, &OptionsDialog::onPageChanged);
    connect(okBtn, &QPushButton::clicked, this, &OptionsDialog::onOk);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_applyBtn, &QPushButton::clicked, this, &OptionsDialog::onApply);

    // Restore last selected page — General when unset or out of range (a stale
    // index from an older build must not silently land on some other page).
    const int storedPage = theUiState.optionsLastPage();
    const int lastPage =
        (storedPage >= 0 && storedPage < PageCount) ? storedPage : int{PageGeneral};
    m_sidebar->setCurrentItemId(lastPage);
    onPageChanged(lastPage);

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
    connect(m_langCombo, &QComboBox::currentIndexChanged, this, &OptionsDialog::markDirty);
    connect(m_enableOnlineSigCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_preventStandbyCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_showSplashCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_startWithOSCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);

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
    connect(m_showSpeedGraphCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_speedGraphTimeSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
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
    connect(m_separateIPv6QueueCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);

    // Server page
    connect(m_addServersFromServerCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_addServersFromClientsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_safeServerConnectCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_autoConnectStaticOnlyCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_useServerPrioritiesCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_useUserSortedServerListCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
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
    connect(m_proxyUsenetCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);

    // Directories page
    connect(m_incomingDirEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_tempDirEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_sharedDirsModel, &QAbstractItemModel::dataChanged, this, &OptionsDialog::markDirty);

    // Files page
    connect(m_addFilesPausedCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_saveLoadSourcesCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
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
    connect(m_rememberUploadQueueCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_filterLANIPsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_showExtControlsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_a4afSaveCpuCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_disableArchPreviewCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_ed2kHostnameEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_ed2kLinkAdvertiseIPv6Check, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_checkDiskspaceCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_minFreeDiskSpaceSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_commitFilesGroup, &QButtonGroup::idToggled, this, &OptionsDialog::markDirty);
    connect(m_extractMetaDataGroup, &QButtonGroup::idToggled, this, &OptionsDialog::markDirty);
    connect(m_logToDiskCoreCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logToDiskGuiCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_verboseCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logLevelSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_logSourceExchangeCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_serverVerboseCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logBannedClientsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logRatingDescCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logSecureIdentCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logFilteredIPsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logFileSavingCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logA4AFCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logUlDlEventsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logRawSocketPacketsCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logWebServerCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_logPublicIPCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_enableIpcLogCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_startCoreWithConsoleCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_closeUPnPCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_portMapPcpCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_portMapNatPmpCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_portMapUPnPCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_portMapIPv6Check, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_portMapLeaseSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
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
    connect(m_enableMiniMuleCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
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
            // Guarded: the dialog can close before the reply — or the failure a dropped
            // connection sends — arrives.
            m_ipc->sendRequest(std::move(req), [this, self = QPointer<OptionsDialog>(this)](const Ipc::IpcMessage& resp) {
                if (!self)
                    return;
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
        m_proxyUsenetCheck->setEnabled(on);
    });
    connect(m_proxyAuthCheck, &QCheckBox::toggled, this, [this](bool on) {
        bool proxyOn = m_proxyEnableCheck->isChecked();
        m_proxyUserEdit->setEnabled(proxyOn && on);
        m_proxyPasswordEdit->setEnabled(proxyOn && on);
    });

    // A designed minimum, not {}: every page is behind a scroll area now, so the
    // layout minimum has collapsed to the sidebar plus a scrollbar and would let the
    // window be dragged down to a few hundred pixels. Fit::Layout, not Fit::Content --
    // the latter asks ContentScrollArea for the content's *full* height, which is the
    // 1300 px Usenet column this all exists to stop.
    // 700, not the old 640: measured against every page's preferred height, that is
    // where the scrollbar stops appearing on the ordinary ones (General 665, Web
    // Interface 674) while still fitting a 768 px screen. The three list pages -- Usenet,
    // Feeds, Indexers -- scroll, which is what the scroll areas are for.
    DialogSizing::applySize(this, QSize(720, 520), QSize(800, 700),
                            DialogSizing::Fit::Layout);
}

OptionsDialog::~OptionsDialog() = default;

void OptionsDialog::selectPage(int page)
{
    if (page >= 0 && page < PageCount)
        m_sidebar->setCurrentItemId(page);
}

void OptionsDialog::done(int result)
{
    // Remember the page across every close path — OK, Cancel, Esc and the
    // window close button all route through QDialog::done().
    const int page = m_sidebar->currentItemId();
    if (page >= 0 && page < PageCount)
        theUiState.setOptionsLastPage(page);

    QDialog::done(result);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void OptionsDialog::onPageChanged(int page)
{
    if (page < 0 || page >= PageCount)
        return;

    m_pages->setCurrentIndex(page);

    const QString name = m_sidebar->currentItemText();
    m_pageHeader->setText(QStringLiteral("  %1").arg(name));
    // MorphXT puts the whole trail in the title bar (PreferencesDlg.cpp:582); the blue
    // header bar keeps saying only where you are.
    setWindowTitle(tr("Options -> %1 -> %2").arg(m_sidebar->currentGroupTitle(), name));
}

void OptionsDialog::onOk()
{
    saveSettings();
    accept();
}

void OptionsDialog::onApply()
{
    saveSettings();
    m_applyBtn->setEnabled(false);
}

void OptionsDialog::markDirty()
{
    if (m_loading)
        return;
    m_applyBtn->setEnabled(true);
}

namespace {

/// Value label beside a Connection-page limit slider.
QString limitText(int kbps)
{
    return QStringLiteral("%1 KB/s").arg(kbps);
}

/// Room for a spin box's special value text.
///
/// QFormLayout::ExpandingFieldsGrow only grows fields whose size policy expands, and a
/// spin box's does not -- it gets its size hint, which is measured from the widest
/// *number* it can hold. A word standing in for a number ("Never back off" for 0) is
/// simply wider than that, so the line edit scrolls sideways instead and the first
/// letters are the ones that go.
void fitSpecialValue(QAbstractSpinBox* spin)
{
    if (!spin || spin->specialValueText().isEmpty())
        return;
    const int text = spin->fontMetrics().horizontalAdvance(spin->specialValueText());
    spin->setMinimumWidth(std::max(spin->sizeHint().width(), text + 44));
}

/// Room for @p rows of items plus the header.
///
/// Inside a scroll area every page is laid out at its layout *minimum* the moment the
/// window is too short for it, and a QTreeWidget's own minimum is a header and about one
/// row. Without a floor said out loud here, the first page that has to scroll paints its
/// list as a sliver -- worst on Directories, whose tree carries the layout's only stretch
/// factor and has no trailing addStretch() to give way instead.
enum class ListGrowth {
    Free,    ///< the list takes any slack the page has -- it *is* the page's content
    Capped,  ///< stays at @p rows: a chooser above a form, where growing only pushes
             ///< the form it belongs to off the bottom
};

void giveListRoom(QAbstractItemView* view, int rows = 5,
                  ListGrowth growth = ListGrowth::Free)
{
    if (!view)
        return;
    const int rowHeight = view->fontMetrics().height() + 6;
    int       header    = 0;
    if (const auto* tree = qobject_cast<const QTreeWidget*>(view))
        header = tree->header()->sizeHint().height();

    const int wanted = header + rows * rowHeight + 2 * view->frameWidth();
    view->setMinimumHeight(wanted);
    if (growth == ListGrowth::Capped) {
        view->setMaximumHeight(wanted);
        view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Setup: sidebar
// ---------------------------------------------------------------------------

void OptionsDialog::setupSidebar()
{
    struct PageDef {
        Page        id;
        const char* label;
        QStyle::StandardPixmap icon;
        const char* originalIcon;  // resource path when using original eMule icons
    };

    // Grouping follows MorphXT's CPreferencesDlg::Localize() (PreferencesDlg.cpp:351),
    // with Usenet taking the slot "Mod options" held there. The order here is the
    // *sidebar's*; the Page values keep the stacked-page order, which is what lets the
    // two diverge without touching anything that persists a page index.
    static constexpr PageDef kGeneralPages[] = {
        {PageGeneral,       "General",               QStyle::SP_FileDialogDetailedView, "Preferences.ico"},
        {PageDisplay,       "Display",               QStyle::SP_DesktopIcon,            "Display.ico"},
        {PageConnection,    "Connection",            QStyle::SP_DriveNetIcon,           "Connection.ico"},
        {PageServer,        "Server",                QStyle::SP_ComputerIcon,           "Server.ico"},
        {PageDirectories,   "Directories",           QStyle::SP_DirIcon,                "Folders.ico"},
        {PageFiles,         "Files",                 QStyle::SP_FileIcon,               "FileTypeAny.ico"},
        {PageNotifications, "Notifications",         QStyle::SP_MessageBoxInformation,  "Notifications.ico"},
        {PageMessages,      "Messages and Comments", QStyle::SP_MessageBoxQuestion,     "Chat.ico"},
        {PageSecurity,      "Security",              QStyle::SP_CustomBase,             "Security.ico"},
    };
    static constexpr PageDef kAdvancedPages[] = {
        {PageProxy,         "Proxy",                 QStyle::SP_BrowserReload,          "Proxy.ico"},
        {PageIRC,           "IRC",                   QStyle::SP_DialogApplyButton,      "IRC.ico"},
        {PageStatistics,    "Statistics",            QStyle::SP_DialogHelpButton,       "Statistics.ico"},
        {PageScheduler,     "Scheduler",             QStyle::SP_DialogResetButton,      "Scheduler.ico"},
        {PageWebInterface,  "Web Interface",         QStyle::SP_DriveNetIcon,           "Web.ico"},
        {PageExtended,      "Extended",              QStyle::SP_DialogCancelButton,     "Tweak.ico"},
    };
    static constexpr PageDef kUsenetPages[] = {
        {PageUsenet,        "Usenet",                QStyle::SP_DriveNetIcon,           "Usenet.ico"},
        {PageIndexers,      "Indexers",              QStyle::SP_FileDialogContentsView, "Search.ico"},
        {PageFeeds,         "Feeds",                 QStyle::SP_BrowserReload,          "SearchEdit.ico"},
    };

    // A page added to the enum and to setupPages() but forgotten here would simply be
    // unreachable, with nothing to say so. The count catches a missing entry, the bit set
    // catches a duplicated one -- neither alone catches both.
    constexpr auto idMask = [](const auto& table) constexpr {
        unsigned mask = 0;
        for (const PageDef& def : table)
            mask |= 1u << unsigned(def.id);
        return mask;
    };
    static_assert(std::size(kGeneralPages) + std::size(kAdvancedPages)
                          + std::size(kUsenetPages) == std::size_t(PageCount),
                  "every Page must appear in a sidebar group");
    static_assert((idMask(kGeneralPages) | idMask(kAdvancedPages) | idMask(kUsenetPages))
                      == (1u << unsigned(PageCount)) - 1u,
                  "every Page must appear in exactly one sidebar group");

    const bool useOriginal = thePrefs.useOriginalIcons();

    const auto fill = [this, useOriginal](const QString& title, const auto& table) {
        const int group = m_sidebar->addGroup(title);
        for (const PageDef& def : table) {
            QIcon qicon;
            if (useOriginal)
                qicon = QIcon(QStringLiteral(":/icons/") + QLatin1String(def.originalIcon));
            else if (def.icon == QStyle::SP_CustomBase)
                qicon = makePadlockIcon();
            else
                qicon = style()->standardIcon(def.icon);
            m_sidebar->addItem(group, qicon, tr(def.label), int(def.id));
        }
    };

    fill(tr("General options"),  kGeneralPages);
    fill(tr("Advanced options"), kAdvancedPages);
    fill(tr("Usenet"),           kUsenetPages);
}

// ---------------------------------------------------------------------------
// Setup: pages
// ---------------------------------------------------------------------------

void OptionsDialog::addPage(Page id, QWidget* page, PageScroll scroll)
{
    // The stack index *is* the Page value: UiState::optionsLastPage() and the numeric
    // --options N fallback both hand us one straight from a previous run.
    Q_ASSERT(m_pages->count() == int(id));

    if (scroll == PageScroll::Self) {
        m_pages->addWidget(page);
        return;
    }

    DialogSizing::enableHeightForWidth(page);
    auto* area = new ContentScrollArea(m_pages);
    // A scroll area has no meaningful minimum width, so wrapping would drop the width
    // floor along with the height one -- and with the horizontal bar switched off, that
    // clips instead of scrolling. Keep the width this page has always asked for.
    if (page->layout())
        area->setMinimumWidth(page->layout()->totalMinimumSize().width());
    area->setWidget(page);
    m_pages->addWidget(area);
}

void OptionsDialog::setupPages()
{
    addPage(PageGeneral,       createGeneralPage());
    addPage(PageDisplay,       createDisplayPage());
    addPage(PageConnection,    createConnectionPage());
    addPage(PageProxy,         createProxyPage());
    addPage(PageServer,        createServerPage());
    addPage(PageDirectories,   createDirectoriesPage());
    addPage(PageFiles,         createFilesPage());
    addPage(PageNotifications, createNotificationsPage());
    addPage(PageStatistics,    createStatisticsPage());
    addPage(PageIRC,           createIRCPage());
    addPage(PageMessages,      createMessagesPage());
    addPage(PageSecurity,      createSecurityPage());
    addPage(PageScheduler,     createSchedulerPage());
    addPage(PageWebInterface,  createWebInterfacePage());

    // Usenet — news server accounts (NNTP). Its two tabs scroll individually, so the
    // tab bar stays put rather than scrolling away with the form under it.
    addPage(PageUsenet,        createUsenetPage(), PageScroll::Self);

    addPage(PageIndexers,      createIndexersPage());
    addPage(PageFeeds,         createFeedsPage());

    // Extended keeps its own scroll area: the red warning and the two sliders are
    // deliberately outside it, and a wrapper here would scroll them away.
    addPage(PageExtended,      createExtendedPage(), PageScroll::Self);
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

    // Discover available translations from .qm files. Every candidate directory,
    // not just the first with any: main() picks one directory to load from, but the
    // combo offers the union of what all of them hold.
    const QStringList langSearchPaths =
        eMule::AppConfig::langCandidates(QCoreApplication::applicationDirPath());
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
    connect(webServicesBtn, &QPushButton::clicked, this, [this] {
        // webservices.dat, not eMule.tmpl -- that one is the web *server* template.
        // MFC opens <configdir>webservices.dat (srchybrid/OtherFunctions.cpp:1071).
        const QString path = WebServices::userFilePath();
        if (!QFile::exists(path)) {
            // Seeding should have placed it; recover from the shipped copy. Never
            // open the shipped one directly -- edits there are lost on the next sync.
            const QString shipped = WebServices::instance().servicesFilePath();
            if (shipped == path || !QFile::copy(shipped, path)) {
                QMessageBox::warning(this, tr("Web Services"),
                                     tr("webservices.dat was not found in the config folder."));
                return;
            }
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
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

    m_showSpeedGraphCheck = new QCheckBox(tr("Show speed graph in toolbar"), page);
    layout->addWidget(m_showSpeedGraphCheck);

    auto* graphTimeRow = new QHBoxLayout;
    graphTimeRow->addSpacing(20);
    graphTimeRow->addWidget(new QLabel(tr("Speed graph time range (minutes):"), page));
    m_speedGraphTimeSpin = new QSpinBox(page);
    m_speedGraphTimeSpin->setRange(1, 60);
    graphTimeRow->addWidget(m_speedGraphTimeSpin);
    graphTimeRow->addStretch();
    layout->addLayout(graphTimeRow);

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
    m_capacityDownloadSpin->setRange(1, 1000000);
    m_capacityDownloadSpin->setSuffix(tr(" KB/s"));
    capLayout->addWidget(m_capacityDownloadSpin, 0, 1);

    capLayout->addWidget(new QLabel(tr("Upload"), capGroup), 1, 0);
    m_capacityUploadSpin = new QSpinBox(capGroup);
    m_capacityUploadSpin->setRange(1, 1000000);
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
    connect(testPortsBtn, &QPushButton::clicked, this, [this] { openPortTest(); });
    portLayout->addWidget(testPortsBtn, 1, 3);

    m_upnpCheck = new QCheckBox(tr("Use UPnP to Setup Ports"), portGroup);
    portLayout->addWidget(m_upnpCheck, 2, 0, 1, 2);

    // Real forwarding status, rather than the first-start wizard's 30-second
    // guess. Populated from GetNetworkInfo's portmap section.
    m_portMapStatusLabel = new QLabel(tr("Port forwarding: unknown"), portGroup);
    m_portMapStatusLabel->setEnabled(false);
    portLayout->addWidget(m_portMapStatusLabel, 2, 2, 1, 2);

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
    m_separateIPv6QueueCheck = new QCheckBox(tr("Separate IPv6 queue"), netGroup);
    m_separateIPv6QueueCheck->setToolTip(
        tr("Alternate freed upload slots between IPv4 and IPv6 clients when both are "
           "waiting, so IPv6 peers are not outbid on score alone. When only one family "
           "is waiting, no slot is held back."));
    netLayout->addWidget(m_separateIPv6QueueCheck);
    row4->addWidget(netGroup);

    layout->addLayout(row4);

    layout->addStretch();

    // --- Wire slider ↔ label sync ---
    connect(m_downloadLimitSlider, &QSlider::valueChanged, this, [this](int val) {
        m_downloadLimitLabel->setText(limitText(val));
    });
    connect(m_uploadLimitSlider, &QSlider::valueChanged, this, [this](int val) {
        m_uploadLimitLabel->setText(limitText(val));
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

    m_proxyUsenetCheck = new QCheckBox(tr("Use for news servers"), generalGroup);
    m_proxyUsenetCheck->setToolTip(
        tr("Route Usenet downloads, availability checks and the news server Test "
           "button through this proxy too. News servers switch over as soon as you "
           "press OK.\n\n"
           "Every Usenet connection then passes through the proxy, so its speed caps "
           "the download, and many HTTP proxies only allow connections to port 443."));
    generalLayout->addWidget(m_proxyUsenetCheck);
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
    m_proxyUsenetCheck->setEnabled(false);

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

    // "Use the manual (user-sorted) server order" — enables Move Up/Down in the
    // server list and tries servers in that order at auto-connect (#24).
    m_useUserSortedServerListCheck = new QCheckBox(tr("Use the manual server order (drag/Move Up-Down)"), miscGroup);
    miscLayout->addWidget(m_useUserSortedServerListCheck);

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

    giveListRoom(m_sharedDirsTree, 8);
    sharedLayout->addWidget(m_sharedDirsTree);
    layout->addWidget(sharedGroup, 1);  // stretch factor for the tree

    // --- Add UNC share button (Windows only) ---
    auto* uncBtn = new QPushButton(tr("Add UNC share"), page);
#ifdef Q_OS_WIN
    connect(uncBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        QString uncPath = QInputDialog::getText(this, tr("Add UNC Share"),
            tr("Enter UNC path (e.g., \\\\server\\share):"),
            QLineEdit::Normal, QStringLiteral("\\\\"), &ok);
        if (!ok || uncPath.isEmpty())
            return;
        uncPath = uncPath.trimmed();
        if (!uncPath.startsWith(QStringLiteral("\\\\"))) {
            QMessageBox::warning(this, tr("Invalid Path"),
                tr("A UNC path must start with \\\\."));
            return;
        }
        // Ensure trailing separator
        if (!uncPath.endsWith(QLatin1Char('/')) && !uncPath.endsWith(QLatin1Char('\\')))
            uncPath += QLatin1Char('\\');
        auto* fsModel = static_cast<CheckableFileSystemModel*>(m_sharedDirsModel);
        if (fsModel->checkedPaths().contains(uncPath))
            return;
        fsModel->setData(fsModel->index(0, 0), Qt::Checked, Qt::CheckStateRole); // trigger change
        // Directly add to checked set via setCheckedPaths
        auto paths = fsModel->checkedPaths();
        paths.append(uncPath);
        fsModel->setCheckedPaths(paths);
    });
#else
    uncBtn->setEnabled(false);
    uncBtn->setToolTip(tr("UNC shares are only supported on Windows"));
#endif
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

    m_saveLoadSourcesCheck = new QCheckBox(tr("Remember download sources between restarts"),
                                           initGroup);
    m_saveLoadSourcesCheck->setToolTip(
        tr("Stores each download's best sources in the temp folder and reconnects to them on "
           "the next start, so a rare file does not have to find its peers again."));
    initLayout->addWidget(m_saveLoadSourcesCheck);

    auto* cleanupRow = new QHBoxLayout;
    m_autoCleanupFilenamesCheck = new QCheckBox(tr("Auto cleanup file names of new downloads"), initGroup);
    cleanupRow->addWidget(m_autoCleanupFilenamesCheck);
    cleanupRow->addStretch();
    auto* editCleanupBtn = new QPushButton(tr("Edit..."), initGroup);
    connect(editCleanupBtn, &QPushButton::clicked, this, [this]() {
        auto* dlg = new QDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle(tr("Filename Cleanup Rules"));
        auto* layout = new QVBoxLayout(dlg);
        layout->addWidget(new QLabel(
            tr("Define patterns to automatically clean up filenames of new downloads.\n"
               "Each rule replaces a regex pattern with a replacement string.")));

        auto* table = new ListTreeWidget(dlg);
        table->setHeaderLabels({tr("Pattern"), tr("Replacement"), tr("Enabled")});
        table->setRootIsDecorated(false);
        table->setColumnCount(3);
        // Interactive, not Stretch/ResizeToContents: a Qt-owned width can't be
        // resized by the user, so there would be nothing to remember.
        table->header()->setStretchLastSection(true);
        table->bindColumns(QStringLiteral("optionsFilenameRules"), {240, 200, 90});
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
        connect(addBtn, &QPushButton::clicked, dlg, [addRule]() {
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

        DialogSizing::applySize(dlg, QSize(500, 350), {}, DialogSizing::Fit::Layout);
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

    m_watchClipboardCheck = new QCheckBox(tr("Watch clipboard for eD2K links"), miscGroup);
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

    // MFC says "New eMule version detected"; this fires on a new version of *this*
    // app (VersionChecker's newVersionAvailable), so the name has to match it.
    m_notifyNewVersionCheck = new QCheckBox(tr("New eMule Qt version detected"), whenGroup);
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
    // Not a ListTreeWidget: single column with a hidden header, so there is no
    // column layout to persist.
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
    giveListRoom(m_ircMiscTree, 8);
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
            if (!f.open(QIODevice::WriteOnly))
                return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    filterLevelRow->addWidget(editBtn);
    filterLevelRow->addStretch();
    ipFilterLayout->addLayout(filterLevelRow);

    // Update from URL row
    ipFilterLayout->addWidget(new QLabel(
        tr("Update from URL: (filter.dat- or PeerGuardian-format, .gz/.zip accepted)"),
        ipFilterGroup));
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

        // Most public lists ship compressed; the names are the ones MFC looks for inside
        // an archive (srchybrid/PPgSecurity.cpp:246-250).
        eMule::HttpFileDownload::Options opts;
        opts.preferredNames = {QStringLiteral("ipfilter.dat"),
                               QStringLiteral("guarding.p2p"),
                               QStringLiteral("guardian.p2p")};

        eMule::HttpFileDownload::get(this, QUrl(url), opts,
            [this, loadBtn](bool ok, const QByteArray& data, const QString& entryName,
                            const QString& error) {
                loadBtn->setEnabled(true);
                loadBtn->setText(tr("Load"));

                if (!ok) {
                    QMessageBox::warning(this, tr("IP Filter"),
                        tr("Failed to download IP filter: %1").arg(error));
                    return;
                }
                if (data.isEmpty()) {
                    QMessageBox::warning(this, tr("IP Filter"),
                        tr("Downloaded IP filter is empty."));
                    return;
                }

                const QString path = QDir(thePrefs.configDir())
                                         .filePath(QStringLiteral("ipfilter.dat"));
                QFile f(path);
                if (!f.open(QIODevice::WriteOnly)) {
                    QMessageBox::warning(this, tr("IP Filter"),
                        tr("Failed to save IP filter: %1").arg(f.errorString()));
                    return;
                }
                f.write(data);
                f.close();

                if (m_ipc && m_ipc->isConnected()) {
                    Ipc::IpcMessage msg(Ipc::IpcMsgType::ReloadIPFilter);
                    m_ipc->sendRequest(std::move(msg));
                }
                QMessageBox::information(this, tr("IP Filter"),
                    entryName.isEmpty()
                        ? tr("IP filter updated and reloaded.")
                        : tr("IP filter updated and reloaded (unpacked \"%1\").").arg(entryName));
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

    // The palette the user is editing. It lives in uistate.yml, not preferences.yml:
    // these colours only ever paint in this process, and the daemon owns the prefs file.
    m_statsColors = theUiState.statsColors();

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

    // Color selector row. The entry order and the index behind each entry are MFC's
    // (srchybrid/PPgStats.cpp:207-238) — the list is grouped for reading, so the index
    // must ride along as item data rather than being the row number.
    auto* colorRow = new QHBoxLayout;
    m_statsColorSelector = new QComboBox(colorsGroup);
    const std::array<std::pair<QString, int>, UiState::kStatsColorCount> colorEntries = {{
        {tr("Background"), 0},
        {tr("Grid"), 1},
        {tr("Download Session"), 4},
        {tr("Download Average"), 3},
        {tr("Download Current"), 2},
        {tr("Download Usenet"), 15},
        {tr("Upload Session"), 7},
        {tr("Upload Average"), 6},
        {tr("Upload Current"), 5},
        {tr("Upload Slots (no overhead)"), 14},
        {tr("Upload Friend Slots"), 13},
        {tr("Active Connections"), 8},
        {tr("Active Uploads"), 10},
        {tr("Total Uploads"), 9},
        {tr("Active Downloads"), 12},
        {tr("Icon Bar"), 11},
    }};
    for (const auto& [label, index] : colorEntries)
        m_statsColorSelector->addItem(label, index);

    m_statsColorBtn = new QPushButton(colorsGroup);
    m_statsColorBtn->setFixedSize(48, 24);
    m_statsColorBtn->setFlat(true);
    m_statsColorBtn->setAutoFillBackground(true);

    auto* defaultBtn = new QPushButton(tr("Default"), colorsGroup);
    defaultBtn->setToolTip(tr("Restore this colour to the eMule default"));

    auto selectedColorIndex = [this]() {
        const QVariant data = m_statsColorSelector->currentData();
        const int idx = data.isValid() ? data.toInt() : -1;
        return (idx >= 0 && idx < UiState::kStatsColorCount) ? idx : -1;
    };

    auto updateColorBtn = [this, selectedColorIndex]() {
        const int idx = selectedColorIndex();
        if (idx < 0)
            return;
        const QColor& c = m_statsColors[static_cast<size_t>(idx)];
        // An invalid colour is the tray meter's "follow the system theme" state; it has
        // no swatch to show, so the button says so instead.
        m_statsColorBtn->setText(c.isValid() ? QString() : tr("Auto"));
        m_statsColorBtn->setStyleSheet(c.isValid()
            ? QStringLiteral("background-color: %1; border: 1px solid gray;").arg(c.name())
            : QStringLiteral("border: 1px solid gray;"));
    };
    connect(m_statsColorSelector, &QComboBox::currentIndexChanged, this, updateColorBtn);
    connect(m_statsColorBtn, &QPushButton::clicked, this,
            [this, updateColorBtn, selectedColorIndex]() {
        const int idx = selectedColorIndex();
        if (idx < 0)
            return;
        const QColor current = m_statsColors[static_cast<size_t>(idx)];
        const QColor chosen = QColorDialog::getColor(
            current.isValid() ? current : QColor(Qt::white), this, tr("Select Color"));
        if (chosen.isValid()) {
            m_statsColors[static_cast<size_t>(idx)] = chosen;
            updateColorBtn();
            markDirty();
        }
    });
    connect(defaultBtn, &QPushButton::clicked, this,
            [this, updateColorBtn, selectedColorIndex]() {
        const int idx = selectedColorIndex();
        if (idx < 0)
            return;
        m_statsColors[static_cast<size_t>(idx)] =
            UiState::defaultStatsColors()[static_cast<size_t>(idx)];
        updateColorBtn();
        markDirty();
    });
    updateColorBtn();

    colorRow->addWidget(m_statsColorSelector, 1);
    colorRow->addWidget(m_statsColorBtn);
    colorRow->addWidget(defaultBtn);
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
    // The web server (UI) and the REST API are independent checkboxes — neither
    // greys the other. updateWebEnabledStates() only greys sub-controls that have
    // no effect for the current selection. Anything that changes which controls
    // are relevant (either top-level checkbox, HTTPS, guest) re-runs it.
    connect(m_webEnabledCheck, &QCheckBox::toggled, this, [this] {
        updateWebEnabledStates();
        markDirty();
    });
    connect(m_webRestApiCheck, &QCheckBox::toggled, this, [this] {
        updateWebEnabledStates();
        markDirty();
    });
    connect(m_webHttpsCheck, &QCheckBox::toggled, this, [this] {
        updateWebEnabledStates();
        markDirty();
    });
    connect(m_webGuestEnabledCheck, &QCheckBox::toggled, this, [this] {
        updateWebEnabledStates();
        markDirty();
    });

    // The template is read once when the web server starts, so an edit to the
    // live .tmpl needs either a daemon restart or this. Only the content is
    // re-read: a changed path is a config change and goes through Apply.
    connect(m_webTemplateReloadBtn, &QPushButton::clicked, this, [this] {
        if (!m_ipc)
            return;
        m_ipc->sendRequest(Ipc::IpcMessage(Ipc::IpcMsgType::ReloadWebTemplate),
                           [](const Ipc::IpcMessage& resp) {
                               if (!resp.isValid())
                                   return;   // connection dropped: neither reloaded nor refused
                               StatusBarNotifier::post(
                                   resp.fieldBool(0)
                                       ? OptionsDialog::tr("Web template reloaded")
                                       : OptionsDialog::tr("Web template reload failed"),
                                   4000);
                           });
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

    // Mark dirty for all editable controls. m_webEnabledCheck and
    // m_webRestApiCheck are wired above (they also refresh enable states).
    for (auto* cb : {m_webGzipCheck, m_webUPnPCheck, m_webAdminHiLevCheck})
        connect(cb, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    for (auto* le : {m_webTemplateEdit, m_webCertEdit, m_webKeyEdit, m_webApiKeyEdit,
                     m_webAdminPasswordEdit, m_webGuestPasswordEdit})
        connect(le, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    for (auto* sb : {m_webPortSpin, m_webSessionTimeoutSpin})
        connect(sb, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);

    // Initial state
    updateWebEnabledStates();

    return page;
}

// ---------------------------------------------------------------------------
// Extended page — matches MFC "Options Extended*.png" (PPgTweaks)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Usenet — news server accounts
//
// The list travels over GetNewsServers=720 / SetNewsServers=721 rather than the
// generic preference map, because it carries credentials. Two rules follow from
// that and shape this page:
//
//   - The daemon never sends a password to the GUI, only `hasPassword`. The
//     password field therefore shows a placeholder, not a value, and a GUI
//     screenshot or an IPC log cannot leak a provider credential.
//   - An entry saved without a `password` field keeps the stored one. So the
//     field is only attached to an entry the user actually retyped.
// ---------------------------------------------------------------------------

namespace {

/// The "Test" button and the label that reports what the server said, as one
/// form row. Used by both the Usenet and the Indexers page -- same widget, same
/// two traps:
///
///  - The label has to be told its height follows its width. A word-wrapping
///    QLabel reports a *one-line* minimum height, and QWidgetItem asks the size
///    policy rather than the widget, so without this a two-line error is laid
///    out as one line and the second line is painted outside the row. Caller
///    must opt the enclosing group box in too, or the chain breaks one level up.
///  - The stretch below only pays off on a form whose fields grow; QMacStyle
///    defaults QFormLayout to FieldsStayAtSizeHint, where it buys nothing.
/// QLayout::invalidate() does not reach nested layouts, and a QWidget only ever
/// invalidates its own. A row built as a layout inside a form therefore keeps a
/// stale cached height after one of its widgets changes.
void invalidateLayoutTree(QLayout* layout)
{
    if (!layout)
        return;

    for (int i = 0; i < layout->count(); ++i)
        invalidateLayoutTree(layout->itemAt(i)->layout());

    layout->invalidate();
}

void addTestRow(QFormLayout* form, QWidget* parent, QPushButton*& button,
                QLabel*& result)
{
    auto* row = new QHBoxLayout;
    button = new QPushButton(QObject::tr("Test"), parent);
    row->addWidget(button);

    result = new QLabel(parent);
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    DialogSizing::enableHeightForWidth(result);
    row->addWidget(result, 1);

    form->addRow(QString{}, row);
}

/// The same shape as addTestRow: a wrapping, selectable label with one button
/// beside it. Extracted rather than copied because the Usenet page now has two
/// of these and a third would be the point at which they drifted apart.
void addLabelledButtonRow(QFormLayout* form, QWidget* parent, const QString& caption,
                          const QString& buttonText, QPushButton*& button, QLabel*& result)
{
    auto* row = new QHBoxLayout;

    result = new QLabel(parent);
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    DialogSizing::enableHeightForWidth(result);
    row->addWidget(result, 1);

    button = new QPushButton(buttonText, parent);
    row->addWidget(button);

    form->addRow(caption, row);
}

} // namespace

QWidget* OptionsDialog::createUsenetPage()
{
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(4, 4, 4, 4);

    m_usenetEnabledCheck = new QCheckBox(tr("Enable Usenet downloads"), page);
    m_usenetEnabledCheck->setToolTip(
        tr("Gates automatic activity only. Adding a download by hand always works."));
    pageLayout->addWidget(m_usenetEnabledCheck);

    // Two tabs rather than the one ~1300 px column this page used to be -- which, being
    // the tallest of the eighteen, set the minimum height of every one of them.
    //
    // The split is by *scope*, not by how advanced a setting looks: everything on
    // Account belongs to the one server highlighted in the table there, everything on
    // Advanced applies to the whole Usenet engine. Anything else and a per-account
    // field ends up on a tab with no account chooser on it, reading as global.
    auto* tabs = new QTabWidget(page);
    pageLayout->addWidget(tabs, 1);

    // Each tab scrolls on its own, so the tab bar stays put instead of scrolling away
    // with the form under it. That is why setupPages() hands this page over as
    // PageScroll::Self -- an outer wrapper on top would mean two vertical scrollbars.
    const auto addTab = [tabs](const QString& title) {
        auto* body = new QWidget;
        auto* layout = new QVBoxLayout(body);
        layout->setContentsMargins(4, 4, 4, 4);
        DialogSizing::enableHeightForWidth(body);
        auto* area = new ContentScrollArea(tabs);
        area->setWidget(body);
        tabs->addTab(area, title);
        return layout;
    };
    auto* accountLayout  = addTab(tr("Account"));

    auto* advancedLayout = addTab(tr("Advanced"));

    // -- Account list -------------------------------------------------------
    auto* serversGroup = new QGroupBox(tr("News servers"), page);
    auto* serversLayout = new QVBoxLayout(serversGroup);

    auto* table = new ListTreeWidget(serversGroup);
    m_usenetServerTable = table;
    m_usenetServerTable->setHeaderLabels(
        {tr("Name"), tr("Host"), tr("Port"), tr("Priority"), tr("Connections"), tr("Used")});
    m_usenetServerTable->setRootIsDecorated(false);
    m_usenetServerTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_usenetServerTable->setColumnCount(6);
    m_usenetServerTable->header()->setStretchLastSection(true);
    // Going from five columns to six makes QHeaderView reject the stored state
    // once, so widths fall back to these defaults a single time. That is the
    // whole cost; the layout persists again from the next run.
    // Six columns have to fit where five did, so Host and Name give up the room
    // rather than the table growing a horizontal scrollbar it never had. 0 for
    // the last: stretchLastSection owns it, and resizing a stretched section
    // fights the stretch instead of widening it.
    table->bindColumns(QStringLiteral("optionsUsenetServers"), {95, 130, 42, 52, 78, 0});
    giveListRoom(m_usenetServerTable, 5, ListGrowth::Capped);
    serversLayout->addWidget(m_usenetServerTable);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_usenetAddBtn = new QPushButton(tr("Add"), serversGroup);
    m_usenetRemoveBtn = new QPushButton(tr("Remove"), serversGroup);
    btnRow->addWidget(m_usenetAddBtn);
    btnRow->addWidget(m_usenetRemoveBtn);
    serversLayout->addLayout(btnRow);
    accountLayout->addWidget(serversGroup);

    // -- Details ------------------------------------------------------------
    auto* details = new QGroupBox(tr("Account"), page);
    // Prefix shared with the group below: tst_OptionsDialogSizing uses it to assert that
    // no per-account field has drifted onto the Advanced tab.
    details->setObjectName(QStringLiteral("usenetAccountIdentity"));
    auto* form = new QFormLayout(details);
    // Without this the fields keep their size hint on macOS and the test result
    // label never reaches the right edge, however much room the group box has.
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    DialogSizing::enableHeightForWidth(details);

    m_usenetEntryEnabledCheck = new QCheckBox(tr("Enabled"), details);
    form->addRow(m_usenetEntryEnabledCheck);

    m_usenetNameEdit = new QLineEdit(details);
    m_usenetNameEdit->setPlaceholderText(tr("Display name (optional)"));
    form->addRow(tr("Name:"), m_usenetNameEdit);

    m_usenetHostEdit = new QLineEdit(details);
    m_usenetHostEdit->setPlaceholderText(QStringLiteral("news.example.com"));
    form->addRow(tr("Host:"), m_usenetHostEdit);

    auto* portRow = new QHBoxLayout;
    m_usenetPortSpin = new QSpinBox(details);
    m_usenetPortSpin->setRange(1, 65535);
    m_usenetPortSpin->setValue(kDefaultNntpTlsPort);
    portRow->addWidget(m_usenetPortSpin);
    portRow->addSpacing(12);
    portRow->addWidget(new QLabel(tr("Encryption:"), details));
    m_usenetTlsCombo = new QComboBox(details);
    // Order matches NntpTlsMode, so currentIndex() is the enum value.
    m_usenetTlsCombo->addItem(tr("None (119)"));
    m_usenetTlsCombo->addItem(tr("SSL/TLS (563)"));
    m_usenetTlsCombo->addItem(tr("STARTTLS"));
    m_usenetTlsCombo->setCurrentIndex(int(NntpTlsMode::Implicit));
    portRow->addWidget(m_usenetTlsCombo);
    portRow->addStretch();
    form->addRow(tr("Port:"), portRow);

    m_usenetUserEdit = new QLineEdit(details);
    form->addRow(tr("User:"), m_usenetUserEdit);

    m_usenetPassEdit = new QLineEdit(details);
    m_usenetPassEdit->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Password:"), m_usenetPassEdit);

    m_usenetConnSpin = new QSpinBox(details);
    m_usenetConnSpin->setRange(1, 100);
    m_usenetConnSpin->setValue(kDefaultMaxConnections);
    m_usenetConnSpin->setToolTip(
        tr("Never set this above what your provider allows — exceeding the limit "
           "gets the account throttled, not queued."));
    form->addRow(tr("Connections:"), m_usenetConnSpin);

    m_usenetLevelSpin = new QSpinBox(details);
    m_usenetLevelSpin->setRange(0, 99);
    m_usenetLevelSpin->setToolTip(
        tr("Lower is tried first. A higher level is only used for articles that "
           "every server below reported as missing — that is what makes a block "
           "or fill account worth having."));
    form->addRow(tr("Priority level:"), m_usenetLevelSpin);

    addTestRow(form, details, m_usenetTestBtn, m_usenetTestResult);

    accountLayout->addWidget(details);

    // -- Details, continued -------------------------------------------------
    // The same selected account, tuned rather than identified. Stays on this tab even
    // though these are the advanced fields: the table above is the only thing that says
    // *which* account they belong to, and a per-account form on the Advanced tab reads
    // as a global setting. Not titled "Account" twice -- two group boxes of that name in
    // one dialog is unreadable in a bug report.
    auto* advDetails = new QGroupBox(tr("Account options"), page);
    advDetails->setObjectName(QStringLiteral("usenetAccountTuning"));
    auto* advForm = new QFormLayout(advDetails);
    advForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    DialogSizing::enableHeightForWidth(advDetails);

    m_usenetRetentionSpin = new QSpinBox(advDetails);
    m_usenetRetentionSpin->setRange(0, 10000);
    m_usenetRetentionSpin->setSpecialValueText(tr("Unknown"));
    fitSpecialValue(m_usenetRetentionSpin);
    m_usenetRetentionSpin->setSuffix(tr(" days"));
    advForm->addRow(tr("Retention:"), m_usenetRetentionSpin);

    m_usenetGroupSpin = new QSpinBox(advDetails);
    m_usenetGroupSpin->setRange(0, 99);
    m_usenetGroupSpin->setSpecialValueText(tr("None"));
    fitSpecialValue(m_usenetGroupSpin);
    m_usenetGroupSpin->setToolTip(
        tr("Accounts sharing a group number count as one for connection limits — "
           "use it when the same provider is reached through two host names, so "
           "the two entries cannot open twice what the plan allows."));
    advForm->addRow(tr("Connection group:"), m_usenetGroupSpin);

    m_usenetCertCombo = new QComboBox(advDetails);
    // Order matches NntpCertVerification.
    m_usenetCertCombo->addItem(tr("None — accept any certificate"));
    m_usenetCertCombo->addItem(tr("Minimal — allow a host name mismatch"));
    m_usenetCertCombo->addItem(tr("Strict"));
    m_usenetCertCombo->setCurrentIndex(int(NntpCertVerification::Strict));
    advForm->addRow(tr("Certificate check:"), m_usenetCertCombo);

    m_usenetOptionalCheck = new QCheckBox(
        tr("Optional — never fail a download on its own"), advDetails);
    advForm->addRow(m_usenetOptionalCheck);

    m_usenetJoinGroupCheck = new QCheckBox(
        tr("Send GROUP before fetching (only needed by a few old servers)"), advDetails);
    advForm->addRow(m_usenetJoinGroupCheck);

    m_usenetQuotaKindCombo = new QComboBox(advDetails);
    // Index order matches NntpQuotaKind, the convention the TLS and certificate
    // combos already use, so currentIndex() *is* the enum.
    m_usenetQuotaKindCombo->addItem(tr("Unmetered"));
    m_usenetQuotaKindCombo->addItem(tr("Monthly allowance"));
    m_usenetQuotaKindCombo->addItem(tr("Block account (prepaid)"));
    advForm->addRow(tr("Allowance:"), m_usenetQuotaKindCombo);

    m_usenetQuotaSpin = new QDoubleSpinBox(advDetails);
    m_usenetQuotaSpin->setRange(0.0, 1000000.0);
    m_usenetQuotaSpin->setDecimals(1);
    m_usenetQuotaSpin->setSuffix(tr(" GB"));
    m_usenetQuotaSpin->setSpecialValueText(tr("No limit"));
    fitSpecialValue(m_usenetQuotaSpin);
    m_usenetQuotaSpin->setToolTip(
        tr("Decimal GB, because that is what an invoice says — the 1024-based GB "
           "used elsewhere in eMule would put a 1000 GB plan 7% over.\n\n"
           "Set it slightly under your plan. The figure is measured here, so it "
           "reads a few percent below your provider's, and articles already in "
           "flight when the limit is reached still finish."));
    advForm->addRow(tr("Allowance size:"), m_usenetQuotaSpin);

    m_usenetQuotaDaySpin = new QSpinBox(advDetails);
    m_usenetQuotaDaySpin->setRange(1, 31);
    m_usenetQuotaDaySpin->setToolTip(
        tr("Your billing day — providers reset on the day you signed up, not on "
           "the 1st. A month shorter than this rolls over on its last day."));
    advForm->addRow(tr("Resets on day:"), m_usenetQuotaDaySpin);

    m_usenetQuotaFallThroughCheck = new QCheckBox(
        tr("When the allowance is spent, use the next priority level"), advDetails);
    m_usenetQuotaFallThroughCheck->setToolTip(
        tr("Off by default: block credit usually costs more per GB than the plan "
           "it would be covering, and spending it without being asked is the one "
           "thing a limit exists to prevent. Left off, downloads wait for the "
           "allowance instead — they are never failed and no article is ever "
           "given up on."));
    advForm->addRow(m_usenetQuotaFallThroughCheck);

    addLabelledButtonRow(advForm, advDetails, tr("Used:"), tr("Correct…"),
                         m_usenetUsageEditBtn, m_usenetUsageLabel);
    // One line, always. A QFormLayout row does not grow for a wrapped label, so
    // a second line is simply cut off — the detail lives in the tooltip instead.
    m_usenetUsageLabel->setWordWrap(false);

    accountLayout->addWidget(advDetails);
    accountLayout->addStretch();

    // -- Everything below is global -----------------------------------------
    // No account chooser on this tab, and none needed: none of it is per-account.
    auto* limitsGroup = new QGroupBox(tr("Downloading"), page);
    auto* limitsLayout = new QVBoxLayout(limitsGroup);

    auto* retryRow = new QHBoxLayout;
    retryRow->addWidget(new QLabel(tr("Retry a failed server after:"), limitsGroup));
    m_usenetRetrySpin = new QSpinBox(limitsGroup);
    m_usenetRetrySpin->setRange(0, 3600);
    m_usenetRetrySpin->setSuffix(tr(" s"));
    m_usenetRetrySpin->setSpecialValueText(tr("Never back off"));
    m_usenetRetrySpin->setToolTip(
        tr("Applies to every account: how long a server that refused or dropped a "
           "connection is passed over before it is tried again."));
    fitSpecialValue(m_usenetRetrySpin);
    retryRow->addWidget(m_usenetRetrySpin);
    retryRow->addStretch();
    limitsLayout->addLayout(retryRow);

    auto* shareRow = new QHBoxLayout;
    shareRow->addWidget(new QLabel(tr("Share of the download limit:"), limitsGroup));
    m_usenetShareSpin = new QSpinBox(limitsGroup);
    m_usenetShareSpin->setRange(1, 99);
    m_usenetShareSpin->setSuffix(tr(" %"));
    m_usenetShareSpin->setToolTip(
        tr("How much of the global download limit Usenet may take while eD2K is "
           "also downloading. Whichever engine is idle lends its whole share to "
           "the other, so this only applies when both are busy."));
    shareRow->addWidget(m_usenetShareSpin);
    shareRow->addStretch();
    limitsLayout->addLayout(shareRow);

    advancedLayout->addWidget(limitsGroup);

    // Before downloading, so it goes above "After downloading" rather than into
    // it: the question this answers is whether to spend anything at all.
    auto* addGroup = new QGroupBox(tr("When adding"), page);
    auto* addLayout = new QVBoxLayout(addGroup);

    auto* healthRow = new QHBoxLayout;
    healthRow->addWidget(new QLabel(tr("Check availability:"), addGroup));
    m_usenetHealthCombo = new QComboBox(addGroup);
    m_usenetHealthCombo->addItem(tr("Do not check"));
    m_usenetHealthCombo->addItem(tr("Sample one article per file"));
    m_usenetHealthCombo->addItem(tr("Check every article"));
    m_usenetHealthCombo->setToolTip(
        tr("Before downloading anything, ask your providers whether they still "
           "hold the release. It costs one small request per article asked "
           "about and no payload at all.\n\n"
           "Sampling asks about the first article of each file, which is "
           "usually enough: providers expire whole posts by date, so a file is "
           "almost always present or absent as a unit. Checking every article "
           "is certain but can mean tens of thousands of requests for a large "
           "release.\n\n"
           "The answer is never a verdict. Nothing here can stop an article "
           "being fetched — an article your providers deny may still arrive, "
           "and a release this pauses downloads normally when you resume it."));
    healthRow->addWidget(m_usenetHealthCombo);
    healthRow->addStretch();
    addLayout->addLayout(healthRow);

    auto* healthMinRow = new QHBoxLayout;
    healthMinRow->addWidget(new QLabel(tr("Pause below:"), addGroup));
    m_usenetHealthMinSpin = new QSpinBox(addGroup);
    m_usenetHealthMinSpin->setRange(0, 100);
    m_usenetHealthMinSpin->setSuffix(tr(" %"));
    m_usenetHealthMinSpin->setSpecialValueText(tr("never"));
    fitSpecialValue(m_usenetHealthMinSpin);
    m_usenetHealthMinSpin->setToolTip(
        tr("A release that looks emptier than this is added paused, with the "
           "reason shown, so you decide rather than the guess. It is never "
           "failed and never refused.\n\n"
           "A shortfall the release's own PAR2 recovery volumes can cover does "
           "not pause it, however low the figure goes."));
    healthMinRow->addWidget(m_usenetHealthMinSpin);
    healthMinRow->addStretch();
    addLayout->addLayout(healthMinRow);

    m_usenetAutoPausedCheck =
        new QCheckBox(tr("Start automatic downloads paused"), addGroup);
    m_usenetAutoPausedCheck->setToolTip(
        tr("Applies to anything queued without you asking for it directly: the "
           "watch folder below, and feeds.\n\n"
           "With this on, an automatic download waits for you to press Resume, so "
           "a feed proposes rather than decides. Anything you add yourself starts "
           "normally either way."));
    addLayout->addWidget(m_usenetAutoPausedCheck);

    advancedLayout->addWidget(addGroup);

    // -- Watch folder -------------------------------------------------------
    auto* watchGroup = new QGroupBox(tr("Watch folder"), page);
    auto* watchLayout = new QVBoxLayout(watchGroup);

    auto* watchIntro = new QLabel(
        tr("Any .nzb file left in this folder is queued and then moved into a "
           "_processed subfolder — or _failed, if it could not be read."),
        watchGroup);
    watchIntro->setWordWrap(true);
    watchLayout->addWidget(watchIntro);

    auto* watchRow = new QHBoxLayout;
    m_usenetWatchDirEdit = new QLineEdit(watchGroup);
    m_usenetWatchDirEdit->setPlaceholderText(tr("No folder is being watched"));
    m_usenetWatchDirEdit->setToolTip(
        tr("A file is only read once it has stopped changing, so a large .nzb "
           "still being copied in is left alone until it is complete.\n\n"
           "It cannot be inside your temp, incoming or configuration folders: "
           "the daemon writes there itself."));
    watchRow->addWidget(m_usenetWatchDirEdit, 1);
    m_usenetWatchDirBrowse = new QPushButton(tr("Browse…"), watchGroup);
    watchRow->addWidget(m_usenetWatchDirBrowse);
    watchLayout->addLayout(watchRow);

    advancedLayout->addWidget(watchGroup);

    // -- Desktop integration ------------------------------------------------
    if (gui::FileAssociation::isRuntimeRegistration()) {
        // macOS declares its document types in the bundle, so there is nothing
        // here to switch on or off.
        auto* desktopGroup = new QGroupBox(tr("Desktop"), page);
        auto* desktopLayout = new QVBoxLayout(desktopGroup);

        m_associateNzbCheck =
            new QCheckBox(tr("Open .nzb files with eMule Qt"), desktopGroup);
        m_associateNzbCheck->setToolTip(
            tr("Claim .nzb files for this copy of eMule Qt, so double-clicking "
               "one queues it. The setting is for you alone and needs no "
               "administrator; it is re-applied at every start, so another "
               "program taking the association does not keep it."));
        desktopLayout->addWidget(m_associateNzbCheck);
        advancedLayout->addWidget(desktopGroup);

        connect(m_associateNzbCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    }

    connect(m_usenetAutoPausedCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_usenetWatchDirEdit, &QLineEdit::textEdited, this, &OptionsDialog::markDirty);
    connect(m_usenetWatchDirBrowse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Watch folder"), m_usenetWatchDirEdit->text());
        if (dir.isEmpty())
            return;
        m_usenetWatchDirEdit->setText(dir);
        markDirty();
    });

    auto* postGroup = new QGroupBox(tr("After downloading"), page);
    auto* postLayout = new QVBoxLayout(postGroup);

    m_usenetPar2Check = new QCheckBox(tr("Verify and repair with PAR2"), postGroup);
    m_usenetPar2Check->setToolTip(
        tr("Check the finished files against the release's PAR2 set and repair any "
           "damage from its recovery volumes. The recovery volumes are only "
           "downloaded when something actually needs repairing.\n\n"
           "With this off, a release with missing articles fails instead of being "
           "shared, because there is no way to tell whether it is intact."));
    postLayout->addWidget(m_usenetPar2Check);

    m_usenetRenameCheck = new QCheckBox(tr("Restore filenames from PAR2"), postGroup);
    m_usenetRenameCheck->setToolTip(
        tr("Obfuscated releases are posted under meaningless filenames. The PAR2 "
           "metadata carries the real ones, and without them the archives cannot "
           "be identified for unpacking either."));
    postLayout->addWidget(m_usenetRenameCheck);

    m_usenetSfvCheck = new QCheckBox(tr("Verify with SFV when there is no PAR2"), postGroup);
    m_usenetSfvCheck->setToolTip(
        tr("A release posted without a PAR2 set often comes with an .sfv file "
           "instead. Its checksums cannot repair anything, but a release they call "
           "damaged is not published."));
    postLayout->addWidget(m_usenetSfvCheck);

    m_usenetUnpackCheck = new QCheckBox(tr("Unpack archives"), postGroup);
    m_usenetUnpackCheck->setToolTip(
        tr("Extract RAR, 7z and ZIP volume sets once they have been verified.\n\n"
           "Password-protected archives need 7-Zip or unrar installed — eMule's "
           "own archive reader can only decrypt ZIP."));
    postLayout->addWidget(m_usenetUnpackCheck);

    m_usenetDirectUnpackCheck =
        new QCheckBox(tr("Unpack while downloading"), postGroup);
    m_usenetDirectUnpackCheck->setToolTip(
        tr("Extract each archive volume as soon as it finishes instead of waiting "
           "for the whole release, so the content is ready the moment the download "
           "is.\n\n"
           "It is the same extraction, moved earlier, so it costs no extra disk "
           "space. If the release turns out to need repairing, the result is "
           "discarded and it is unpacked again afterwards."));
    postLayout->addWidget(m_usenetDirectUnpackCheck);

    m_usenetEncryptedPreviewCheck =
        new QCheckBox(tr("Preview password-protected releases while downloading"), postGroup);
    m_usenetEncryptedPreviewCheck->setToolTip(
        tr("An encrypted archive cannot be read a piece at a time, so previewing "
           "one means decrypting it again from the first volume every time more "
           "of it arrives.\n\n"
           "Nothing runs unless a preview is actually open, and only RAR releases "
           "can do it at all — an incomplete 7z set decodes to nothing."));
    postLayout->addWidget(m_usenetEncryptedPreviewCheck);

    auto* unpackerRow = new QHBoxLayout;
    unpackerRow->addWidget(new QLabel(tr("Unpacker:"), postGroup));
    m_usenetUnpackerEdit = new QLineEdit(postGroup);
    m_usenetUnpackerEdit->setPlaceholderText(tr("automatic (7zz, 7z, unrar)"));
    m_usenetUnpackerEdit->setToolTip(
        tr("Path to a 7-Zip or unrar binary, for password-protected archives.\n\n"
           "Leave this empty to search the usual locations. Set it when eMule runs "
           "as a background service, whose search path is often much shorter than "
           "the one a terminal has."));
    unpackerRow->addWidget(m_usenetUnpackerEdit, 1);
    postLayout->addLayout(unpackerRow);

    m_usenetCleanupCheck =
        new QCheckBox(tr("Delete archives and PAR2 files after unpacking"), postGroup);
    m_usenetCleanupCheck->setToolTip(
        tr("Keep only the unpacked content. Turning this off roughly doubles the "
           "disk space a release uses and shares the archive volumes and recovery "
           "files with eD2K peers, who have no use for them."));
    postLayout->addWidget(m_usenetCleanupCheck);

    auto* checksForm = new QFormLayout;
    m_usenetUnrepairableCombo = new QComboBox(postGroup);
    m_usenetUnrepairableCombo->addItem(tr("Keep downloading"));   // 0
    m_usenetUnrepairableCombo->addItem(tr("Pause it"));           // 1
    m_usenetUnrepairableCombo->addItem(tr("Fail it"));            // 2
    m_usenetUnrepairableCombo->setToolTip(
        tr("A release that has lost more than its recovery files could ever repair "
           "stops here instead of using up your allowance until the final check. "
           "The estimate only ever errs towards downloading.\n\n"
           "Resume downloads it anyway."));
    checksForm->addRow(tr("When a download cannot be repaired:"), m_usenetUnrepairableCombo);

    m_usenetUnwantedCombo = new QComboBox(postGroup);
    m_usenetUnwantedCombo->addItem(tr("Publish anyway"));   // 0
    m_usenetUnwantedCombo->addItem(tr("Pause it"));         // 1
    m_usenetUnwantedCombo->addItem(tr("Fail it"));          // 2
    m_usenetUnwantedCombo->setToolTip(
        tr("A movie or episode whose download contains programs or shortcuts is "
           "almost always a fake. Only releases with video or audio in them are "
           "checked, so software downloads are not affected.\n\n"
           "Resume publishes it anyway."));
    checksForm->addRow(tr("When a media release has unwanted files:"), m_usenetUnwantedCombo);

    m_usenetUnwantedEdit = new QLineEdit(postGroup);
    m_usenetUnwantedEdit->setToolTip(
        tr("File extensions, separated by commas. A video file that is not really a "
           "video counts as well. Leave this empty to turn the check off."));
    checksForm->addRow(tr("Unwanted file types:"), m_usenetUnwantedEdit);
    postLayout->addLayout(checksForm);

    advancedLayout->addWidget(postGroup);

    advancedLayout->addStretch();

    // -- Wiring -------------------------------------------------------------
    // Gates auto-start only, so it greys nothing -- see updateUsenetEnabledStates().
    connect(m_usenetEnabledCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_usenetRetrySpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    connect(m_usenetShareSpin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);

    for (QCheckBox* box : {m_usenetPar2Check, m_usenetRenameCheck, m_usenetUnpackCheck,
                           m_usenetDirectUnpackCheck, m_usenetEncryptedPreviewCheck,
                           m_usenetCleanupCheck, m_usenetSfvCheck}) {
        connect(box, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    }
    connect(m_usenetUnpackerEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_usenetUnrepairableCombo, &QComboBox::currentIndexChanged, this,
            &OptionsDialog::markDirty);
    connect(m_usenetUnwantedCombo, &QComboBox::currentIndexChanged, this,
            &OptionsDialog::markDirty);
    connect(m_usenetUnwantedEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    // A list nothing acts on is a list nobody should be editing.
    connect(m_usenetUnwantedCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) { m_usenetUnwantedEdit->setEnabled(index > 0); });

    // Unpacking is what produces the payload; with it off there is nothing to
    // clean up around, and cleanup would only delete the files just downloaded.
    connect(m_usenetUnpackCheck, &QCheckBox::toggled, m_usenetCleanupCheck,
            &QWidget::setEnabled);
    // Same reasoning: unpacking early is still unpacking.
    connect(m_usenetUnpackCheck, &QCheckBox::toggled, m_usenetDirectUnpackCheck,
            &QWidget::setEnabled);
    // And previewing an encrypted set *is* unpacking it, one prefix at a time.
    connect(m_usenetUnpackCheck, &QCheckBox::toggled, m_usenetEncryptedPreviewCheck,
            &QWidget::setEnabled);

    connect(m_usenetHealthCombo, &QComboBox::currentIndexChanged, this,
            &OptionsDialog::markDirty);
    connect(m_usenetHealthMinSpin, &QSpinBox::valueChanged, this,
            &OptionsDialog::markDirty);
    // A threshold means nothing with no check behind it, and greying it says so
    // rather than leaving a control that silently does nothing.
    connect(m_usenetHealthCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) { m_usenetHealthMinSpin->setEnabled(index > 0); });

    connect(m_usenetAddBtn, &QPushButton::clicked, this, &OptionsDialog::addNewsServer);
    connect(m_usenetRemoveBtn, &QPushButton::clicked, this, &OptionsDialog::removeNewsServer);
    connect(m_usenetTestBtn, &QPushButton::clicked, this, &OptionsDialog::testNewsServer);

    connect(m_usenetServerTable, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                // Commit the row being left before showing the next one, or
                // every edit is lost the moment the selection moves.
                applyNewsServerDetails();
                populateNewsServerDetails(current ? m_usenetServerTable->indexOfTopLevelItem(current)
                                                  : -1);
            });

    // Every detail widget writes straight back into the working copy, so there
    // is no separate "Apply" step to forget. Only the edited row is repainted: a
    // full rebuild would churn every item on each keystroke and re-enter the
    // clear()+restore path that once swallowed addNewsServer()'s selection.
    const auto commit = [this] {
        applyNewsServerDetails();
        updateNewsServerRow(m_currentNewsServer);
        markDirty();
    };
    connect(m_usenetNameEdit, &QLineEdit::editingFinished, this, commit);
    connect(m_usenetHostEdit, &QLineEdit::editingFinished, this, commit);
    connect(m_usenetUserEdit, &QLineEdit::editingFinished, this, commit);
    connect(m_usenetPassEdit, &QLineEdit::editingFinished, this, commit);
    connect(m_usenetPortSpin, &QSpinBox::valueChanged, this, commit);
    connect(m_usenetConnSpin, &QSpinBox::valueChanged, this, commit);
    connect(m_usenetLevelSpin, &QSpinBox::valueChanged, this, commit);
    connect(m_usenetRetentionSpin, &QSpinBox::valueChanged, this, commit);
    connect(m_usenetGroupSpin, &QSpinBox::valueChanged, this, commit);
    connect(m_usenetTlsCombo, &QComboBox::currentIndexChanged, this, [this, commit](int index) {
        // Move the port with the encryption mode, but only while it still holds
        // the default for the previous mode — never stomp a port the user typed.
        const int port = m_usenetPortSpin->value();
        if (index == int(NntpTlsMode::Implicit) && port == kDefaultNntpPort)
            m_usenetPortSpin->setValue(kDefaultNntpTlsPort);
        else if (index != int(NntpTlsMode::Implicit) && port == kDefaultNntpTlsPort)
            m_usenetPortSpin->setValue(kDefaultNntpPort);
        commit();
    });
    connect(m_usenetCertCombo, &QComboBox::currentIndexChanged, this, commit);
    connect(m_usenetEntryEnabledCheck, &QCheckBox::toggled, this, commit);
    connect(m_usenetOptionalCheck, &QCheckBox::toggled, this, commit);
    connect(m_usenetJoinGroupCheck, &QCheckBox::toggled, this, commit);
    connect(m_usenetQuotaSpin, &QDoubleSpinBox::valueChanged, this, commit);
    connect(m_usenetQuotaDaySpin, &QSpinBox::valueChanged, this, commit);
    connect(m_usenetQuotaFallThroughCheck, &QCheckBox::toggled, this, commit);
    connect(m_usenetQuotaKindCombo, &QComboBox::currentIndexChanged, this,
            [this, commit] {
        commit();
        updateUsenetEnabledStates();   // the size and day fields follow the kind
    });
    connect(m_usenetUsageEditBtn, &QPushButton::clicked,
            this, &OptionsDialog::onCorrectNewsServerUsage);

    // Initial state: nothing selected yet, so the Account box starts greyed.
    updateUsenetEnabledStates();

    return page;
}

void OptionsDialog::updateUsenetEnabledStates()
{
    // Row selection is the only gate. The "Enable Usenet downloads" switch drives
    // auto-start and deliberately greys nothing, so a provider can be set up before
    // the engine is turned on.
    const bool anySelected = m_currentNewsServer >= 0;
    const std::initializer_list<QWidget*> detailWidgets{
        m_usenetNameEdit, m_usenetHostEdit, m_usenetUserEdit, m_usenetPassEdit,
        m_usenetPortSpin, m_usenetConnSpin, m_usenetLevelSpin,
        m_usenetRetentionSpin, m_usenetGroupSpin, m_usenetTlsCombo, m_usenetCertCombo,
        m_usenetEntryEnabledCheck, m_usenetOptionalCheck,
        m_usenetJoinGroupCheck, m_usenetQuotaKindCombo, m_usenetTestBtn,
        m_usenetRemoveBtn};
    for (QWidget* w : detailWidgets) {
        if (w)
            w->setEnabled(anySelected);
    }

    // The allowance fields follow the kind, the way "Unpack while downloading"
    // follows "Unpack archives": a size on an unmetered account and a billing
    // day on a prepaid block are both meaningless.
    const int kind = m_usenetQuotaKindCombo ? m_usenetQuotaKindCombo->currentIndex() : 0;
    if (m_usenetQuotaSpin)
        m_usenetQuotaSpin->setEnabled(anySelected && kind != int(NntpQuotaKind::None));
    if (m_usenetQuotaDaySpin)
        m_usenetQuotaDaySpin->setEnabled(anySelected && kind == int(NntpQuotaKind::Monthly));
    if (m_usenetQuotaFallThroughCheck)
        m_usenetQuotaFallThroughCheck->setEnabled(anySelected
                                                  && kind != int(NntpQuotaKind::None));
    // Nothing to correct until the daemon has told us what it has measured.
    if (m_usenetUsageEditBtn) {
        const bool known = anySelected && m_currentNewsServer < m_newsServers.size()
            && m_newsServers.at(m_currentNewsServer).contains(QStringLiteral("periodBytes"));
        m_usenetUsageEditBtn->setEnabled(known);
    }
}

void OptionsDialog::loadNewsServers()
{
    if (!m_ipc)
        return;

    m_ipc->sendRequest(Ipc::IpcMessage(Ipc::IpcMsgType::GetNewsServers),
                       [this](const Ipc::IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;

        m_newsServers.clear();
        const QCborArray rows = resp.fieldArray(1);
        m_newsServers.reserve(int(rows.size()));
        for (const auto& row : rows) {
            if (row.isMap())
                m_newsServers.append(row.toMap());
        }

        m_currentNewsServer = -1;
        refreshNewsServerTable();
        selectNewsServer(m_newsServers.isEmpty() ? -1 : 0);
    });
}

void OptionsDialog::saveNewsServers()
{
    if (!m_ipc)
        return;

    applyNewsServerDetails();

    QCborArray rows;
    for (const QCborMap& server : std::as_const(m_newsServers))
        rows.append(server);

    Ipc::IpcMessage msg(Ipc::IpcMsgType::SetNewsServers);
    msg.append(rows);
    // OK saves and then destroys the dialog, so this reply usually finds it gone: the
    // box then has no parent, and a dropped connection shows none at all.
    m_ipc->sendRequest(std::move(msg), [self = QPointer<OptionsDialog>(this)](const Ipc::IpcMessage& resp) {
        if (resp.fieldBool(0) || !resp.isValid())
            return;
        QMessageBox::warning(self, tr("News servers"),
                             resp.fieldString(1).isEmpty()
                                 ? tr("The news server list could not be saved.")
                                 : resp.fieldString(1));
    });
}

void OptionsDialog::refreshNewsServerTable()
{
    if (!m_usenetServerTable)
        return;

    const QSignalBlocker block(m_usenetServerTable);
    const int keep = m_currentNewsServer;
    m_usenetServerTable->clear();

    for (int i = 0; i < m_newsServers.size(); ++i) {
        new QTreeWidgetItem(m_usenetServerTable);
        updateNewsServerRow(i);
    }

    if (keep >= 0 && keep < m_usenetServerTable->topLevelItemCount())
        m_usenetServerTable->setCurrentItem(m_usenetServerTable->topLevelItem(keep));
}

void OptionsDialog::updateNewsServerRow(int index)
{
    if (!m_usenetServerTable || index < 0 || index >= m_newsServers.size()
        || index >= m_usenetServerTable->topLevelItemCount())
        return;

    // Row position is the list index: this table never enables sorting, so
    // nothing can reorder rows out from under the working copy.
    QTreeWidgetItem* item = m_usenetServerTable->topLevelItem(index);
    const QCborMap& s = m_newsServers.at(index);
    const QString host = s.value(QStringLiteral("host")).toString();
    const QString name = s.value(QStringLiteral("name")).toString();

    item->setText(0, name.isEmpty() ? host : name);
    item->setText(1, host);
    item->setText(2, QString::number(s.value(QStringLiteral("port")).toInteger(kDefaultNntpTlsPort)));
    item->setText(3, QString::number(s.value(QStringLiteral("level")).toInteger(0)));
    item->setText(4, QString::number(s.value(QStringLiteral("maxConnections"))
                                     .toInteger(kDefaultMaxConnections)));
    const qint64 used = s.value(QStringLiteral("periodBytes")).toInteger(0);
    item->setText(5, used > 0 ? formatQuotaGb(used) : QString{});

    // Disabled accounts stay visible but read as inactive, the way a disabled
    // schedule entry does. Re-enabling clears the role instead of painting a
    // "normal" brush -- an explicit one would override the palette and survive a
    // theme change, and rows are now reused rather than rebuilt.
    const bool on = s.value(QStringLiteral("enabled")).toBool(true);
    for (int c = 0; c < m_usenetServerTable->columnCount(); ++c) {
        if (on)
            item->setData(c, Qt::ForegroundRole, QVariant());
        else
            item->setForeground(c, palette().brush(QPalette::Disabled, QPalette::Text));
    }
}

void OptionsDialog::selectNewsServer(int index)
{
    // Never relies on currentItemChanged firing: refreshNewsServerTable() blocks the
    // table's signals, and setCurrentItem() is a no-op when the row is already
    // current -- either one silently skips the detail pane's update.
    if (index < 0 || index >= m_usenetServerTable->topLevelItemCount())
        index = -1;
    {
        const QSignalBlocker block(m_usenetServerTable);
        m_usenetServerTable->setCurrentItem(
            index >= 0 ? m_usenetServerTable->topLevelItem(index) : nullptr);
    }
    populateNewsServerDetails(index);
}

void OptionsDialog::populateNewsServerDetails(int index)
{
    m_currentNewsServer = (index >= 0 && index < m_newsServers.size()) ? index : -1;
    updateUsenetEnabledStates();
    if (m_usenetTestResult) {
        m_usenetTestResult->clear();
        m_usenetTestResult->setStyleSheet(QString{});
    }

    if (m_currentNewsServer < 0) {
        const QSignalBlocker b1(m_usenetNameEdit), b2(m_usenetHostEdit),
            b3(m_usenetUserEdit), b4(m_usenetPassEdit);
        m_usenetNameEdit->clear();
        m_usenetHostEdit->clear();
        m_usenetUserEdit->clear();
        m_usenetPassEdit->clear();
        m_usenetPassEdit->setPlaceholderText(QString{});
        return;
    }

    const QCborMap& s = m_newsServers.at(m_currentNewsServer);

    const QSignalBlocker b1(m_usenetNameEdit), b2(m_usenetHostEdit), b3(m_usenetPortSpin),
        b4(m_usenetTlsCombo), b5(m_usenetUserEdit), b6(m_usenetPassEdit),
        b7(m_usenetConnSpin), b8(m_usenetLevelSpin), b9(m_usenetRetentionSpin),
        bg(m_usenetGroupSpin),
        b10(m_usenetCertCombo), b11(m_usenetEntryEnabledCheck), b12(m_usenetOptionalCheck),
        b13(m_usenetJoinGroupCheck), bq1(m_usenetQuotaKindCombo), bq2(m_usenetQuotaSpin),
        bq3(m_usenetQuotaDaySpin), bq4(m_usenetQuotaFallThroughCheck);

    m_usenetNameEdit->setText(s.value(QStringLiteral("name")).toString());
    m_usenetHostEdit->setText(s.value(QStringLiteral("host")).toString());
    m_usenetPortSpin->setValue(int(s.value(QStringLiteral("port")).toInteger(kDefaultNntpTlsPort)));
    m_usenetTlsCombo->setCurrentIndex(
        int(s.value(QStringLiteral("tls")).toInteger(int(NntpTlsMode::Implicit))));
    m_usenetUserEdit->setText(s.value(QStringLiteral("user")).toString());
    m_usenetConnSpin->setValue(
        int(s.value(QStringLiteral("maxConnections")).toInteger(kDefaultMaxConnections)));
    m_usenetLevelSpin->setValue(int(s.value(QStringLiteral("level")).toInteger(0)));
    m_usenetRetentionSpin->setValue(int(s.value(QStringLiteral("retention")).toInteger(0)));
    m_usenetGroupSpin->setValue(int(s.value(QStringLiteral("group")).toInteger(0)));
    m_usenetCertCombo->setCurrentIndex(
        int(s.value(QStringLiteral("certVerification"))
                .toInteger(int(NntpCertVerification::Strict))));
    m_usenetEntryEnabledCheck->setChecked(s.value(QStringLiteral("enabled")).toBool(true));
    m_usenetOptionalCheck->setChecked(s.value(QStringLiteral("optional")).toBool(false));
    m_usenetJoinGroupCheck->setChecked(s.value(QStringLiteral("joinGroup")).toBool(false));
    m_usenetQuotaKindCombo->setCurrentIndex(
        int(s.value(QStringLiteral("quotaKind")).toInteger(int(NntpQuotaKind::None))));
    m_usenetQuotaSpin->setValue(
        double(s.value(QStringLiteral("quotaBytes")).toInteger(0)) / 1e9);
    m_usenetQuotaDaySpin->setValue(int(s.value(QStringLiteral("quotaResetDay")).toInteger(1)));
    m_usenetQuotaFallThroughCheck->setChecked(
        s.value(QStringLiteral("quotaFallThrough")).toBool(false));
    updateNewsServerUsageLabel();

    // The daemon sends `hasPassword`, never the password. Show that a secret is
    // stored without pretending to display it: typing here replaces it, leaving
    // it alone keeps it.
    const bool stored = s.value(QStringLiteral("password")).isString()
                        || s.value(QStringLiteral("hasPassword")).toBool(false);
    m_usenetPassEdit->setText(s.value(QStringLiteral("password")).toString());
    m_usenetPassEdit->setPlaceholderText(
        stored ? tr("(unchanged)") : tr("(none set)"));

    // Again, now that the combo holds *this* row's kind. The call at the top of
    // this function ran against the previous row's, and the fill above is under
    // a QSignalBlocker, so currentIndexChanged never fired to correct it — which
    // left the allowance fields greyed on a metered account.
    updateUsenetEnabledStates();
}

void OptionsDialog::applyNewsServerDetails()
{
    if (m_currentNewsServer < 0 || m_currentNewsServer >= m_newsServers.size())
        return;

    QCborMap s = m_newsServers.at(m_currentNewsServer);
    s.insert(QStringLiteral("name"), m_usenetNameEdit->text().trimmed());
    s.insert(QStringLiteral("host"), m_usenetHostEdit->text().trimmed());
    s.insert(QStringLiteral("port"), m_usenetPortSpin->value());
    s.insert(QStringLiteral("tls"), m_usenetTlsCombo->currentIndex());
    s.insert(QStringLiteral("user"), m_usenetUserEdit->text());
    s.insert(QStringLiteral("maxConnections"), m_usenetConnSpin->value());
    s.insert(QStringLiteral("level"), m_usenetLevelSpin->value());
    s.insert(QStringLiteral("retention"), m_usenetRetentionSpin->value());
    s.insert(QStringLiteral("group"), m_usenetGroupSpin->value());
    s.insert(QStringLiteral("certVerification"), m_usenetCertCombo->currentIndex());
    s.insert(QStringLiteral("enabled"), m_usenetEntryEnabledCheck->isChecked());
    s.insert(QStringLiteral("optional"), m_usenetOptionalCheck->isChecked());
    s.insert(QStringLiteral("joinGroup"), m_usenetJoinGroupCheck->isChecked());
    s.insert(QStringLiteral("quotaKind"), m_usenetQuotaKindCombo->currentIndex());
    // Decimal GB in, bytes out: an invoice says GB, and storing the typed figure
    // would make the unit part of the stored value.
    s.insert(QStringLiteral("quotaBytes"),
             qint64(m_usenetQuotaSpin->value() * 1e9 + 0.5));
    s.insert(QStringLiteral("quotaResetDay"), m_usenetQuotaDaySpin->value());
    s.insert(QStringLiteral("quotaFallThrough"), m_usenetQuotaFallThroughCheck->isChecked());

    // Only attach `password` when the user actually typed one. An absent field
    // means "keep the stored secret" -- which is the only way a GUI that was
    // never given the password can round-trip the list without erasing it.
    const QString typed = m_usenetPassEdit->text();
    if (!typed.isEmpty())
        s.insert(QStringLiteral("password"), typed);
    else
        s.remove(QStringLiteral("password"));

    m_newsServers[m_currentNewsServer] = s;
}

void OptionsDialog::updateNewsServerUsageLabel()
{
    if (!m_usenetUsageLabel)
        return;
    if (m_currentNewsServer < 0 || m_currentNewsServer >= m_newsServers.size()) {
        m_usenetUsageLabel->clear();
        return;
    }

    const QCborMap& s = m_newsServers.at(m_currentNewsServer);
    if (!s.contains(QStringLiteral("periodBytes"))) {
        // The engine is not running, so nothing has been measured to report.
        m_usenetUsageLabel->setText(tr("not measured — the Usenet engine is stopped"));
        return;
    }

    const qint64 used = s.value(QStringLiteral("periodBytes")).toInteger(0);
    const qint64 allowance = s.value(QStringLiteral("quotaBytes")).toInteger(0);
    const int kind = int(s.value(QStringLiteral("quotaKind")).toInteger(0));
    const QString resets = s.value(QStringLiteral("resetsOn")).toString();

    QString text;
    if (kind == int(NntpQuotaKind::None) || allowance <= 0)
        text = tr("%1 used").arg(formatQuotaGb(used));
    else
        text = tr("%1 of %2").arg(formatQuotaGb(used), formatQuotaGb(allowance));

    if (kind == int(NntpQuotaKind::Monthly) && !resets.isEmpty()) {
        // Short and local: the row is one line, and the daemon's ISO date is for
        // the wire, not for reading.
        const QDate when = QDate::fromString(resets, Qt::ISODate);
        text += tr(", resets %1").arg(when.isValid()
                                          ? QLocale::system().toString(when, QLocale::ShortFormat)
                                          : resets);
    }
    if (s.value(QStringLiteral("overQuota")).toBool(false))
        text += tr(" — spent");

    // Say whose number it is. Ours is the application-level inbound byte count,
    // so it reads a few percent under the provider's and a user comparing the
    // two would otherwise file it as a bug.
    m_usenetUsageLabel->setText(text);
    m_usenetUsageLabel->setToolTip(
        tr("Measured here, not reported by the provider — NNTP has no command "
           "that asks. Expect a few percent below your provider's own figure."));
}

void OptionsDialog::onCorrectNewsServerUsage()
{
    if (!m_ipc || m_currentNewsServer < 0 || m_currentNewsServer >= m_newsServers.size())
        return;

    const QCborMap& s = m_newsServers.at(m_currentNewsServer);
    const QString accountId = s.value(QStringLiteral("accountId")).toString();
    if (accountId.isEmpty())
        return;

    // Typing in the provider's own figure is strictly more useful than a reset
    // button, and it is the only recourse when a plan changes mid-period or a
    // block account is topped up. 0 is the reset.
    bool ok = false;
    const double current = double(s.value(QStringLiteral("periodBytes")).toInteger(0)) / 1e9;
    const double value = QInputDialog::getDouble(
        this, tr("Correct usage"),
        tr("Used this period, in GB.\n\nEnter what your provider's control panel "
           "says, or 0 to start again."),
        current, 0.0, 1000000.0, 1, &ok);
    if (!ok)
        return;

    Ipc::IpcMessage msg(Ipc::IpcMsgType::SetNewsServerUsage);
    msg.append(accountId);
    msg.append(qint64(value * 1e9 + 0.5));
    msg.append(qint64(-1));   // leave the all-time figure alone
    m_ipc->sendRequest(std::move(msg), [this, self = QPointer<OptionsDialog>(this)](const Ipc::IpcMessage& resp) {
        if (!self || !resp.isValid())
            return;
        if (!resp.fieldBool(0)) {
            QMessageBox::warning(this, tr("News servers"),
                                 resp.fieldString(1).isEmpty()
                                     ? tr("The usage counter could not be changed.")
                                     : resp.fieldString(1));
            return;
        }
        // Re-read rather than patch the working copy: the daemon may have rolled
        // the period over in the same breath.
        loadNewsServers();
    });
}

void OptionsDialog::addNewsServer()
{
    if (m_newsServers.size() >= Preferences::kMaxUsenetServers) {
        QMessageBox::information(this, tr("News servers"),
                                 tr("At most %1 news servers can be configured.")
                                     .arg(Preferences::kMaxUsenetServers));
        return;
    }

    applyNewsServerDetails();

    QCborMap fresh;
    fresh.insert(QStringLiteral("name"), tr("New server"));
    fresh.insert(QStringLiteral("host"), QString{});
    fresh.insert(QStringLiteral("port"), int(kDefaultNntpTlsPort));
    fresh.insert(QStringLiteral("tls"), int(NntpTlsMode::Implicit));
    fresh.insert(QStringLiteral("maxConnections"), kDefaultMaxConnections);
    fresh.insert(QStringLiteral("level"), 0);
    fresh.insert(QStringLiteral("certVerification"), int(NntpCertVerification::Strict));
    fresh.insert(QStringLiteral("enabled"), true);
    m_newsServers.append(fresh);

    // Leave m_currentNewsServer alone until the row exists: setting it first makes
    // refreshNewsServerTable() restore the new row under its own signal blocker, and
    // the select below then has nothing left to change.
    refreshNewsServerTable();
    selectNewsServer(int(m_newsServers.size()) - 1);
    m_usenetHostEdit->setFocus();
    markDirty();
}

void OptionsDialog::removeNewsServer()
{
    if (m_currentNewsServer < 0 || m_currentNewsServer >= m_newsServers.size())
        return;

    m_newsServers.removeAt(m_currentNewsServer);
    m_currentNewsServer = -1;
    refreshNewsServerTable();
    selectNewsServer(m_usenetServerTable->topLevelItemCount() > 0 ? 0 : -1);
    markDirty();
}

void OptionsDialog::testNewsServer()
{
    if (!m_ipc || m_currentNewsServer < 0)
        return;

    applyNewsServerDetails();
    const QCborMap server = m_newsServers.at(m_currentNewsServer);
    if (server.value(QStringLiteral("host")).toString().trimmed().isEmpty()) {
        m_usenetTestResult->setText(tr("Enter a host name first."));
        return;
    }

    m_usenetTestBtn->setEnabled(false);
    m_usenetTestResult->setText(tr("Connecting…"));
    m_usenetTestResult->setStyleSheet(QString{});

    Ipc::IpcMessage msg(Ipc::IpcMsgType::TestNewsServer);
    msg.append(server);
    m_ipc->sendRequest(std::move(msg), [this, self = QPointer<OptionsDialog>(this)](const Ipc::IpcMessage& resp) {
        if (!self)
            return;
        m_usenetTestBtn->setEnabled(m_currentNewsServer >= 0);
        if (!resp.isValid()) {
            m_usenetTestResult->clear();   // connection dropped: no verdict either way
            return;
        }
        if (!resp.fieldBool(0)) {
            m_usenetTestResult->setText(resp.fieldString(1));
            m_usenetTestResult->setStyleSheet(QStringLiteral("color: #c62828;"));
            revealTestResult(m_usenetTestResult);
            return;
        }
        const QCborArray result = resp.fieldArray(1);
        const bool ok = result.at(0).toBool(false);
        // The provider's own status line, verbatim: "481 Authentication failed"
        // tells the user which half of the form to fix; "connection failed"
        // does not.
        m_usenetTestResult->setText(result.at(1).toString());
        m_usenetTestResult->setStyleSheet(
            ok ? QStringLiteral("color: #2e7d32;") : QStringLiteral("color: #c62828;"));
        revealTestResult(m_usenetTestResult);
    });
}

// ---------------------------------------------------------------------------
// Indexers page — the shared newznab / torznab search accounts
//
// Its own page rather than a second list on the Usenet page, because the client
// is shared: newznab and torznab are the same API, Prowlarr and NZBHydra2 serve
// both from one endpoint, and a BitTorrent module would configure its indexers
// right here.
//
// Deliberately the same eight-function shape as the news-server list above. Both
// solve the problem of editing a list whose secrets the GUI is never given, and
// two answers to that would be one too many.
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createIndexersPage()
{
    auto* page = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    auto* intro = new QLabel(
        tr("Search indexers answer keyword searches and hand back an NZB. They are "
           "separate from your news servers: on Usenet the provider you download "
           "from and the service you search are different businesses."), page);
    intro->setWordWrap(true);
    mainLayout->addWidget(intro);

    // -- Account list -------------------------------------------------------
    auto* listGroup = new QGroupBox(tr("Indexers"), page);
    auto* listLayout = new QVBoxLayout(listGroup);

    auto* table = new ListTreeWidget(listGroup);
    m_indexerTable = table;
    m_indexerTable->setHeaderLabels({tr("Name"), tr("URL"), tr("Type"), tr("API key")});
    m_indexerTable->setRootIsDecorated(false);
    m_indexerTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_indexerTable->setColumnCount(4);
    m_indexerTable->header()->setStretchLastSection(true);
    table->bindColumns(QStringLiteral("optionsIndexers"), {130, 240, 80, 70});
    giveListRoom(m_indexerTable, 5, ListGrowth::Capped);
    listLayout->addWidget(m_indexerTable);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_indexerAddBtn = new QPushButton(tr("Add"), listGroup);
    m_indexerRemoveBtn = new QPushButton(tr("Remove"), listGroup);
    btnRow->addWidget(m_indexerAddBtn);
    btnRow->addWidget(m_indexerRemoveBtn);
    listLayout->addLayout(btnRow);
    mainLayout->addWidget(listGroup);

    // -- Details ------------------------------------------------------------
    auto* details = new QGroupBox(tr("Indexer"), page);
    auto* form = new QFormLayout(details);
    // See createUsenetPage(): the same two settings are what let the test result
    // label use the group's full width and grow a line when it wraps.
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    DialogSizing::enableHeightForWidth(details);

    m_indexerEntryEnabledCheck = new QCheckBox(tr("Enabled"), details);
    form->addRow(m_indexerEntryEnabledCheck);

    m_indexerNameEdit = new QLineEdit(details);
    m_indexerNameEdit->setPlaceholderText(tr("Display name"));
    m_indexerNameEdit->setToolTip(
        tr("Also the identity of this account: it names the cached capabilities "
           "and appears in the Indexer column of the results."));
    form->addRow(tr("Name:"), m_indexerNameEdit);

    m_indexerUrlEdit = new QLineEdit(details);
    m_indexerUrlEdit->setPlaceholderText(QStringLiteral("https://api.example.org/"));
    m_indexerUrlEdit->setToolTip(
        tr("The API base URL. A bare host gets \"/api\" added; a URL that already "
           "has a path is used exactly as typed, which is what Jackett and "
           "NZBHydra2 endpoints need."));
    form->addRow(tr("API URL:"), m_indexerUrlEdit);

    m_indexerApiKeyEdit = new QLineEdit(details);
    m_indexerApiKeyEdit->setEchoMode(QLineEdit::Password);
    form->addRow(tr("API key:"), m_indexerApiKeyEdit);

    m_indexerKindCombo = new QComboBox(details);
    // Order matches IndexerKind, so currentIndex() is the enum value.
    m_indexerKindCombo->addItem(tr("Newznab (Usenet)"));
    m_indexerKindCombo->addItem(tr("Torznab (BitTorrent)"));
    m_indexerKindCombo->addItem(tr("Both — Prowlarr, NZBHydra2"));
    form->addRow(tr("Type:"), m_indexerKindCombo);

    addTestRow(form, details, m_indexerTestBtn, m_indexerTestResult);

    mainLayout->addWidget(details);

    // -- Search settings ----------------------------------------------------
    auto* searchGroup = new QGroupBox(tr("Searching"), page);
    auto* searchForm = new QFormLayout(searchGroup);

    m_indexerLimitSpin = new QSpinBox(searchGroup);
    m_indexerLimitSpin->setRange(10, 1000);
    m_indexerLimitSpin->setToolTip(
        tr("Rows to ask for per request. An indexer that allows fewer silently "
           "returns fewer, so this is an upper bound rather than a promise."));
    searchForm->addRow(tr("Results per request:"), m_indexerLimitSpin);

    m_indexerPagesSpin = new QSpinBox(searchGroup);
    m_indexerPagesSpin->setRange(1, 20);
    m_indexerPagesSpin->setToolTip(
        tr("How many pages one search may fetch from each indexer.\n\n"
           "Every page is an API call against the allowance your account has, so "
           "this is a spending limit, not a speed setting."));
    searchForm->addRow(tr("Pages per search:"), m_indexerPagesSpin);

    m_indexerTimeoutSpin = new QSpinBox(searchGroup);
    m_indexerTimeoutSpin->setRange(5, 300);
    m_indexerTimeoutSpin->setSuffix(tr(" s"));
    searchForm->addRow(tr("Request timeout:"), m_indexerTimeoutSpin);

    m_indexerCapsRefreshSpin = new QSpinBox(searchGroup);
    m_indexerCapsRefreshSpin->setRange(1, 365);
    m_indexerCapsRefreshSpin->setSuffix(tr(" days"));
    m_indexerCapsRefreshSpin->setToolTip(
        tr("How often to re-read what each indexer supports. A stale answer never "
           "blocks a search — it only means a query field stays greyed out that "
           "the indexer has since started accepting."));
    searchForm->addRow(tr("Refresh capabilities every:"), m_indexerCapsRefreshSpin);

    mainLayout->addWidget(searchGroup);
    mainLayout->addStretch();

    // -- Wiring -------------------------------------------------------------
    connect(m_indexerAddBtn, &QPushButton::clicked, this, &OptionsDialog::addIndexer);
    connect(m_indexerRemoveBtn, &QPushButton::clicked, this, &OptionsDialog::removeIndexer);
    connect(m_indexerTestBtn, &QPushButton::clicked, this, &OptionsDialog::testIndexer);

    for (QSpinBox* box : {m_indexerLimitSpin, m_indexerPagesSpin,
                          m_indexerTimeoutSpin, m_indexerCapsRefreshSpin}) {
        connect(box, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    }

    connect(m_indexerTable, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                // Commit the row being left before showing the next one, or every
                // edit is lost the moment the selection moves.
                applyIndexerDetails();
                populateIndexerDetails(
                    current ? m_indexerTable->indexOfTopLevelItem(current) : -1);
            });

    const auto commit = [this] {
        applyIndexerDetails();
        updateIndexerRow(m_currentIndexer);
        markDirty();
    };
    connect(m_indexerNameEdit, &QLineEdit::editingFinished, this, commit);
    connect(m_indexerUrlEdit, &QLineEdit::editingFinished, this, commit);
    connect(m_indexerApiKeyEdit, &QLineEdit::editingFinished, this, commit);
    connect(m_indexerKindCombo, &QComboBox::currentIndexChanged, this,
            [commit](int) { commit(); });
    connect(m_indexerEntryEnabledCheck, &QCheckBox::toggled, this,
            [commit](bool) { commit(); });

    return page;
}

// ---------------------------------------------------------------------------
// Feeds page — saved searches, polled on a schedule
// ---------------------------------------------------------------------------

QWidget* OptionsDialog::createFeedsPage()
{
    auto* page = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    auto* intro = new QLabel(
        tr("A feed is a search that runs on its own and queues what it finds. Its "
           "first check adds nothing — it only records what the indexer already "
           "lists, because otherwise a new feed would download everything still "
           "on the server."), page);
    intro->setWordWrap(true);
    mainLayout->addWidget(intro);

    // -- Feed list ----------------------------------------------------------
    auto* listGroup = new QGroupBox(tr("Feeds"), page);
    auto* listLayout = new QVBoxLayout(listGroup);

    auto* table = new ListTreeWidget(listGroup);
    m_feedTable = table;
    m_feedTable->setHeaderLabels({tr("Name"), tr("Search"), tr("Every"), tr("Last checked")});
    m_feedTable->setRootIsDecorated(false);
    m_feedTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_feedTable->setColumnCount(4);
    m_feedTable->header()->setStretchLastSection(true);
    table->bindColumns(QStringLiteral("optionsFeeds"), {130, 200, 70, 140});
    giveListRoom(m_feedTable, 5, ListGrowth::Capped);
    listLayout->addWidget(m_feedTable);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_feedAddBtn = new QPushButton(tr("Add"), listGroup);
    m_feedRemoveBtn = new QPushButton(tr("Remove"), listGroup);
    btnRow->addWidget(m_feedAddBtn);
    btnRow->addWidget(m_feedRemoveBtn);
    listLayout->addLayout(btnRow);
    mainLayout->addWidget(listGroup);

    // -- Details ------------------------------------------------------------
    auto* details = new QGroupBox(tr("Feed"), page);
    auto* form = new QFormLayout(details);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    DialogSizing::enableHeightForWidth(details);

    m_feedEnabledCheck = new QCheckBox(tr("Enabled"), details);
    form->addRow(m_feedEnabledCheck);

    m_feedNameEdit = new QLineEdit(details);
    m_feedNameEdit->setPlaceholderText(tr("Display name"));
    m_feedNameEdit->setToolTip(
        tr("Also the identity of this feed: it names the file that remembers what "
           "the feed has already seen."));
    form->addRow(tr("Name:"), m_feedNameEdit);

    m_feedKindCombo = new QComboBox(details);
    // Order matches IndexerFeedKind, so currentIndex() is the enum value.
    m_feedKindCombo->addItem(tr("Search my indexers"));
    m_feedKindCombo->addItem(tr("An RSS address I paste"));
    form->addRow(tr("Source:"), m_feedKindCombo);

    m_feedQueryEdit = new QLineEdit(details);
    m_feedQueryEdit->setToolTip(
        tr("Keywords. Leave it empty to take everything new in the categories "
           "below."));
    form->addRow(tr("Search for:"), m_feedQueryEdit);

    m_feedCategoriesEdit = new QLineEdit(details);
    m_feedCategoriesEdit->setPlaceholderText(tr("e.g. 2000, 5000"));
    m_feedCategoriesEdit->setToolTip(
        tr("Newznab category numbers, separated by commas. Empty means every "
           "category."));
    form->addRow(tr("Categories:"), m_feedCategoriesEdit);

    m_feedIndexersEdit = new QLineEdit(details);
    m_feedIndexersEdit->setToolTip(
        tr("Which indexers to ask, by name and separated by commas. Empty means "
           "all of them.\n\nAdding one later does not fetch its back catalogue: "
           "a new indexer gets its own first check, which adds nothing."));
    form->addRow(tr("Indexers:"), m_feedIndexersEdit);

    m_feedUrlEdit = new QLineEdit(details);
    m_feedUrlEdit->setPlaceholderText(QStringLiteral("https://indexer.example/rss?..."));
    m_feedUrlEdit->setToolTip(
        tr("The RSS address from your indexer's website. It contains your API "
           "key, so it is stored encrypted and is only ever shown back to you "
           "with the key hidden."));
    form->addRow(tr("Feed URL:"), m_feedUrlEdit);

    m_feedAcceptEdit = new QLineEdit(details);
    m_feedAcceptEdit->setToolTip(
        tr("Only queue releases whose name matches this pattern. Empty accepts "
           "everything."));
    form->addRow(tr("Must match:"), m_feedAcceptEdit);

    m_feedRejectEdit = new QLineEdit(details);
    m_feedRejectEdit->setToolTip(
        tr("Never queue a release whose name matches this pattern. It wins over "
           "the one above."));
    form->addRow(tr("Must not match:"), m_feedRejectEdit);

    m_feedMinSizeSpin = new QSpinBox(details);
    m_feedMinSizeSpin->setRange(0, 1024 * 1024);
    m_feedMinSizeSpin->setSuffix(tr(" MB"));
    m_feedMinSizeSpin->setSpecialValueText(tr("no minimum"));
    fitSpecialValue(m_feedMinSizeSpin);
    form->addRow(tr("Smallest:"), m_feedMinSizeSpin);

    m_feedMaxSizeSpin = new QSpinBox(details);
    m_feedMaxSizeSpin->setRange(0, 1024 * 1024);
    m_feedMaxSizeSpin->setSuffix(tr(" MB"));
    m_feedMaxSizeSpin->setSpecialValueText(tr("no maximum"));
    fitSpecialValue(m_feedMaxSizeSpin);
    form->addRow(tr("Largest:"), m_feedMaxSizeSpin);

    m_feedMaxAgeSpin = new QSpinBox(details);
    m_feedMaxAgeSpin->setRange(0, 3650);
    m_feedMaxAgeSpin->setSuffix(tr(" days"));
    m_feedMaxAgeSpin->setSpecialValueText(tr("any age"));
    fitSpecialValue(m_feedMaxAgeSpin);
    form->addRow(tr("Posted within:"), m_feedMaxAgeSpin);

    m_feedIntervalSpin = new QSpinBox(details);
    m_feedIntervalSpin->setRange(IndexerFeed::kMinIntervalMinutes, 10080);
    m_feedIntervalSpin->setSuffix(tr(" minutes"));
    m_feedIntervalSpin->setToolTip(
        tr("How often to check. Fifteen minutes is the floor: most indexers ask "
           "for no more than that, and checking harder gets an account "
           "suspended."));
    form->addRow(tr("Check every:"), m_feedIntervalSpin);

    // The download category everything this feed queues lands in. Named in full
    // in the UI too: "Categories" three rows up is the *indexer's* category ids.
    m_feedDownloadCategoryCombo = new QComboBox(details);
    m_feedDownloadCategoryCombo->addItem(tr("No category"), 0);
    m_feedDownloadCategoryCombo->setToolTip(
        tr("Which download category this feed's matches go into. The category "
           "decides the folder they finish in, and it is resolved when a release "
           "completes — so repointing the category moves what is still running "
           "with it."));
    form->addRow(tr("Download category:"), m_feedDownloadCategoryCombo);

    m_feedGrabExistingCheck = new QCheckBox(tr("Queue what it already lists"), details);
    m_feedGrabExistingCheck->setToolTip(
        tr("Normally a feed's first check only takes note of what is there and "
           "queues nothing, because everything an indexer still holds is new to "
           "a feed that has never run. Turn this on to take the back catalogue "
           "as well — it can be a great deal of it."));
    form->addRow(m_feedGrabExistingCheck);

    auto* checkRow = new QHBoxLayout;
    m_feedCheckBtn = new QPushButton(tr("Check now"), details);
    checkRow->addWidget(m_feedCheckBtn);
    m_feedStatusLabel = new QLabel(details);
    m_feedStatusLabel->setWordWrap(true);
    checkRow->addWidget(m_feedStatusLabel, 1);
    form->addRow(QString(), checkRow);

    mainLayout->addWidget(details);
    mainLayout->addStretch();

    connect(m_feedTable, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        applyFeedDetails();
        selectFeed(current ? m_feedTable->indexOfTopLevelItem(current) : -1);
    });
    connect(m_feedAddBtn, &QPushButton::clicked, this, &OptionsDialog::addFeed);
    connect(m_feedRemoveBtn, &QPushButton::clicked, this, &OptionsDialog::removeFeed);
    connect(m_feedCheckBtn, &QPushButton::clicked, this, &OptionsDialog::checkFeedNow);

    connect(m_feedKindCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        // A URL feed carries its own query; a saved search builds one.
        const bool have = m_currentFeed >= 0 && m_currentFeed < m_feeds.size();
        const bool isUrl = index == 1;
        m_feedUrlEdit->setEnabled(have && isUrl);
        m_feedQueryEdit->setEnabled(have && !isUrl);
        m_feedCategoriesEdit->setEnabled(have && !isUrl);
        m_feedIndexersEdit->setEnabled(have && !isUrl);
        markDirty();
    });

    for (QLineEdit* edit : {m_feedNameEdit, m_feedQueryEdit, m_feedCategoriesEdit,
                            m_feedIndexersEdit, m_feedUrlEdit, m_feedAcceptEdit,
                            m_feedRejectEdit}) {
        connect(edit, &QLineEdit::textEdited, this, &OptionsDialog::markDirty);
    }
    for (QSpinBox* spin : {m_feedMinSizeSpin, m_feedMaxSizeSpin, m_feedMaxAgeSpin,
                           m_feedIntervalSpin}) {
        connect(spin, &QSpinBox::valueChanged, this, &OptionsDialog::markDirty);
    }
    for (QCheckBox* check : {m_feedEnabledCheck, m_feedGrabExistingCheck})
        connect(check, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_feedDownloadCategoryCombo, &QComboBox::currentIndexChanged, this,
            &OptionsDialog::markDirty);

    loadDownloadCategories();

    // Start in the nothing-selected state. loadFeeds() only reaches selectFeed()
    // once the daemon answers, and until then an enabled-looking form invites
    // typing into fields that belong to no feed.
    selectFeed(-1);

    return page;
}

void OptionsDialog::loadIndexers()
{
    if (!m_ipc)
        return;

    m_ipc->sendRequest(Ipc::IpcMessage(Ipc::IpcMsgType::GetIndexers),
                       [this](const Ipc::IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;

        m_indexers.clear();
        const QCborArray rows = resp.fieldArray(1);
        m_indexers.reserve(int(rows.size()));
        for (const auto& row : rows) {
            if (row.isMap())
                m_indexers.append(row.toMap());
        }

        m_currentIndexer = -1;
        refreshIndexerTable();
        selectIndexer(m_indexers.isEmpty() ? -1 : 0);
    });
}

void OptionsDialog::saveIndexers()
{
    if (!m_ipc)
        return;

    applyIndexerDetails();

    QCborArray rows;
    for (const QCborMap& indexer : std::as_const(m_indexers))
        rows.append(indexer);

    Ipc::IpcMessage msg(Ipc::IpcMsgType::SetIndexers);
    msg.append(rows);
    // Same as saveNewsServers(): the dialog is usually gone by the reply.
    m_ipc->sendRequest(std::move(msg), [self = QPointer<OptionsDialog>(this)](const Ipc::IpcMessage& resp) {
        if (resp.fieldBool(0) || !resp.isValid())
            return;
        QMessageBox::warning(self, tr("Indexers"),
                             resp.fieldString(1).isEmpty()
                                 ? tr("The indexer list could not be saved.")
                                 : resp.fieldString(1));
    });
}

void OptionsDialog::loadDownloadCategories()
{
    if (!m_ipc || !m_feedDownloadCategoryCombo)
        return;

    m_ipc->sendRequest(
        Ipc::IpcMessage(Ipc::IpcMsgType::GetCategories),
        [this, self = QPointer<OptionsDialog>(this)](const Ipc::IpcMessage& resp) {
            if (!self || !m_feedDownloadCategoryCombo || !resp.fieldBool(0))
                return;

            // Remember the selection by category index, not by row: the list can
            // have been re-ordered between the two.
            const int previous = m_feedDownloadCategoryCombo->currentData().toInt();

            const QSignalBlocker blocker(m_feedDownloadCategoryCombo);
            m_feedDownloadCategoryCombo->clear();
            m_feedDownloadCategoryCombo->addItem(tr("No category"), 0);

            int i = 0;
            for (const auto& value : resp.fieldArray(1)) {
                // Index 0 is the implicit "All", which is not a category a feed
                // files into — it is the absence of one, already offered above.
                if (i++ == 0 || !value.isMap())
                    continue;
                const QString title = value.toMap().value(QStringLiteral("title")).toString();
                m_feedDownloadCategoryCombo->addItem(
                    title.isEmpty() ? QStringLiteral("?") : title, i - 1);
            }

            const int row = m_feedDownloadCategoryCombo->findData(previous);
            m_feedDownloadCategoryCombo->setCurrentIndex(row >= 0 ? row : 0);
        });
}

void OptionsDialog::loadFeeds()
{
    if (!m_ipc)
        return;

    m_ipc->sendRequest(Ipc::IpcMessage(Ipc::IpcMsgType::GetIndexerFeeds),
                       [this](const Ipc::IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;

        m_feeds.clear();
        const QCborArray rows = resp.fieldArray(1);
        m_feeds.reserve(int(rows.size()));
        for (const auto& row : rows) {
            if (!row.isMap())
                continue;
            QCborMap feed = row.toMap();
            // The URL arrives redacted, and `url` doubles as "the new one the
            // user typed". Keep the redacted text under its own key so the field
            // can show it without applyFeedDetails() mistaking it for an edit —
            // and so an untouched feed does not appear to lose its address.
            feed.insert(QStringLiteral("displayUrl"), feed.value(QStringLiteral("url")));
            feed.remove(QStringLiteral("url"));
            m_feeds.append(feed);
        }

        m_currentFeed = -1;
        refreshFeedTable();
        selectFeed(m_feeds.isEmpty() ? -1 : 0);
    });
}

void OptionsDialog::saveFeeds()
{
    if (!m_ipc)
        return;

    applyFeedDetails();

    QCborArray rows;
    for (const QCborMap& feed : std::as_const(m_feeds)) {
        QCborMap row = feed;
        row.remove(QStringLiteral("displayUrl"));   // ours, not the wire's
        rows.append(row);
    }

    Ipc::IpcMessage msg(Ipc::IpcMsgType::SetIndexerFeeds);
    msg.append(rows);
    // Same as saveNewsServers(): the dialog is usually gone by the reply.
    m_ipc->sendRequest(std::move(msg), [self = QPointer<OptionsDialog>(this)](const Ipc::IpcMessage& resp) {
        if (resp.fieldBool(0) || !resp.isValid())
            return;
        QMessageBox::warning(self, tr("Feeds"),
                             resp.fieldString(1).isEmpty()
                                 ? tr("The feed list could not be saved.")
                                 : resp.fieldString(1));
    });
}

void OptionsDialog::refreshFeedTable()
{
    if (!m_feedTable)
        return;

    const QSignalBlocker block(m_feedTable);
    const int keep = m_currentFeed;
    m_feedTable->clear();

    for (int i = 0; i < m_feeds.size(); ++i) {
        new QTreeWidgetItem(m_feedTable);
        updateFeedRow(i);
    }

    if (keep >= 0 && keep < m_feeds.size())
        m_feedTable->setCurrentItem(m_feedTable->topLevelItem(keep));
}

void OptionsDialog::updateFeedRow(int index)
{
    if (!m_feedTable || index < 0 || index >= m_feeds.size())
        return;
    QTreeWidgetItem* item = m_feedTable->topLevelItem(index);
    if (item == nullptr)
        return;

    const QCborMap& feed = m_feeds.at(index);
    const bool isUrl = feed.value(QStringLiteral("kind")).toString() == QLatin1String("url");

    item->setText(0, feed.value(QStringLiteral("name")).toString());
    item->setText(1, isUrl ? feed.value(QStringLiteral("displayUrl")).toString()
                           : feed.value(QStringLiteral("query")).toString());
    item->setText(2, tr("%1 min")
                         .arg(feed.value(QStringLiteral("intervalMinutes")).toInteger(30)));

    const qint64 polled = feed.value(QStringLiteral("lastPolled")).toInteger(0);
    const QString error = feed.value(QStringLiteral("lastError")).toString();
    if (feed.value(QStringLiteral("polling")).toBool())
        item->setText(3, tr("checking…"));
    else if (!error.isEmpty())
        item->setText(3, error);
    else if (polled > 0)
        item->setText(3, QDateTime::fromSecsSinceEpoch(polled).toString(Qt::TextDate));
    else
        item->setText(3, tr("never"));

    // Greyed rather than hidden: a disabled feed still has history worth seeing.
    const bool enabled = feed.value(QStringLiteral("enabled")).toBool(true);
    for (int col = 0; col < m_feedTable->columnCount(); ++col)
        item->setForeground(col, enabled ? QBrush() : QBrush(Qt::gray));
}

void OptionsDialog::selectFeed(int index)
{
    m_currentFeed = index;
    populateFeedDetails(index);

    const bool have = index >= 0 && index < m_feeds.size();
    if (m_feedRemoveBtn)
        m_feedRemoveBtn->setEnabled(have);
    if (m_feedCheckBtn)
        m_feedCheckBtn->setEnabled(have);
}

void OptionsDialog::populateFeedDetails(int index)
{
    if (!m_feedNameEdit)
        return;

    const bool have = index >= 0 && index < m_feeds.size();
    const QCborMap feed = have ? m_feeds.at(index) : QCborMap{};

    const QSignalBlocker b1(m_feedNameEdit);
    const QSignalBlocker b2(m_feedKindCombo);
    const QSignalBlocker b3(m_feedQueryEdit);
    const QSignalBlocker b4(m_feedCategoriesEdit);
    const QSignalBlocker b5(m_feedIndexersEdit);
    const QSignalBlocker b6(m_feedUrlEdit);
    const QSignalBlocker b7(m_feedAcceptEdit);
    const QSignalBlocker b8(m_feedRejectEdit);
    const QSignalBlocker b9(m_feedMinSizeSpin);
    const QSignalBlocker b10(m_feedMaxSizeSpin);
    const QSignalBlocker b11(m_feedMaxAgeSpin);
    const QSignalBlocker b12(m_feedIntervalSpin);
    const QSignalBlocker b13(m_feedEnabledCheck);
    const QSignalBlocker b14(m_feedGrabExistingCheck);
    const QSignalBlocker b15(m_feedDownloadCategoryCombo);

    m_feedNameEdit->setText(feed.value(QStringLiteral("name")).toString());
    const bool isUrl = feed.value(QStringLiteral("kind")).toString() == QLatin1String("url");
    m_feedKindCombo->setCurrentIndex(isUrl ? 1 : 0);
    m_feedQueryEdit->setText(feed.value(QStringLiteral("query")).toString());

    QStringList categories;
    for (const auto& cat : feed.value(QStringLiteral("categories")).toArray())
        categories.append(QString::number(cat.toInteger(0)));
    m_feedCategoriesEdit->setText(categories.join(QStringLiteral(", ")));

    QStringList indexers;
    for (const auto& name : feed.value(QStringLiteral("indexers")).toArray())
        indexers.append(name.toString());
    m_feedIndexersEdit->setText(indexers.join(QStringLiteral(", ")));

    // Arrives redacted, and is sent back only when the user retypes it — the
    // same round-trip rule the indexer API key uses, because it *is* one.
    m_feedUrlEdit->setText(feed.value(QStringLiteral("displayUrl")).toString());
    m_feedAcceptEdit->setText(feed.value(QStringLiteral("accept")).toString());
    m_feedRejectEdit->setText(feed.value(QStringLiteral("reject")).toString());
    m_feedMinSizeSpin->setValue(
        int(feed.value(QStringLiteral("minSize")).toInteger(0) / (1024 * 1024)));
    m_feedMaxSizeSpin->setValue(
        int(feed.value(QStringLiteral("maxSize")).toInteger(0) / (1024 * 1024)));
    m_feedMaxAgeSpin->setValue(int(feed.value(QStringLiteral("maxAgeDays")).toInteger(0)));
    m_feedIntervalSpin->setValue(
        int(feed.value(QStringLiteral("intervalMinutes")).toInteger(30)));
    m_feedEnabledCheck->setChecked(feed.value(QStringLiteral("enabled")).toBool(true));
    m_feedGrabExistingCheck->setChecked(
        feed.value(QStringLiteral("grabExisting")).toBool(false));

    // findData, not an index: the combo holds category *indices* as item data,
    // and a feed can name one the list no longer has. -1 then falls back to
    // "No category" rather than silently selecting whoever took the slot.
    const int wantCat = int(feed.value(QStringLiteral("downloadCategory")).toInteger(0));
    const int catRow = m_feedDownloadCategoryCombo->findData(wantCat);
    m_feedDownloadCategoryCombo->setCurrentIndex(catRow >= 0 ? catRow : 0);

    // The kind decides which half of the form applies — but with nothing
    // selected neither does, so `have` gates both.
    m_feedUrlEdit->setEnabled(have && isUrl);
    m_feedQueryEdit->setEnabled(have && !isUrl);
    m_feedCategoriesEdit->setEnabled(have && !isUrl);
    m_feedIndexersEdit->setEnabled(have && !isUrl);

    for (QWidget* w : {static_cast<QWidget*>(m_feedNameEdit),
                       static_cast<QWidget*>(m_feedKindCombo),
                       static_cast<QWidget*>(m_feedAcceptEdit),
                       static_cast<QWidget*>(m_feedRejectEdit),
                       static_cast<QWidget*>(m_feedMinSizeSpin),
                       static_cast<QWidget*>(m_feedMaxSizeSpin),
                       static_cast<QWidget*>(m_feedMaxAgeSpin),
                       static_cast<QWidget*>(m_feedIntervalSpin),
                       static_cast<QWidget*>(m_feedEnabledCheck),
                       static_cast<QWidget*>(m_feedGrabExistingCheck),
                       static_cast<QWidget*>(m_feedDownloadCategoryCombo)}) {
        w->setEnabled(have);
    }

    if (m_feedStatusLabel) {
        const qint64 matched = feed.value(QStringLiteral("lastMatched")).toInteger(0);
        const qint64 seen = feed.value(QStringLiteral("seenCount")).toInteger(0);
        m_feedStatusLabel->setText(
            have ? tr("%1 queued on the last check; %2 releases remembered.")
                       .arg(matched).arg(seen)
                 : QString());
    }
}

void OptionsDialog::applyFeedDetails()
{
    if (m_currentFeed < 0 || m_currentFeed >= m_feeds.size() || !m_feedNameEdit)
        return;

    QCborMap feed = m_feeds.at(m_currentFeed);
    feed.insert(QStringLiteral("name"), m_feedNameEdit->text().trimmed());
    feed.insert(QStringLiteral("kind"),
                m_feedKindCombo->currentIndex() == 1 ? QStringLiteral("url")
                                                     : QStringLiteral("search"));
    feed.insert(QStringLiteral("query"), m_feedQueryEdit->text().trimmed());

    QCborArray categories;
    const auto catParts = m_feedCategoriesEdit->text().split(QLatin1Char(','),
                                                             Qt::SkipEmptyParts);
    for (const QString& part : catParts) {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (ok)
            categories.append(value);
    }
    feed.insert(QStringLiteral("categories"), categories);

    QCborArray indexers;
    const auto nameParts = m_feedIndexersEdit->text().split(QLatin1Char(','),
                                                            Qt::SkipEmptyParts);
    for (const QString& part : nameParts) {
        if (!part.trimmed().isEmpty())
            indexers.append(part.trimmed());
    }
    feed.insert(QStringLiteral("indexers"), indexers);

    // Only sent when the user actually typed one. SetIndexerFeeds reads a
    // *missing* url as "keep the stored one", which is what lets the dialog
    // round-trip a feed whose address it was only ever shown redacted.
    const QString typedUrl = m_feedUrlEdit->text().trimmed();
    if (typedUrl != feed.value(QStringLiteral("displayUrl")).toString()) {
        feed.insert(QStringLiteral("url"), typedUrl);
        feed.insert(QStringLiteral("displayUrl"), typedUrl);
    } else {
        feed.remove(QStringLiteral("url"));
    }

    feed.insert(QStringLiteral("accept"), m_feedAcceptEdit->text());
    feed.insert(QStringLiteral("reject"), m_feedRejectEdit->text());
    feed.insert(QStringLiteral("minSize"),
                qint64(m_feedMinSizeSpin->value()) * 1024 * 1024);
    feed.insert(QStringLiteral("maxSize"),
                qint64(m_feedMaxSizeSpin->value()) * 1024 * 1024);
    feed.insert(QStringLiteral("maxAgeDays"), m_feedMaxAgeSpin->value());
    feed.insert(QStringLiteral("intervalMinutes"), m_feedIntervalSpin->value());
    feed.insert(QStringLiteral("enabled"), m_feedEnabledCheck->isChecked());
    feed.insert(QStringLiteral("grabExisting"), m_feedGrabExistingCheck->isChecked());
    feed.insert(QStringLiteral("downloadCategory"),
                m_feedDownloadCategoryCombo->currentData().toInt());

    m_feeds[m_currentFeed] = feed;
    updateFeedRow(m_currentFeed);
}

void OptionsDialog::addFeed()
{
    applyFeedDetails();

    QCborMap feed;
    feed.insert(QStringLiteral("name"), tr("New feed"));
    feed.insert(QStringLiteral("kind"), QStringLiteral("search"));
    feed.insert(QStringLiteral("enabled"), true);
    feed.insert(QStringLiteral("intervalMinutes"), 30);
    // Off, and the whole safety argument rests on it staying that way by
    // default: everything an indexer still lists is new to a feed that has never
    // run.
    feed.insert(QStringLiteral("grabExisting"), false);
    m_feeds.append(feed);

    refreshFeedTable();
    selectFeed(int(m_feeds.size()) - 1);
    if (m_feedNameEdit) {
        m_feedNameEdit->setFocus();
        m_feedNameEdit->selectAll();
    }
    markDirty();
}

void OptionsDialog::removeFeed()
{
    if (m_currentFeed < 0 || m_currentFeed >= m_feeds.size())
        return;

    m_feeds.removeAt(m_currentFeed);
    const int next = qMin(m_currentFeed, int(m_feeds.size()) - 1);
    m_currentFeed = -1;
    refreshFeedTable();
    selectFeed(next);
    markDirty();
}

void OptionsDialog::checkFeedNow()
{
    if (!m_ipc || m_currentFeed < 0 || m_currentFeed >= m_feeds.size())
        return;

    // The daemon polls the feed it has stored, so an edit that is still only in
    // this dialog would be checked in its old form. Save first.
    saveFeeds();

    const QString name = m_feeds.at(m_currentFeed).value(QStringLiteral("name")).toString();
    Ipc::IpcMessage msg(Ipc::IpcMsgType::PollIndexerFeedNow);
    msg.append(name);
    m_ipc->sendRequest(std::move(msg), [this, self = QPointer<OptionsDialog>(this), name](const Ipc::IpcMessage& resp) {
        if (!self || !resp.isValid())
            return;
        if (!resp.fieldBool(0)) {
            QMessageBox::warning(this, tr("Feeds"),
                                 resp.fieldString(1).isEmpty()
                                     ? tr("\"%1\" could not be checked.").arg(name)
                                     : resp.fieldString(1));
            return;
        }
        if (m_feedStatusLabel)
            m_feedStatusLabel->setText(tr("Checking \"%1\"…").arg(name));
    });
}

void OptionsDialog::applyFeedStatus(const QCborMap& status)
{
    const QString name = status.value(QStringLiteral("name")).toString();
    for (int i = 0; i < m_feeds.size(); ++i) {
        if (m_feeds.at(i).value(QStringLiteral("name")).toString() != name)
            continue;

        QCborMap feed = m_feeds.at(i);
        for (const auto& key : {QStringLiteral("lastPolled"), QStringLiteral("lastError"),
                                QStringLiteral("lastMatched"), QStringLiteral("seenCount"),
                                QStringLiteral("polling")}) {
            feed.insert(key, status.value(key));
        }
        m_feeds[i] = feed;
        updateFeedRow(i);
        if (i == m_currentFeed)
            populateFeedDetails(i);
        return;
    }
}

void OptionsDialog::refreshIndexerTable()
{
    if (!m_indexerTable)
        return;

    const QSignalBlocker block(m_indexerTable);
    const int keep = m_currentIndexer;
    m_indexerTable->clear();

    for (int i = 0; i < m_indexers.size(); ++i) {
        new QTreeWidgetItem(m_indexerTable);
        updateIndexerRow(i);
    }

    if (keep >= 0 && keep < m_indexerTable->topLevelItemCount())
        m_indexerTable->setCurrentItem(m_indexerTable->topLevelItem(keep));
}

void OptionsDialog::updateIndexerRow(int index)
{
    if (!m_indexerTable || index < 0 || index >= m_indexers.size()
        || index >= m_indexerTable->topLevelItemCount())
        return;

    QTreeWidgetItem* item = m_indexerTable->topLevelItem(index);
    const QCborMap& ix = m_indexers.at(index);

    static const char* kindNames[] = {
        QT_TR_NOOP("Newznab"), QT_TR_NOOP("Torznab"), QT_TR_NOOP("Both")
    };
    const auto kind = int(ix.value(QStringLiteral("kind")).toInteger(0));

    item->setText(0, ix.value(QStringLiteral("name")).toString());
    item->setText(1, ix.value(QStringLiteral("url")).toString());
    item->setText(2, tr(kindNames[(kind >= 0 && kind <= 2) ? kind : 0]));
    // Whether a key is stored, never the key. The daemon does not send it and the
    // GUI has nowhere to get one.
    item->setText(3, ix.value(QStringLiteral("hasApiKey")).toBool(false) ? tr("yes")
                                                                        : tr("no"));

    const bool on = ix.value(QStringLiteral("enabled")).toBool(true);
    for (int c = 0; c < m_indexerTable->columnCount(); ++c) {
        if (on)
            item->setData(c, Qt::ForegroundRole, QVariant());
        else
            item->setForeground(c, palette().brush(QPalette::Disabled, QPalette::Text));
    }
}

void OptionsDialog::selectIndexer(int index)
{
    if (index < 0 || index >= m_indexerTable->topLevelItemCount())
        index = -1;
    {
        const QSignalBlocker block(m_indexerTable);
        m_indexerTable->setCurrentItem(
            index >= 0 ? m_indexerTable->topLevelItem(index) : nullptr);
    }
    populateIndexerDetails(index);
}

void OptionsDialog::populateIndexerDetails(int index)
{
    m_currentIndexer = (index >= 0 && index < m_indexers.size()) ? index : -1;

    if (m_indexerTestResult) {
        m_indexerTestResult->clear();
        m_indexerTestResult->setStyleSheet(QString{});
    }

    const bool have = m_currentIndexer >= 0;
    for (QWidget* w : {static_cast<QWidget*>(m_indexerNameEdit),
                       static_cast<QWidget*>(m_indexerUrlEdit),
                       static_cast<QWidget*>(m_indexerApiKeyEdit),
                       static_cast<QWidget*>(m_indexerKindCombo),
                       static_cast<QWidget*>(m_indexerEntryEnabledCheck),
                       static_cast<QWidget*>(m_indexerTestBtn),
                       static_cast<QWidget*>(m_indexerRemoveBtn)}) {
        w->setEnabled(have);
    }

    const QSignalBlocker b1(m_indexerNameEdit), b2(m_indexerUrlEdit),
        b3(m_indexerApiKeyEdit), b4(m_indexerKindCombo), b5(m_indexerEntryEnabledCheck);

    if (!have) {
        m_indexerNameEdit->clear();
        m_indexerUrlEdit->clear();
        m_indexerApiKeyEdit->clear();
        m_indexerApiKeyEdit->setPlaceholderText(QString{});
        m_indexerKindCombo->setCurrentIndex(0);
        m_indexerEntryEnabledCheck->setChecked(false);
        return;
    }

    const QCborMap& ix = m_indexers.at(m_currentIndexer);
    m_indexerNameEdit->setText(ix.value(QStringLiteral("name")).toString());
    m_indexerUrlEdit->setText(ix.value(QStringLiteral("url")).toString());
    m_indexerKindCombo->setCurrentIndex(int(ix.value(QStringLiteral("kind")).toInteger(0)));
    m_indexerEntryEnabledCheck->setChecked(ix.value(QStringLiteral("enabled")).toBool(true));

    // The field starts empty even when a key is stored, because the daemon never
    // sends it. The placeholder says so — otherwise an empty box reads as "no key
    // configured" and the user retypes one they already have.
    m_indexerApiKeyEdit->setText(ix.value(QStringLiteral("apiKey")).toString());
    m_indexerApiKeyEdit->setPlaceholderText(
        ix.value(QStringLiteral("hasApiKey")).toBool(false)
            ? tr("(a key is stored — leave empty to keep it)")
            : QString{});
}

void OptionsDialog::applyIndexerDetails()
{
    if (m_currentIndexer < 0 || m_currentIndexer >= m_indexers.size())
        return;

    QCborMap ix = m_indexers.at(m_currentIndexer);
    ix.insert(QStringLiteral("name"), m_indexerNameEdit->text().trimmed());
    ix.insert(QStringLiteral("url"), m_indexerUrlEdit->text().trimmed());
    ix.insert(QStringLiteral("kind"), m_indexerKindCombo->currentIndex());
    ix.insert(QStringLiteral("enabled"), m_indexerEntryEnabledCheck->isChecked());

    // An empty field means "keep the stored key", so the field is only written
    // when the user actually typed one. SetIndexers reads a *missing* apiKey the
    // same way — sending an empty string would erase the key instead.
    const QString typed = m_indexerApiKeyEdit->text();
    if (!typed.isEmpty()) {
        ix.insert(QStringLiteral("apiKey"), typed);
        ix.insert(QStringLiteral("hasApiKey"), true);
    } else {
        ix.remove(QStringLiteral("apiKey"));
    }

    m_indexers[m_currentIndexer] = ix;
}

void OptionsDialog::addIndexer()
{
    if (m_indexers.size() >= Preferences::kMaxIndexers) {
        QMessageBox::information(this, tr("Indexers"),
                                 tr("At most %1 indexers can be configured.")
                                     .arg(Preferences::kMaxIndexers));
        return;
    }

    applyIndexerDetails();

    QCborMap fresh;
    fresh.insert(QStringLiteral("name"), tr("New indexer"));
    fresh.insert(QStringLiteral("url"), QString{});
    fresh.insert(QStringLiteral("kind"), int(IndexerKind::Newznab));
    fresh.insert(QStringLiteral("enabled"), true);
    fresh.insert(QStringLiteral("hasApiKey"), false);
    m_indexers.append(fresh);

    refreshIndexerTable();
    selectIndexer(int(m_indexers.size()) - 1);
    m_indexerUrlEdit->setFocus();
    markDirty();
}

void OptionsDialog::removeIndexer()
{
    if (m_currentIndexer < 0 || m_currentIndexer >= m_indexers.size())
        return;

    m_indexers.removeAt(m_currentIndexer);
    m_currentIndexer = -1;
    refreshIndexerTable();
    selectIndexer(m_indexerTable->topLevelItemCount() > 0 ? 0 : -1);
    markDirty();
}

void OptionsDialog::testIndexer()
{
    if (!m_ipc || m_currentIndexer < 0)
        return;

    applyIndexerDetails();
    const QCborMap ix = m_indexers.at(m_currentIndexer);
    if (ix.value(QStringLiteral("url")).toString().trimmed().isEmpty()) {
        m_indexerTestResult->setText(tr("Enter an API URL first."));
        return;
    }

    m_indexerTestBtn->setEnabled(false);
    m_indexerTestResult->setText(tr("Contacting the indexer…"));
    m_indexerTestResult->setStyleSheet(QString{});

    Ipc::IpcMessage msg(Ipc::IpcMsgType::TestIndexer);
    msg.append(ix);
    m_ipc->sendRequest(std::move(msg), [this, self = QPointer<OptionsDialog>(this)](const Ipc::IpcMessage& resp) {
        if (!self)
            return;
        m_indexerTestBtn->setEnabled(m_currentIndexer >= 0);
        if (!resp.isValid()) {
            m_indexerTestResult->clear();   // connection dropped: no verdict either way
            return;
        }
        if (!resp.fieldBool(0)) {
            m_indexerTestResult->setText(resp.fieldString(1));
            m_indexerTestResult->setStyleSheet(QStringLiteral("color: #c62828;"));
            revealTestResult(m_indexerTestResult);
            return;
        }
        const QCborArray result = resp.fieldArray(1);
        const bool ok = result.at(0).toBool(false);
        // The indexer's own words: "Incorrect user credentials" says which field
        // to fix, where "Unauthorized" leaves the user guessing.
        m_indexerTestResult->setText(ok ? result.at(1).toString()
                                        : result.at(2).toString());
        m_indexerTestResult->setStyleSheet(
            ok ? QStringLiteral("color: #2e7d32;") : QStringLiteral("color: #c62828;"));
        revealTestResult(m_indexerTestResult);
    });
}

/// Bring a test result into view.
///
/// A provider answers with its own status line, so the text is unbounded: "502
/// Authentication Failed" fits on one line, a certificate mismatch does not. The label
/// wraps, but its extra lines never reach QLayout::minimumSize() -- only heightForWidth()
/// knows about them -- so nothing above notices that the row grew.
///
/// This used to grow the window instead, because the page had no scrollbar and its
/// trailing stretch was already at zero. It has one now, so the answer is to scroll to
/// the message rather than to ratchet the dialog's minimum height up for the rest of the
/// session with nothing able to give it back.
void OptionsDialog::revealTestResult(QLabel* result)
{
    QWidget* group = result ? result->parentWidget() : nullptr;
    if (!group || !group->layout())
        return;

    // The label's new text has not reached the cached heights yet: setText() only
    // invalidates the *widget's* layout and posts a LayoutRequest, so the nested row
    // layout still answers with the height the previous message needed. Invalidate the
    // whole subtree, or the scroll below is one message stale.
    invalidateLayoutTree(group->layout());

    for (QWidget* w = result->parentWidget(); w; w = w->parentWidget()) {
        if (auto* area = qobject_cast<QScrollArea*>(w)) {
            area->ensureWidgetVisible(result);
            return;
        }
    }
}

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
    m_maxConPerFiveSpin->setRange(1, 50);   // core clamps to 1-50 on load
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
    fitSpecialValue(m_serverKeepAliveSpin);
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

    m_rememberUploadQueueCheck = new QCheckBox(
        tr("Remember the upload queue between restarts"), scrollWidget);
    m_rememberUploadQueueCheck->setToolTip(
        tr("Stores the longest-waiting clients in your upload queue and puts them back, with "
           "the places they had earned, when eMule starts again. They are not contacted on "
           "startup — they simply wait their turn as usual."));
    scrollLayout->addWidget(m_rememberUploadQueueCheck);

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
    m_ed2kHostnameEdit->setToolTip(tr("A DNS name or an IPv6 literal"));
    hostnameRow->addWidget(m_ed2kHostnameEdit);
    hostnameRow->addStretch();
    scrollLayout->addLayout(hostnameRow);

    m_ed2kLinkAdvertiseIPv6Check = new QCheckBox(
        tr("Add own IPv6 address to eD2K links"), scrollWidget);
    m_ed2kLinkAdvertiseIPv6Check->setToolTip(
        tr("Only when a public IPv6 address is confirmed. Legacy clients ignore it."));
    scrollLayout->addWidget(m_ed2kLinkAdvertiseIPv6Check);

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
    // One switch per process: the daemon and the GUI write their own set of files
    // (<name>.log plus <name>_Verbose.log and <name>_Kad.log) in the config
    // directory, so a line can always be traced back to the process that emitted
    // it. Kad gets a file of its own because it would otherwise swamp the verbose
    // log — the same reason it has its own tab.
    m_logToDiskCoreCheck = new QCheckBox(tr("Write eMule core logs to disk"), scrollWidget);
    scrollLayout->addWidget(m_logToDiskCoreCheck);

    m_logToDiskGuiCheck = new QCheckBox(tr("Write eMule GUI logs to disk"), scrollWidget);
    scrollLayout->addWidget(m_logToDiskGuiCheck);

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

    m_logSourceExchangeCheck = new QCheckBox(
        tr("Log client source exchange and server source queries/answers"), verboseGroup);
    verboseLayout->addWidget(m_logSourceExchangeCheck);

    // Independent of the "Enabled" master above: routes to the Verbose tab via a
    // dedicated log category, so it can diagnose a connect without the full firehose.
    m_serverVerboseCheck = new QCheckBox(
        tr("Log server connection && search details (TCP/UDP handshake)"), verboseGroup);
    verboseLayout->addWidget(m_serverVerboseCheck);

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

    m_logWebServerCheck = new QCheckBox(tr("Log web server requests"), verboseGroup);
    verboseLayout->addWidget(m_logWebServerCheck);

    m_logPublicIPCheck = new QCheckBox(tr("Log public IP address on startup"), verboseGroup);
    verboseLayout->addWidget(m_logPublicIPCheck);

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

    // The old "Skip WAN IP/PPP setup" checkboxes are gone: miniupnpc picks the
    // WANIPConnection vs WANPPPConnection service itself, so nothing could
    // honour them and nothing in core ever read them.
    m_portMapPcpCheck = new QCheckBox(tr("PCP (RFC 6887) — preferred, supports IPv6"), upnpGroup);
    upnpLayout->addWidget(m_portMapPcpCheck);

    m_portMapNatPmpCheck = new QCheckBox(tr("NAT-PMP (RFC 6886) — IPv4 only"), upnpGroup);
    upnpLayout->addWidget(m_portMapNatPmpCheck);

    m_portMapUPnPCheck = new QCheckBox(tr("UPnP IGD — fallback"), upnpGroup);
    upnpLayout->addWidget(m_portMapUPnPCheck);

    m_portMapIPv6Check = new QCheckBox(tr("Open IPv6 firewall pinholes"), upnpGroup);
    upnpLayout->addWidget(m_portMapIPv6Check);

    auto* leaseRow = new QHBoxLayout();
    leaseRow->addWidget(new QLabel(tr("Requested lease:"), upnpGroup));
    m_portMapLeaseSpin = new QSpinBox(upnpGroup);
    m_portMapLeaseSpin->setRange(120, 86400);
    m_portMapLeaseSpin->setSingleStep(60);
    m_portMapLeaseSpin->setSuffix(tr(" s"));
    leaseRow->addWidget(m_portMapLeaseSpin);
    leaseRow->addStretch();
    upnpLayout->addLayout(leaseRow);

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
    // ×100: 2000 to 50000, the core's range. MFC stops at 10000 (PPgTweaks.cpp:497),
    // which would silently lower a larger stored value on the next OK.
    m_queueSizeSlider->setRange(20, 500);
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
        m_logSourceExchangeCheck->setEnabled(on);
        m_logBannedClientsCheck->setEnabled(on);
        m_logRatingDescCheck->setEnabled(on);
        m_logSecureIdentCheck->setEnabled(on);
        m_logFilteredIPsCheck->setEnabled(on);
        m_logFileSavingCheck->setEnabled(on);
        m_logA4AFCheck->setEnabled(on);
        m_logUlDlEventsCheck->setEnabled(on);
        m_logRawSocketPacketsCheck->setEnabled(on);
        m_logWebServerCheck->setEnabled(on);
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
    auto* schedTable = new ListTreeWidget(page);
    m_schedTable = schedTable;
    m_schedTable->setHeaderLabels({tr("Title"), tr("Days"), tr("Start Time")});
    m_schedTable->setRootIsDecorated(false);
    m_schedTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_schedTable->setColumnCount(3);
    m_schedTable->header()->setStretchLastSection(true);
    schedTable->bindColumns(QStringLiteral("optionsScheduler"), {200, 180, 100});
    giveListRoom(m_schedTable, 5, ListGrowth::Capped);
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
    auto* schedActionsTable = new ListTreeWidget(actionGroup);
    m_schedActionsTable = schedActionsTable;
    m_schedActionsTable->setHeaderLabels({tr("Action"), tr("Value")});
    m_schedActionsTable->setRootIsDecorated(false);
    m_schedActionsTable->setColumnCount(2);
    m_schedActionsTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_schedActionsTable->header()->setStretchLastSection(true);
    schedActionsTable->bindColumns(QStringLiteral("optionsSchedulerActions"), {260, 160});
    giveListRoom(m_schedActionsTable, 4, ListGrowth::Capped);
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
    m_showSpeedGraphCheck->setChecked(thePrefs.showSpeedGraph());
    m_speedGraphTimeSpin->setValue(static_cast<int>(thePrefs.speedGraphTimeRangeMin()));
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

    // The GUI acts on this one (LogWidget, IpcClient), so its own copy is the truth;
    // GetPreferences never carried it, which loaded the box unticked every time.
    m_enableIpcLogCheck->setChecked(thePrefs.enableIpcLog());

    // Load daemon-owned settings: fetch synchronously from daemon if connected,
    // otherwise fall back to local thePrefs.
    m_loading = true;
    if (m_ipc && m_ipc->isConnected()) {
        // Shared rather than captured by reference: a reply landing after the timeout
        // used to write into this stack frame after it was gone.
        struct Pending {
            QCborMap prefs;
            QEventLoop* loop = nullptr;
        };
        const auto pending = std::make_shared<Pending>();
        QEventLoop loop;
        pending->loop = &loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        Ipc::IpcMessage req(Ipc::IpcMsgType::GetPreferences);
        const int seqId = m_ipc->sendRequest(std::move(req), [pending](const Ipc::IpcMessage& resp) {
            if (resp.fieldBool(0))
                pending->prefs = resp.fieldMap(1);
            if (pending->loop)
                pending->loop->quit();
        });
        if (seqId >= 0) {
            timeout.start(3000);
            loop.exec();
        }
        pending->loop = nullptr;
        m_ipc->cancelRequest(seqId);
        if (!pending->prefs.isEmpty())
            fillDaemonSettings(pending->prefs);
        else
            fillDaemonSettingsFromPrefs();
    } else {
        fillDaemonSettingsFromPrefs();
    }
    m_loading = false;
    m_daemonSettingsLoaded = true;
    m_applyBtn->setEnabled(false);
    loadSchedulerData();
    loadNewsServers();
    loadIndexers();
    loadFeeds();
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
               "Restart eMule for all connections to use the new proxy settings.\n\n"
               "News server connections switch over immediately."));
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
    thePrefs.setShowSpeedGraph(m_showSpeedGraphCheck->isChecked());
    thePrefs.setSpeedGraphTimeRangeMin(static_cast<uint32_t>(m_speedGraphTimeSpin->value()));
    thePrefs.setStoreSearches(m_storeSearchesCheck->isChecked());
    thePrefs.setDisableKnownClientList(m_disableKnownClientListCheck->isChecked());
    thePrefs.setDisableQueueList(m_disableQueueListCheck->isChecked());
    thePrefs.setUseAutoCompletion(m_useAutoCompletionCheck->isChecked());
    thePrefs.setUseOriginalIcons(m_useOriginalIconsCheck->isChecked());
    // Keep the GUI's copy of this view-flag current so the server panel's manual-order
    // display mode and Move Up/Down menu reflect the change immediately (#24).
    thePrefs.setUseUserSortedServerList(m_useUserSortedServerListCheck->isChecked());
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
        req.append(QStringLiteral("separateIPv6Queue"));
        req.append(m_separateIPv6QueueCheck->isChecked());

        // Server page
        req.append(QStringLiteral("safeServerConnect"));
        req.append(m_safeServerConnectCheck->isChecked());
        req.append(QStringLiteral("autoConnectStaticOnly"));
        req.append(m_autoConnectStaticOnlyCheck->isChecked());
        req.append(QStringLiteral("useServerPriorities"));
        req.append(m_useServerPrioritiesCheck->isChecked());
        req.append(QStringLiteral("addServersFromServer"));
        req.append(m_addServersFromServerCheck->isChecked());
        req.append(QStringLiteral("useUserSortedServerList"));
        req.append(m_useUserSortedServerListCheck->isChecked());
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
        req.append(QStringLiteral("usenetUseProxy"));
        req.append(m_proxyUsenetCheck->isChecked());

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
        req.append(QStringLiteral("useSaveLoadSources"));
        req.append(m_saveLoadSourcesCheck->isChecked());
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

        // Usenet page. The server list goes over SetNewsServers=721 instead --
        // it carries credentials and has its own keep-the-stored-password rule.
        req.append(QStringLiteral("usenetEnabled"));
        req.append(m_usenetEnabledCheck->isChecked());
        req.append(QStringLiteral("usenetRetryIntervalSeconds"));
        req.append(static_cast<qint64>(m_usenetRetrySpin->value()));
        req.append(QStringLiteral("usenetDownloadSharePercent"));
        req.append(static_cast<qint64>(m_usenetShareSpin->value()));
        req.append(QStringLiteral("usenetPar2Repair"));
        req.append(m_usenetPar2Check->isChecked());
        req.append(QStringLiteral("usenetPar2RenameFiles"));
        req.append(m_usenetRenameCheck->isChecked());
        req.append(QStringLiteral("usenetUnpack"));
        req.append(m_usenetUnpackCheck->isChecked());
        req.append(QStringLiteral("usenetDirectUnpack"));
        req.append(m_usenetDirectUnpackCheck->isChecked());
        req.append(QStringLiteral("usenetEncryptedPreview"));
        req.append(m_usenetEncryptedPreviewCheck->isChecked());
        req.append(QStringLiteral("usenetExternalUnpacker"));
        req.append(m_usenetUnpackerEdit->text().trimmed());
        req.append(QStringLiteral("usenetCleanupAfterUnpack"));
        req.append(m_usenetCleanupCheck->isChecked());
        req.append(QStringLiteral("usenetSfvCheck"));
        req.append(m_usenetSfvCheck->isChecked());
        req.append(QStringLiteral("usenetUnrepairableAction"));
        req.append(static_cast<qint64>(m_usenetUnrepairableCombo->currentIndex()));
        req.append(QStringLiteral("usenetUnwantedAction"));
        req.append(static_cast<qint64>(m_usenetUnwantedCombo->currentIndex()));
        req.append(QStringLiteral("usenetUnwantedExtensions"));
        req.append(m_usenetUnwantedEdit->text().trimmed());
        req.append(QStringLiteral("usenetHealthCheck"));
        req.append(static_cast<qint64>(m_usenetHealthCombo->currentIndex()));
        req.append(QStringLiteral("usenetHealthMinPercent"));
        req.append(static_cast<qint64>(m_usenetHealthMinSpin->value()));
        req.append(QStringLiteral("usenetAutoAddPaused"));
        req.append(m_usenetAutoPausedCheck->isChecked());
        req.append(QStringLiteral("usenetWatchDir"));
        req.append(m_usenetWatchDirEdit->text().trimmed());

        // Indexers page. The account list goes over SetIndexers=701 instead --
        // it carries API keys and has its own keep-the-stored-key rule.
        req.append(QStringLiteral("indexerResultLimit"));
        req.append(static_cast<qint64>(m_indexerLimitSpin->value()));
        req.append(QStringLiteral("indexerMaxPages"));
        req.append(static_cast<qint64>(m_indexerPagesSpin->value()));
        req.append(QStringLiteral("indexerTimeoutSeconds"));
        req.append(static_cast<qint64>(m_indexerTimeoutSpin->value()));
        req.append(QStringLiteral("indexerCapsRefreshDays"));
        req.append(static_cast<qint64>(m_indexerCapsRefreshSpin->value()));

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
        req.append(QStringLiteral("logToDiskCore"));
        req.append(m_logToDiskCoreCheck->isChecked());
        req.append(QStringLiteral("logToDiskGui"));
        req.append(m_logToDiskGuiCheck->isChecked());
        req.append(QStringLiteral("verbose"));
        req.append(m_verboseCheck->isChecked());
        req.append(QStringLiteral("closeUPnPOnExit"));
        req.append(m_closeUPnPCheck->isChecked());
        req.append(QStringLiteral("portMapProtocols"));
        req.append(static_cast<qint64>(portMapProtocolMask()));
        req.append(QStringLiteral("portMapIPv6"));
        req.append(m_portMapIPv6Check->isChecked());
        req.append(QStringLiteral("portMapLeaseSecs"));
        req.append(static_cast<qint64>(m_portMapLeaseSpin->value()));
        req.append(QStringLiteral("fileBufferSize"));
        req.append(static_cast<qint64>(m_fileBufferSlider->value()) * 16384); // slider to bytes
        req.append(QStringLiteral("useCreditSystem"));
        req.append(m_useCreditSystemCheck->isChecked());
        req.append(QStringLiteral("rememberUploadQueue"));
        req.append(m_rememberUploadQueueCheck->isChecked());
        req.append(QStringLiteral("a4afSaveCpu"));
        req.append(m_a4afSaveCpuCheck->isChecked());
        req.append(QStringLiteral("autoArchivePreviewStart"));
        req.append(!m_disableArchPreviewCheck->isChecked());
        req.append(QStringLiteral("ed2kHostname"));
        req.append(m_ed2kHostnameEdit->text());
        req.append(QStringLiteral("ed2kLinkAdvertiseIPv6"));
        req.append(m_ed2kLinkAdvertiseIPv6Check->isChecked());
        req.append(QStringLiteral("showExtControls"));
        req.append(m_showExtControlsCheck->isChecked());
        req.append(QStringLiteral("commitFiles"));
        req.append(static_cast<qint64>(m_commitFilesGroup->checkedId()));
        req.append(QStringLiteral("extractMetaData"));
        req.append(static_cast<qint64>(m_extractMetaDataGroup->checkedId()));
        req.append(QStringLiteral("logLevel"));
        req.append(static_cast<qint64>(m_logLevelSpin->value()));
        req.append(QStringLiteral("logSourceExchange"));
        req.append(m_logSourceExchangeCheck->isChecked());
        req.append(QStringLiteral("serverVerboseLog"));
        req.append(m_serverVerboseCheck->isChecked());
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
        req.append(QStringLiteral("logWebServer"));
        req.append(m_logWebServerCheck->isChecked());
        req.append(QStringLiteral("logPublicIP"));
        req.append(m_logPublicIPCheck->isChecked());
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

        // GUI-only settings (synced to daemon for YAML persistence)
        // General page
        req.append(QStringLiteral("promptOnExit"));
        req.append(m_promptOnExitCheck->isChecked());
        req.append(QStringLiteral("startMinimized"));
        req.append(m_startMinimizedCheck->isChecked());
        req.append(QStringLiteral("showSplashScreen"));
        req.append(m_showSplashCheck->isChecked());
        req.append(QStringLiteral("enableOnlineSignature"));
        req.append(m_enableOnlineSigCheck->isChecked());
#ifdef Q_OS_WIN
        req.append(QStringLiteral("enableMiniMule"));
        req.append(m_enableMiniMuleCheck->isChecked());
#endif
        req.append(QStringLiteral("preventStandby"));
        req.append(m_preventStandbyCheck->isChecked());
        req.append(QStringLiteral("versionCheckEnabled"));
        req.append(m_versionCheckBox->isChecked());
        req.append(QStringLiteral("versionCheckDays"));
        req.append(static_cast<qint64>(m_versionCheckDaysSpin->value()));
        req.append(QStringLiteral("bringToFrontOnLinkClick"));
        req.append(m_bringToFrontCheck->isChecked());
        req.append(QStringLiteral("language"));
        req.append(m_langCombo->currentData().toString());
        req.append(QStringLiteral("startWithOS"));
        req.append(m_startWithOSCheck->isChecked());

        // Display page
        req.append(QStringLiteral("depth3D"));
        req.append(static_cast<qint64>(m_depth3DSlider->value()));
        req.append(QStringLiteral("tooltipDelay"));
        req.append(static_cast<qint64>(m_tooltipDelaySpin->value()));
        req.append(QStringLiteral("minimizeToTray"));
        req.append(m_minimizeToTrayCheck->isChecked());
        req.append(QStringLiteral("transferDoubleClick"));
        req.append(m_transferDoubleClickCheck->isChecked());
        req.append(QStringLiteral("showDwlPercentage"));
        req.append(m_showDwlPercentageCheck->isChecked());
        req.append(QStringLiteral("showRatesInTitle"));
        req.append(m_showRatesInTitleCheck->isChecked());
        req.append(QStringLiteral("showCatTabInfos"));
        req.append(m_showCatTabInfosCheck->isChecked());
        req.append(QStringLiteral("autoRemoveFinishedDownloads"));
        req.append(m_autoRemoveFinishedCheck->isChecked());
        req.append(QStringLiteral("showTransToolbar"));
        req.append(m_showTransToolbarCheck->isChecked());
        req.append(QStringLiteral("showSpeedGraph"));
        req.append(m_showSpeedGraphCheck->isChecked());
        req.append(QStringLiteral("speedGraphTimeRangeMin"));
        req.append(static_cast<qint64>(m_speedGraphTimeSpin->value()));
        req.append(QStringLiteral("storeSearches"));
        req.append(m_storeSearchesCheck->isChecked());
        req.append(QStringLiteral("disableKnownClientList"));
        req.append(m_disableKnownClientListCheck->isChecked());
        req.append(QStringLiteral("disableQueueList"));
        req.append(m_disableQueueListCheck->isChecked());
        req.append(QStringLiteral("useAutoCompletion"));
        req.append(m_useAutoCompletionCheck->isChecked());
        req.append(QStringLiteral("useOriginalIcons"));
        req.append(m_useOriginalIconsCheck->isChecked());
        req.append(QStringLiteral("logFont"));
        req.append(m_currentLogFont);

        // Files page (GUI-only)
        req.append(QStringLiteral("watchClipboard4ED2KLinks"));
        req.append(m_watchClipboardCheck->isChecked());
        req.append(QStringLiteral("useAdvancedCalcRemainingTime"));
        req.append(m_advancedCalcRemainingCheck->isChecked());
        req.append(QStringLiteral("videoPlayerCommand"));
        req.append(m_videoPlayerCmdEdit->text());
        req.append(QStringLiteral("videoPlayerArgs"));
        req.append(m_videoPlayerArgsEdit->text());
        req.append(QStringLiteral("createBackupToPreview"));
        req.append(m_createBackupToPreviewCheck->isChecked());
        req.append(QStringLiteral("autoCleanupFilenames"));
        req.append(m_autoCleanupFilenamesCheck->isChecked());

        // Notifications page (GUI-side)
        req.append(QStringLiteral("notifySoundType"));
        req.append(static_cast<qint64>(m_soundGroup->checkedId()));
        req.append(QStringLiteral("notifySoundFile"));
        req.append(m_soundFileEdit->text());

        // IRC page
        req.append(QStringLiteral("ircServer"));
        req.append(m_ircServerEdit->text());
        req.append(QStringLiteral("ircNick"));
        req.append(m_ircNickEdit->text());
        req.append(QStringLiteral("ircUseChannelFilter"));
        req.append(m_ircUseChannelFilterCheck->isChecked());
        req.append(QStringLiteral("ircChannelFilter"));
        req.append(m_ircChannelFilterNameEdit->text() + QLatin1Char('|')
            + QString::number(m_ircChannelFilterUsersSpin->value()));
        req.append(QStringLiteral("ircUsePerform"));
        req.append(m_ircUsePerformCheck->isChecked());
        req.append(QStringLiteral("ircPerformString"));
        req.append(m_ircPerformEdit->text());
        req.append(QStringLiteral("ircConnectHelpChannel"));
        req.append(root->child(0)->checkState(0) == Qt::Checked);
        req.append(QStringLiteral("ircLoadChannelList"));
        req.append(root->child(1)->checkState(0) == Qt::Checked);
        req.append(QStringLiteral("ircAddTimestamp"));
        req.append(root->child(2)->checkState(0) == Qt::Checked);
        req.append(QStringLiteral("ircIgnoreMiscInfoMessages"));
        req.append(ignoreParent->child(0)->checkState(0) == Qt::Checked);
        req.append(QStringLiteral("ircIgnoreJoinMessages"));
        req.append(ignoreParent->child(1)->checkState(0) == Qt::Checked);
        req.append(QStringLiteral("ircIgnorePartMessages"));
        req.append(ignoreParent->child(2)->checkState(0) == Qt::Checked);
        req.append(QStringLiteral("ircIgnoreQuitMessages"));
        req.append(ignoreParent->child(3)->checkState(0) == Qt::Checked);

        // Messages page (GUI-only)
        req.append(QStringLiteral("showSmileys"));
        req.append(m_showSmileysCheck->isChecked());
        req.append(QStringLiteral("indicateRatings"));
        req.append(m_indicateRatingsCheck->isChecked());

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
        thePrefs.setSeparateIPv6Queue(m_separateIPv6QueueCheck->isChecked());

        // Server page fallback
        thePrefs.setSafeServerConnect(m_safeServerConnectCheck->isChecked());
        thePrefs.setAutoConnectStaticOnly(m_autoConnectStaticOnlyCheck->isChecked());
        thePrefs.setUseServerPriorities(m_useServerPrioritiesCheck->isChecked());
        thePrefs.setAddServersFromServer(m_addServersFromServerCheck->isChecked());
        thePrefs.setUseUserSortedServerList(m_useUserSortedServerListCheck->isChecked());
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
        thePrefs.setUsenetUseProxy(m_proxyUsenetCheck->isChecked());

        // Directories page fallback
        thePrefs.setIncomingDir(m_incomingDirEdit->text());
        thePrefs.setTempDirs({m_tempDirEdit->text()});
        thePrefs.setSharedDirs(static_cast<CheckableFileSystemModel*>(m_sharedDirsModel)->checkedPaths());

        // Files page (daemon-side) fallback
        thePrefs.setAddNewFilesPaused(m_addFilesPausedCheck->isChecked());
        thePrefs.setUseSaveLoadSources(m_saveLoadSourcesCheck->isChecked());
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
        thePrefs.setLogToDiskCore(m_logToDiskCoreCheck->isChecked());
        thePrefs.setLogToDiskGui(m_logToDiskGuiCheck->isChecked());
        thePrefs.setVerbose(m_verboseCheck->isChecked());
        thePrefs.setCloseUPnPOnExit(m_closeUPnPCheck->isChecked());
        thePrefs.setPortMapProtocols(portMapProtocolMask());
        thePrefs.setPortMapIPv6(m_portMapIPv6Check->isChecked());
        thePrefs.setPortMapLeaseSecs(static_cast<uint32>(m_portMapLeaseSpin->value()));
        thePrefs.setFileBufferSize(static_cast<uint32>(m_fileBufferSlider->value()) * 16384);
        thePrefs.setUseCreditSystem(m_useCreditSystemCheck->isChecked());
        thePrefs.setRememberUploadQueue(m_rememberUploadQueueCheck->isChecked());
        thePrefs.setA4afSaveCpu(m_a4afSaveCpuCheck->isChecked());
        thePrefs.setAutoArchivePreviewStart(!m_disableArchPreviewCheck->isChecked());
        thePrefs.setEd2kHostname(m_ed2kHostnameEdit->text());
        thePrefs.setEd2kLinkAdvertiseIPv6(m_ed2kLinkAdvertiseIPv6Check->isChecked());
        thePrefs.setCommitFiles(m_commitFilesGroup->checkedId());
        thePrefs.setExtractMetaData(m_extractMetaDataGroup->checkedId());
        thePrefs.setLogLevel(m_logLevelSpin->value());
        thePrefs.setLogSourceExchange(m_logSourceExchangeCheck->isChecked());
        thePrefs.setServerVerboseLog(m_serverVerboseCheck->isChecked());
        thePrefs.setLogBannedClients(m_logBannedClientsCheck->isChecked());
        thePrefs.setLogRatingDescReceived(m_logRatingDescCheck->isChecked());
        thePrefs.setLogSecureIdent(m_logSecureIdentCheck->isChecked());
        thePrefs.setLogFilteredIPs(m_logFilteredIPsCheck->isChecked());
        thePrefs.setLogFileSaving(m_logFileSavingCheck->isChecked());
        thePrefs.setLogA4AF(m_logA4AFCheck->isChecked());
        thePrefs.setLogUlDlEvents(m_logUlDlEventsCheck->isChecked());
        thePrefs.setLogRawSocketPackets(m_logRawSocketPacketsCheck->isChecked());
        thePrefs.setLogWebServer(m_logWebServerCheck->isChecked());
        thePrefs.setLogPublicIP(m_logPublicIPCheck->isChecked());
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

        // Only persist locally when there is no daemon at all (not just
        // a temporary disconnect).  If a daemon was configured (m_ipc != null)
        // it is the sole owner of preferences.yml.
        if (!m_ipc)
            thePrefs.save();
    }

    saveSchedulerData();
    saveNewsServers();
    // GUI-only, so it never travels over SetPreferences: the daemon owns
    // preferences.yml and knows nothing about the desktop it is not running on,
    // and it is this executable's path that gets registered.
    if (m_associateNzbCheck
        && m_associateNzbCheck->isChecked() != theUiState.associateNzbFiles()) {
        theUiState.setAssociateNzbFiles(m_associateNzbCheck->isChecked());
        QString assocError;
        const bool ok = m_associateNzbCheck->isChecked()
                            ? gui::FileAssociation::registerNzbFileType(assocError)
                            : gui::FileAssociation::unregisterNzbFileType(assocError);
        if (!ok) {
            QMessageBox::warning(this, tr("File types"),
                                 tr("Could not update the .nzb file association: %1")
                                     .arg(assocError));
        }
    }

    saveIndexers();
    saveFeeds();

    // The graph palette is GUI-only state, so it goes to uistate.yml rather than over
    // SetPreferences. The tray meter picks its colour up on the next 1 s rate tick.
    theUiState.setStatsColors(m_statsColors);

    // Apply settings to live statistics panel
    if (m_statsPanel)
        m_statsPanel->applySettings();
}

// ---------------------------------------------------------------------------
// Private: fill daemon-owned widgets from IPC response (QCborMap)
// ---------------------------------------------------------------------------

void OptionsDialog::fillDaemonSettings(const QCborMap& prefs)
{
    m_nickEdit->setText(prefs.value(QStringLiteral("nick")).toString());

    // Connection page
    auto capDown = static_cast<int>(prefs.value(QStringLiteral("maxGraphDownloadRate")).toInteger(100));
    auto capUp   = static_cast<int>(prefs.value(QStringLiteral("maxGraphUploadRate")).toInteger(100));
    m_capacityDownloadSpin->setValue(capDown);
    m_capacityUploadSpin->setValue(capUp);

    auto maxDown = static_cast<int>(prefs.value(QStringLiteral("maxDownload")).toInteger(0));
    auto maxUp   = static_cast<int>(prefs.value(QStringLiteral("maxUpload")).toInteger(0));
    // An unlimited (0) limit parks its slider at the capacity, so ticking the box later
    // offers the line rate rather than the slider's floor of 1 KB/s
    // (MFC PPgConnection.cpp:177-184).
    m_downloadLimitCheck->setChecked(maxDown > 0);
    m_downloadLimitSlider->setEnabled(maxDown > 0);
    m_downloadLimitLabel->setEnabled(maxDown > 0);
    m_downloadLimitSlider->setValue(maxDown > 0 ? maxDown : capDown);
    m_uploadLimitCheck->setChecked(maxUp > 0);
    m_uploadLimitSlider->setEnabled(maxUp > 0);
    m_uploadLimitLabel->setEnabled(maxUp > 0);
    m_uploadLimitSlider->setValue(maxUp > 0 ? maxUp : capUp);
    // valueChanged stays quiet when the value didn't move, so set the labels here too.
    m_downloadLimitLabel->setText(limitText(m_downloadLimitSlider->value()));
    m_uploadLimitLabel->setText(limitText(m_uploadLimitSlider->value()));

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
    // Defaults to true — bare toBool() would silently uncheck it against an older daemon.
    m_separateIPv6QueueCheck->setChecked(
        prefs.value(QStringLiteral("separateIPv6Queue")).toBool(true));

    // Server page
    m_safeServerConnectCheck->setChecked(prefs.value(QStringLiteral("safeServerConnect")).toBool());
    m_autoConnectStaticOnlyCheck->setChecked(prefs.value(QStringLiteral("autoConnectStaticOnly")).toBool());
    m_useServerPrioritiesCheck->setChecked(prefs.value(QStringLiteral("useServerPriorities")).toBool());
    m_addServersFromServerCheck->setChecked(prefs.value(QStringLiteral("addServersFromServer")).toBool());
    m_useUserSortedServerListCheck->setChecked(prefs.value(QStringLiteral("useUserSortedServerList")).toBool());
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
    m_proxyUsenetCheck->setChecked(prefs.value(QStringLiteral("usenetUseProxy")).toBool(true));
    m_proxyTypeCombo->setEnabled(proxyOn);
    m_proxyHostEdit->setEnabled(proxyOn);
    m_proxyPortSpin->setEnabled(proxyOn);
    m_proxyAuthCheck->setEnabled(proxyOn);
    m_proxyUserEdit->setEnabled(proxyOn && authOn);
    m_proxyPasswordEdit->setEnabled(proxyOn && authOn);
    m_proxyUsenetCheck->setEnabled(proxyOn);

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
    m_saveLoadSourcesCheck->setChecked(
        prefs.value(QStringLiteral("useSaveLoadSources")).toBool(true));
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

    // Usenet page
    m_usenetEnabledCheck->setChecked(prefs.value(QStringLiteral("usenetEnabled")).toBool(false));
    m_usenetRetrySpin->setValue(
        static_cast<int>(prefs.value(QStringLiteral("usenetRetryIntervalSeconds")).toInteger(60)));
    m_usenetShareSpin->setValue(
        static_cast<int>(prefs.value(QStringLiteral("usenetDownloadSharePercent")).toInteger(50)));
    m_usenetPar2Check->setChecked(
        prefs.value(QStringLiteral("usenetPar2Repair")).toBool(true));
    m_usenetRenameCheck->setChecked(
        prefs.value(QStringLiteral("usenetPar2RenameFiles")).toBool(true));
    m_usenetUnpackCheck->setChecked(
        prefs.value(QStringLiteral("usenetUnpack")).toBool(true));
    m_usenetCleanupCheck->setChecked(
        prefs.value(QStringLiteral("usenetCleanupAfterUnpack")).toBool(true));
    m_usenetDirectUnpackCheck->setChecked(
        prefs.value(QStringLiteral("usenetDirectUnpack")).toBool(true));
    m_usenetEncryptedPreviewCheck->setChecked(
        prefs.value(QStringLiteral("usenetEncryptedPreview")).toBool(true));
    m_usenetUnpackerEdit->setText(
        prefs.value(QStringLiteral("usenetExternalUnpacker")).toString());
    m_usenetSfvCheck->setChecked(prefs.value(QStringLiteral("usenetSfvCheck")).toBool(true));
    m_usenetUnrepairableCombo->setCurrentIndex(
        int(prefs.value(QStringLiteral("usenetUnrepairableAction")).toInteger(1)));
    m_usenetUnwantedCombo->setCurrentIndex(
        int(prefs.value(QStringLiteral("usenetUnwantedAction")).toInteger(1)));
    // An older daemon sends no key; empty would read as "check off".
    m_usenetUnwantedEdit->setText(
        prefs.contains(QStringLiteral("usenetUnwantedExtensions"))
            ? prefs.value(QStringLiteral("usenetUnwantedExtensions")).toString()
            : QString(Preferences::kDefaultUsenetUnwantedExtensions));
    m_usenetUnwantedEdit->setEnabled(m_usenetUnwantedCombo->currentIndex() > 0);
    m_usenetHealthCombo->setCurrentIndex(
        int(prefs.value(QStringLiteral("usenetHealthCheck")).toInteger(1)));
    m_usenetHealthMinSpin->setValue(
        int(prefs.value(QStringLiteral("usenetHealthMinPercent")).toInteger(95)));
    if (m_usenetAutoPausedCheck) {
        m_usenetAutoPausedCheck->setChecked(
            prefs.value(QStringLiteral("usenetAutoAddPaused")).toBool(false));
    }
    if (m_usenetWatchDirEdit)
        m_usenetWatchDirEdit->setText(prefs.value(QStringLiteral("usenetWatchDir")).toString());
    if (m_associateNzbCheck)
        m_associateNzbCheck->setChecked(theUiState.associateNzbFiles());
    m_usenetHealthMinSpin->setEnabled(m_usenetHealthCombo->currentIndex() > 0);
    m_usenetCleanupCheck->setEnabled(m_usenetUnpackCheck->isChecked());
    m_usenetDirectUnpackCheck->setEnabled(m_usenetUnpackCheck->isChecked());
    m_usenetEncryptedPreviewCheck->setEnabled(m_usenetUnpackCheck->isChecked());
    updateUsenetEnabledStates();

    // Indexers page
    m_indexerLimitSpin->setValue(
        static_cast<int>(prefs.value(QStringLiteral("indexerResultLimit")).toInteger(100)));
    m_indexerPagesSpin->setValue(
        static_cast<int>(prefs.value(QStringLiteral("indexerMaxPages")).toInteger(3)));
    m_indexerTimeoutSpin->setValue(
        static_cast<int>(prefs.value(QStringLiteral("indexerTimeoutSeconds")).toInteger(30)));
    m_indexerCapsRefreshSpin->setValue(
        static_cast<int>(prefs.value(QStringLiteral("indexerCapsRefreshDays")).toInteger(7)));

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
    m_webAdminHiLevCheck->setChecked(prefs.value(QStringLiteral("webServerAdminAllowHiLevFunc")).toBool());
    m_webGuestEnabledCheck->setChecked(prefs.value(QStringLiteral("webServerGuestEnabled")).toBool());
    updateWebEnabledStates();

    // Statistics page
    m_statsGraphUpdateSlider->setValue(static_cast<int>(prefs.value(QStringLiteral("graphsUpdateSec")).toInteger(3)));
    m_statsAvgTimeSlider->setValue(static_cast<int>(prefs.value(QStringLiteral("statsAverageMinutes")).toInteger(5)));
    m_statsFillGraphsCheck->setChecked(prefs.value(QStringLiteral("fillGraphs")).toBool());
    m_statsYScaleSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("statsConnectionsMax")).toInteger(100)));
    {
        auto ratio = static_cast<uint32_t>(prefs.value(QStringLiteral("statsConnectionsRatio")).toInteger(3));
        static constexpr int ratioValues[] = {1, 2, 3, 4, 5, 10, 20};
        int ratioIdx = 2;
        for (int ri = 0; ri < 7; ++ri) {
            if (static_cast<uint32_t>(ratioValues[ri]) == ratio) { ratioIdx = ri; break; }
        }
        m_statsRatioCombo->setCurrentIndex(ratioIdx);
    }
    m_statsTreeUpdateSlider->setValue(static_cast<int>(prefs.value(QStringLiteral("statsUpdateSec")).toInteger(5)));

    // Extended page
    m_maxConPerFiveSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("maxConsPerFive")).toInteger(20)));
    m_maxHalfOpenSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("maxHalfConnections")).toInteger(9)));
    m_serverKeepAliveSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("serverKeepAliveTimeout")).toInteger(0)) / 60000);
    m_useCreditSystemCheck->setChecked(prefs.value(QStringLiteral("useCreditSystem")).toBool(true));
    // toBool(true), not bare toBool(): the default is on, and an older daemon that does not
    // send the key at all must not read back as "user turned it off".
    m_rememberUploadQueueCheck->setChecked(
        prefs.value(QStringLiteral("rememberUploadQueue")).toBool(true));
    m_filterLANIPsCheck->setChecked(prefs.value(QStringLiteral("filterLANIPs")).toBool(true));
    m_showExtControlsCheck->setChecked(prefs.value(QStringLiteral("showExtControls")).toBool());
    m_a4afSaveCpuCheck->setChecked(prefs.value(QStringLiteral("a4afSaveCpu")).toBool());
    m_disableArchPreviewCheck->setChecked(!prefs.value(QStringLiteral("autoArchivePreviewStart")).toBool(true));
    m_ed2kHostnameEdit->setText(prefs.value(QStringLiteral("ed2kHostname")).toString());
    m_ed2kLinkAdvertiseIPv6Check->setChecked(
        prefs.value(QStringLiteral("ed2kLinkAdvertiseIPv6")).toBool(true));
    bool diskCheck = prefs.value(QStringLiteral("checkDiskspace")).toBool();
    m_checkDiskspaceCheck->setChecked(diskCheck);
    m_minFreeDiskSpaceSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("minFreeDiskSpace")).toInteger(20971520)) / (1024 * 1024));
    m_minFreeDiskSpaceSpin->setEnabled(diskCheck);
    if (auto* btn = m_commitFilesGroup->button(static_cast<int>(prefs.value(QStringLiteral("commitFiles")).toInteger(1))))
        btn->setChecked(true);
    if (auto* btn = m_extractMetaDataGroup->button(static_cast<int>(prefs.value(QStringLiteral("extractMetaData")).toInteger(1))))
        btn->setChecked(true);
    m_logToDiskCoreCheck->setChecked(prefs.value(QStringLiteral("logToDiskCore")).toBool());
    m_logToDiskGuiCheck->setChecked(prefs.value(QStringLiteral("logToDiskGui")).toBool());
    bool verboseOn = prefs.value(QStringLiteral("verbose")).toBool(true);
    m_verboseCheck->setChecked(verboseOn);
    m_logLevelSpin->setValue(static_cast<int>(prefs.value(QStringLiteral("logLevel")).toInteger(5)));
    m_logLevelSpin->setEnabled(verboseOn);
    m_logSourceExchangeCheck->setChecked(prefs.value(QStringLiteral("logSourceExchange")).toBool());
    m_logSourceExchangeCheck->setEnabled(verboseOn);
    // Independent channel — stays enabled regardless of the verbose master toggle.
    m_serverVerboseCheck->setChecked(prefs.value(QStringLiteral("serverVerboseLog")).toBool());
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
    m_logWebServerCheck->setChecked(prefs.value(QStringLiteral("logWebServer")).toBool());
    m_logWebServerCheck->setEnabled(verboseOn);
    m_logPublicIPCheck->setChecked(prefs.value(QStringLiteral("logPublicIP")).toBool());
    m_startCoreWithConsoleCheck->setChecked(prefs.value(QStringLiteral("startCoreWithConsole")).toBool());
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
    const auto mask = static_cast<uint32>(
        prefs.value(QStringLiteral("portMapProtocols")).toInteger(7));
    m_portMapPcpCheck->setChecked((mask & 1u) != 0);
    m_portMapNatPmpCheck->setChecked((mask & 2u) != 0);
    m_portMapUPnPCheck->setChecked((mask & 4u) != 0);
    m_portMapIPv6Check->setChecked(prefs.value(QStringLiteral("portMapIPv6")).toBool(true));
    m_portMapLeaseSpin->setValue(
        static_cast<int>(prefs.value(QStringLiteral("portMapLeaseSecs")).toInteger(3600)));
    m_fileBufferSlider->setValue(static_cast<int>(prefs.value(QStringLiteral("fileBufferSize")).toInteger(245760)) / 16384);
    m_queueSizeSlider->setValue(static_cast<int>(prefs.value(QStringLiteral("queueSize")).toInteger(5000)) / 100);

#ifdef Q_OS_WIN
    m_autotakeEd2kCheck->setChecked(prefs.value(QStringLiteral("autotakeEd2kLinks")).toBool(true));
    m_winFirewallCheck->setChecked(prefs.value(QStringLiteral("openPortsOnWinFirewall")).toBool());
    m_sparsePartFilesCheck->setChecked(prefs.value(QStringLiteral("sparsePartFiles")).toBool());
    m_allocFullFileCheck->setChecked(prefs.value(QStringLiteral("allocFullFile")).toBool());
    m_resolveShellLinksCheck->setChecked(prefs.value(QStringLiteral("resolveShellLinks")).toBool());
    if (auto* btn = m_multiUserSharingGroup->button(static_cast<int>(prefs.value(QStringLiteral("multiUserSharing")).toInteger(2))))
        btn->setChecked(true);
#endif
}

// ---------------------------------------------------------------------------
// Private: fill daemon-owned widgets from local thePrefs (fallback)
// ---------------------------------------------------------------------------

void OptionsDialog::fillDaemonSettingsFromPrefs()
{
    // The same map GetPreferences sends. This used to be a hand-picked subset, so every
    // key after the Files page fell through to fillDaemonSettings' hardcoded fallbacks
    // (a 240 KB file buffer, 20 connections / 5 s) — and the next OK saved them.
    fillDaemonSettings(thePrefs.toIpcMap());
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

// ---------------------------------------------------------------------------
// updateWebEnabledStates — grey Web Interface sub-controls that have no effect
// for the current selection. The two top-level checkboxes (web UI + REST API)
// are independent and always enabled; neither disables the other.
// ---------------------------------------------------------------------------

void OptionsDialog::updateWebEnabledStates()
{
    const bool webOn  = m_webEnabledCheck->isChecked();    // template web UI
    const bool restOn = m_webRestApiCheck->isChecked();    // JSON REST API
    const bool anyOn  = webOn || restOn;

    // Shared server-level controls — relevant whenever either surface is served.
    m_webPortSpin->setEnabled(anyOn);
    m_webGzipCheck->setEnabled(anyOn);
    m_webUPnPCheck->setEnabled(anyOn);
    m_webHttpsCheck->setEnabled(anyOn);
    const bool httpsOn = anyOn && m_webHttpsCheck->isChecked();
    m_webCreateCertBtn->setEnabled(httpsOn);
    m_webCertEdit->setEnabled(httpsOn);
    m_webCertBrowseBtn->setEnabled(httpsOn);
    m_webKeyEdit->setEnabled(httpsOn);
    m_webKeyBrowseBtn->setEnabled(httpsOn);

    // REST-only: the API key authenticates /api/v1/* (X-Api-Key).
    m_webApiKeyEdit->setEnabled(restOn);

    // Web-UI-only: template, session login and admin/guest accounts.
    m_webTemplateEdit->setEnabled(webOn);
    m_webTemplateBrowseBtn->setEnabled(webOn);
    m_webTemplateReloadBtn->setEnabled(webOn);
    m_webSessionTimeoutSpin->setEnabled(webOn);
    m_webAdminPasswordEdit->setEnabled(webOn);
    m_webAdminHiLevCheck->setEnabled(webOn);
    m_webGuestEnabledCheck->setEnabled(webOn);
    m_webGuestPasswordEdit->setEnabled(webOn && m_webGuestEnabledCheck->isChecked());
}

// ---------------------------------------------------------------------------
// Port test
// ---------------------------------------------------------------------------

void OptionsDialog::openPortTest()
{
    const int tcp = m_tcpPortSpin->value();
    const int udp = m_udpDisableCheck->isChecked() ? 0 : m_udpPortSpin->value();

    // Legacy IPv4-only tester, kept in case it is wanted again: porttest.emule-project.net has no
    // AAAA record and dials back to the IPv4 address it observes, so behind CGNAT or any shared
    // egress its verdict is guaranteed to be "failed" regardless of how the ports are configured.
    //QDesktopServices::openUrl(QUrl(
    //    QStringLiteral("https://porttest.emule-project.net/connectiontest.php?tcpport=%1&udpport=%2")
    //        .arg(tcp).arg(udp)));

    // The page can only observe the family the browser happens to reach it over, so hand it our
    // own public addresses for the other one — otherwise a v6-preferring browser silently leaves
    // IPv4 untested, and vice versa. The daemon is the authority here: it may run on a different
    // host than this GUI, in which case our own addresses would be the wrong ones to test.
    if (!m_ipc) {
        openPortTestUrl(tcp, udp, QString(), QString());
        return;
    }

    Ipc::IpcMessage req(Ipc::IpcMsgType::GetNetworkInfo);
    m_ipc->sendRequest(std::move(req), [this, tcp, udp](const Ipc::IpcMessage& resp) {
        const QCborMap ed2k = resp.fieldMap(1).value(QStringLiteral("ed2k")).toMap();
        openPortTestUrl(tcp, udp,
                        ed2k.value(QStringLiteral("publicIPv4")).toString(),
                        ed2k.value(QStringLiteral("publicIPv6")).toString());
    });
}

void OptionsDialog::openPortTestUrl(int tcpPort, int udpPort,
                                    const QString& ipv4, const QString& ipv6)
{
    QUrl url(QLatin1String(kWebsiteUrl) + QLatin1String(kPortTestPath));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("tcpport"), QString::number(tcpPort));
    query.addQueryItem(QStringLiteral("udpport"), QString::number(udpPort));
    if (!ipv4.isEmpty())
        query.addQueryItem(QStringLiteral("ip4"), ipv4);
    if (!ipv6.isEmpty())
        query.addQueryItem(QStringLiteral("ip6"), ipv6);
    url.setQuery(query);

    QDesktopServices::openUrl(url);
}

quint32 OptionsDialog::portMapProtocolMask() const
{
    quint32 mask = 0;
    if (m_portMapPcpCheck != nullptr && m_portMapPcpCheck->isChecked())
        mask |= 1u;
    if (m_portMapNatPmpCheck != nullptr && m_portMapNatPmpCheck->isChecked())
        mask |= 2u;
    if (m_portMapUPnPCheck != nullptr && m_portMapUPnPCheck->isChecked())
        mask |= 4u;
    return mask;
}

} // namespace eMule
