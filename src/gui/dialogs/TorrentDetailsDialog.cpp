#include "pch.h"
/// @file TorrentDetailsDialog.cpp
/// @brief The torrent inspector: Info, Activity and Options — implementation.

#include "dialogs/TorrentDetailsDialog.h"

#include "app/IpcClient.h"
#include "controls/TorrentListModel.h"
#include "controls/TorrentPieceGrid.h"
#include "utils/MenuUtils.h"
#include "utils/StatusBarNotifier.h"
#include "utils/StringUtils.h"
#include "utils/TorrentLinks.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace eMule {

namespace {

constexpr int kPollMs = 2000;
/// an Options edit goes out this long after the last change
constexpr int kSendDelayMs = 600;
/// Modes of the seed limit combos: BtLimitMode on the wire
enum LimitMode { ModeGlobal = 0, ModeUnlimited = 1, ModeCustom = 2 };

/// the tab the last dialog was left on, for the next one
int s_lastTab = TorrentDetailsDialog::TabActivity;

QString na()
{
    return TorrentDetailsDialog::tr("N/A");
}

QString dateText(qint64 epoch)
{
    if (epoch <= 0)
        return na();
    return QLocale().toString(QDateTime::fromSecsSinceEpoch(epoch), QLocale::LongFormat);
}

QString percent(double share)
{
    return QStringLiteral("%1%").arg(share * 100.0, 0, 'f', 1);
}

/// "<b>Label:</b>" + a value label, as every detail sheet has it.
QLabel* addRow(QFormLayout* form, const QString& label)
{
    auto* value = detailValueLabel({});
    value->setMinimumWidth(120);
    form->addRow(QStringLiteral("<b>%1:</b>").arg(label), value);
    return value;
}

QFormLayout* makeForm(QWidget* parent)
{
    auto* form = new QFormLayout(parent);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return form;
}

void fillLimitCombo(QComboBox* combo)
{
    combo->addItem(TorrentDetailsDialog::tr("Use global settings"), ModeGlobal);
    combo->addItem(TorrentDetailsDialog::tr("Seed regardless"), ModeUnlimited);
    combo->addItem(TorrentDetailsDialog::tr("Stop seeding at:"), ModeCustom);
}

void selectMode(QComboBox* combo, int mode)
{
    const int at = combo->findData(mode);
    combo->setCurrentIndex(at >= 0 ? at : 0);
}

} // namespace

TorrentDetailsDialog::TorrentDetailsDialog(IpcClient* ipc, const TorrentListModel* model,
                                           const QString& torrentId, QWidget* parent)
    : DetailDialog(parent)
    , m_ipc(ipc)
    , m_model(model)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowIcon(QIcon(QStringLiteral(":/icons/Torrent.ico")));
    setDesignedSize(QSize(640, 420), QSize(760, 460));

    // Header: icon, name, "N files, size"
    auto* header = new QHBoxLayout;
    m_icon = new QLabel(this);
    header->addWidget(m_icon, 0, Qt::AlignTop);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    m_name = new QLabel(this);
    QFont f = m_name->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() * 1.25);
    m_name->setFont(f);
    m_name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_name->setWordWrap(true);
    titles->addWidget(m_name);
    m_summary = new QLabel(this);
    titles->addWidget(m_summary);
    header->addLayout(titles, 1);
    contentLayout()->addLayout(header);

    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    m_note->hide();
    contentLayout()->addWidget(m_note);

    m_tabs = new QTabWidget(this);
    m_tabs->insertTab(TabInfo, buildInfoTab(), menuIcon("FileInfo.ico"), tr("Info"));
    m_tabs->insertTab(TabActivity, buildActivityTab(), menuIcon("Statistics.ico"), tr("Activity"));
    m_tabs->insertTab(TabOptions, buildOptionsTab(), menuIcon("Preferences.ico"), tr("Options"));
    m_tabs->setCurrentIndex(s_lastTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int tab) {
        s_lastTab = tab;
        setWindowTitle(tr("%1 — Torrent Inspector").arg(m_tabs->tabText(tab)));
    });
    setWindowTitle(tr("%1 — Torrent Inspector").arg(m_tabs->tabText(s_lastTab)));
    contentLayout()->addWidget(m_tabs, 1);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollMs);
    connect(m_pollTimer, &QTimer::timeout, this, [this] {
        refreshRow();
        poll();
    });
    m_pollTimer->start();

    m_sendTimer = new QTimer(this);
    m_sendTimer->setSingleShot(true);
    m_sendTimer->setInterval(kSendDelayMs);
    connect(m_sendTimer, &QTimer::timeout, this, &TorrentDetailsDialog::sendOptions);

    // The walker landed on another torrent
    connect(this, &DetailDialog::navigated, this, &TorrentDetailsDialog::setTorrentId);

    setTorrentId(torrentId);
}

