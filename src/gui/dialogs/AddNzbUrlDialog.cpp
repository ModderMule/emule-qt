#include "pch.h"
#include "dialogs/AddNzbUrlDialog.h"

#include "app/IpcClient.h"
#include "utils/NzbAdd.h"
#include "utils/StatusBarNotifier.h"

#include <QMessageBox>
#include <QPointer>

namespace eMule {

AddNzbUrlDialog::AddNzbUrlDialog(IpcClient* ipc, const QStringList& categories,
                                 QWidget* parent)
    : PasteTextDialog(
          Chrome{tr("Add NZB from URL"),
                 QStringLiteral(":/icons/Usenet.ico"),
                 tr("NZB URLs:"),
                 tr("Paste one or more http(s) links to .nzb files here, one per line..."),
                 tr("Download"),
                 QSize(470, 290),
                 tr("Password:"),
                 /*readOnlyText*/ false,
                 categories},
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
    // Split out by the reply's outcome int, never by reading its sentence: a
    // reworded or translated refusal must not change which pile a URL lands in.
    auto alreadyHave = std::make_shared<QStringList>();
    auto alreadyHaveReasons = std::make_shared<QStringList>();
    const QPointer<AddNzbUrlDialog> self(this);

    // Everything that could be asked has been asked by the time this runs.
    auto finish = [self, failures, added] {
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
    };

    auto onEach = [failures, added, alreadyHave, alreadyHaveReasons]
                  (const QString& url, const Ipc::IpcMessage& reply) {
        if (reply.fieldBool(0)) {
            ++(*added);
            return;
        }
        const QString why = reply.fieldString(1);
        if (static_cast<gui::NzbAddOutcome>(reply.fieldInt(2))
            == gui::NzbAddOutcome::AlreadyDownloaded)
        {
            // Not a failure: the release has left the queue and wanting it again
            // is legitimate. Held back so the whole paste raises one question.
            alreadyHave->append(url);
            alreadyHaveReasons->append(why);
            return;
        }
        failures->append(QStringLiteral("%1 — %2").arg(
            url, why.isEmpty() ? tr("Could not reach the eMule core.") : why));
    };

    // Read once, before setBusy() disables the field. Every URL in one paste
    // gets the same passphrase, which is what a release posted in parts wants.
    const QString pw = password();
    const int category = queueCategory();
    const int priority = queuePriority();
    const bool paused = queuePaused();

    setBusy(true);
    m_ipc->sendBatchRequest(
        urls,
        [pw, category, priority, paused](const QString& url) {
            Ipc::IpcMessage msg(Ipc::IpcMsgType::AddNzbUrl);
            msg.append(url);
            msg.append(false);   // automatic — a person pasted these
            msg.append(false);   // force
            msg.append(pw);      // archive passphrase, usually empty
            msg.append(qint64(category));
            msg.append(qint64(priority));
            msg.append(paused);
            return msg;
        },
        this,
        [self, failures, alreadyHave, alreadyHaveReasons, onEach, finish, pw,
         category, priority, paused] {
            if (!self)
                return;
            if (alreadyHave->isEmpty()) {
                finish();
                return;
            }

            // One question for the whole paste. Asking per URL would put a modal
            // in front of the user once per line of something they pasted once.
            const bool again =
                QMessageBox::question(
                    self, tr("Add NZB from URL"),
                    tr("You have already downloaded %n of these. Download them again?\n\n%1",
                       "", alreadyHave->size())
                        .arg(alreadyHaveReasons->join(QLatin1Char('\n'))),
                    QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
            if (!again || !self->m_ipc) {
                finish();
                return;
            }

            // Same requests, one bit different — nothing was held on the daemon
            // between the question and this answer.
            self->m_ipc->sendBatchRequest(
                *alreadyHave,
                [pw, category, priority, paused](const QString& url) {
                    Ipc::IpcMessage msg(Ipc::IpcMsgType::AddNzbUrl);
                    msg.append(url);
                    msg.append(false);   // automatic
                    msg.append(true);    // force
                    msg.append(pw);
                    msg.append(qint64(category));
                    msg.append(qint64(priority));
                    msg.append(paused);
                    return msg;
                },
                self, finish, onEach);
        },
        onEach);
}

} // namespace eMule
