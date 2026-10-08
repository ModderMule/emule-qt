#include "pch.h"
#include "dialogs/FirstStartWizard.h"

#include "dialogs/PortChangeNotice.h"
#include "dialogs/PortMapStatusText.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "prefs/Preferences.h"
#include "utils/DialogSizing.h"
#include "utils/StringUtils.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMovie>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace eMule {

namespace {

// Speed list rows: what a row stands for, in the item's UserRole. >= 0 is a preset index.
constexpr int kRowKeep = -3;
constexpr int kRowRecommended = -2;
constexpr int kRowCustom = -1;

constexpr int kPagePorts = 0;
constexpr int kPageSpeed = 1;

// A limit in KiB/s as shown in the list; 0 = unlimited.
QString limitText(uint32 kib)
{
    return kib > 0 ? formatByteRate(quint64(kib) * 1024, 0) : FirstStartWizard::tr("Unlimited");
}

} // namespace

FirstStartWizard::FirstStartWizard(IpcClient* ipc, QWidget* parent, StartPage startPage)
    : QDialog(parent)
    , m_ipc(ipc)
{
    setWindowTitle(tr("eMule First Runtime Wizard"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/Wizard.ico")));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 8);
    mainLayout->setSpacing(0);

    setupHeader();

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(setupPortPage());
    m_pages->addWidget(setupSpeedPage());
    mainLayout->addWidget(m_pages, 1);

    setupButtons();

    // GUI's own copy first, so the pages are never empty; the daemon's answer replaces it.
    fillFromSettings(thePrefs.toIpcMap());
    requestDaemonSettings();

    showPage(startPage == StartPage::Speed ? kPageSpeed : kPagePorts);

    // Fixed like the MFC wizard, but never smaller than its own explanatory text.
    DialogSizing::applyFixedSize(this, QSize(530, 460));
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
    m_titleLabel = new QLabel(header);
    auto titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    m_titleLabel->setFont(titleFont);

    m_subtitleLabel = new QLabel(header);
    m_subtitleLabel->setContentsMargins(16, 0, 0, 0);

    textLayout->addWidget(m_titleLabel);
    textLayout->addWidget(m_subtitleLabel);
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
// Page 1 — TCP/UDP ports + UPnP button, Kad / eD2K checkboxes
// ---------------------------------------------------------------------------

QWidget* FirstStartWizard::setupPortPage()
{
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);
    DialogSizing::enableHeightForWidth(page);

    auto* container = new QWidget(page);
    auto* vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(16, 12, 16, 4);
    // Relays the wrapped height of the two paragraphs below up to the fixed dialog size.
    DialogSizing::enableHeightForWidth(container);

    auto* descLabel = new QLabel(
        tr("eMule uses two ports for communication with servers and clients. "
           "These ports must be free and available for remote clients. "
           "The TCP port must be available to ensure the main functionality of eMule. "
           "The UDP port is used for Kad (serverless network) and to reduce "
           "network usage (Overhead)."),
        container);
    descLabel->setWordWrap(true);
    DialogSizing::enableHeightForWidth(descLabel);
    vbox->addWidget(descLabel);

    vbox->addSpacing(4);

    auto* changeLabel = new QLabel(
        tr("You can change the ports here while no network activities have started."),
        container);
    changeLabel->setWordWrap(true);
    DialogSizing::enableHeightForWidth(changeLabel);
    vbox->addWidget(changeLabel);

    vbox->addSpacing(8);

    // Port row: TCP + UDP + UPnP button
    auto* portRow = new QHBoxLayout;

    auto* tcpLabel = new QLabel(tr("TCP:"), container);
    m_tcpPortSpin = new QSpinBox(container);
    m_tcpPortSpin->setRange(1, 65535);

    auto* udpLabel = new QLabel(tr("UDP:"), container);
    m_udpPortSpin = new QSpinBox(container);
    m_udpPortSpin->setRange(1, 65535);
    m_udpDisableCheck = new QCheckBox(tr("Disable"), container);
    connect(m_udpDisableCheck, &QCheckBox::toggled, m_udpPortSpin, &QWidget::setDisabled);

    m_upnpBtn = new QPushButton(tr("Use UPnP to Setup Ports"), container);

    portRow->addWidget(tcpLabel);
    portRow->addWidget(m_tcpPortSpin);
    portRow->addSpacing(12);
    portRow->addWidget(udpLabel);
    portRow->addWidget(m_udpPortSpin);
    portRow->addWidget(m_udpDisableCheck);
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

    // Result of the mapping; two lines reserved so the fixed-size dialog never clips it
    m_upnpStatus = new QLabel(container);
    m_upnpStatus->setObjectName(QStringLiteral("upnpStatus"));
    m_upnpStatus->setWordWrap(true);
    m_upnpStatus->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_upnpStatus->setMinimumHeight(2 * m_upnpStatus->fontMetrics().lineSpacing());
    vbox->addWidget(m_upnpStatus);

    // Safety net only: the daemon reports the outcome, see showPortMapStatus()
    m_upnpTimer = new QTimer(this);
    m_upnpTimer->setSingleShot(true);
    m_upnpTimer->setInterval(30000);
    connect(m_upnpTimer, &QTimer::timeout, this, &FirstStartWizard::onUPnPTimeout);
    if (m_ipc) {
        // Only while the button's request is open; later changes are not this dialog's news
        connect(m_ipc, &IpcClient::portMapStatusChanged, this, [this](const Ipc::IpcMessage& msg) {
            if (m_upnpTimer->isActive())
                showPortMapStatus(msg.fieldMap(0));
        });
    }

    connect(m_upnpBtn, &QPushButton::clicked, this, &FirstStartWizard::onUPnPSetup);

    pageLayout->addWidget(container);

    auto* separator = new QFrame(page);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    pageLayout->addWidget(separator);

    auto* group = new QGroupBox(tr("Choose which Network(s) you want to use"), page);
    auto* groupLayout = new QHBoxLayout(group);
    groupLayout->setContentsMargins(16, 8, 16, 8);

    m_kadCheck = new QCheckBox(tr("Kad"), group);
    m_ed2kCheck = new QCheckBox(tr("eD2K"), group);

    groupLayout->addWidget(m_kadCheck);
    groupLayout->addSpacing(40);
    groupLayout->addWidget(m_ed2kCheck);
    groupLayout->addStretch();

    auto* wrapper = new QWidget(page);
    auto* wrapperLayout = new QVBoxLayout(wrapper);
    wrapperLayout->setContentsMargins(16, 8, 16, 0);
    wrapperLayout->addWidget(group);

    pageLayout->addWidget(wrapper);
    pageLayout->addStretch();
    return page;
}

// ---------------------------------------------------------------------------
// Page 2 — line type list (MFC connection wizard) + custom rates
// ---------------------------------------------------------------------------

QWidget* FirstStartWizard::setupSpeedPage()
{
    auto* page = new QWidget(this);
    auto* vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(16, 12, 16, 4);
    DialogSizing::enableHeightForWidth(page);

    auto* descLabel = new QLabel(
        tr("Select your internet connection. eMule derives its download and upload "
           "limits from it, leaving room for your other applications. "
           "You can change the limits at any time in the Options."),
        page);
    descLabel->setWordWrap(true);
    DialogSizing::enableHeightForWidth(descLabel);
    vbox->addWidget(descLabel);

    m_speedList = new QTreeWidget(page);
    m_speedList->setRootIsDecorated(false);
    m_speedList->setUniformRowHeights(true);
    m_speedList->setHeaderLabels({tr("Type"), tr("Down"), tr("Up")});
    m_speedList->setColumnWidth(0, 230);
    m_speedList->setColumnWidth(1, 110);
    m_speedList->setMinimumHeight(170);

    const auto addRow = [this](const QString& name, int kind) {
        auto* item = new QTreeWidgetItem(m_speedList, {name});
        item->setData(0, Qt::UserRole, kind);
        return item;
    };
    m_keepItem = addRow(tr("Keep current settings"), kRowKeep);
    m_recommendedItem = addRow(tr("Unknown (recommended defaults)"), kRowRecommended);
    m_customItem = addRow(tr("Custom"), kRowCustom);
    m_customItem->setText(1, tr("(enter below)"));
    m_customItem->setText(2, tr("(enter below)"));

    const auto mbitText = [](double mbit) { return tr("%1 Mbit/s").arg(mbit); };
    for (int i = 0; i < static_cast<int>(kConnectionPresets.size()); ++i) {
        const auto& preset = kConnectionPresets[static_cast<size_t>(i)];
        auto* item = addRow(QString::fromLatin1(preset.name), i);
        item->setText(1, mbitText(preset.downMbit));
        item->setText(2, mbitText(preset.upMbit));
    }
    vbox->addWidget(m_speedList, 1);

    // Custom rates, as on the line's contract
    auto* customRow = new QHBoxLayout;
    const auto makeSpin = [page] {
        auto* spin = new QDoubleSpinBox(page);
        spin->setRange(0.1, 100000.0);
        spin->setDecimals(1);
        spin->setSuffix(tr(" Mbit/s"));
        return spin;
    };
    m_customDownSpin = makeSpin();
    m_customUpSpin = makeSpin();
    customRow->addWidget(new QLabel(tr("Download:"), page));
    customRow->addWidget(m_customDownSpin);
    customRow->addSpacing(12);
    customRow->addWidget(new QLabel(tr("Upload:"), page));
    customRow->addWidget(m_customUpSpin);
    customRow->addStretch();
    vbox->addLayout(customRow);

    m_speedResult = new QLabel(page);
    vbox->addWidget(m_speedResult);

    connect(m_speedList, &QTreeWidget::currentItemChanged,
            this, &FirstStartWizard::onSpeedSelectionChanged);
    connect(m_customDownSpin, &QDoubleSpinBox::valueChanged,
            this, &FirstStartWizard::onSpeedSelectionChanged);
    connect(m_customUpSpin, &QDoubleSpinBox::valueChanged,
            this, &FirstStartWizard::onSpeedSelectionChanged);
    connect(m_speedList, &QTreeWidget::itemDoubleClicked, this, &FirstStartWizard::onNext);
    return page;
}

// ---------------------------------------------------------------------------
// Button row — Back, Next/Finish, Cancel, Help
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

    m_nextBtn = new QPushButton(tr("Next >"), this);
    m_nextBtn->setDefault(true);

    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    auto* helpBtn = new QPushButton(tr("Help"), this);

    btnLayout->addStretch();
    btnLayout->addWidget(m_backBtn);
    btnLayout->addWidget(m_nextBtn);
    btnLayout->addSpacing(24);
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(helpBtn);

    connect(m_backBtn, &QPushButton::clicked, this, &FirstStartWizard::onBack);
    connect(m_nextBtn, &QPushButton::clicked, this, &FirstStartWizard::onNext);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(helpBtn, &QPushButton::clicked, this, &FirstStartWizard::onHelp);

    static_cast<QVBoxLayout*>(layout())->addLayout(btnLayout);
}

void FirstStartWizard::showPage(int page)
{
    m_pages->setCurrentIndex(page);
    const bool speed = page == kPageSpeed;
    m_titleLabel->setText(speed ? tr("Connection Speed") : tr("Ports and Connection"));
    m_subtitleLabel->setText(speed ? tr("Bandwidth") : tr("Connection"));
    m_backBtn->setEnabled(speed);
    m_nextBtn->setText(speed ? tr("Finish") : tr("Next >"));
}

// ---------------------------------------------------------------------------
// Current values — the daemon owns them; the GUI's copy is only the first paint
// ---------------------------------------------------------------------------

void FirstStartWizard::requestDaemonSettings()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    Ipc::IpcMessage req(Ipc::IpcMsgType::GetPreferences);
    // A dropped connection answers every pending callback with an invalid message.
    m_ipc->sendRequest(std::move(req), [self = QPointer<FirstStartWizard>(this)](
                                           const Ipc::IpcMessage& resp) {
        if (self && resp.isValid() && resp.fieldBool(0))
            self->fillFromSettings(resp.fieldMap(1));
    });
}