void TorrentDetailsDialog::setTorrentId(const QString& id)
{
    if (id.isEmpty())
        return;
    if (m_optionsDirty && id != m_id)
        sendOptions();   // what the user typed belongs to the torrent they typed it for
    m_id = id;
    setSubjectKey(id);
    m_grid->setAvailabilityMap({});
    m_optionsDirty = false;
    refreshRow();
    QTimer::singleShot(0, this, &TorrentDetailsDialog::poll);
}

void TorrentDetailsDialog::setDetails(const QCborMap& details)
{
    m_note->hide();
    QByteArray avail = details.value(QStringLiteral("availabilityMap")).toByteArray();
    // libtorrent stops counting once we seed: every piece is here, so it is available
    if (avail.isEmpty() && m_seedBuckets > 0)
        avail = QByteArray(m_seedBuckets, char(255));
    m_grid->setAvailabilityMap(avail);
    const QCborMap options = details.value(QStringLiteral("options")).toMap();
    if (!options.isEmpty())
        applyOptions(options);
}

void TorrentDetailsDialog::setRow(const TorrentRow& row)
{
    const bool folder = row.numFiles > 1;
    m_icon->setPixmap(style()->standardIcon(folder ? QStyle::SP_DirIcon : QStyle::SP_FileIcon).pixmap(40, 40));
    m_name->setText(row.name);
    const qint64 total = row.totalSize > 0 ? row.totalSize : row.size;
    m_summary->setText(row.hasMetadata
                           ? tr("%n file(s), %1", nullptr, std::max(1, row.numFiles)).arg(formatByteSize(total))
                           : tr("Getting metadata…"));

    // Info
    if (row.hasMetadata && row.pieces > 0)
        m_size->setText(tr("%1 (%n piece(s) @ %2)", nullptr, row.pieces)
                            .arg(formatByteSize(total), formatByteSize(row.pieceLength)));
    else
        m_size->setText(na());
    m_location->setText(row.savePath);
    m_hashV1->setText(row.hashV1.isEmpty() ? na() : row.hashV1);
    m_hashV2->setText(row.hashV2.isEmpty() ? na() : row.hashV2);
    m_privacy->setText(row.isPrivate ? tr("Private to this tracker — DHT and PEX disabled")
                                     : tr("Public torrent"));
    if (!row.creator.isEmpty() && row.createdAt > 0)
        m_origin->setText(tr("Created by %1 on %2").arg(row.creator, dateText(row.createdAt)));
    else if (!row.creator.isEmpty())
        m_origin->setText(tr("Created by %1").arg(row.creator));
    else if (row.createdAt > 0)
        m_origin->setText(tr("Created on %1").arg(dateText(row.createdAt)));
    else
        m_origin->setText(na());
    m_magnet = gui::torrentMagnetLink(row.hashV1, row.name);
    m_magnetLabel->setText(m_magnet.isEmpty() ? na() : m_magnet);
    m_copyMagnet->setEnabled(!m_magnet.isEmpty());
    if (m_comment->toPlainText() != row.comment)
        m_comment->setPlainText(row.comment);

    // Activity
    m_state->setText(torrentStateName(row.state));
    const double haveShare = total > 0 ? double(row.haveBytes) / double(total) : 0.0;
    const double wantedShare = row.size > 0 ? double(row.done) / double(row.size) : 0.0;
    m_progress->setText(row.hasMetadata ? tr("%1 (%2 selected)").arg(percent(haveShare), percent(wantedShare))
                                        : na());
    m_have->setText(formatByteSize(row.haveBytes));
    m_availability->setText(row.availability < 0 ? na()
                                                 : QStringLiteral("%1").arg(row.availability, 0, 'f', 2));
    m_downloaded->setText(formatByteSize(row.downloaded));
    m_uploaded->setText(formatByteSize(row.uploaded));
    m_failed->setText(formatByteSize(row.failedBytes));
    const double ratio = row.ratio();
    m_ratio->setText(ratio == -2 ? QStringLiteral("∞") : ratio < 0 ? na() : QString::number(ratio, 'f', 2));
    if (m_error->toPlainText() != row.error)
        m_error->setPlainText(row.error);
    m_grid->setProgressMap(row.isComplete() && row.pieceMap.isEmpty() ? QByteArray(1, char(255))
                                                                      : row.pieceMap);
    m_seedBuckets = row.isComplete() ? std::max<int>(1, int(row.pieceMap.size())) : 0;

    m_added->setText(dateText(row.addedAt));
    m_completed->setText(dateText(row.completedAt));
    if (row.lastActivity <= 0)
        m_lastActivity->setText(na());
    else if (row.rateDown > 0 || row.rateUp > 0)
        m_lastActivity->setText(tr("Active now"));
    else
        m_lastActivity->setText(tr("%1 ago").arg(formatSecondsLongHM(
            std::max<qint64>(0, QDateTime::currentSecsSinceEpoch() - row.lastActivity))));
    m_downloadingTime->setText(formatSecondsLongHM(std::max<qint64>(0, row.activeSeconds - row.seedingSeconds)));
    m_seedingTime->setText(formatSecondsLongHM(row.seedingSeconds));
    m_remaining->setText(row.isComplete() ? na() : row.eta >= 0 ? formatSecondsLongHM(row.eta) : tr("Unknown"));
}

