#include "pch.h"
/// @file BtI2pGroup.cpp
/// @brief The "I2P" box of Options > BitTorrent > Connection — implementation.

#include "dialogs/BtI2pGroup.h"

#include "app/IpcClient.h"
#include "dialogs/RememberedSizeDialog.h"
#include "prefs/Preferences.h"
#include "IpcMessage.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QIcon>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

/// The I2P tracker list: one announce URL per line.
class I2pTrackersDialog final : public RememberedSizeDialog {
public:
    I2pTrackersDialog(const QStringList& trackers, QWidget* parent)
        : RememberedSizeDialog(parent)
    {
        setWindowTitle(BtI2pGroup::tr("I2P Trackers"));
        setWindowIcon(QIcon(QStringLiteral(":/icons/Torrent.ico")));
        auto* layout = new QVBoxLayout(this);
        auto* label = new QLabel(BtI2pGroup::tr("Added to I2P torrents, and to all torrents in mixed mode. "
                                                "One per line:"), this);
        label->setWordWrap(true);
        layout->addWidget(label);
        m_edit = new QPlainTextEdit(this);
        m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
        m_edit->setPlaceholderText(QStringLiteral("http://tracker.example.i2p/announce"));
        m_edit->setPlainText(trackers.join(QLatin1Char('\n')));
        layout->addWidget(m_edit, 1);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        auto* defaults = buttons->addButton(BtI2pGroup::tr("Defaults"), QDialogButtonBox::ResetRole);
        defaults->setToolTip(BtI2pGroup::tr("The public I2P trackers this version ships with"));
        connect(defaults, &QPushButton::clicked, this,
                [this] { m_edit->setPlainText(Preferences::defaultBtI2pTrackers().join(QLatin1Char('\n'))); });
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
        resize(560, 320);
    }

    [[nodiscard]] QStringList trackers() const
    {
        QStringList out;
        for (const QString& line : m_edit->toPlainText().split(QLatin1Char('\n')))
            if (const QString t = line.trimmed(); !t.isEmpty() && !out.contains(t))
                out << t;
        return out;
    }

protected:
    [[nodiscard]] QString sizeKey() const override { return QStringLiteral("I2pTrackersDialog"); }

private:
    QPlainTextEdit* m_edit = nullptr;
};

} // namespace

