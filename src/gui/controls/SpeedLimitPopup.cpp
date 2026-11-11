#include "pch.h"
#include "controls/SpeedLimitPopup.h"

#include "app/IpcClient.h"
#include "app/SkinIcons.h"
#include "controls/SpeedLimitWidget.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace eMule {

SpeedLimitPopup::SpeedLimitPopup(QWidget* parent)
    : QFrame(parent, Qt::Popup)
{
    setFrameShape(QFrame::StyledPanel);
    setAttribute(Qt::WA_DeleteOnClose, false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // Header as the tray menu's: Speed.ico, "eMule Speed", bold
    auto* header = new QHBoxLayout;
    auto* icon = new QLabel(this);
    setSkinPixmap(icon, QStringLiteral(":/icons/Speed.ico"));
    auto* title = new QLabel(tr("eMule Speed"), this);
    QFont bold = title->font();
    bold.setBold(true);
    title->setFont(bold);
    header->addWidget(icon);
    header->addWidget(title);
    header->addStretch();
    layout->addLayout(header);

    m_speed = new SpeedLimitWidget(this);
    layout->addWidget(m_speed);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(4);
    layout->addLayout(buttons);
    m_fullBtn = addButton(QStringLiteral(":/icons/SpeedMax.ico"), tr("Set Full Up/Down-Speed"));
    m_throttleBtn = addButton(QStringLiteral(":/icons/SpeedMin.ico"), tr("Throttle Up/Down-Speed"));
    m_usenetBtn = addButton(QStringLiteral(":/icons/Pause.ico"), tr("Pause Usenet"));
    m_usenetBtn->setCheckable(true);
    m_optionsBtn = addButton(QStringLiteral(":/icons/Preferences.ico"), tr("Options"));
    for (QToolButton* b : {m_fullBtn, m_throttleBtn, m_usenetBtn, m_optionsBtn})
        buttons->addWidget(b);
    buttons->addStretch();

    connect(m_fullBtn, &QToolButton::clicked, m_speed, &SpeedLimitWidget::setFull);
    connect(m_throttleBtn, &QToolButton::clicked, m_speed, &SpeedLimitWidget::throttle);
    connect(m_speed, &SpeedLimitWidget::limitsChanged, this, &SpeedLimitPopup::updateButtons);
    // clicked(), not toggled(): the state follows the daemon's push
    connect(m_usenetBtn, &QToolButton::clicked, this, [this](bool checked) {
        if (m_ipc)
            m_ipc->requestUsenetPause(checked);
    });
    connect(m_optionsBtn, &QToolButton::clicked, this, [this] {
        hide();
        emit optionsRequested();
    });
}

void SpeedLimitPopup::setIpcClient(IpcClient* ipc)
{
    m_ipc = ipc;
    m_speed->setIpcClient(ipc);
    if (!m_ipc)
        return;
    connect(m_ipc, &IpcClient::usenetEnginePausedChanged, this, &SpeedLimitPopup::updateButtons,
            Qt::UniqueConnection);
}

void SpeedLimitPopup::showAbove(QWidget* anchor)
{
    m_speed->refresh(m_ipc && m_ipc->isConnected());
    updateButtons();
    adjustSize();

    // Right edge on the anchor's, above it; kept on the anchor's screen
    const QPoint anchorTopRight = anchor->mapToGlobal(QPoint(anchor->width(), 0));
    QPoint pos(anchorTopRight.x() - width(), anchorTopRight.y() - height() - 2);
    if (const QScreen* screen = anchor->screen()) {
        const QRect avail = screen->availableGeometry();
        pos.setX(std::clamp(pos.x(), avail.left(), std::max(avail.left(), avail.right() - width())));
        if (pos.y() < avail.top())   // no room above: below the anchor
            pos.setY(anchor->mapToGlobal(QPoint(0, anchor->height())).y() + 2);
    }
    move(pos);
    show();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

QToolButton* SpeedLimitPopup::addButton(const QString& iconPath, const QString& text)
{
    auto* btn = new QToolButton(this);
    btn->setIcon(skinIcon(iconPath));
    btn->setIconSize(QSize(16, 16));
    btn->setToolTip(text);
    btn->setAutoRaise(true);
    return btn;
}

void SpeedLimitPopup::updateButtons()
{
    const bool online = m_ipc && m_ipc->isConnected();
    m_fullBtn->setEnabled(online);
    m_throttleBtn->setEnabled(online && m_speed->canThrottle());
    m_usenetBtn->setEnabled(online);
    const QSignalBlocker block(m_usenetBtn);
    m_usenetBtn->setChecked(m_ipc && m_ipc->usenetEnginePaused());
}

} // namespace eMule