QCborMap TorrentDetailsDialog::optionsFromUi() const
{
    return QCborMap{
        {QStringLiteral("downloadLimitKb"), m_limitDown->isChecked() ? m_limitDownKb->value() : 0},
        {QStringLiteral("uploadLimitKb"), m_limitUp->isChecked() ? m_limitUpKb->value() : 0},
        {QStringLiteral("maxPeers"), m_maxPeers->value()},
        {QStringLiteral("ratioMode"), m_ratioMode->currentData().toInt()},
        {QStringLiteral("ratioPercent"), qRound(m_ratio100->value() * 100.0)},
        {QStringLiteral("seedTimeMode"), m_seedTimeMode->currentData().toInt()},
        {QStringLiteral("seedTimeMinutes"), m_seedMinutes->value()},
    };
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

QWidget* TorrentDetailsDialog::buildInfoTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* details = new QGroupBox(tr("Details"), page);
    auto* form = makeForm(details);
    m_size = addRow(form, tr("Size"));
    m_location = addRow(form, tr("Location"));
    m_hashV1 = addRow(form, tr("Hash v1"));
    m_hashV2 = addRow(form, tr("Hash v2"));
    m_privacy = addRow(form, tr("Privacy"));
    m_origin = addRow(form, tr("Origin"));

    auto* magnetRow = new QHBoxLayout;
    m_magnetLabel = detailValueLabel({});
    m_magnetLabel->setMinimumWidth(120);
    m_magnetLabel->setWordWrap(true);
    magnetRow->addWidget(m_magnetLabel, 1);
    m_copyMagnet = new QToolButton(details);
    m_copyMagnet->setIcon(menuIcon("Copy.ico"));
    m_copyMagnet->setToolTip(tr("Copy Magnet Link"));
    m_copyMagnet->setAutoRaise(true);
    connect(m_copyMagnet, &QToolButton::clicked, this, &TorrentDetailsDialog::copyMagnet);
    magnetRow->addWidget(m_copyMagnet, 0, Qt::AlignTop);
    form->addRow(QStringLiteral("<b>%1:</b>").arg(tr("Magnet")), magnetRow);
    layout->addWidget(details);

    auto* commentBox = new QGroupBox(tr("Comment"), page);
    auto* commentLayout = new QVBoxLayout(commentBox);
    m_comment = new QTextBrowser(commentBox);
    m_comment->setOpenExternalLinks(true);
    m_comment->setMinimumHeight(50);
    commentLayout->addWidget(m_comment);
    layout->addWidget(commentBox, 1);
    return page;
}

