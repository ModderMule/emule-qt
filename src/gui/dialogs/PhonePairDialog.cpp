#include "pch.h"
/// @file PhonePairDialog.cpp
/// @brief QR code + link a phone scans to reach this core.

#include "dialogs/PhonePairDialog.h"

#include "utils/DialogSizing.h"
#include "utils/qrcodegen/qrcodegen.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace eMule {

namespace {

/// Black-on-white QR of @p text, 4-module quiet zone, @p scale px per module
QImage qrImage(const QString& text, int scale)
{
    using qrcodegen::QrCode;
    const QrCode qr = QrCode::encodeText(text.toUtf8().constData(), QrCode::Ecc::MEDIUM);
    constexpr int border = 4;
    const int n = qr.getSize() + 2 * border;
    QImage img(n, n, QImage::Format_Mono);
    img.fill(1);
    for (int y = 0; y < qr.getSize(); ++y)
        for (int x = 0; x < qr.getSize(); ++x)
            if (qr.getModule(x, y))
                img.setPixel(x + border, y + border, 0);
    return img.scaled(n * scale, n * scale, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

} // namespace

PhonePairDialog::PhonePairDialog(const QString& host, uint16_t port, const QString& token,
                                 const QString& fingerprint, QWidget* parent)
    : RememberedSizeDialog(parent)
    , m_host(host)
    , m_port(port)
    , m_token(token)
    , m_fingerprint(fingerprint)
{
    setWindowTitle(tr("Pair Phone"));
    auto* layout = new QVBoxLayout(this);

    auto* header = new QLabel(
        tr("Scan this code with the qDonkey app, or send the link to the phone. The phone "
           "must reach this address: same network, a VPN such as Tailscale, or a forwarded port."),
        this);
    header->setWordWrap(true);
    DialogSizing::enableHeightForWidth(header);
    layout->addWidget(header);

    m_qrLabel = new QLabel(this);
    m_qrLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_qrLabel, 1);

    m_tokenCheck = new QCheckBox(tr("Include core token"), this);
    m_tokenCheck->setToolTip(tr("Whoever scans the code then has full control of the core. "
                                "When off, the app asks for the token."));
    m_tokenCheck->setChecked(!m_token.isEmpty());
    m_tokenCheck->setEnabled(!m_token.isEmpty());
    layout->addWidget(m_tokenCheck);

    auto* linkRow = new QHBoxLayout;
    m_linkEdit = new QLineEdit(this);
    m_linkEdit->setReadOnly(true);
    auto* copyBtn = new QPushButton(tr("Copy Link"), this);
    linkRow->addWidget(m_linkEdit, 1);
    linkRow->addWidget(copyBtn);
    layout->addLayout(linkRow);

    if (m_fingerprint.isEmpty()) {
        auto* note = new QLabel(tr("The core's certificate is unknown here: the app trusts the "
                                   "first one it sees."), this);
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
        DialogSizing::enableHeightForWidth(note);
        layout->addWidget(note);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);

    connect(m_tokenCheck, &QCheckBox::toggled, this, &PhonePairDialog::refresh);
    connect(copyBtn, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(m_linkEdit->text()); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refresh();
    DialogSizing::applySize(this, QSize(380, 480), QSize(420, 540));
}

QString PhonePairDialog::pairLink(const QString& host, uint16_t port, const QString& token,
                                  const QString& fingerprint)
{
    // each value percent-encoded by hand: a token may hold & = + %
    const auto item = [](QLatin1StringView key, const QString& value) -> QString {
        return key + u'=' + QString::fromLatin1(QUrl::toPercentEncoding(value));
    };
    QStringList items{item(QLatin1StringView("h"), host),
                      item(QLatin1StringView("p"), QString::number(port))};
    if (!fingerprint.isEmpty())
        items.append(item(QLatin1StringView("fp"), QString(fingerprint).remove(u':').toLower()));
    if (!token.isEmpty())
        items.append(item(QLatin1StringView("t"), token));
    return QStringLiteral("qdonkey://pair?") + items.join(u'&');
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void PhonePairDialog::refresh()
{
    const QString link = pairLink(m_host, m_port, m_tokenCheck->isChecked() ? m_token : QString{},
                                  m_fingerprint);
    m_linkEdit->setText(link);
    m_qrLabel->setPixmap(QPixmap::fromImage(qrImage(link, 6)));
}

} // namespace eMule
