#pragma once

/// @file OptionsDialog.h
/// @brief Options/Preferences dialog matching the MFC eMule Options window.
///
/// Left sidebar with category icons, right stacked widget for page content,
/// and OK/Cancel/Apply buttons at the bottom.

#include <QDialog>
#include <QIcon>

class QLabel;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QListWidget;
class QStackedWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QTreeView;
class QFileSystemModel;

namespace eMule {

class IpcClient;

class OptionsDialog : public QDialog {
    Q_OBJECT

public:
    explicit OptionsDialog(IpcClient* ipc, QWidget* parent = nullptr);
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
    QWidget* createPlaceholderPage(const QString& title);

    void loadSettings();
    void saveSettings();
    static QIcon makePadlockIcon();

    IpcClient* m_ipc = nullptr;
    bool m_loading = false;
    QListWidget* m_sidebar = nullptr;
    QStackedWidget* m_pages = nullptr;
    QLabel* m_pageHeader = nullptr;
    QPushButton* m_applyBtn = nullptr;

    // General page controls
    QLineEdit* m_nickEdit = nullptr;
    QComboBox* m_langCombo = nullptr;
    QCheckBox* m_promptOnExitCheck = nullptr;
    QCheckBox* m_startMinimizedCheck = nullptr;

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
};

} // namespace eMule
