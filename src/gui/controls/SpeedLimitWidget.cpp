#include "pch.h"
#include "controls/SpeedLimitWidget.h"

#include "app/IpcClient.h"
#include "app/SkinIcons.h"
#include "IpcMessage.h"
#include "prefs/Preferences.h"

#include <QCborMap>
#include <QGridLayout>
#include <QLabel>
#include <QPointer>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>

#include <algorithm>

namespace eMule {

using SpeedRules::Limits;

SpeedLimitWidget::SpeedLimitWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(8, 4, 8, 4);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);

    // Download first, as MFC's tray menu
    m_downSlider = addRow(0, QStringLiteral(":/icons/Download.ico"), tr("Download:"), m_downSpin);
    m_upSlider = addRow(1, QStringLiteral(":/icons/Upload.ico"), tr("Upload:"), m_upSpin);

    connect(m_upSlider, &QSlider::valueChanged, this, &SpeedLimitWidget::onUpMoved);
    connect(m_upSpin, &QSpinBox::valueChanged, this, &SpeedLimitWidget::onUpMoved);
    connect(m_downSlider, &QSlider::valueChanged, this, &SpeedLimitWidget::onDownMoved);
    connect(m_downSpin, &QSpinBox::valueChanged, this, &SpeedLimitWidget::onDownMoved);

    // Dragging sends once it rests, not per pixel
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(100);
    connect(m_debounce, &QTimer::timeout, this, &SpeedLimitWidget::sendLimits);
}

void SpeedLimitWidget::setIpcClient(IpcClient* ipc)
{
    m_ipc = ipc;
}

void SpeedLimitWidget::refresh(bool ipcConnected)
{
    // 0 = unlimited: shown at the capacity, which is what Full sets
    const uint32 up = thePrefs.configuredMaxUpload();
    const uint32 down = thePrefs.configuredMaxDownload();
    showLimits({up != 0 ? up : capUp(), down != 0 ? down : capDown()});

    for (QWidget* w : {static_cast<QWidget*>(m_upSlider), static_cast<QWidget*>(m_upSpin),
                       static_cast<QWidget*>(m_downSlider), static_cast<QWidget*>(m_downSpin)})
        w->setEnabled(ipcConnected);
}

void SpeedLimitWidget::setFull()
{
    const uint32 up = capUp();
    showLimits({up, SpeedRules::clampDownForUp(up, capDown(), capDown())});
    commit();
}

void SpeedLimitWidget::throttle()
{
    if (!canThrottle())
        return;
    showLimits(SpeedRules::throttleStep(limits(), capUp(), capDown()));
    commit();
}

bool SpeedLimitWidget::canThrottle() const
{
    return SpeedRules::canThrottle(limits(), capUp(), capDown());
}

Limits SpeedLimitWidget::limits() const
{
    return {static_cast<uint32>(m_upSlider->value()), static_cast<uint32>(m_downSlider->value())};
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

QSlider* SpeedLimitWidget::addRow(int row, const QString& iconPath, const QString& label,
                                  QSpinBox*& spin)
{
    auto* grid = static_cast<QGridLayout*>(layout());

    auto* icon = new QLabel(this);
    setSkinPixmap(icon, iconPath);
    auto* slider = new QSlider(Qt::Horizontal, this);
    slider->setMinimumWidth(120);
    spin = new QSpinBox(this);
    spin->setSuffix(tr(" KB/s"));
    spin->setKeyboardTracking(false);   // "50" must not pass through 5 and pull the other down

    grid->addWidget(icon, row, 0);
    grid->addWidget(new QLabel(label, this), row, 1);
    grid->addWidget(slider, row, 2);
    grid->addWidget(spin, row, 3);
    return slider;
}

void SpeedLimitWidget::onUpMoved(int value)
{
    // MFC CheckUp: a lower upload pulls the download down
    Limits l{static_cast<uint32>(value), static_cast<uint32>(m_downSlider->value())};
    l.down = SpeedRules::clampDownForUp(l.up, l.down, capDown());
    showLimits(l);
    commit();
}

void SpeedLimitWidget::onDownMoved(int value)
{
    // MFC CheckDown: a higher download pushes the upload up
    Limits l{static_cast<uint32>(m_upSlider->value()), static_cast<uint32>(value)};
    l.up = SpeedRules::raiseUpForDown(l.up, l.down, capUp());
    l.down = SpeedRules::clampDownForUp(l.up, l.down, capDown());   // capacity held the upload
    showLimits(l);
    commit();
}

void SpeedLimitWidget::showLimits(Limits limits)
{
    // Floor..capacity; a stored value outside it widens the range so it shows as it is
    const auto apply = [](QSlider* slider, QSpinBox* spin, uint32 value, uint32 floor, uint32 cap) {
        const int v = static_cast<int>(value);
        const int lo = std::min(static_cast<int>(floor), v);
        const int hi = std::max(static_cast<int>(cap), v);
        const QSignalBlocker b1(slider);
        const QSignalBlocker b2(spin);
        slider->setRange(lo, hi);
        spin->setRange(lo, hi);
        slider->setValue(v);
        spin->setValue(v);
    };
    apply(m_upSlider, m_upSpin, limits.up, SpeedRules::floorUp(capUp()), capUp());
    apply(m_downSlider, m_downSpin, limits.down, SpeedRules::floorDown(capUp(), capDown()), capDown());
}

void SpeedLimitWidget::commit()
{
    m_debounce->start();
    emit limitsChanged();
}

void SpeedLimitWidget::sendLimits()
{
    const Limits l = limits();
    thePrefs.setMaxUpload(l.up);
    thePrefs.setMaxDownload(l.down);

    if (!m_ipc || !m_ipc->isConnected())
        return;
    Ipc::IpcMessage req(Ipc::IpcMsgType::SetPreferences);
    req.append(QStringLiteral("maxUpload"));
    req.append(static_cast<qint64>(l.up));
    req.append(QStringLiteral("maxDownload"));
    req.append(static_cast<qint64>(l.down));
    m_ipc->sendRequest(std::move(req), [self = QPointer(this)](const Ipc::IpcMessage& resp) {
        // The core held the pair to the ratio: show what is in force
        if (!self || !resp.isValid() || !resp.fieldBool(0))
            return;
        const QCborMap held = resp.fieldMap(1);
        if (!held.contains(QStringLiteral("maxUpload")))
            return;
        const auto up = static_cast<uint32>(held.value(QStringLiteral("maxUpload")).toInteger());
        const auto down = static_cast<uint32>(held.value(QStringLiteral("maxDownload")).toInteger());
        thePrefs.setMaxUpload(up);
        thePrefs.setMaxDownload(down);
        self->showLimits({up != 0 ? up : self->capUp(), down != 0 ? down : self->capDown()});
        emit self->limitsChanged();
    });
}

uint32 SpeedLimitWidget::capUp() const
{
    return std::max<uint32>(1, thePrefs.maxGraphUploadRate());
}

uint32 SpeedLimitWidget::capDown() const
{
    return std::max<uint32>(1, thePrefs.maxGraphDownloadRate());
}

} // namespace eMule
