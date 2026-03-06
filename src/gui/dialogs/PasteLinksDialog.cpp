#include "pch.h"
#include "dialogs/PasteLinksDialog.h"

#include "app/IpcClient.h"
#include "controls/DownloadListModel.h"
#include "controls/SharedFilesModel.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "protocol/ED2KLink.h"

#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace eMule {

PasteLinksDialog::PasteLinksDialog(IpcClient* ipc,
                                   DownloadListModel* dlModel,
                                   SharedFilesModel* sfModel,
                                   QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
    , m_dlModel(dlModel)
    , m_sfModel(sfModel)
{
    setWindowTitle(tr("Paste eD2K Links"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/eD2kLinkPaste.ico")));
    resize(450, 250);

    auto* layout = new QVBoxLayout(this);

    auto* label = new QLabel(tr("eD2K Links:"), this);
    layout->addWidget(label);

    m_edit = new QPlainTextEdit(this);
    m_edit->setPlaceholderText(tr("Paste one or more ed2k:// links here, one per line..."));
    layout->addWidget(m_edit);

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();

    m_downloadBtn = new QPushButton(tr("Download"), this);
    m_downloadBtn->setDefault(true);
    m_downloadBtn->setEnabled(false);
    btnLayout->addWidget(m_downloadBtn);

    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    btnLayout->addWidget(cancelBtn);

    layout->addLayout(btnLayout);

    connect(m_edit, &QPlainTextEdit::textChanged, this, [this] {
        m_downloadBtn->setEnabled(!m_edit->toPlainText().trimmed().isEmpty());
    });
    connect(m_downloadBtn, &QPushButton::clicked, this, &PasteLinksDialog::onDownload);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void PasteLinksDialog::onDownload()
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Not Connected"),
            tr("Not connected to the daemon."));
        return;
    }

    const QString text = m_edit->toPlainText().trimmed();
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    int downloaded = 0;
    int skipped = 0;
    QStringList invalid;

    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        auto parsed = parseED2KLink(trimmed);
        if (!parsed) {
            invalid << trimmed;
            continue;
        }

        auto* fileLink = std::get_if<ED2KFileLink>(&*parsed);
        if (!fileLink) {
            invalid << trimmed;
            continue;
        }

        QString hashHex;
        for (uint8 b : fileLink->hash)
            hashHex += QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0'));

        if ((m_dlModel && m_dlModel->containsHash(hashHex))
            || (m_sfModel && m_sfModel->containsHash(hashHex))) {
            ++skipped;
            continue;
        }

        Ipc::IpcMessage msg(Ipc::IpcMsgType::DownloadSearchFile);
        msg.append(hashHex);
        msg.append(fileLink->name);
        msg.append(static_cast<int64_t>(fileLink->size));
        m_ipc->sendRequest(std::move(msg));
        ++downloaded;
    }

    if (!invalid.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Links"),
            tr("The following links could not be parsed:\n\n%1")
                .arg(invalid.join(QLatin1Char('\n'))));
    }

    if (skipped > 0) {
        QMessageBox::information(this, tr("Links Skipped"),
            tr("%n file(s) skipped (already downloading or shared).", nullptr, skipped));
    }

    if (downloaded > 0)
        accept();
}

} // namespace eMule