QWidget* TorrentDetailsDialog::buildActivityTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QHBoxLayout(page);

    // Transfer, with the piece grid beside it
    auto* transfer = new QGroupBox(tr("Transfer"), page);
    auto* transferLayout = new QHBoxLayout(transfer);
    auto* form = makeForm(nullptr);
    m_state = addRow(form, tr("State"));
    m_progress = addRow(form, tr("Progress"));
    m_have = addRow(form, tr("Have"));
    m_availability = addRow(form, tr("Availability"));
    m_downloaded = addRow(form, tr("Downloaded"));
    m_uploaded = addRow(form, tr("Uploaded"));
    m_failed = addRow(form, tr("Failed DL"));
    m_ratio = addRow(form, tr("Ratio"));
    m_error = new QPlainTextEdit(transfer);
    m_error->setReadOnly(true);
    m_error->setMaximumHeight(70);
    form->addRow(QStringLiteral("<b>%1:</b>").arg(tr("Error")), m_error);
    auto* formColumn = new QVBoxLayout;
    formColumn->addLayout(form);
    formColumn->addStretch(1);   // rows stay together, as Transmission has them
    transferLayout->addLayout(formColumn, 1);

    auto* gridColumn = new QVBoxLayout;
    m_grid = new TorrentPieceGrid(transfer);
    m_grid->setFixedSize(180, 180);
    gridColumn->addWidget(m_grid);
    auto* toggle = new QHBoxLayout;
    toggle->setSpacing(0);
    auto* group = new QButtonGroup(transfer);
    auto segment = [&](const QString& text, TorrentPieceGrid::Mode mode) {
        auto* b = new QToolButton(transfer);
        b->setText(text);
        b->setCheckable(true);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        group->addButton(b);
        toggle->addWidget(b);
        connect(b, &QToolButton::toggled, this, [this, mode](bool on) {
            if (on)
                m_grid->setMode(mode);
        });
        return b;
    };
    m_gridProgress = segment(tr("Progress"), TorrentPieceGrid::Mode::Progress);
    m_gridAvailable = segment(tr("Available"), TorrentPieceGrid::Mode::Available);
    m_gridProgress->setToolTip(tr("Share of each part that is downloaded"));
    m_gridAvailable->setToolTip(tr("Peers that have each part; red: nobody connected has it"));
    m_gridProgress->setChecked(true);
    gridColumn->addLayout(toggle);
    gridColumn->addStretch(1);
    transferLayout->addLayout(gridColumn);
    layout->addWidget(transfer, 3);

    // Dates and times
    auto* right = new QVBoxLayout;
    auto* dates = new QGroupBox(tr("Dates"), page);
    auto* datesForm = makeForm(dates);
    m_added = addRow(datesForm, tr("Added"));
    m_completed = addRow(datesForm, tr("Completed"));
    m_lastActivity = addRow(datesForm, tr("Last Activity"));
    right->addWidget(dates);
    auto* elapsed = new QGroupBox(tr("Time Elapsed"), page);
    auto* elapsedForm = makeForm(elapsed);
    m_downloadingTime = addRow(elapsedForm, tr("Downloading"));
    m_seedingTime = addRow(elapsedForm, tr("Seeding"));
    m_remaining = addRow(elapsedForm, tr("Remaining"));
    right->addWidget(elapsed);
    right->addStretch(1);
    layout->addLayout(right, 2);
    return page;
}

