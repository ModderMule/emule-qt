#include "pch.h"

/// @file PreviewImageView.cpp
/// @brief Picture viewer for search Details › Images — see PreviewImageView.h.

#include "controls/PreviewImageView.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace eMule {

namespace {

constexpr int kThumbHeight = 72;

} // namespace

PreviewImageView::PreviewImageView(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);

    m_message = new QLabel(m_stack);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_stack->addWidget(m_message);

    auto* page = new QWidget(m_stack);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* row = new QHBoxLayout;
    m_prev = new QToolButton(page);
    m_prev->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    m_prev->setAutoRaise(true);
    m_prev->setToolTip(tr("Previous image"));
    m_next = new QToolButton(page);
    m_next->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    m_next->setAutoRaise(true);
    m_next->setToolTip(tr("Next image"));
    m_large = new QLabel(page);
    m_large->setAlignment(Qt::AlignCenter);
    m_large->setMinimumSize(160, 120);
    m_large->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    row->addWidget(m_prev);
    row->addWidget(m_large, 1);
    row->addWidget(m_next);
    layout->addLayout(row, 1);

    m_caption = new QLabel(page);
    m_caption->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_caption);

    m_strip = new QListWidget(page);
    m_strip->setViewMode(QListView::IconMode);
    m_strip->setFlow(QListView::LeftToRight);
    m_strip->setWrapping(false);
    m_strip->setMovement(QListView::Static);
    m_strip->setIconSize(QSize(kThumbHeight * 16 / 9, kThumbHeight));
    m_strip->setFixedHeight(kThumbHeight + 2 * style()->pixelMetric(QStyle::PM_ScrollBarExtent));
    m_strip->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_strip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_strip);
    m_stack->addWidget(page);

    connect(m_prev, &QToolButton::clicked, this, [this] { showImage(m_current - 1); });
    connect(m_next, &QToolButton::clicked, this, [this] { showImage(m_current + 1); });
    connect(m_strip, &QListWidget::currentRowChanged, this, [this](int r) {
        if (r >= 0 && r != m_current)
            showImage(r);
    });
}

void PreviewImageView::showMessage(const QString& text)
{
    m_message->setText(text);
    m_stack->setCurrentWidget(m_message);
}

QString PreviewImageView::message() const
{
    return m_stack->currentWidget() == m_message ? m_message->text() : QString();
}

void PreviewImageView::setImages(const QList<Image>& images, const QString& emptyText)
{
    m_images.clear();
    m_strip->clear();
    m_current = -1;
    // covers first, then posted, then frames: what the poster chose leads
    for (const QString kind : {QStringLiteral("cover"), QStringLiteral("posted"), QStringLiteral("frame")}) {
        for (const Image& img : images) {
            if (img.kind != kind)
                continue;
            QImage decoded = QImage::fromData(img.data);
            if (decoded.isNull())
                continue;
            m_images.append({img.name, std::move(decoded)});
        }
    }
    if (m_images.isEmpty()) {
        showMessage(emptyText);
        return;
    }
    for (const Shown& s : std::as_const(m_images)) {
        auto* item = new QListWidgetItem(
            QIcon(QPixmap::fromImage(s.image.scaledToHeight(kThumbHeight, Qt::SmoothTransformation))),
            QString(), m_strip);
        item->setToolTip(s.caption);
    }
    m_strip->setVisible(m_images.size() > 1);
    m_stack->setCurrentIndex(1);
    showImage(0);
}

void PreviewImageView::showImage(int index)
{
    if (m_images.isEmpty())
        return;
    m_current = std::clamp(index, 0, int(m_images.size()) - 1);
    m_prev->setEnabled(m_current > 0);
    m_next->setEnabled(m_current + 1 < m_images.size());
    m_caption->setText(tr("%1 (%2 of %3)").arg(m_images.at(m_current).caption).arg(m_current + 1)
                           .arg(m_images.size()));
    if (m_strip->currentRow() != m_current)
        m_strip->setCurrentRow(m_current);
    updateLarge();
}

void PreviewImageView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateLarge();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void PreviewImageView::updateLarge()
{
    if (m_current < 0 || m_current >= m_images.size())
        return;
    const QImage& img = m_images.at(m_current).image;
    const QSize box = m_large->size();
    // never upscale: a small preview stays sharp
    const QImage fitted = img.width() > box.width() || img.height() > box.height()
                              ? img.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                              : img;
    m_large->setPixmap(QPixmap::fromImage(fitted));
}

} // namespace eMule
