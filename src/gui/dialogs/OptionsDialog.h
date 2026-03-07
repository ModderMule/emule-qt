#pragma once

/// @file OptionsDialog.h
/// @brief Options/Preferences dialog matching the MFC eMule Options window.
///
/// Left sidebar with category icons, right stacked widget for page content,
/// and OK/Cancel/Apply buttons at the bottom.

#include <QColor>
#include <QDialog>
#include <QIcon>

#include <array>
#include <ctime>
#include <vector>

class QLabel;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QButtonGroup;
class QListWidget;
class QRadioButton;
class QStackedWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class QTreeView;
class QFileSystemModel;
class QColorDialog;
class QTimeEdit;

namespace eMule {

class IpcClient;
class StatisticsPanel;

class OptionsDialog : public QDialog {
    Q_OBJECT

public:
    explicit OptionsDialog(IpcClient* ipc, StatisticsPanel* statsPanel = nullptr,
                           QWidget* parent = nullptr);
    ~OptionsDialog() override;

    /// Switch to a specific page by index.
    void selectPage(int page);

    /// Page indices matching sidebar order.
    enum Page {
        PageGeneral = 0,
        PageDisplay,
        PageConnection,
        PageProxy,
        PageServer,
        PageDirectories,
        PageFiles,
        PageNotifications,
        PageStatistics,
        PageIRC,
        PageMessages,
        PageSecurity,
        PageScheduler,
        PageWebInterface,
        PageExtended,
        PageCount
    };

private slots:
    void onPageChanged(int row);
    void onOk();
    void onApply();
    void markDirty();

private:
    void setupSidebar();
    void setupPages();
    void setupButtons();

    QWidget* createGeneralPage();
    QWidget* createDisplayPage();
    QWidget* createConnectionPage();
    QWidget* createProxyPage();
    QWidget* createServerPage();
    QWidget* createDirectoriesPage();
    QWidget* createFilesPage();
    QWidget* createNotificationsPage();
    QWidget* createIRCPage();
    QWidget* createMessagesPage();
    QWidget* createStatisticsPage();
    QWidget* createSecurityPage();
    QWidget* createSchedulerPage();
    QWidget* createWebInterfacePage();
    QWidget* createExtendedPage();
    QWidget* createPlaceholderPage(const QString& title);

    void loadSettings();
    void saveSettings();
    void loadSchedulerData();
    void saveSchedulerData();
    void refreshScheduleTable();
    void populateScheduleDetails(int index);
    void applyScheduleDetails();
    void showScheduleActionsMenu(const QPoint& pos);
    static QIcon makePadlockIcon();

    IpcClient* m_ipc = nullptr;
    QListWidget* m_sidebar = nullptr;
    QStackedWidget* m_pages = nullptr;
    QLabel* m_pageHeader = nullptr;
    QPushButton* m_applyBtn = nullptr;
    bool m_loading = false;              // true while IPC callback populates widgets
    bool m_daemonSettingsLoaded = false;  // true after IPC GetPreferences callback

    // General page controls
    QLineEdit* m_nickEdit = nullptr;
    QComboBox* m_langCombo = nullptr;
    QCheckBox* m_promptOnExitCheck = nullptr;
    QCheckBox* m_startMinimizedCheck = nullptr;
    QCheckBox* m_showSplashCheck = nullptr;
    QCheckBox* m_enableOnlineSigCheck = nullptr;
#ifdef Q_OS_WIN
    QCheckBox* m_enableMiniMuleCheck = nullptr;
#endif
    QCheckBox* m_preventStandbyCheck = nullptr;
    QCheckBox* m_startWithOSCheck = nullptr;
    QCheckBox* m_versionCheckBox = nullptr;
    QSpinBox*  m_versionCheckDaysSpin = nullptr;
    QCheckBox* m_bringToFrontCheck = nullptr;