void FirstStartWizard::fillFromSettings(const QCborMap& prefs)
{
    const auto number = [&prefs](QLatin1StringView key) {
        return static_cast<uint32>(prefs.value(key).toInteger());
    };

    const auto tcpPort = number(QLatin1StringView("port"));
    const auto udpPort = number(QLatin1StringView("udpPort"));
    m_tcpPortSpin->setValue(tcpPort > 0 ? static_cast<int>(tcpPort) : Preferences::randomTCPPort());
    // UDP 0 = disabled; keep it that way rather than switching it on behind the user's back.
    m_udpDisableCheck->setChecked(udpPort == 0);
    m_udpPortSpin->setValue(udpPort > 0 ? static_cast<int>(udpPort) : Preferences::randomUDPPort());

    m_kadCheck->setChecked(prefs.value(QLatin1StringView("kadEnabled")).toBool());
    m_ed2kCheck->setChecked(prefs.value(QLatin1StringView("networkED2K")).toBool());

    m_current = {number(QLatin1StringView("maxGraphDownloadRate")),
                 number(QLatin1StringView("maxGraphUploadRate")),
                 number(QLatin1StringView("maxDownload")),
                 number(QLatin1StringView("maxUpload"))};

    m_keepItem->setText(1, limitText(m_current.maxDown));
    m_keepItem->setText(2, limitText(m_current.maxUp));
    const BandwidthSettings recommended = recommendedBandwidth();
    m_recommendedItem->setText(1, limitText(recommended.maxDown));
    m_recommendedItem->setText(2, limitText(recommended.maxUp));

    m_customDownSpin->setValue(kiBToMbit(m_current.capDown));
    m_customUpSpin->setValue(kiBToMbit(m_current.capUp));

    // An install still on the old shipped limits never tuned them: offer the new defaults.
    m_speedList->setCurrentItem(m_current == legacyDefaultBandwidth() ? m_recommendedItem
                                                                      : m_keepItem);
    onSpeedSelectionChanged();
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void FirstStartWizard::onBack()
{
    showPage(kPagePorts);
}

void FirstStartWizard::onNext()
{
    if (!networksValid())
        return;

    if (m_pages->currentIndex() == kPagePorts)
        showPage(kPageSpeed);
    else
        finish();
}

void FirstStartWizard::onSpeedSelectionChanged()
{
    const auto* item = m_speedList->currentItem();
    const bool custom = item == m_customItem;
    m_customDownSpin->setEnabled(custom);
    m_customUpSpin->setEnabled(custom);

    const BandwidthSettings result = selectedBandwidth().value_or(m_current);
    m_speedResult->setText(tr("Download limit: %1    Upload limit: %2")
                               .arg(limitText(result.maxDown), limitText(result.maxUp)));
}

// ---------------------------------------------------------------------------
// UPnP — enable UPnP and let the daemon handle port mapping
// ---------------------------------------------------------------------------

void FirstStartWizard::onUPnPSetup()
{
    m_upnpRequested = true;
    thePrefs.setEnableUPnP(true);

    if (!m_ipc || !m_ipc->isConnected()) {
        // No daemon to write preferences.yml for us, and nobody to ask the router
        thePrefs.save();
        endUPnPWait(tr("The ports are forwarded when the core starts."), false);
        return;
    }

    m_upnpBtn->setEnabled(false);
    m_upnpProgress->setVisible(true);
    showPortMapStatus({});   // pending
    m_upnpTimer->start();

    Ipc::IpcMessage req(Ipc::IpcMsgType::SetPreferences);
    req.append(QStringLiteral("enableUPnP"));
    req.append(true);
    req.append(QStringLiteral("port"));
    req.append(static_cast<qint64>(m_tcpPortSpin->value()));
    req.append(QStringLiteral("udpPort"));
    req.append(static_cast<qint64>(m_udpDisableCheck->isChecked() ? 0 : m_udpPortSpin->value()));
    // The daemon has started its mapper by the time it answers; a mapper that was
    // already running pushes nothing new, so ask for its state.
    m_ipc->sendRequest(std::move(req), [self = QPointer<FirstStartWizard>(this)](
                                           const Ipc::IpcMessage& resp) {
        if (!self)
            return;
        if (resp.isValid() && resp.fieldBool(0))
            self->requestPortMapStatus();
        else
            self->endUPnPWait(tr("The core did not accept the port settings."), true);
    });
}

void FirstStartWizard::onUPnPTimeout()
{
    endUPnPWait(tr("No answer from the core about port forwarding."), true);
}

void FirstStartWizard::showPortMapStatus(const QCborMap& info)
{
    const PortMapSummary summary = portMapStatusSummary(info);
    if (summary.outcome == PortMapOutcome::Pending) {
        m_upnpStatus->setPalette(QPalette());
        m_upnpStatus->setText(summary.text);
        return;
    }
    endUPnPWait(summary.text, summary.outcome == PortMapOutcome::Failed);
}

// ---------------------------------------------------------------------------
// Help — open project website
// ---------------------------------------------------------------------------

void FirstStartWizard::onHelp()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://emule-qt.org")));
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

bool FirstStartWizard::networksValid()
{
    if (m_kadCheck->isChecked() || m_ed2kCheck->isChecked())
        return true;

    showPage(kPagePorts);
    QMessageBox::warning(this, tr("Network"),
        tr("You must enable at least one network (Kad or eD2K)."));
    return false;
}

/// The limits the selected row stands for; nullopt = leave the bandwidth alone.
std::optional<BandwidthSettings> FirstStartWizard::selectedBandwidth() const
{
    const auto* item = m_speedList->currentItem();
    const int kind = item ? item->data(0, Qt::UserRole).toInt() : kRowKeep;
    switch (kind) {
    case kRowKeep:
        return std::nullopt;
    case kRowRecommended:
        return recommendedBandwidth();
    case kRowCustom:
        return limitsForLine(m_customDownSpin->value(), m_customUpSpin->value());
    default: {
        const auto& preset = kConnectionPresets[static_cast<size_t>(kind)];
        return limitsForLine(preset.downMbit, preset.upMbit);
    }
    }
}

/// Finish — the daemon writes preferences.yml; save here only when there is none.
void FirstStartWizard::finish()
{
    const auto tcpPort = static_cast<uint16>(m_tcpPortSpin->value());
    const auto udpPort = m_udpDisableCheck->isChecked()
        ? uint16(0) : static_cast<uint16>(m_udpPortSpin->value());
    const bool kadEnabled = m_kadCheck->isChecked();
    const bool ed2kEnabled = m_ed2kCheck->isChecked();
    const auto bandwidth = selectedBandwidth();

    m_applied.clear();
    m_applied.insert(QStringLiteral("port"), tcpPort);
    m_applied.insert(QStringLiteral("udpPort"), udpPort);
    m_applied.insert(QStringLiteral("kadEnabled"), kadEnabled);
    m_applied.insert(QStringLiteral("networkED2K"), ed2kEnabled);
    if (m_upnpRequested)
        m_applied.insert(QStringLiteral("enableUPnP"), true);
    if (bandwidth) {
        // Capacity before the limits: the upload limit is clamped to it.
        m_applied.insert(QStringLiteral("maxGraphDownloadRate"), bandwidth->capDown);
        m_applied.insert(QStringLiteral("maxGraphUploadRate"), bandwidth->capUp);
        m_applied.insert(QStringLiteral("maxDownload"), bandwidth->maxDown);
        m_applied.insert(QStringLiteral("maxUpload"), bandwidth->maxUp);
    }

    // GUI's in-memory copy, so this session reads what was just chosen
    thePrefs.setPort(tcpPort);
    thePrefs.setUdpPort(udpPort);
    thePrefs.setKadEnabled(kadEnabled);
    thePrefs.setNetworkED2K(ed2kEnabled);
    if (bandwidth) {
        thePrefs.setMaxGraphDownloadRate(bandwidth->capDown);
        thePrefs.setMaxGraphUploadRate(bandwidth->capUp);
        thePrefs.setMaxDownload(bandwidth->maxDown);
        thePrefs.setMaxUpload(bandwidth->maxUp);
    }

    if (m_ipc && m_ipc->isConnected()) {
        Ipc::IpcMessage req(Ipc::IpcMsgType::SetPreferences);
        const auto send = [&req](const char* key, qint64 value) {
            req.append(QString::fromLatin1(key));
            req.append(value);
        };
        send("port", tcpPort);
        send("udpPort", udpPort);
        req.append(QStringLiteral("kadEnabled"));
        req.append(kadEnabled);
        req.append(QStringLiteral("networkED2K"));
        req.append(ed2kEnabled);
        if (bandwidth) {
            send("maxGraphDownloadRate", bandwidth->capDown);
            send("maxGraphUploadRate", bandwidth->capUp);
            send("maxDownload", bandwidth->maxDown);
            send("maxUpload", bandwidth->maxUp);
        }
        // The wizard is gone by the time this answers: the notice goes to its parent
        m_ipc->sendRequest(std::move(req), [parent = QPointer<QWidget>(parentWidget())](
                                               const Ipc::IpcMessage& resp) {
            showPortChangeResult(parent, resp);
        });
    } else {
        thePrefs.save();
    }

    accept();
}

void FirstStartWizard::requestPortMapStatus()
{
    Ipc::IpcMessage req(Ipc::IpcMsgType::GetNetworkInfo);
    m_ipc->sendRequest(std::move(req), [self = QPointer<FirstStartWizard>(this)](
                                           const Ipc::IpcMessage& resp) {
        // Invalid = connection dropped; the timer reports that
        if (self && resp.isValid() && self->m_upnpTimer->isActive())
            self->showPortMapStatus(resp.fieldMap(1).value(QLatin1StringView("portmap")).toMap());
    });
}

void FirstStartWizard::endUPnPWait(const QString& text, bool failed)
{
    m_upnpTimer->stop();
    m_upnpProgress->setVisible(false);
    m_upnpBtn->setEnabled(true);

    QPalette pal;
    if (failed)
        pal.setColor(QPalette::WindowText, QColor(200, 40, 40));
    m_upnpStatus->setPalette(pal);
    m_upnpStatus->setText(text);
}

} // namespace eMule