BtI2pGroup::BtI2pGroup(IpcClient* ipc, QWidget* parent)
    : QGroupBox(tr("I2P"), parent)
    , m_ipc(ipc)
    , m_trackers(Preferences::defaultBtI2pTrackers())
{
    auto* form = new QFormLayout(this);

    auto* note = new QLabel(tr("Downloads torrents over the I2P network: peers see an I2P address, never "
                               "your IP. Slower than direct connections, and needs no open port."),
                            this);
    note->setWordWrap(true);
    form->addRow(note);

    // ids match the btI2pMode value
    m_modeGroup = new QButtonGroup(this);
    auto* modeBox = new QVBoxLayout;
    const auto modeRadio = [&](const QString& text, const QString& tip, int id) {
        auto* radio = new QRadioButton(text, this);
        radio->setToolTip(tip);
        m_modeGroup->addButton(radio, id);
        modeBox->addWidget(radio);
    };
    modeRadio(tr("Off"), tr("Torrents use direct connections only."), Preferences::kBtI2pOff);
    modeRadio(tr("Bundled I2P router (i2pd)"),
              tr("eMule runs its own I2P router. The first start takes a few minutes to join the network."),
              Preferences::kBtI2pBundled);
    modeRadio(tr("External I2P router"),
              tr("A router you run yourself (i2pd or Java I2P) with its SAM bridge on."), Preferences::kBtI2pExternal);
    m_modeGroup->button(Preferences::kBtI2pOff)->setChecked(true);
    form->addRow(tr("Router:"), modeBox);

    auto* samRow = new QHBoxLayout;
    m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), this);
    m_hostEdit->setToolTip(tr("Host of the external router's SAM bridge"));
    samRow->addWidget(m_hostEdit, 1);
    samRow->addWidget(new QLabel(tr("Port:"), this));
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(Preferences::kDefaultBtI2pSamPort);
    samRow->addWidget(m_portSpin);
    form->addRow(tr("SAM bridge:"), samRow);

    m_routerPortSpin = new QSpinBox(this);
    m_routerPortSpin->setRange(1, 65535);
    m_routerPortSpin->setToolTip(tr("TCP and UDP port of the bundled router. Mapped with UPnP when the "
                                    "BitTorrent port is."));
    form->addRow(tr("Router port:"), m_routerPortSpin);

    auto* tunnelRow = new QHBoxLayout;
    m_lengthSpin = new QSpinBox(this);
    m_lengthSpin->setRange(1, 7);
    m_lengthSpin->setValue(3);
    m_lengthSpin->setToolTip(tr("Routers each tunnel passes. More is more anonymous and slower."));
    tunnelRow->addWidget(m_lengthSpin);
    tunnelRow->addWidget(new QLabel(tr("hops,"), this));
    m_quantitySpin = new QSpinBox(this);
    m_quantitySpin->setRange(1, 16);
    m_quantitySpin->setValue(3);
    m_quantitySpin->setToolTip(tr("Tunnels each way. More is faster and uses more of the router."));
    tunnelRow->addWidget(m_quantitySpin);
    tunnelRow->addWidget(new QLabel(tr("tunnels each way"), this));
    tunnelRow->addStretch();
    form->addRow(tr("Tunnels:"), tunnelRow);

    m_mixedCheck = new QCheckBox(tr("Mixed mode: I2P torrents also use direct peers (less anonymous)"), this);
    m_mixedCheck->setToolTip(tr("I2P torrents also use the DHT, normal trackers and direct peers, and normal "
                                "torrents get the I2P trackers too, so both swarms share the files."));
    form->addRow(m_mixedCheck);
    m_onlyNewCheck = new QCheckBox(tr("Add new torrents as I2P only"), this);
    m_onlyNewCheck->setToolTip(tr("The default of the \"I2P only\" box when adding torrents."));
    form->addRow(m_onlyNewCheck);
    m_ed3kJoinCheck = new QCheckBox(tr("I2P torrents may also download over ed3k"), this);
    m_ed3kJoinCheck->setToolTip(tr("Their info hash then goes out over ed3k. How anonymous that is follows "
                                   "the ed3k hiding settings, not I2P."));
    m_ed3kJoinCheck->setChecked(true);
    form->addRow(m_ed3kJoinCheck);

    m_trackersBtn = new QPushButton(tr("I2P Trackers..."), this);
    connect(m_trackersBtn, &QPushButton::clicked, this, &BtI2pGroup::editTrackers);
    auto* trackerRow = new QHBoxLayout;
    trackerRow->addWidget(m_trackersBtn);
    trackerRow->addStretch();
    form->addRow(trackerRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Status:"), m_statusLabel);

    connect(m_modeGroup, &QButtonGroup::idToggled, this, [this](int, bool on) {
        if (!on)
            return;
        updateEnabled();
        emit changed();
    });
    for (QCheckBox* box : {m_mixedCheck, m_onlyNewCheck, m_ed3kJoinCheck})
        connect(box, &QCheckBox::toggled, this, &BtI2pGroup::changed);
    for (QSpinBox* box : {m_portSpin, m_routerPortSpin, m_lengthSpin, m_quantitySpin})
        connect(box, &QSpinBox::valueChanged, this, &BtI2pGroup::changed);
    connect(m_hostEdit, &QLineEdit::textEdited, this, &BtI2pGroup::changed);
    if (m_ipc)
        connect(m_ipc, &IpcClient::btStatusChanged, this, [this](const IpcMessage& msg) { applyStatus(msg.fieldMap(0)); });
    updateEnabled();
}

void BtI2pGroup::load(const QCborMap& prefs)
{
    const QSignalBlocker blockGroup(m_modeGroup);
    std::vector<QSignalBlocker> blockers;
    blockers.reserve(8);
    for (QObject* o : std::initializer_list<QObject*>{m_hostEdit, m_portSpin, m_routerPortSpin, m_lengthSpin,
                                                      m_quantitySpin, m_mixedCheck, m_onlyNewCheck, m_ed3kJoinCheck})
        blockers.emplace_back(o);

    const int mode = std::clamp(int(prefs.value(QStringLiteral("btI2pMode")).toInteger(Preferences::kBtI2pOff)),
                                Preferences::kBtI2pOff, Preferences::kBtI2pExternal);
    if (QAbstractButton* b = m_modeGroup->button(mode))
        b->setChecked(true);
    m_hostEdit->setText(prefs.value(QStringLiteral("btI2pHost")).toString(QStringLiteral("127.0.0.1")));
    m_portSpin->setValue(int(prefs.value(QStringLiteral("btI2pPort")).toInteger(Preferences::kDefaultBtI2pSamPort)));
    m_routerPortSpin->setValue(int(prefs.value(QStringLiteral("btI2pRouterPort")).toInteger(Preferences::kBtI2pRouterPortMin)));
    m_lengthSpin->setValue(int(prefs.value(QStringLiteral("btI2pTunnelLength")).toInteger(3)));
    m_quantitySpin->setValue(int(prefs.value(QStringLiteral("btI2pTunnelQuantity")).toInteger(3)));
    m_mixedCheck->setChecked(prefs.value(QStringLiteral("btI2pMixed")).toBool(false));
    m_onlyNewCheck->setChecked(prefs.value(QStringLiteral("btI2pOnlyNew")).toBool(false));
    m_ed3kJoinCheck->setChecked(prefs.value(QStringLiteral("btI2pEd3kJoin")).toBool(true));
    if (prefs.contains(QStringLiteral("btI2pTrackers")))
        m_trackers = prefs.value(QStringLiteral("btI2pTrackers"))
                         .toString()
                         .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    m_ed3kOn = prefs.value(QStringLiteral("ed3kEnabled")).toBool(true);
    updateEnabled();
    requestStatus();
}