    // Core (IPC) settings
    QLineEdit* m_coreAddressEdit = nullptr;
    QSpinBox*  m_corePortSpin = nullptr;
    QLineEdit* m_coreTokenEdit = nullptr;
    QPushButton* m_shutdownCoreBtn = nullptr;

    // Display page controls
    QSlider* m_depth3DSlider = nullptr;
    QSpinBox* m_tooltipDelaySpin = nullptr;
    QCheckBox* m_minimizeToTrayCheck = nullptr;
    QCheckBox* m_transferDoubleClickCheck = nullptr;
    QCheckBox* m_showDwlPercentageCheck = nullptr;
    QCheckBox* m_showRatesInTitleCheck = nullptr;
    QCheckBox* m_showCatTabInfosCheck = nullptr;
    QCheckBox* m_autoRemoveFinishedCheck = nullptr;
    QCheckBox* m_showTransToolbarCheck = nullptr;
    QCheckBox* m_storeSearchesCheck = nullptr;
    QCheckBox* m_disableKnownClientListCheck = nullptr;
    QCheckBox* m_disableQueueListCheck = nullptr;
    QCheckBox* m_useAutoCompletionCheck = nullptr;
    QCheckBox* m_useOriginalIconsCheck = nullptr;
    bool m_initialUseOriginalIcons = false;
    QPushButton* m_selectFontBtn = nullptr;
    QLabel* m_fontPreviewLabel = nullptr;
    QString m_currentLogFont;

    // Connection page controls
    QSpinBox*  m_capacityDownloadSpin = nullptr;
    QSpinBox*  m_capacityUploadSpin = nullptr;
    QCheckBox* m_downloadLimitCheck = nullptr;
    QCheckBox* m_uploadLimitCheck = nullptr;
    QLabel*    m_downloadLimitLabel = nullptr;
    QLabel*    m_uploadLimitLabel = nullptr;
    QSlider*   m_downloadLimitSlider = nullptr;
    QSlider*   m_uploadLimitSlider = nullptr;
    QSpinBox*  m_tcpPortSpin = nullptr;
    QSpinBox*  m_udpPortSpin = nullptr;
    QCheckBox* m_udpDisableCheck = nullptr;
    QCheckBox* m_upnpCheck = nullptr;
    QSpinBox*  m_maxSourcesSpin = nullptr;
    QSpinBox*  m_maxConnectionsSpin = nullptr;
    QCheckBox* m_autoConnectCheck = nullptr;
    QCheckBox* m_reconnectCheck = nullptr;
    QCheckBox* m_overheadCheck = nullptr;
    QCheckBox* m_kadEnabledCheck = nullptr;
    QCheckBox* m_ed2kEnabledCheck = nullptr;

    // Proxy page controls
    QCheckBox*  m_proxyEnableCheck = nullptr;
    QComboBox*  m_proxyTypeCombo = nullptr;
    QLineEdit*  m_proxyHostEdit = nullptr;
    QSpinBox*   m_proxyPortSpin = nullptr;
    QCheckBox*  m_proxyAuthCheck = nullptr;
    QLineEdit*  m_proxyUserEdit = nullptr;
    QLineEdit*  m_proxyPasswordEdit = nullptr;

    // Server page controls
    QSpinBox*  m_deadServerRetriesSpin = nullptr;
    QCheckBox* m_autoUpdateServerListCheck = nullptr;
    QPushButton* m_listUrlBtn = nullptr;
    QString m_serverListURLValue;
    QCheckBox* m_addServersFromServerCheck = nullptr;
    QCheckBox* m_addServersFromClientsCheck = nullptr;
    QCheckBox* m_smartLowIdCheck = nullptr;
    QCheckBox* m_safeServerConnectCheck = nullptr;
    QCheckBox* m_autoConnectStaticOnlyCheck = nullptr;
    QCheckBox* m_useServerPrioritiesCheck = nullptr;
    QCheckBox* m_manualHighPrioCheck = nullptr;

