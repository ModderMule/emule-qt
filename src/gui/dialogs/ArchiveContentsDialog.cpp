#include "pch.h"
#include "dialogs/ArchiveContentsDialog.h"

#include "controls/ArchiveContentsView.h"
#include "utils/ArchiveContentsIpc.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "app/IpcClient.h"

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPointer>
#include <QVBoxLayout>

namespace eMule {

ArchiveContentsDialog::ArchiveContentsDialog(IpcClient* ipc, const QString& title, const QString& kind,
                                             const QCborMap& payload, const QString& folder, QWidget* parent)
    : RememberedSizeDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Archive Contents: %1").arg(title));
    auto* layout = new QVBoxLayout(this);
    // a queued download: entries from ed3k, or what its unpack extracts
    m_view = new ArchiveContentsView(this, false);
    m_view->setQueued(true);
    layout->addWidget(m_view, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    ArchiveContentsIpc::attach(m_view, ipc, [kind, payload] { return std::pair{kind, payload}; }, folder);
    QPointer<IpcClient> client(ipc);
    connect(m_view, &ArchiveContentsView::downloadArchiveRequested, this,
            [this, client, payload](const QHash<QString, QStringList>& entries, const QString&) {
        if (!client || !client->isConnected())
            return;
        Ipc::IpcMessage msg(Ipc::IpcMsgType::SetArchiveEntries);
        msg.append(payload);
        msg.append(ArchiveContentsIpc::entriesToCbor(entries));
        QPointer<ArchiveContentsDialog> self(this);
        client->sendRequest(std::move(msg), [self](const Ipc::IpcMessage& resp) {
            if (!self)
                return;
            if (!resp.isValid() || !resp.fieldBool(0)) {
                QMessageBox::warning(self, self->windowTitle(),
                                     resp.isValid() ? resp.fieldString(1) : tr("The core did not answer."));
                return;
            }
            self->m_view->statusLabel()->setText(tr("Saved: the download extracts only the checked files."));
        });
    });
    // #224: every file of an archive is here: copied from disk, its volumes not downloaded
    connect(m_view, &ArchiveContentsView::takeFromDiskRequested, this, [this, client, payload, folder](const QCborArray& archives) {
        if (!client || !client->isConnected() || archives.isEmpty())
            return;
        Ipc::IpcMessage msg(Ipc::IpcMsgType::TakeArchiveFromDisk);
        msg.append(payload);
        msg.append(archives);
        msg.append(folder);
        msg.append(qint64(0));
        QPointer<ArchiveContentsDialog> self(this);
        client->sendRequest(std::move(msg), [self](const Ipc::IpcMessage& resp) {
            if (!self)
                return;
            if (!resp.isValid() || !resp.fieldBool(0)) {
                QMessageBox::warning(self, self->windowTitle(),
                                     resp.isValid() ? resp.fieldString(1) : tr("The core did not answer."));
                return;
            }
            const QCborMap r = resp.fieldMap(1);
            QString text = tr("%n file(s) copied from disk; %1 archive part(s) skipped.", nullptr,
                              int(r.value(QStringLiteral("added")).toInteger()))
                               .arg(r.value(QStringLiteral("skipped")).toInteger());
            if (const QString note = r.value(QStringLiteral("note")).toString(); !note.isEmpty())
                text += u' ' + note;
            self->m_view->statusLabel()->setText(text);
        });
    });
    resize(640, 420);
    m_view->ensureLookup();
}

} // namespace eMule
