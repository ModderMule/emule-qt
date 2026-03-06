#include "pch.h"
#ifdef Q_OS_WIN

#include "app/MiniMuleWidget.h"

#include <QApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

namespace eMule {

namespace {

/// Create a stat row: icon + label name + value label, returns the value label.
QLabel* makeStatRow(QLayout* layout, const QString& iconPath,
                    const QString& name)
{
    auto* row = new QHBoxLayout;
    row->setSpacing(6);

    auto* icon = new QLabel;
    icon->setFixedSize(16, 16);
    icon->setScaledContents(true);
    icon->setPixmap(QIcon(iconPath).pixmap(16, 16));
    row->addWidget(icon);

    auto* nameLabel = new QLabel(name);
    nameLabel->setStyleSheet(QStringLiteral("color: #663300; font-weight: bold;"));
    row->addWidget(nameLabel);

    auto* valueLabel = new QLabel;
    valueLabel->setStyleSheet(QStringLiteral("color: #333333;"));
    row->addWidget(valueLabel);
    row->addStretch();

    layout->addItem(row);
    return valueLabel;
}

} // anonymous namespace

MiniMuleWidget::MiniMuleWidget(QSystemTrayIcon* trayIcon, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_trayIcon(trayIcon)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowOpacity(0.95);
    setFixedSize(260, 175);

    setupUi();

    // Auto-close timer: 3 seconds after mouse leaves
    m_autoCloseTimer = new QTimer(this);
    m_autoCloseTimer->setSingleShot(true);
    m_autoCloseTimer->setInterval(3000);
    connect(m_autoCloseTimer, &QTimer::timeout, this, &QWidget::hide);
}

void MiniMuleWidget::updateStats(bool connected, double upKBs, double downKBs,
                                 int completed, qint64 freeBytes)
{
    m_connectedLabel->setText(connected ? tr("Yes") : tr("No"));
    m_connectedLabel->setStyleSheet(connected
        ? QStringLiteral("color: green; font-weight: bold;")
        : QStringLiteral("color: red; font-weight: bold;"));

    m_uploadLabel->setText(QStringLiteral("%1 KB/s").arg(upKBs, 0, 'f', 1));
    m_downloadLabel->setText(QStringLiteral("%1 KB/s").arg(downKBs, 0, 'f', 1));
    m_completedLabel->setText(QString::number(completed));

    // Format free space in human-readable form
    const QLocale locale;
    if (freeBytes >= qint64(1) << 30)
        m_freeSpaceLabel->setText(QStringLiteral("%1 GB")
            .arg(static_cast<double>(freeBytes) / (1 << 30), 0, 'f', 2));
    else if (freeBytes >= qint64(1) << 20)
        m_freeSpaceLabel->setText(QStringLiteral("%1 MB")
            .arg(static_cast<double>(freeBytes) / (1 << 20), 0, 'f', 1));
    else
        m_freeSpaceLabel->setText(QStringLiteral("%1 KB")
            .arg(freeBytes / 1024));
}

void MiniMuleWidget::showNearTray()
{
    // Get tray icon geometry
    QRect trayRect = m_trayIcon->geometry();
    if (trayRect.isNull() || !trayRect.isValid()) {
        // Fallback: bottom-right corner of primary screen
        if (auto* screen = QGuiApplication::primaryScreen()) {
            const QRect avail = screen->availableGeometry();
            trayRect = QRect(avail.right() - 50, avail.bottom() - 30, 50, 30);
        }
    }

    const QPoint trayCenter = trayRect.center();
    auto* screen = QGuiApplication::screenAt(trayCenter);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect avail = screen->availableGeometry();

    int x = trayCenter.x() - width() / 2;
    int y = 0;

    // Place above tray if tray is at bottom, below if at top
    if (trayRect.top() > avail.center().y()) {
        // Tray is in the bottom half — place popup above it
        y = trayRect.top() - height() - 4;
    } else {
        // Tray is in the top half — place popup below it
        y = trayRect.bottom() + 4;
    }

    // Clamp to screen edges with 8px margin
    constexpr int margin = 8;
    x = qBound(avail.left() + margin, x, avail.right() - width() - margin);
    y = qBound(avail.top() + margin, y, avail.bottom() - height() - margin);

    move(x, y);
    show();
    raise();

    // Start auto-close timer
    m_autoCloseTimer->start();
}

void MiniMuleWidget::enterEvent(QEnterEvent* /*event*/)
{
    m_autoCloseTimer->stop();
}

void MiniMuleWidget::leaveEvent(QEvent* /*event*/)
{
    m_autoCloseTimer->start();
}

void MiniMuleWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Orange/brown gradient background matching MFC MiniMule
    QLinearGradient grad(0, 0, 0, height());
    grad.setColorAt(0.0, QColor(0xFF, 0xDD, 0xAA));  // light orange top
    grad.setColorAt(1.0, QColor(0xFF, 0xCC, 0x99));   // slightly darker bottom
    p.setBrush(grad);
    p.setPen(QPen(QColor(0xCC, 0x99, 0x66), 1.5));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
}

