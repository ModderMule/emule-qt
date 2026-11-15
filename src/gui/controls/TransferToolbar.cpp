#include "pch.h"
/// @file TransferToolbar.cpp
/// @brief Section header toolbar widget — implementation.

#include "controls/TransferToolbar.h"

#include "utils/ColorUtils.h"

#include <QButtonGroup>
#include <QCursor>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>

namespace eMule {

namespace {

// Hover fill shared by the header and the buttons; QMacStyle draws none for auto-raise
void paintHoverPanel(QPainter& p, const QWidget* w, const QRect& r)
{
    const QPalette& pal = w->palette();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(blend(pal.color(QPalette::Window), pal.color(QPalette::ButtonText), 0.30));
    p.setBrush(blend(pal.color(QPalette::Window), pal.color(QPalette::ButtonText), 0.12));
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
}

class HoverToolButton : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        if (underMouse() && isEnabled()) {
            QPainter p(this);
            paintHoverPanel(p, this, rect());
        }
        QToolButton::paintEvent(event);
    }
};

} // namespace

TransferToolbar::TransferToolbar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(22);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(4, 0, 0, 0);
    m_layout->setSpacing(1);

    // Leading icon (shown before the label, e.g. current view icon)
    m_iconLabel = new QLabel;
    m_iconLabel->setFixedSize(16, 16);
    m_iconLabel->setScaledContents(true);
    m_iconLabel->hide(); // hidden until setLeadingIcon() is called
    m_layout->addWidget(m_iconLabel);

    m_label = new QLabel;
    QFont bold = m_label->font();
    bold.setBold(true);
    m_label->setFont(bold);
    m_layout->addWidget(m_label);

    // Icon + label act as MFC's IDC_DOWNLOAD_ICO / IDC_UPLOAD_ICO button: a click
    // shows the next list, with or without the buttons; hover shows that
    setMouseTracking(true);
    for (QLabel* l : {m_iconLabel, m_label}) {
        l->setMouseTracking(true);
        l->installEventFilter(this);
    }

    // Trailing stretch — buttons are inserted before this via addButton()
    m_layout->addStretch(1);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    connect(m_group, &QButtonGroup::idClicked, this, &TransferToolbar::buttonClicked);
}

int TransferToolbar::addButton(const QIcon& icon, const QString& tooltip)
{
    auto* btn = new HoverToolButton;
    btn->setIcon(icon);
    btn->setToolTip(tooltip);
    btn->setCheckable(true);
    btn->setAutoRaise(true);
    btn->setFixedSize(20, 20);
    btn->setIconSize(QSize(16, 16));

    btn->setVisible(m_buttonsVisible);

    const int id = m_nextId++;
    m_group->addButton(btn, id);
    // Insert before the trailing stretch so buttons stay left-aligned
    m_layout->insertWidget(m_layout->count() - 1, btn);
    return id;
}

void TransferToolbar::checkButton(int id)
{
    if (auto* btn = m_group->button(id))
        btn->setChecked(true);
}

void TransferToolbar::setButtonEnabled(int id, bool on)
{
    if (auto* btn = m_group->button(id))
        btn->setEnabled(on);
}

void TransferToolbar::setButtonsVisible(bool on)
{
    m_buttonsVisible = on;
    for (auto* btn : m_group->buttons())
        btn->setVisible(on);
    updateHeaderHover();
    update();
}

void TransferToolbar::reserveLabelWidth(const QStringList& texts)
{
    m_reservedTexts = texts;
    applyLabelWidth();
}

void TransferToolbar::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::FontChange)
        applyLabelWidth();
    QWidget::changeEvent(event);
}

bool TransferToolbar::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::MouseMove:
        updateHeaderHover();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void TransferToolbar::leaveEvent(QEvent* event)
{
    updateHeaderHover();
    QWidget::leaveEvent(event);
}

void TransferToolbar::mouseMoveEvent(QMouseEvent* event)
{
    updateHeaderHover();
    QWidget::mouseMoveEvent(event);
}

void TransferToolbar::paintEvent(QPaintEvent* event)
{
    if (m_headerHovered) {
        QPainter p(this);
        paintHoverPanel(p, this, headerRect());
    }
    QWidget::paintEvent(event);
}

void TransferToolbar::mouseReleaseEvent(QMouseEvent* event)
{
    // Without buttons the whole header cycles; with them only the icon + label do
    if (event->button() == Qt::LeftButton
        && rect().contains(event->position().toPoint())
        && headerRect().contains(event->position().toPoint()))
        emit cycleRequested();
    QWidget::mouseReleaseEvent(event);
}

void TransferToolbar::setLabelText(const QString& text)
{
    m_label->setText(text);
}

void TransferToolbar::setLeadingIcon(const QIcon& icon)
{
    if (icon.isNull()) {
        m_iconLabel->hide();
    } else {
        m_iconLabel->setPixmap(icon.pixmap(16, 16));
        m_iconLabel->show();
    }
}

void TransferToolbar::applyLabelWidth()
{
    // MFC WND1_BUTTON_WIDTH / WND2_BUTTON_WIDTH: fixed label, buttons never move
    constexpr int kMfcLabelWidth = 170;
    const QFontMetrics fm(m_label->font());
    int width = kMfcLabelWidth;
    for (const QString& text : std::as_const(m_reservedTexts))
        width = std::max(width, fm.horizontalAdvance(text));
    m_label->setMinimumWidth(width + m_label->contentsMargins().left()
                             + m_label->contentsMargins().right());
}

QRect TransferToolbar::headerRect() const
{
    // Without buttons the whole header is the button
    if (!m_buttonsVisible)
        return rect();
    const int left = m_iconLabel->isVisible() ? m_iconLabel->geometry().left()
                                              : m_label->geometry().left();
    const QRect label = m_label->geometry();
    // Label is padded to 170 px; hug the text instead
    const int textRight = std::min(label.right(),
        label.left() + m_label->fontMetrics().horizontalAdvance(m_label->text()) + 3);
    return QRect(QPoint(left - 2, 1), QPoint(textRight, height() - 2));
}

void TransferToolbar::updateHeaderHover()
{
    const QPoint pos = mapFromGlobal(QCursor::pos());
    const bool hovered = rect().contains(pos) && headerRect().contains(pos);
    if (hovered != m_headerHovered) {
        m_headerHovered = hovered;
        update();
    }
}

} // namespace eMule
