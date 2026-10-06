#include "pch.h"
#include "dialogs/PeerPreviewDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace eMule {

PeerPreviewDialog::PeerPreviewDialog(const QString& fileName, std::vector<QImage> frames,
                                     QWidget* parent)
    : QDialog(parent)
    , m_frames(std::move(frames))
{
    setWindowTitle(tr("Preview: %1").arg(fileName));
    setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(this);

    m_image = new QLabel(this);
    m_image->setAlignment(Qt::AlignCenter);
    // Room for the largest frame, so paging does not resize the window.
    QSize largest(200, 150);
    for (const QImage& frame : m_frames)
        largest = largest.expandedTo(frame.size());
    m_image->setMinimumSize(largest.boundedTo(QSize(1024, 768)));
    layout->addWidget(m_image, 1);

    auto* row = new QHBoxLayout;
    auto* prior = new QPushButton(tr("Prior"), this);
    auto* next = new QPushButton(tr("Next"), this);
    auto* close = new QPushButton(tr("Close"), this);
    m_counter = new QLabel(this);
    row->addWidget(prior);
    row->addWidget(next);
    row->addWidget(m_counter, 1, Qt::AlignCenter);
    row->addWidget(close);
    layout->addLayout(row);

    const bool several = m_frames.size() > 1;
    prior->setEnabled(several);
    next->setEnabled(several);
    close->setDefault(true);

    connect(prior, &QPushButton::clicked, this, [this] { showFrame(m_current - 1); });
    connect(next, &QPushButton::clicked, this, [this] { showFrame(m_current + 1); });
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    showFrame(0);
}

void PeerPreviewDialog::showFrame(int index)
{
    const int count = frameCount();
    if (count == 0) {
        m_counter->setText(tr("No images"));
        return;
    }
    m_current = ((index % count) + count) % count;

    QPixmap pixmap = QPixmap::fromImage(m_frames[static_cast<size_t>(m_current)]);
    const QSize room = m_image->minimumSize();
    if (pixmap.width() > room.width() || pixmap.height() > room.height())
        pixmap = pixmap.scaled(room, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_image->setPixmap(pixmap);
    m_counter->setText(tr("Image %1 of %2").arg(m_current + 1).arg(count));
}

} // namespace eMule