// ---------------------------------------------------------------------------
// Private setup
// ---------------------------------------------------------------------------

void MiniMuleWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 8, 12, 8);
    mainLayout->setSpacing(4);

    // Title
    auto* title = new QLabel(QStringLiteral("eMule Qt"));
    title->setStyleSheet(QStringLiteral(
        "color: #663300; font-weight: bold; font-size: 12px;"));
    title->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(title);

    mainLayout->addSpacing(2);

    // Stat rows
    m_connectedLabel = makeStatRow(mainLayout,
        QStringLiteral(":/icons/ConnectDo.ico"), tr("Connected"));
    m_uploadLabel = makeStatRow(mainLayout,
        QStringLiteral(":/icons/Upload.ico"), tr("Upload"));
    m_downloadLabel = makeStatRow(mainLayout,
        QStringLiteral(":/icons/Download.ico"), tr("Download"));
    m_completedLabel = makeStatRow(mainLayout,
        QStringLiteral(":/icons/Transfer.ico"), tr("Completed"));
    m_freeSpaceLabel = makeStatRow(mainLayout,
        QStringLiteral(":/icons/HardDisk.ico"), tr("Free Space"));

    mainLayout->addStretch();

    // Action buttons
    auto* btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(4);

    auto makeBtn = [&](const QString& iconPath, const QString& tooltip) {
        auto* btn = new QPushButton;
        btn->setFixedSize(28, 28);
        btn->setIcon(QIcon(iconPath));
        btn->setIconSize(QSize(20, 20));
        btn->setToolTip(tooltip);
        btn->setFlat(true);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { border: 1px solid #CC9966; border-radius: 3px; "
            "background: rgba(255,255,255,80); }"
            "QPushButton:hover { background: rgba(255,255,255,160); }"));
        btnLayout->addWidget(btn);
        return btn;
    };

    m_restoreBtn = makeBtn(QStringLiteral(":/icons/Display.ico"), tr("Restore Window"));
    m_incomingBtn = makeBtn(QStringLiteral(":/icons/Incoming.ico"), tr("Open Incoming Folder"));
    m_optionsBtn = makeBtn(QStringLiteral(":/icons/Preferences.ico"), tr("Options"));
    btnLayout->addStretch();

    mainLayout->addLayout(btnLayout);

    connect(m_restoreBtn, &QPushButton::clicked, this, &MiniMuleWidget::restoreRequested);
    connect(m_incomingBtn, &QPushButton::clicked, this, &MiniMuleWidget::openIncomingRequested);
    connect(m_optionsBtn, &QPushButton::clicked, this, &MiniMuleWidget::optionsRequested);
}

} // namespace eMule

#endif // Q_OS_WIN
