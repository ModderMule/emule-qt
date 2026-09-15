#pragma once

/// @file OptionsDialog.h
/// @brief Options/Preferences dialog matching the MFC eMule Options window.
///
/// Left sidebar with category icons, right stacked widget for page content,
/// and OK/Cancel/Apply buttons at the bottom.

#include "app/UiState.h"   // kStatsColorCount — the palette this dialog edits

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
class QRadioButton;
class QStackedWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class QTreeView;
class QFileSystemModel;
class QColorDialog;
class QTimeEdit;

namespace eMule {

class AccordionSidebar;
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

    /// Indices into the stacked widget, and the value persisted as
    /// UiState::optionsLastPage(). The sidebar groups these differently and carries
    /// the values as item ids, so this order is free to stay put -- reordering it
    /// would silently repoint every stored index and every numeric --options N.
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
        PageUsenet,
        PageIndexers,
        PageFeeds,
        PageExtended,
        PageCount
    };

protected:
    /// Capture the active sidebar page on every close path (OK, Cancel, Esc,
    /// window close) so the next open restores it.
    void done(int result) override;

private slots:
    void onPageChanged(int row);
    void onOk();
    void onApply();
    void markDirty();

private:
    void setupSidebar();
    void setupPages();
    void setupButtons();

    /// Whether setupPages() gives a page its scrollbar, or the page brings its own.
    enum class PageScroll {
        Wrap,   ///< wrapped in a ContentScrollArea by addPage()
        Self,   ///< scrolls internally; a second area would mean two scrollbars
    };

    /// Add @p page at stack index @p id. A page left unwrapped sets the minimum
    /// height of *every* page, because QStackedLayout's minimum is the max over all
    /// of them -- so the wrapping is stated here once rather than in 18 builders.
    void addPage(Page id, QWidget* page, PageScroll scroll = PageScroll::Wrap);

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
    QWidget* createUsenetPage();
    QWidget* createIndexersPage();
    QWidget* createFeedsPage();
    QWidget* createExtendedPage();
    QWidget* createPlaceholderPage(const QString& title);

    /// Enable/disable the Web Interface controls from the two independent
    /// checkboxes (web UI + REST API). Neither checkbox greys the other; this
    /// only greys sub-controls that have no effect for the current selection.
    void updateWebEnabledStates();

    void loadSettings();
    void saveSettings();
    void fillDaemonSettings(const QCborMap& prefs);
    void fillDaemonSettingsFromPrefs();
    void loadSchedulerData();
    void saveSchedulerData();

    // -- Usenet -------------------------------------------------------------
    // The server list travels over its own opcodes rather than the generic
    // preference map, because it carries credentials: the daemon never sends a
    // password to the GUI, and an entry saved without one keeps the stored one.
    void loadNewsServers();
    void saveNewsServers();
    void refreshNewsServerTable();
    void updateNewsServerRow(int index);
    void selectNewsServer(int index);
    void populateNewsServerDetails(int index);
    void applyNewsServerDetails();
    /// Refresh the "Used" line for the selected account. The figure is the
    /// daemon's measurement and rides read-only on GetNewsServers.
    void updateNewsServerUsageLabel();
    /// Type in what the provider actually says, or 0 to start again.
    void onCorrectNewsServerUsage();
    void addNewsServer();
    void removeNewsServer();
    void testNewsServer();

    // Indexers — the same eight-function shape as the news-server list above,
    // deliberately: they solve the same problem (a list whose secrets the GUI is
    // never given) and diverging would mean two answers to it.
    void loadIndexers();
    void saveIndexers();
    void refreshIndexerTable();
    void updateIndexerRow(int index);
    void selectIndexer(int index);
    void populateIndexerDetails(int index);
    void applyIndexerDetails();
    void addIndexer();
    void removeIndexer();
    void testIndexer();

    // Feeds — the same shape again, for the same reason. The one difference is
    // "Check now" where the indexer page has "Test": a feed is not something you
    // can test, only something you can make run early.
    void loadFeeds();

    /// Fill the feed page's download-category combo from the daemon's list.
    /// Index 0 is skipped: "All" is the absence of a category, and the combo
    /// already offers that as "No category".
    void loadDownloadCategories();
    void saveFeeds();
    void refreshFeedTable();
    void updateFeedRow(int index);
    void selectFeed(int index);
    void populateFeedDetails(int index);
    void applyFeedDetails();
    void addFeed();
    void removeFeed();
    void checkFeedNow();

    /// Apply one PushIndexerFeedStatus report to the table, so a feed that polls
    /// while the dialog is open updates in place.
    void applyFeedStatus(const QCborMap& status);

    /// Scroll a test result into view. Shared by both pages; see the definition for
    /// why the label's own height is a message stale when this is called.
    void revealTestResult(QLabel* result);

    void updateUsenetEnabledStates();
    void refreshScheduleTable();
    void populateScheduleDetails(int index);
    void applyScheduleDetails();
    void showScheduleActionsMenu(const QPoint& pos);

    /// Open the web port test for the ports currently entered in the Connection page.
    /// Asks the daemon for its public addresses first, so both address families get a verdict.
    void openPortTest();

    /// Launch the port test page. Empty @p ipv4 / @p ipv6 hints are omitted from the URL.
    void openPortTestUrl(int tcpPort, int udpPort, const QString& ipv4, const QString& ipv6);

    static QIcon makePadlockIcon();

    IpcClient* m_ipc = nullptr;
    AccordionSidebar* m_sidebar = nullptr;
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
    QCheckBox* m_showSpeedGraphCheck = nullptr;
    QSpinBox*  m_speedGraphTimeSpin = nullptr;
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
    /// Bitmask for the portMapProtocols pref: 1 = PCP, 2 = NAT-PMP, 4 = UPnP.
    [[nodiscard]] quint32 portMapProtocolMask() const;

    QCheckBox* m_upnpCheck = nullptr;
    QLabel*    m_portMapStatusLabel = nullptr;
    QSpinBox*  m_maxSourcesSpin = nullptr;
    QSpinBox*  m_maxConnectionsSpin = nullptr;
    QCheckBox* m_autoConnectCheck = nullptr;
    QCheckBox* m_reconnectCheck = nullptr;
    QCheckBox* m_overheadCheck = nullptr;
    QCheckBox* m_kadEnabledCheck = nullptr;
    QCheckBox* m_ed2kEnabledCheck = nullptr;
    QCheckBox* m_separateIPv6QueueCheck = nullptr;

    // Proxy page controls
    QCheckBox*  m_proxyEnableCheck = nullptr;
    QComboBox*  m_proxyTypeCombo = nullptr;
    QLineEdit*  m_proxyHostEdit = nullptr;
    QSpinBox*   m_proxyPortSpin = nullptr;
    QCheckBox*  m_proxyAuthCheck = nullptr;
    QLineEdit*  m_proxyUserEdit = nullptr;
    QLineEdit*  m_proxyPasswordEdit = nullptr;
    QCheckBox*  m_proxyUsenetCheck = nullptr;

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
    QCheckBox* m_useUserSortedServerListCheck = nullptr;
    QCheckBox* m_manualHighPrioCheck = nullptr;

    // Directories page controls
    QLineEdit* m_incomingDirEdit = nullptr;
    QLineEdit* m_tempDirEdit = nullptr;
    QTreeView* m_sharedDirsTree = nullptr;
    QFileSystemModel* m_sharedDirsModel = nullptr;

    // Files page controls
    QCheckBox* m_addFilesPausedCheck = nullptr;
    QCheckBox* m_saveLoadSourcesCheck = nullptr;
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
    // -- Usenet page --------------------------------------------------------
    QCheckBox*    m_usenetEnabledCheck = nullptr;
    QSpinBox*     m_usenetRetrySpin = nullptr;
    QSpinBox*     m_usenetShareSpin = nullptr;
    QCheckBox*    m_usenetPar2Check = nullptr;
    QCheckBox*    m_usenetRenameCheck = nullptr;
    QCheckBox*    m_usenetUnpackCheck = nullptr;
    QCheckBox*    m_usenetDirectUnpackCheck = nullptr;
    QCheckBox*    m_usenetEncryptedPreviewCheck = nullptr;
    QLineEdit*    m_usenetUnpackerEdit = nullptr;
    QComboBox*    m_usenetHealthCombo = nullptr;
    QSpinBox*     m_usenetHealthMinSpin = nullptr;
    QCheckBox*    m_usenetAutoPausedCheck = nullptr;
    QLineEdit*    m_usenetWatchDirEdit = nullptr;
    QPushButton*  m_usenetWatchDirBrowse = nullptr;
    QCheckBox*    m_associateNzbCheck = nullptr;
    QCheckBox*    m_usenetCleanupCheck = nullptr;
    QCheckBox*    m_usenetSfvCheck = nullptr;
    QComboBox*    m_usenetUnrepairableCombo = nullptr;
    QComboBox*    m_usenetUnwantedCombo = nullptr;
    QLineEdit*    m_usenetUnwantedEdit = nullptr;
    QTreeWidget*  m_usenetServerTable = nullptr;
    QPushButton*  m_usenetAddBtn = nullptr;
    QPushButton*  m_usenetRemoveBtn = nullptr;
    QPushButton*  m_usenetTestBtn = nullptr;
    QLineEdit*    m_usenetNameEdit = nullptr;
    QLineEdit*    m_usenetHostEdit = nullptr;
    QSpinBox*     m_usenetPortSpin = nullptr;
    QComboBox*    m_usenetTlsCombo = nullptr;
    QComboBox*    m_usenetCertCombo = nullptr;
    QLineEdit*    m_usenetUserEdit = nullptr;
    QLineEdit*    m_usenetPassEdit = nullptr;
    QSpinBox*     m_usenetLevelSpin = nullptr;
    QSpinBox*     m_usenetConnSpin = nullptr;
    QSpinBox*     m_usenetRetentionSpin = nullptr;
    QSpinBox*     m_usenetGroupSpin = nullptr;
    QCheckBox*    m_usenetEntryEnabledCheck = nullptr;
    QCheckBox*    m_usenetOptionalCheck = nullptr;
    QCheckBox*    m_usenetJoinGroupCheck = nullptr;
    QComboBox*    m_usenetQuotaKindCombo = nullptr;
    QDoubleSpinBox* m_usenetQuotaSpin = nullptr;
    QSpinBox*     m_usenetQuotaDaySpin = nullptr;
    QCheckBox*    m_usenetQuotaFallThroughCheck = nullptr;
    QLabel*       m_usenetUsageLabel = nullptr;
    QPushButton*  m_usenetUsageEditBtn = nullptr;
    QLabel*       m_usenetTestResult = nullptr;

    /// Working copy of the server list. `password` is only set on an entry the
    /// user actually retyped; every other entry is sent without the field so the
    /// daemon keeps what it has.
    QList<QCborMap> m_newsServers;
    int m_currentNewsServer = -1;

    // Indexers page
    QTreeWidget*  m_indexerTable = nullptr;
    QPushButton* m_indexerAddBtn = nullptr;
    QPushButton* m_indexerRemoveBtn = nullptr;
    QPushButton* m_indexerTestBtn = nullptr;
    QLabel* m_indexerTestResult = nullptr;
    QCheckBox* m_indexerEntryEnabledCheck = nullptr;
    QLineEdit* m_indexerNameEdit = nullptr;
    QLineEdit* m_indexerUrlEdit = nullptr;
    QLineEdit* m_indexerApiKeyEdit = nullptr;
    QComboBox* m_indexerKindCombo = nullptr;
    QSpinBox* m_indexerLimitSpin = nullptr;
    QSpinBox* m_indexerPagesSpin = nullptr;
    QSpinBox* m_indexerTimeoutSpin = nullptr;
    QSpinBox* m_indexerCapsRefreshSpin = nullptr;

    QList<QCborMap> m_indexers;
    int m_currentIndexer = -1;

    // Feeds page
    QTreeWidget* m_feedTable = nullptr;
    QPushButton* m_feedAddBtn = nullptr;
    QPushButton* m_feedRemoveBtn = nullptr;
    QPushButton* m_feedCheckBtn = nullptr;
    QLabel* m_feedStatusLabel = nullptr;
    QCheckBox* m_feedEnabledCheck = nullptr;
    QLineEdit* m_feedNameEdit = nullptr;
    QComboBox* m_feedKindCombo = nullptr;
    QLineEdit* m_feedQueryEdit = nullptr;
    QLineEdit* m_feedCategoriesEdit = nullptr;
    QLineEdit* m_feedIndexersEdit = nullptr;
    QLineEdit* m_feedUrlEdit = nullptr;
    QLineEdit* m_feedAcceptEdit = nullptr;
    QLineEdit* m_feedRejectEdit = nullptr;
    QSpinBox* m_feedMinSizeSpin = nullptr;
    QSpinBox* m_feedMaxSizeSpin = nullptr;
    QSpinBox* m_feedMaxAgeSpin = nullptr;
    QSpinBox* m_feedIntervalSpin = nullptr;
    QCheckBox* m_feedGrabExistingCheck = nullptr;
    QComboBox* m_feedDownloadCategoryCombo = nullptr;

    QList<QCborMap> m_feeds;
    int m_currentFeed = -1;

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
    /// Working copy of the graph palette; committed to theUiState on Apply.
    std::array<QColor, UiState::kStatsColorCount> m_statsColors;
    StatisticsPanel* m_statsPanel = nullptr;

    // Extended page controls
    QSpinBox*     m_maxConPerFiveSpin = nullptr;
    QSpinBox*     m_maxHalfOpenSpin = nullptr;
    QSpinBox*     m_serverKeepAliveSpin = nullptr;
    QSpinBox*     m_minFreeDiskSpaceSpin = nullptr;
    QSpinBox*     m_logLevelSpin = nullptr;
    QCheckBox*    m_useCreditSystemCheck = nullptr;
    QCheckBox*    m_rememberUploadQueueCheck = nullptr;
    QCheckBox*    m_filterLANIPsCheck = nullptr;
    QCheckBox*    m_a4afSaveCpuCheck = nullptr;
    QCheckBox*    m_disableArchPreviewCheck = nullptr;
    QCheckBox*    m_showExtControlsCheck = nullptr;
    QLineEdit*    m_ed2kHostnameEdit = nullptr;
    QCheckBox*    m_ed2kLinkAdvertiseIPv6Check = nullptr;
    QCheckBox*    m_checkDiskspaceCheck = nullptr;
    QCheckBox*    m_logToDiskCoreCheck = nullptr;
    QCheckBox*    m_logToDiskGuiCheck = nullptr;
    QCheckBox*    m_verboseCheck = nullptr;
    QCheckBox*    m_logSourceExchangeCheck = nullptr;
    QCheckBox*    m_serverVerboseCheck = nullptr;
    QCheckBox*    m_logBannedClientsCheck = nullptr;
    QCheckBox*    m_logRatingDescCheck = nullptr;
    QCheckBox*    m_logSecureIdentCheck = nullptr;
    QCheckBox*    m_logFilteredIPsCheck = nullptr;
    QCheckBox*    m_logFileSavingCheck = nullptr;
    QCheckBox*    m_logA4AFCheck = nullptr;
    QCheckBox*    m_logUlDlEventsCheck = nullptr;
    QCheckBox*    m_logRawSocketPacketsCheck = nullptr;
    QCheckBox*    m_logWebServerCheck = nullptr;
    QCheckBox*    m_logPublicIPCheck = nullptr;
    QCheckBox*    m_enableIpcLogCheck = nullptr;
    QCheckBox*    m_startCoreWithConsoleCheck = nullptr;
    QCheckBox*    m_closeUPnPCheck = nullptr;
    QCheckBox*    m_portMapPcpCheck = nullptr;
    QCheckBox*    m_portMapNatPmpCheck = nullptr;
    QCheckBox*    m_portMapUPnPCheck = nullptr;
    QCheckBox*    m_portMapIPv6Check = nullptr;
    QSpinBox*     m_portMapLeaseSpin = nullptr;
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