QWidget* TorrentDetailsDialog::buildOptionsTab()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* speed = new QGroupBox(tr("Speed"), page);
    auto* speedForm = makeForm(speed);
    auto limitRow = [&](const QString& text, QCheckBox*& check, QSpinBox*& spin) {
        check = new QCheckBox(text, speed);
        spin = new QSpinBox(speed);
        spin->setRange(1, 1'000'000);
        spin->setValue(100);
        spin->setSuffix(tr(" KiB/s"));
        speedForm->addRow(check, spin);
        connect(check, &QCheckBox::toggled, this, &TorrentDetailsDialog::onOptionsEdited);
        connect(spin, &QSpinBox::valueChanged, this, &TorrentDetailsDialog::onOptionsEdited);
    };
    limitRow(tr("Limit download speed:"), m_limitDown, m_limitDownKb);
    limitRow(tr("Limit upload speed:"), m_limitUp, m_limitUpKb);
    layout->addWidget(speed);

    auto* seeding = new QGroupBox(tr("Seeding Limits"), page);
    auto* seedForm = makeForm(seeding);
    auto* ratioRow = new QHBoxLayout;
    m_ratioMode = new QComboBox(seeding);
    fillLimitCombo(m_ratioMode);
    m_ratio100 = new QDoubleSpinBox(seeding);
    m_ratio100->setRange(0.01, 1000.0);
    m_ratio100->setDecimals(2);
    m_ratio100->setSingleStep(0.1);
    m_ratio100->setValue(1.0);
    ratioRow->addWidget(m_ratioMode);
    ratioRow->addWidget(m_ratio100);
    ratioRow->addStretch(1);
    seedForm->addRow(tr("Ratio:"), ratioRow);
    auto* timeRow = new QHBoxLayout;
    m_seedTimeMode = new QComboBox(seeding);
    fillLimitCombo(m_seedTimeMode);
    m_seedMinutes = new QSpinBox(seeding);
    m_seedMinutes->setRange(1, 1'000'000);
    m_seedMinutes->setValue(60);
    m_seedMinutes->setSuffix(tr(" min"));
    timeRow->addWidget(m_seedTimeMode);
    timeRow->addWidget(m_seedMinutes);
    timeRow->addStretch(1);
    seedForm->addRow(tr("Seed time:"), timeRow);
    for (QComboBox* combo : {m_ratioMode, m_seedTimeMode})
        connect(combo, &QComboBox::currentIndexChanged, this, &TorrentDetailsDialog::onOptionsEdited);
    connect(m_ratio100, &QDoubleSpinBox::valueChanged, this, &TorrentDetailsDialog::onOptionsEdited);
    connect(m_seedMinutes, &QSpinBox::valueChanged, this, &TorrentDetailsDialog::onOptionsEdited);
    layout->addWidget(seeding);

    auto* peers = new QGroupBox(tr("Peer Connections"), page);
    auto* peerForm = makeForm(peers);
    m_maxPeers = new QSpinBox(peers);
    m_maxPeers->setRange(0, 10'000);
    m_maxPeers->setSpecialValueText(tr("Default"));
    m_maxPeers->setMinimumWidth(110);
    peerForm->addRow(tr("Maximum peers:"), m_maxPeers);
    connect(m_maxPeers, &QSpinBox::valueChanged, this, &TorrentDetailsDialog::onOptionsEdited);
    layout->addWidget(peers);
    layout->addStretch(1);

    updateOptionEnables();
    return page;
}