    // Directories page controls
    QLineEdit* m_incomingDirEdit = nullptr;
    QLineEdit* m_tempDirEdit = nullptr;
    QTreeView* m_sharedDirsTree = nullptr;
    QFileSystemModel* m_sharedDirsModel = nullptr;

    // Files page controls
    QCheckBox* m_addFilesPausedCheck = nullptr;
    QCheckBox* m_autoSharedFilesPrioCheck = nullptr;
    QCheckBox* m_autoDownloadPrioCheck = nullptr;
    QCheckBox* m_autoCleanupFilenamesCheck = nullptr;
    QCheckBox* m_transferFullChunksCheck = nullptr;
    QCheckBox* m_previewPrioCheck = nullptr;
    QCheckBox* m_watchClipboardCheck = nullptr;
    QCheckBox* m_advancedCalcRemainingCheck = nullptr;
    QCheckBox* m_startNextPausedCheck = nullptr;
    QCheckBox* m_preferSameCatCheck = nullptr;
    QCheckBox* m_onlySameCatCheck = nullptr;
    QCheckBox* m_rememberDownloadedCheck = nullptr;
    QCheckBox* m_rememberCancelledCheck = nullptr;
    QLineEdit* m_videoPlayerCmdEdit = nullptr;
    QLineEdit* m_videoPlayerArgsEdit = nullptr;
    QCheckBox* m_createBackupToPreviewCheck = nullptr;

    // Notifications page controls
    QRadioButton* m_noSoundRadio = nullptr;
    QRadioButton* m_playSoundRadio = nullptr;
    QRadioButton* m_speakRadio = nullptr;
    QButtonGroup* m_soundGroup = nullptr;
    QLineEdit* m_soundFileEdit = nullptr;
    QPushButton* m_soundBrowseBtn = nullptr;
    QPushButton* m_testSoundBtn = nullptr;
    QCheckBox* m_notifyLogCheck = nullptr;
    QCheckBox* m_notifyChatCheck = nullptr;
    QCheckBox* m_notifyChatMsgCheck = nullptr;
    QCheckBox* m_notifyDownloadAddedCheck = nullptr;
    QCheckBox* m_notifyDownloadFinishedCheck = nullptr;
    QCheckBox* m_notifyNewVersionCheck = nullptr;
    QCheckBox* m_notifyUrgentCheck = nullptr;
    QCheckBox* m_emailEnabledCheck = nullptr;
    QPushButton* m_smtpServerBtn = nullptr;
    QLineEdit* m_emailRecipientEdit = nullptr;
    QLineEdit* m_emailSenderEdit = nullptr;

    // IRC page controls
    QLineEdit* m_ircServerEdit = nullptr;
    QLineEdit* m_ircNickEdit = nullptr;
    QCheckBox* m_ircUseChannelFilterCheck = nullptr;
    QLineEdit* m_ircChannelFilterNameEdit = nullptr;
    QSpinBox*  m_ircChannelFilterUsersSpin = nullptr;
    QCheckBox* m_ircUsePerformCheck = nullptr;
    QLineEdit* m_ircPerformEdit = nullptr;
    QTreeWidget* m_ircMiscTree = nullptr;

    // Messages and Comments page controls
    QLineEdit* m_messageFilterEdit = nullptr;
    QCheckBox* m_msgFriendsOnlyCheck = nullptr;
    QCheckBox* m_advancedSpamFilterCheck = nullptr;
    QCheckBox* m_requireCaptchaCheck = nullptr;
    QCheckBox* m_showSmileysCheck = nullptr;
    QLineEdit* m_commentFilterEdit = nullptr;
    QCheckBox* m_indicateRatingsCheck = nullptr;

