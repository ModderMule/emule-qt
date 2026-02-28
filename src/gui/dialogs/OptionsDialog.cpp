#include "dialogs/OptionsDialog.h"

#include "app/IpcClient.h"
#include "prefs/Preferences.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
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

    // Load current settings into controls
    loadSettings();
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

    // Placeholder pages for the rest
    static const char* placeholderNames[] = {
        "Display", "Connection", "Proxy", "Server", "Directories",
        "Files", "Notifications", "Statistics", "IRC",
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

    // Daemon settings — request via IPC
    if (m_ipc && m_ipc->isConnected()) {
        Ipc::IpcMessage req(Ipc::IpcMsgType::GetPreferences);
        m_ipc->sendRequest(std::move(req), [this](const Ipc::IpcMessage& resp) {
            if (!resp.fieldBool(0))
                return;
            const QCborMap prefs = resp.fieldMap(1);
            m_nickEdit->setText(prefs.value(QStringLiteral("nick")).toString());
        });
    } else {
        // Fallback: use local preferences copy
        m_nickEdit->setText(thePrefs.nick());
    }
}

// ---------------------------------------------------------------------------
// Save settings from controls
// ---------------------------------------------------------------------------

void OptionsDialog::saveSettings()
{
    // GUI-only settings — save locally
    thePrefs.setPromptOnExit(m_promptOnExitCheck->isChecked());
    thePrefs.setStartMinimized(m_startMinimizedCheck->isChecked());
    thePrefs.save();

    // Daemon settings — send via IPC
    if (m_ipc && m_ipc->isConnected()) {
        Ipc::IpcMessage req(Ipc::IpcMsgType::SetPreferences);
        req.append(QStringLiteral("nick"));
        req.append(m_nickEdit->text());
        m_ipc->sendRequest(std::move(req));
    } else {
        // Fallback: save locally
        thePrefs.setNick(m_nickEdit->text());
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
