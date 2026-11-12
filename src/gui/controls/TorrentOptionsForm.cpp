#include "pch.h"

/// @file TorrentOptionsForm.cpp
/// @brief Speed, seeding and peer limits of torrents — implementation.

#include "controls/TorrentOptionsForm.h"

#include "prefs/Preferences.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace eMule {

namespace {

/// Modes of the seed limit combos: BtLimitMode on the wire
enum LimitMode { ModeGlobal = 0, ModeUnlimited = 1, ModeCustom = 2 };
/// BtI2pMode on the wire
enum I2pMode { I2pDirect = 0, I2pOnly = 1, I2pMixed = 2 };

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
    combo->addItem(TorrentOptionsForm::tr("Use global settings"), ModeGlobal);
    combo->addItem(TorrentOptionsForm::tr("Seed regardless"), ModeUnlimited);
    combo->addItem(TorrentOptionsForm::tr("Stop seeding at:"), ModeCustom);
}

void selectMode(QComboBox* combo, int mode)
{
    const int at = combo->findData(mode);
    combo->setCurrentIndex(at >= 0 ? at : 0);
}

} // namespace

TorrentOptionsForm::TorrentOptionsForm(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* speed = new QGroupBox(tr("Speed"), this);
    auto* speedForm = makeForm(speed);
    auto limitRow = [&](Field field, const QString& text, QCheckBox*& check, QSpinBox*& spin) {
        check = new QCheckBox(text, speed);
        spin = new QSpinBox(speed);
        spin->setRange(1, 1'000'000);
        spin->setValue(100);
        spin->setSuffix(tr(" KiB/s"));
        spin->setToolTip(tr("Also limited by the global and BitTorrent limits."));
        auto* row = new QHBoxLayout;
        row->addWidget(spin, 1);
        row->addWidget(addVariesLabel(field, speed));
        speedForm->addRow(check, row);
        connect(check, &QCheckBox::toggled, this, [this, field] { markEdited(field); });
        connect(spin, &QSpinBox::valueChanged, this, [this, field] { markEdited(field); });
    };
    limitRow(FieldDown, tr("Limit download speed:"), m_limitDown, m_limitDownKb);
    limitRow(FieldUp, tr("Limit upload speed:"), m_limitUp, m_limitUpKb);
    applyRateMaxima();
    layout->addWidget(speed);

    auto* seeding = new QGroupBox(tr("Seeding Limits"), this);
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
    ratioRow->addWidget(addVariesLabel(FieldRatio, seeding));
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
    timeRow->addWidget(addVariesLabel(FieldSeedTime, seeding));
    timeRow->addStretch(1);
    seedForm->addRow(tr("Seed time:"), timeRow);
    connect(m_ratioMode, &QComboBox::currentIndexChanged, this, [this] { markEdited(FieldRatio); });
    connect(m_ratio100, &QDoubleSpinBox::valueChanged, this, [this] { markEdited(FieldRatio); });
    connect(m_seedTimeMode, &QComboBox::currentIndexChanged, this, [this] { markEdited(FieldSeedTime); });
    connect(m_seedMinutes, &QSpinBox::valueChanged, this, [this] { markEdited(FieldSeedTime); });
    layout->addWidget(seeding);

    auto* peers = new QGroupBox(tr("Peer Connections"), this);
    auto* peerForm = makeForm(peers);
    m_maxPeers = new QSpinBox(peers);
    m_maxPeers->setRange(0, 10'000);
    m_maxPeers->setSpecialValueText(tr("Default"));
    m_maxPeers->setMinimumWidth(110);
    auto* peerRow = new QHBoxLayout;
    peerRow->addWidget(m_maxPeers);
    peerRow->addWidget(addVariesLabel(FieldMaxPeers, peers));
    peerRow->addStretch(1);
    peerForm->addRow(tr("Maximum peers:"), peerRow);
    connect(m_maxPeers, &QSpinBox::valueChanged, this, [this] { markEdited(FieldMaxPeers); });
    m_i2pMode = new QComboBox(peers);
    m_i2pMode->addItem(tr("Direct only"), I2pDirect);
    m_i2pMode->addItem(tr("I2P only"), I2pOnly);
    m_i2pMode->addItem(tr("I2P and direct"), I2pMixed);
    m_i2pMode->setToolTip(tr("Peers this torrent uses. Peers of a kind no longer wanted are disconnected."));
    auto* i2pRow = new QHBoxLayout;
    i2pRow->addWidget(m_i2pMode);
    i2pRow->addWidget(addVariesLabel(FieldI2p, peers));
    i2pRow->addStretch(1);
    peerForm->addRow(tr("I2P:"), i2pRow);
    connect(m_i2pMode, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_filling && !m_i2pEnabled && m_i2pMode->currentData().toInt() != I2pDirect)
            m_i2pWarn = true;
        markEdited(FieldI2p);
    });
    layout->addWidget(peers);
    layout->addStretch(1);

    updateEnables();
}

void TorrentOptionsForm::setOptions(const QCborMap& options)
{
    setOptionsList({options});
}

