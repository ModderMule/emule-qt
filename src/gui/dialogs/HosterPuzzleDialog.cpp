/// @file HosterPuzzleDialog.cpp
/// @brief A hoster's image puzzle — implementation.

#include "pch.h"
#include "dialogs/HosterPuzzleDialog.h"

#include <QDialogButtonBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace eMule {

HosterPuzzleDialog::HosterPuzzleDialog(const QByteArray& image, const QString& host, const QString& fileName,
                                       const QString& explain, int timeoutS, QWidget* parent)
    : QDialog(parent)
    , m_left(timeoutS > 0 ? timeoutS : 300)
{
    setWindowTitle(tr("Puzzle from %1").arg(host));
    auto* layout = new QVBoxLayout(this);
    if (!fileName.isEmpty()) {
        auto* file = new QLabel(tr("For %1").arg(fileName), this);
        file->setTextFormat(Qt::PlainText);
        layout->addWidget(file);
    }
    auto* picture = new QLabel(this);
    picture->setAlignment(Qt::AlignCenter);
    // the bytes come from a hoster's page: only decoded as an image, never shown as text
    QImage img = QImage::fromData(image);
    if (img.isNull()) {
        picture->setText(tr("The puzzle image could not be shown."));
    } else {
        if (img.width() < 200)
            img = img.scaledToWidth(img.width() * 2, Qt::SmoothTransformation);
        picture->setPixmap(QPixmap::fromImage(img));
    }
    layout->addWidget(picture);
    if (!explain.isEmpty()) {
        auto* hint = new QLabel(explain, this);
        hint->setWordWrap(true);
        hint->setTextFormat(Qt::PlainText);
        layout->addWidget(hint);
    }
    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(tr("Type what the image shows"));
    layout->addWidget(m_edit);
    m_countdown = new QLabel(this);
    layout->addWidget(m_countdown);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    auto* skip = buttons->addButton(tr("Skip"), QDialogButtonBox::RejectRole);
    Q_UNUSED(skip)
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &HosterPuzzleDialog::tick);
    m_timer.start();
    tick();
    m_edit->setFocus();
}

void HosterPuzzleDialog::done(int result)
{
    if (!m_sent) {
        m_sent = true;
        m_timer.stop();
        emit answered(result == QDialog::Accepted ? m_edit->text().trimmed() : QString());
    }
    QDialog::done(result);
}

void HosterPuzzleDialog::tick()
{
    m_countdown->setText(tr("%n second(s) left", nullptr, m_left));
    if (--m_left < 0)
        reject();
}

} // namespace eMule
