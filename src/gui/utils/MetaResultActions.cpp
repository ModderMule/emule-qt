#include "pch.h"
/// @file MetaResultActions.cpp
/// @brief Actions on eNode torrent/Usenet search rows, including the login detour.

#include "utils/MetaResultActions.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/SearchResultsModel.h"
#include "dialogs/MetaAccountDialog.h"
#include "utils/NzbAdd.h"
#include "utils/StatusBarNotifier.h"

#include <QApplication>
#include <QCborMap>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>

namespace eMule {

using namespace Ipc;

namespace {

QString startDir()
{
    const QString dir = theUiState.metaFileSaveDir();
    if (!dir.isEmpty() && QDir(dir).exists())
        return dir;
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

QString safeName(QString name, bool nzb)
{
    static const QString bad = QStringLiteral("\\/:*?\"<>|");
    for (QChar& c : name)
        if (bad.contains(c) || c.unicode() < 0x20)
            c = u'_';
    name = name.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("release");
    return name + (nzb ? QStringLiteral(".nzb") : QStringLiteral(".torrent"));
}

} // namespace

MetaResultActions::MetaResultActions(IpcClient* ipc, QWidget* parentWidget)
    : QObject(parentWidget)
    , m_ipc(ipc)
    , m_parent(parentWidget)
{
}

void MetaResultActions::downloadUsenet(quint32 searchID, const QList<Row>& rows, int category,
                                       QPointer<SearchResultsModel> model)
{
    for (const Row& row : rows)
        if (row.nzb)
            sendDownload(searchID, row, category, /*force*/ false, model);
}

void MetaResultActions::saveMetaFiles(quint32 searchID, const QList<Row>& rows)
{
    if (rows.isEmpty())
        return;
    // asked up front: a dialog after the fetch would open from an IPC reply
    if (rows.size() == 1) {
        const Row& row = rows.first();
        const QString filter = row.nzb ? tr("NZB files (*.nzb)") : tr("Torrent files (*.torrent)");
        const QString path = QFileDialog::getSaveFileName(
            m_parent, row.nzb ? tr("Save NZB File") : tr("Save Torrent File"),
            QDir(startDir()).filePath(safeName(row.name, row.nzb)), filter);
        if (path.isEmpty())
            return;
        theUiState.setMetaFileSaveDir(QFileInfo(path).absolutePath());
        fetchAndSave(searchID, row, path, false);
        return;
    }

    const QString dir = QFileDialog::getExistingDirectory(m_parent, tr("Save Files To"), startDir());
    if (dir.isEmpty())
        return;
    theUiState.setMetaFileSaveDir(dir);
    for (const Row& row : rows)
        fetchAndSave(searchID, row, dir, true);
}

void MetaResultActions::manageAccount(const QString& serverAddr, const QString& serverName)
{
    if (auto dlg = m_dialogs.value(serverAddr)) {
        dlg->raise();
        dlg->activateWindow();
        return;
    }
    QCborMap meta{{QStringLiteral("serverAddr"), serverAddr}, {QStringLiteral("serverName"), serverName}};
    auto* dlg = new MetaAccountDialog(m_ipc, meta, MetaAccountDialog::Purpose::Manage, m_parent);
    dlg->open();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void MetaResultActions::sendDownload(quint32 searchID, const Row& row, int category, bool force,
                                     QPointer<SearchResultsModel> model)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage msg(IpcMsgType::DownloadMetaResult);
    msg.append(static_cast<qint64>(searchID));
    msg.append(row.hash);
    msg.append(force);
    msg.append(static_cast<qint64>(category));
    msg.append(qint64(0));   // priority: normal
    msg.append(false);       // paused

    QPointer<MetaResultActions> self(this);
    m_ipc->sendRequest(std::move(msg), [self, searchID, row, category, model](const IpcMessage& resp) {
        if (!self || !resp.isValid())
            return;
        if (resp.fieldBool(0)) {
            StatusBarNotifier::post(tr("Queued \"%1\" for Usenet download.").arg(row.name));
            if (model)
                model->updateKnownTypes({{row.hash, 2}});   // Downloading
            return;
        }

        const QCborMap meta = resp.fieldMap(3);
        const auto retry = [self, searchID, row, category, model] {
            if (self)
                self->sendDownload(searchID, row, category, false, model);
        };
        if (self->handleAuth(meta, retry))
            return;

        const auto outcome = static_cast<gui::NzbAddOutcome>(resp.fieldInt(2));
        const QString reason = resp.fieldString(1);
        // one event-loop turn: never a modal inside the reply's own stack
        QTimer::singleShot(0, self, [self, searchID, row, category, model, outcome, reason] {
            if (!self)
                return;
            if (outcome == gui::NzbAddOutcome::AlreadyDownloaded) {
                const bool again = QMessageBox::question(
                    self->m_parent, tr("Download"), tr("%1\n\nDownload it again?").arg(reason),
                    QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
                if (again)
                    self->sendDownload(searchID, row, category, true, model);
                return;
            }
            self->warn(tr("Could not queue \"%1\": %2").arg(row.name, reason));
        });
    });
}

void MetaResultActions::fetchAndSave(quint32 searchID, const Row& row, const QString& path, bool pathIsDir)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage msg(IpcMsgType::FetchMetaFile);
    msg.append(static_cast<qint64>(searchID));
    msg.append(row.hash);

    QPointer<MetaResultActions> self(this);
    m_ipc->sendRequest(std::move(msg), [self, searchID, row, path, pathIsDir](const IpcMessage& resp) {
        if (!self || !resp.isValid())
            return;
        if (!resp.fieldBool(0)) {
            const auto retry = [self, searchID, row, path, pathIsDir] {
                if (self)
                    self->fetchAndSave(searchID, row, path, pathIsDir);
            };
            if (!self->handleAuth(resp.fieldMap(2), retry))
                self->warn(tr("Could not download \"%1\": %2").arg(row.name, resp.fieldString(1)));
            return;
        }

        const QCborMap data = resp.fieldMap(1);
        const QByteArray content = data.value(QStringLiteral("content")).toByteArray();
        QString target = path;
        if (pathIsDir) {
            QString name = data.value(QStringLiteral("fileName")).toString();
            if (name.isEmpty() || name.contains(u'/') || name.contains(u'\\'))
                name = safeName(row.name, row.nzb);
            target = QDir(path).filePath(name);
        }
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
            self->warn(tr("Could not write %1: %2").arg(QDir::toNativeSeparators(target), file.errorString()));
            return;
        }
        StatusBarNotifier::post(tr("Saved %1").arg(QDir::toNativeSeparators(target)));
    });
}

bool MetaResultActions::handleAuth(const QCborMap& meta, std::function<void()> retry)
{
    const auto status = static_cast<MetaStatus>(meta.value(QStringLiteral("status")).toInteger());
    if (status != MetaStatus::AuthRequired && status != MetaStatus::AccountInactive)
        return false;
    const QString server = meta.value(QStringLiteral("serverAddr")).toString();
    if (server.isEmpty())
        return false;

    m_retries[server].append(std::move(retry));
    if (auto dlg = m_dialogs.value(server)) {
        dlg->updateMeta(meta);
        return true;
    }

    // opened one turn later — never from inside the IPC reply
    QPointer<MetaResultActions> self(this);
    QTimer::singleShot(0, this, [self, meta, server] {
        if (!self || self->m_dialogs.value(server))
            return;
        auto* dlg = new MetaAccountDialog(self->m_ipc, meta, MetaAccountDialog::Purpose::Download,
                                          self->m_parent);
        self->m_dialogs.insert(server, dlg);
        connect(dlg, &QDialog::finished, self, [self, server](int result) {
            if (!self)
                return;
            self->m_dialogs.remove(server);
            const auto retries = self->m_retries.take(server);
            if (result == QDialog::Accepted)
                for (const auto& r : retries)
                    r();
        });
        dlg->open();
    });
    return true;
}

void MetaResultActions::warn(const QString& text)
{
    QPointer<QWidget> parent = m_parent;
    QTimer::singleShot(0, qApp, [parent, text] {
        QMessageBox::warning(parent, tr("Search"), text);
    });
}

} // namespace eMule
