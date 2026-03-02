#include "dialogs/OptionsDialog.h"

#include "app/IpcClient.h"
#include "prefs/Preferences.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCborArray>

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTreeView>
#include <QVBoxLayout>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

OptionsDialog::OptionsDialog(IpcClient* ipc, QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
{
    setWindowTitle(tr("Options"));
    resize(800, 640);

    auto* mainLayout = new QHBoxLayout;

    // Left sidebar
    m_sidebar = new QListWidget(this);
    setupSidebar();
    m_sidebar->setFixedWidth(200);
    m_sidebar->setIconSize(QSize(24, 24));
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
    helpBtn->setEnabled(false); // ToDo: implement help system
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

    // Select first page
    m_sidebar->setCurrentRow(PageGeneral);

    // Load current settings into controls (before wiring change signals
    // so that loading doesn't immediately mark the dialog dirty).
    loadSettings();

    // Apply starts disabled — enabled only when a setting changes.
    m_applyBtn->setEnabled(false);

    // Wire change signals from all editable controls to markDirty.
    connect(m_nickEdit, &QLineEdit::textChanged, this, &OptionsDialog::markDirty);
    connect(m_promptOnExitCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);
    connect(m_startMinimizedCheck, &QCheckBox::toggled, this, &OptionsDialog::markDirty);

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

// ---------------------------------------------------------------------------
// Setup: sidebar
// ---------------------------------------------------------------------------

void OptionsDialog::setupSidebar()
{
    struct PageDef {
        const char* label;
        QStyle::StandardPixmap icon;
    };

    static constexpr PageDef pages[] = {
        {"General",            QStyle::SP_FileDialogDetailedView},
        {"Display",            QStyle::SP_DesktopIcon},
        {"Connection",         QStyle::SP_DriveNetIcon},
        {"Proxy",              QStyle::SP_BrowserReload},
        {"Server",             QStyle::SP_ComputerIcon},
        {"Directories",        QStyle::SP_DirIcon},
        {"Files",              QStyle::SP_FileIcon},
        {"Notifications",      QStyle::SP_MessageBoxInformation},
        {"Statistics",         QStyle::SP_DialogHelpButton},
        {"IRC",                QStyle::SP_DialogApplyButton},
        {"Messages and Comments", QStyle::SP_MessageBoxQuestion},
        {"Security",           QStyle::SP_CustomBase},  // placeholder, painted below
        {"Scheduler",          QStyle::SP_DialogResetButton},
        {"Web Interface",      QStyle::SP_DriveNetIcon},
        {"Extended",           QStyle::SP_DialogCancelButton},
    };

    for (const auto& [label, icon] : pages) {
        QIcon qicon;
        if (icon == QStyle::SP_CustomBase)
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

    // Placeholder pages for the rest
    static const char* placeholderNames[] = {
        "Notifications", "Statistics", "IRC",
        "Messages and Comments", "Security", "Scheduler",
        "Web Interface", "Extended"
    };
    for (const char* name : placeholderNames)
        m_pages->addWidget(createPlaceholderPage(tr(name)));
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
    m_langCombo->addItem(QStringLiteral("Deutsch (Deutschland)"), QStringLiteral("de_DE"));
    m_langCombo->addItem(QStringLiteral("Fran\u00e7ais (France)"), QStringLiteral("fr_FR"));
    m_langCombo->addItem(QStringLiteral("Espa\u00f1ol (Espa\u00f1a)"), QStringLiteral("es_ES"));
    m_langCombo->addItem(QStringLiteral("Italiano (Italia)"), QStringLiteral("it_IT"));
    m_langCombo->addItem(QStringLiteral("Portugu\u00eas (Brasil)"), QStringLiteral("pt_BR"));
    m_langCombo->addItem(QStringLiteral("\u4e2d\u6587 (\u4e2d\u56fd)"), QStringLiteral("zh_CN"));
    m_langCombo->addItem(QStringLiteral("\u65e5\u672c\u8a9e (\u65e5\u672c)"), QStringLiteral("ja_JP"));
    m_langCombo->addItem(QStringLiteral("\ud55c\uad6d\uc5b4 (\ub300\ud55c\ubbfc\uad6d)"), QStringLiteral("ko_KR"));
    m_langCombo->setEnabled(false); // ToDo: implement language switching
    langLayout->addWidget(m_langCombo);
    layout->addWidget(langGroup);

    // --- Miscellaneous group ---
    auto* miscGroup = new QGroupBox(tr("Miscellaneous"), page);
    auto* miscLayout = new QVBoxLayout(miscGroup);

    auto* bringToFrontCheck = new QCheckBox(tr("Bring to front on link click"), miscGroup);
    bringToFrontCheck->setEnabled(false); // ToDo: implement eD2K link handling
    miscLayout->addWidget(bringToFrontCheck);

    m_promptOnExitCheck = new QCheckBox(tr("Prompt on exit"), miscGroup);
    miscLayout->addWidget(m_promptOnExitCheck);

    auto* onlineSigCheck = new QCheckBox(tr("Enable online signature"), miscGroup);
    onlineSigCheck->setEnabled(false); // ToDo: implement online signature
    miscLayout->addWidget(onlineSigCheck);

    auto* miniMuleCheck = new QCheckBox(tr("Enable MiniMule"), miscGroup);
    miniMuleCheck->setEnabled(false); // ToDo: not applicable to Qt version
    miscLayout->addWidget(miniMuleCheck);

    auto* standbyCheck = new QCheckBox(tr("Prevent standby mode while running"), miscGroup);
    standbyCheck->setEnabled(false); // ToDo: platform-specific power management
    miscLayout->addWidget(standbyCheck);

    // Button row
    auto* miscBtnLayout = new QHBoxLayout;
    auto* webServicesBtn = new QPushButton(tr("Edit Web Services..."), miscGroup);
    webServicesBtn->setEnabled(false); // ToDo: implement web services editor
    auto* ed2kLinksBtn = new QPushButton(tr("Handle eD2K Links"), miscGroup);
    ed2kLinksBtn->setEnabled(false); // ToDo: implement eD2K link registration
    miscBtnLayout->addWidget(webServicesBtn);
    miscBtnLayout->addWidget(ed2kLinksBtn);
    miscBtnLayout->addStretch();
    miscLayout->addLayout(miscBtnLayout);

    layout->addWidget(miscGroup);

    // --- Startup group ---
    auto* startupGroup = new QGroupBox(tr("Startup"), page);
    auto* startupLayout = new QVBoxLayout(startupGroup);

    auto* versionCheckRow = new QHBoxLayout;
    auto* versionCheck = new QCheckBox(tr("Check for new version"), startupGroup);
    versionCheck->setEnabled(false); // ToDo: implement version checking
    versionCheckRow->addWidget(versionCheck);
    versionCheckRow->addStretch();
    startupLayout->addLayout(versionCheckRow);

    auto* splashCheck = new QCheckBox(tr("Show splash screen"), startupGroup);
    splashCheck->setEnabled(false); // ToDo: implement splash screen
    startupLayout->addWidget(splashCheck);

    m_startMinimizedCheck = new QCheckBox(tr("Start minimized"), startupGroup);
    startupLayout->addWidget(m_startMinimizedCheck);

    auto* startWithOSCheck = new QCheckBox(
#ifdef Q_OS_MACOS
        tr("Start with macOS")
#elif defined(Q_OS_WIN)
        tr("Start with Windows")
#else
        tr("Start with system")
#endif
        , startupGroup);
    startWithOSCheck->setEnabled(false); // ToDo: implement autostart registration
    startupLayout->addWidget(startWithOSCheck);

    layout->addWidget(startupGroup);

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
    auto* selectFontBtn = new QPushButton(tr("Select Font..."), fontGroup);
    selectFontBtn->setEnabled(false); // ToDo: implement font selection
    fontLayout->addWidget(selectFontBtn);
    fontLayout->addStretch();
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
    testPortsBtn->setEnabled(false); // ToDo: implement port testing
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
    auto* overheadCheck = new QCheckBox(tr("Show overhead bandwidth"), page);
    overheadCheck->setEnabled(false); // ToDo: implement overhead bandwidth display
    checkLayout->addWidget(overheadCheck);
    auto* wizardBtn = new QPushButton(tr("Wizard..."), page);
    wizardBtn->setEnabled(false); // ToDo: implement connection wizard
    wizardBtn->setFixedWidth(100);
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
    m_deadServerRetriesSpin->setRange(1, 10);
    m_deadServerRetriesSpin->setValue(1);
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
    editCleanupBtn->setEnabled(false); // ToDo: implement cleanup rules editor
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
    m_loading = true;

    // GUI-only settings from local preferences
    m_promptOnExitCheck->setChecked(thePrefs.promptOnExit());
    m_startMinimizedCheck->setChecked(thePrefs.startMinimized());

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

    // Files page (GUI-only)
    m_watchClipboardCheck->setChecked(thePrefs.watchClipboard4ED2KLinks());
    m_advancedCalcRemainingCheck->setChecked(thePrefs.useAdvancedCalcRemainingTime());
    m_videoPlayerCmdEdit->setText(thePrefs.videoPlayerCommand());
    m_videoPlayerArgsEdit->setText(thePrefs.videoPlayerArgs());
    m_createBackupToPreviewCheck->setChecked(thePrefs.createBackupToPreview());
    m_autoCleanupFilenamesCheck->setChecked(thePrefs.autoCleanupFilenames());

    // Daemon settings — request via IPC
    if (m_ipc && m_ipc->isConnected()) {
        Ipc::IpcMessage req(Ipc::IpcMsgType::GetPreferences);
        m_ipc->sendRequest(std::move(req), [this](const Ipc::IpcMessage& resp) {
            if (!resp.fieldBool(0))
                return;
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

            m_loading = false;
            m_applyBtn->setEnabled(false);
        });
    } else {
        // Fallback: use local preferences copy
        m_nickEdit->setText(thePrefs.nick());

        // Connection page fallback
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
        m_kadEnabledCheck->setChecked(thePrefs.kadEnabled());
        m_ed2kEnabledCheck->setChecked(thePrefs.networkED2K());

        // Server page fallback
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

        // Proxy page fallback
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

        // Directories page fallback
        m_incomingDirEdit->setText(thePrefs.incomingDir());
        auto tempDirs = thePrefs.tempDirs();
        if (!tempDirs.isEmpty())
            m_tempDirEdit->setText(tempDirs.first());
        static_cast<CheckableFileSystemModel*>(m_sharedDirsModel)->setCheckedPaths(thePrefs.sharedDirs());

        // Files page (daemon-side) fallback
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

        m_loading = false;
    }
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

    // Files page (GUI-only)
    thePrefs.setWatchClipboard4ED2KLinks(m_watchClipboardCheck->isChecked());
    thePrefs.setUseAdvancedCalcRemainingTime(m_advancedCalcRemainingCheck->isChecked());
    thePrefs.setVideoPlayerCommand(m_videoPlayerCmdEdit->text());
    thePrefs.setVideoPlayerArgs(m_videoPlayerArgsEdit->text());
    thePrefs.setCreateBackupToPreview(m_createBackupToPreviewCheck->isChecked());
    thePrefs.setAutoCleanupFilenames(m_autoCleanupFilenamesCheck->isChecked());

    thePrefs.save();

    // Daemon settings — send via IPC
    if (m_ipc && m_ipc->isConnected()) {
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
        req.append(QStringLiteral("kadEnabled"));
        req.append(m_kadEnabledCheck->isChecked());
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

        thePrefs.save();
    }
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