void BtI2pGroup::appendTo(IpcMessage& req) const
{
    const auto put = [&req](const char* key, const QCborValue& v) {
        req.append(QString::fromLatin1(key));
        req.append(v);
    };
    put("btI2pMode", qint64(std::max(0, m_modeGroup->checkedId())));
    put("btI2pHost", m_hostEdit->text().trimmed());
    put("btI2pPort", qint64(m_portSpin->value()));
    put("btI2pRouterPort", qint64(m_routerPortSpin->value()));
    put("btI2pTunnelLength", qint64(m_lengthSpin->value()));
    put("btI2pTunnelQuantity", qint64(m_quantitySpin->value()));
    put("btI2pMixed", m_mixedCheck->isChecked());
    put("btI2pOnlyNew", m_onlyNewCheck->isChecked());
    put("btI2pEd3kJoin", m_ed3kJoinCheck->isChecked());
    put("btI2pTrackers", m_trackers.join(QLatin1Char(' ')));
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void BtI2pGroup::updateEnabled()
{
    const int mode = m_modeGroup->checkedId();
    const bool on = mode != Preferences::kBtI2pOff;
    if (QAbstractButton* bundled = m_modeGroup->button(Preferences::kBtI2pBundled)) {
        // a build without the helper: only a chosen one stays pickable, to switch away
        bundled->setEnabled(m_bundledAvailable || mode == Preferences::kBtI2pBundled);
        bundled->setToolTip(m_bundledAvailable
                                ? tr("eMule runs its own I2P router. The first start takes a few minutes to join "
                                     "the network.")
                                : tr("This build has no bundled I2P router (emuleqt-i2pd)."));
    }
    m_hostEdit->setEnabled(mode == Preferences::kBtI2pExternal);
    m_portSpin->setEnabled(mode == Preferences::kBtI2pExternal);
    m_routerPortSpin->setEnabled(mode == Preferences::kBtI2pBundled);
    for (QWidget* w : std::initializer_list<QWidget*>{m_lengthSpin, m_quantitySpin, m_mixedCheck, m_onlyNewCheck,
                                                      m_trackersBtn})
        w->setEnabled(on);
    m_ed3kJoinCheck->setEnabled(on && m_ed3kOn);
}

void BtI2pGroup::editTrackers()
{
    I2pTrackersDialog dlg(m_trackers, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QStringList list = dlg.trackers();
    if (list == m_trackers)
        return;
    m_trackers = list;
    emit changed();
}

void BtI2pGroup::requestStatus()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    m_ipc->sendPoll(this, IpcMessage(IpcMsgType::GetBtStatus), [this](const IpcMessage& resp) {
        if (resp.isValid() && resp.fieldBool(0))
            applyStatus(resp.fieldMap(1));
    });
}

void BtI2pGroup::applyStatus(const QCborMap& status)
{
    m_bundledAvailable = status.value(QStringLiteral("i2pBundled")).toBool(true);
    updateEnabled();
    if (!status.value(QStringLiteral("i2pEnabled")).toBool(false)) {
        m_statusLabel->setText(status.value(QStringLiteral("running")).toBool(false) ? tr("Off")
                                                                                     : tr("BitTorrent is off"));
        return;
    }
    QString text;
    // I2pRouter::State: 0 stopped, 1 starting, 2 ready, 3 failed; stopped = external router
    switch (status.value(QStringLiteral("i2pRouter")).toInteger(0)) {
    case 1: text = tr("Bundled router starting..."); break;
    case 2: text = tr("Bundled router running"); break;
    case 3: text = tr("Bundled router failed: %1").arg(status.value(QStringLiteral("i2pRouterError")).toString()); break;
    default: break;
    }
    const QString samError = status.value(QStringLiteral("i2pError")).toString();
    const QString sam = !status.value(QStringLiteral("i2pSam")).toBool(false) ? tr("waiting for the router")
                        : samError.isEmpty()                                   ? tr("SAM bridge set")
                                                                               : tr("SAM bridge error: %1").arg(samError);
    m_statusLabel->setText(text.isEmpty() ? sam : text + QStringLiteral(" · ") + sam);
}

} // namespace eMule