void TorrentDetailsDialog::poll()
{
    if (!m_ipc || !m_ipc->isConnected() || !isVisible() || m_id.isEmpty())
        return;
    Ipc::IpcMessage msg(Ipc::IpcMsgType::GetTorrentDetails);
    msg.append(m_id);
    m_ipc->sendPoll(this, std::move(msg), [this, id = m_id](const Ipc::IpcMessage& resp) {
        if (!resp.isValid() || id != m_id)
            return;
        if (resp.type() == Ipc::IpcMsgType::Error) {
            m_note->setText(resp.fieldInt(0) == 404 ? tr("This torrent is no longer in the list.")
                                                    : tr("BitTorrent is not running."));
            m_note->show();
            return;
        }
        if (resp.fieldBool(0))
            setDetails(resp.fieldMap(1));
    });
}

void TorrentDetailsDialog::refreshRow()
{
    if (!m_model)
        return;
    if (const TorrentRow* row = m_model->findById(m_id))
        setRow(*row);
}

void TorrentDetailsDialog::applyOptions(const QCborMap& options)
{
    // never over what the user is typing
    if (m_optionsDirty || m_tabs->widget(TabOptions)->isAncestorOf(QApplication::focusWidget()))
        return;
    m_fillingOptions = true;
    const int down = int(options.value(QStringLiteral("downloadLimitKb")).toInteger());
    const int up = int(options.value(QStringLiteral("uploadLimitKb")).toInteger());
    m_limitDown->setChecked(down > 0);
    if (down > 0)
        m_limitDownKb->setValue(down);
    m_limitUp->setChecked(up > 0);
    if (up > 0)
        m_limitUpKb->setValue(up);
    m_maxPeers->setValue(int(options.value(QStringLiteral("maxPeers")).toInteger()));
    selectMode(m_ratioMode, int(options.value(QStringLiteral("ratioMode")).toInteger()));
    // 0 = never set: the spin boxes keep their defaults
    if (const qint64 ratio = options.value(QStringLiteral("ratioPercent")).toInteger(); ratio > 0)
        m_ratio100->setValue(static_cast<double>(ratio) / 100.0);
    selectMode(m_seedTimeMode, int(options.value(QStringLiteral("seedTimeMode")).toInteger()));
    if (const qint64 minutes = options.value(QStringLiteral("seedTimeMinutes")).toInteger(); minutes > 0)
        m_seedMinutes->setValue(int(minutes));
    updateOptionEnables();
    m_fillingOptions = false;
}

void TorrentDetailsDialog::onOptionsEdited()
{
    updateOptionEnables();
    if (m_fillingOptions)
        return;
    m_optionsDirty = true;
    m_sendTimer->start();
}

void TorrentDetailsDialog::sendOptions()
{
    m_sendTimer->stop();
    if (!m_optionsDirty)
        return;
    m_optionsDirty = false;
    const QCborMap options = optionsFromUi();
    emit optionsSent(options);
    if (!m_ipc || !m_ipc->isConnected() || m_id.isEmpty())
        return;
    Ipc::IpcMessage msg(Ipc::IpcMsgType::SetTorrentOptions);
    msg.append(m_id);
    msg.append(options);
    m_ipc->sendRequest(std::move(msg), [](const Ipc::IpcMessage& resp) {
        if (resp.isValid() && !resp.fieldBool(0))
            StatusBarNotifier::post(tr("Could not change the torrent options."));
    });
}

void TorrentDetailsDialog::updateOptionEnables()
{
    m_limitDownKb->setEnabled(m_limitDown->isChecked());
    m_limitUpKb->setEnabled(m_limitUp->isChecked());
    m_ratio100->setEnabled(m_ratioMode->currentData().toInt() == ModeCustom);
    m_seedMinutes->setEnabled(m_seedTimeMode->currentData().toInt() == ModeCustom);
}

void TorrentDetailsDialog::copyMagnet()
{
    if (m_magnet.isEmpty())
        return;
    QApplication::clipboard()->setText(m_magnet);
    StatusBarNotifier::post(tr("Copied %n magnet link(s).", nullptr, 1), 3000);
}

} // namespace eMule
