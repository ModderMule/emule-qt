#include "pch.h"
#include "dialogs/AddNzbUrlDialog.h"

#include "app/IpcClient.h"
#include "utils/StatusBarNotifier.h"

#include <QMessageBox>
#include <QPointer>

namespace eMule {

AddNzbUrlDialog::AddNzbUrlDialog(IpcClient* ipc, QWidget* parent)
    : PasteTextDialog(
          Chrome{tr("Add NZB from URL"),
                 QStringLiteral(":/icons/Usenet.ico"),
                 tr("NZB URLs:"),
                 tr("Paste one or more http(s) links to .nzb files here, one per line..."),
                 tr("Download")},
          parent)
    , m_ipc(ipc)
{
}

void AddNzbUrlDialog::onAccepted()
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Not Connected"),
                             tr("Not connected to the eMule core."));
        return;
    }

    const QStringList urls = lines();
    if (urls.isEmpty())
        return;

    if (urls.size() > kMaxUrls) {
        QMessageBox::warning(this, tr("Add NZB from URL"),
                             tr("Please add at most %1 links at a time.").arg(kMaxUrls));
        return;
    }

    // One request per URL rather than one carrying a list: a batched reply would
    // have to wait out the slowest link's timeout before saying anything about
    // any of them.
    auto failures = std::make_shared<QStringList>();
    auto added = std::make_shared<int>(0);
    const QPointer<AddNzbUrlDialog> self(this);

    setBusy(true);
    m_ipc->sendBatchRequest(
        urls,
        [](const QString& url) {
            Ipc::IpcMessage msg(Ipc::IpcMsgType::AddNzbUrl);
            msg.append(url);
            return msg;
        },
        this,
        [self, failures, added] {
            if (!self)
                return;
            self->setBusy(false);

            if (*added > 0) {
                StatusBarNotifier::post(
                    tr("Queued %n NZB(s) from URL.", "", *added));
            }
            if (failures->isEmpty()) {
                self->accept();
                return;
            }

            QMessageBox::warning(self, tr("Add NZB from URL"),
                                 tr("These links could not be added:\n\n%1")
                                     .arg(failures->join(QLatin1Char('\n'))));

            // Everything that worked is queued, so leaving it in the box would
            // re-add it on a retry. Only the lines still to fix stay.
            if (*added > 0) {
                self->accept();
                return;
            }
            QStringList retry;
            for (const QString& line : std::as_const(*failures))
                retry.append(line.section(QStringLiteral(" — "), 0, 0));
            self->setLines(retry);
        },
        [failures, added](const QString& url, const Ipc::IpcMessage& reply) {
            if (reply.fieldBool(0)) {
                ++(*added);
                return;
            }
            const QString why = reply.fieldString(1);
            failures->append(QStringLiteral("%1 — %2").arg(
                url, why.isEmpty() ? tr("Could not reach the eMule core.") : why));
        });
}

} // namespace eMule