    // Security page controls
    QPushButton*  m_reloadIPFilterBtn = nullptr;
    QCheckBox*    m_filterServersByIPCheck = nullptr;
    QSpinBox*     m_ipFilterLevelSpin = nullptr;
    QButtonGroup* m_viewSharedGroup = nullptr;
    QCheckBox*    m_cryptLayerRequestedCheck = nullptr;
    QCheckBox*    m_cryptLayerRequiredCheck = nullptr;
    QCheckBox*    m_cryptLayerDisableCheck = nullptr;
    QCheckBox*    m_useSecureIdentCheck = nullptr;
    QCheckBox*    m_enableSearchResultFilterCheck = nullptr;
    QCheckBox*    m_warnUntrustedFilesCheck = nullptr;
    QLineEdit*    m_ipFilterUpdateUrlEdit = nullptr;

    // Scheduler page controls
    QCheckBox*    m_schedEnabledCheck = nullptr;
    QTreeWidget*  m_schedTable = nullptr;
    QPushButton*  m_schedRemoveBtn = nullptr;
    QPushButton*  m_schedNewBtn = nullptr;
    QCheckBox*    m_schedEntryEnabledCheck = nullptr;
    QLineEdit*    m_schedTitleEdit = nullptr;
    QComboBox*    m_schedDayCombo = nullptr;
    QTimeEdit*    m_schedStartTime = nullptr;
    QTimeEdit*    m_schedEndTime = nullptr;
    QCheckBox*    m_schedNoEndTimeCheck = nullptr;
    QTreeWidget*  m_schedActionsTable = nullptr;
    QPushButton*  m_schedApplyBtn = nullptr;

    struct SchedUiEntry {
        QString title;
        time_t startTime = 0;
        time_t endTime = 0;
        int day = 0;
        bool enabled = false;
        struct Action { int type = 0; QString value; };
        std::vector<Action> actions;
    };
    std::vector<SchedUiEntry> m_schedEntries;
    int m_schedSelectedIndex = -1;

    // Statistics page controls
    QSlider*     m_statsGraphUpdateSlider = nullptr;
    QLabel*      m_statsGraphUpdateLabel = nullptr;
    QSlider*     m_statsAvgTimeSlider = nullptr;
    QLabel*      m_statsAvgTimeLabel = nullptr;
    QComboBox*   m_statsColorSelector = nullptr;
    QPushButton* m_statsColorBtn = nullptr;
    QCheckBox*   m_statsFillGraphsCheck = nullptr;
    QSpinBox*    m_statsYScaleSpin = nullptr;
    QComboBox*   m_statsRatioCombo = nullptr;
    QSlider*     m_statsTreeUpdateSlider = nullptr;
    QLabel*      m_statsTreeUpdateLabel = nullptr;
    std::array<QColor, 15> m_statsColors;
    StatisticsPanel* m_statsPanel = nullptr;

    // Extended page controls
    QSpinBox*     m_maxConPerFiveSpin = nullptr;
    QSpinBox*     m_maxHalfOpenSpin = nullptr;
    QSpinBox*     m_serverKeepAliveSpin = nullptr;
    QSpinBox*     m_minFreeDiskSpaceSpin = nullptr;
    QSpinBox*     m_logLevelSpin = nullptr;
    QCheckBox*    m_useCreditSystemCheck = nullptr;
    QCheckBox*    m_filterLANIPsCheck = nullptr;
    QCheckBox*    m_a4afSaveCpuCheck = nullptr;
    QCheckBox*    m_disableArchPreviewCheck = nullptr;
    QCheckBox*    m_showExtControlsCheck = nullptr;
    QLineEdit*    m_ed2kHostnameEdit = nullptr;
    QCheckBox*    m_checkDiskspaceCheck = nullptr;
    QCheckBox*    m_logToDiskCheck = nullptr;
    QCheckBox*    m_verboseCheck = nullptr;
    QCheckBox*    m_verboseLogToDiskCheck = nullptr;
    QCheckBox*    m_logSourceExchangeCheck = nullptr;
    QCheckBox*    m_logBannedClientsCheck = nullptr;
    QCheckBox*    m_logRatingDescCheck = nullptr;
    QCheckBox*    m_logSecureIdentCheck = nullptr;
    QCheckBox*    m_logFilteredIPsCheck = nullptr;
    QCheckBox*    m_logFileSavingCheck = nullptr;
    QCheckBox*    m_logA4AFCheck = nullptr;
    QCheckBox*    m_logUlDlEventsCheck = nullptr;
    QCheckBox*    m_logRawSocketPacketsCheck = nullptr;
    QCheckBox*    m_closeUPnPCheck = nullptr;
    QCheckBox*    m_skipWANIPCheck = nullptr;
    QCheckBox*    m_skipWANPPPCheck = nullptr;
    QButtonGroup* m_commitFilesGroup = nullptr;
    QButtonGroup* m_extractMetaDataGroup = nullptr;
    QSlider*      m_fileBufferSlider = nullptr;
    QSlider*      m_queueSizeSlider = nullptr;
    QLabel*       m_fileBufferLabel = nullptr;
    QLabel*       m_queueSizeLabel = nullptr;