void TorrentOptionsForm::setOptionsList(const QList<QCborMap>& list)
{
    if (list.isEmpty())
        return;
    for (int f = 0; f < FieldCount; ++f) {
        const QList<QString> keys = keysOf(Field(f));
        m_varies[f] = std::ranges::any_of(list, [&](const QCborMap& m) {
            return std::ranges::any_of(keys, [&](const QString& k) { return m.value(k) != list.first().value(k); });
        });
        m_edited[f] = false;
        m_variesLabel[f]->setVisible(m_varies[f]);
    }
    fill(list.first());
}

QCborMap TorrentOptionsForm::options() const
{
    return QCborMap{
        {QStringLiteral("downloadLimitKb"), m_limitDown->isChecked() ? m_limitDownKb->value() : 0},
        {QStringLiteral("uploadLimitKb"), m_limitUp->isChecked() ? m_limitUpKb->value() : 0},
        {QStringLiteral("maxPeers"), m_maxPeers->value()},
        {QStringLiteral("ratioMode"), m_ratioMode->currentData().toInt()},
        {QStringLiteral("ratioPercent"), qRound(m_ratio100->value() * 100.0)},
        {QStringLiteral("seedTimeMode"), m_seedTimeMode->currentData().toInt()},
        {QStringLiteral("seedTimeMinutes"), m_seedMinutes->value()},
        {QStringLiteral("i2pMode"), m_i2pMode->currentData().toInt()},
    };
}

QCborMap TorrentOptionsForm::changedOptions() const
{
    const QCborMap all = options();
    QCborMap out;
    for (int f = 0; f < FieldCount; ++f)
        if (m_edited[f])
            for (const QString& k : keysOf(Field(f)))
                out.insert(k, all.value(k));
    return out;
}

void TorrentOptionsForm::applyRateMaxima()
{
    // thePrefs mirrors the daemon (GetPreferences on connect); 0 = no limit
    const auto ceiling = [](uint32 global, uint32 bt) {
        uint32 kb = 1'000'000;
        for (const uint32 limit : {global, bt})
            if (limit > 0)
                kb = std::min(kb, limit);
        return int(kb);
    };
    m_limitDownKb->setMaximum(ceiling(thePrefs.maxDownload(), thePrefs.btMaxDownloadKb()));
    m_limitUpKb->setMaximum(ceiling(thePrefs.maxUpload(), thePrefs.btMaxUploadKb()));
}

bool TorrentOptionsForm::takeI2pRouterWarning()
{
    return std::exchange(m_i2pWarn, false);
}

QList<QString> TorrentOptionsForm::keysOf(Field field)
{
    switch (field) {
    case FieldDown: return {QStringLiteral("downloadLimitKb")};
    case FieldUp: return {QStringLiteral("uploadLimitKb")};
    case FieldRatio: return {QStringLiteral("ratioMode"), QStringLiteral("ratioPercent")};
    case FieldSeedTime: return {QStringLiteral("seedTimeMode"), QStringLiteral("seedTimeMinutes")};
    case FieldMaxPeers: return {QStringLiteral("maxPeers")};
    case FieldI2p: return {QStringLiteral("i2pMode")};
    case FieldCount: break;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void TorrentOptionsForm::fill(const QCborMap& options)
{
    m_filling = true;
    // the limits may have changed since the last fill
    applyRateMaxima();
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
    m_i2pEnabled = options.value(QStringLiteral("i2pEnabled")).toBool(true);
    m_i2pMixed = options.value(QStringLiteral("i2pMixed")).toBool(false);
    // session mixed mode lets I2P torrents take direct peers: no "I2P only" then
    if (auto* model = qobject_cast<QStandardItemModel*>(m_i2pMode->model()))
        if (QStandardItem* item = model->item(m_i2pMode->findData(I2pOnly))) {
            item->setEnabled(!m_i2pMixed);
            item->setToolTip(m_i2pMixed ? tr("Not while mixed mode is on in Options > BitTorrent > I2P.") : QString());
        }
    selectMode(m_i2pMode, int(options.value(QStringLiteral("i2pMode")).toInteger()));
    updateEnables();
    m_filling = false;
}

void TorrentOptionsForm::markEdited(Field field)
{
    updateEnables();
    if (m_filling)
        return;
    m_edited[field] = true;
    m_variesLabel[field]->hide();   // now one value for all
    emit edited();
}

void TorrentOptionsForm::updateEnables()
{
    m_limitDownKb->setEnabled(m_limitDown->isChecked());
    m_limitUpKb->setEnabled(m_limitUp->isChecked());
    m_ratio100->setEnabled(m_ratioMode->currentData().toInt() == ModeCustom);
    m_seedMinutes->setEnabled(m_seedTimeMode->currentData().toInt() == ModeCustom);
}

QLabel* TorrentOptionsForm::addVariesLabel(Field field, QWidget* parent)
{
    auto* label = new QLabel(tr("Varies"), parent);
    label->setEnabled(false);   // greyed: a hint, not a value
    label->setToolTip(tr("The selected torrents differ here. Change it to set one value for all."));
    label->hide();
    m_variesLabel[field] = label;
    return label;
}

} // namespace eMule