    // USS (Upload SpeedSense) controls
    QCheckBox*    m_dynUpEnabledCheck = nullptr;
    QSpinBox*     m_dynUpPingToleranceSpin = nullptr;
    QSpinBox*     m_dynUpPingToleranceMsSpin = nullptr;
    QRadioButton* m_dynUpRadioPercent = nullptr;
    QRadioButton* m_dynUpRadioMs = nullptr;
    QSpinBox*     m_dynUpGoingUpSpin = nullptr;
    QSpinBox*     m_dynUpGoingDownSpin = nullptr;
    QSpinBox*     m_dynUpNumPingsSpin = nullptr;

#ifdef Q_OS_WIN
    // Windows-only Extended page controls
    QCheckBox*    m_autotakeEd2kCheck = nullptr;
    QCheckBox*    m_winFirewallCheck = nullptr;
    QCheckBox*    m_sparsePartFilesCheck = nullptr;
    QCheckBox*    m_allocFullFileCheck = nullptr;
    QCheckBox*    m_resolveShellLinksCheck = nullptr;
    QButtonGroup* m_multiUserSharingGroup = nullptr;
#endif

    // Web Interface page controls
    QCheckBox*    m_webEnabledCheck = nullptr;
    QCheckBox*    m_webRestApiCheck = nullptr;
    QCheckBox*    m_webGzipCheck = nullptr;
    QCheckBox*    m_webUPnPCheck = nullptr;
    QSpinBox*     m_webPortSpin = nullptr;
    QLineEdit*    m_webTemplateEdit = nullptr;
    QPushButton*  m_webTemplateBrowseBtn = nullptr;
    QPushButton*  m_webTemplateReloadBtn = nullptr;
    QSpinBox*     m_webSessionTimeoutSpin = nullptr;
    QCheckBox*    m_webHttpsCheck = nullptr;
    QPushButton*  m_webCreateCertBtn = nullptr;
    QLineEdit*    m_webCertEdit = nullptr;
    QPushButton*  m_webCertBrowseBtn = nullptr;
    QLineEdit*    m_webKeyEdit = nullptr;
    QPushButton*  m_webKeyBrowseBtn = nullptr;
    QLineEdit*    m_webApiKeyEdit = nullptr;
    QLineEdit*    m_webAdminPasswordEdit = nullptr;
    QCheckBox*    m_webAdminHiLevCheck = nullptr;
    QCheckBox*    m_webGuestEnabledCheck = nullptr;
    QLineEdit*    m_webGuestPasswordEdit = nullptr;

    // SMTP dialog state (not persisted as controls, stored transiently)
    QString m_smtpServer;
    int m_smtpPort = 25;
    int m_smtpAuth = 0;
    bool m_smtpTls = false;
    QString m_smtpUser;
    QString m_smtpPassword;
};

} // namespace eMule
