#include "pch.h"
/// @file MetaResultActions.cpp
/// @brief Actions on eNode torrent/Usenet/web/container search rows, including the login detour.

#include "utils/Clipboard.h"
#include "utils/MetaResultActions.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/SearchResultsModel.h"
#include "dialogs/MetaAccountDialog.h"
#include "dialogs/WebLinksDialog.h"
#include "enodemeta/LinkContainer.h"
#include "utils/Ed2kLinkImporter.h"
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

QString safeName(QString name, const MetaResultActions::Row& row)
{
    static const QString bad = QStringLiteral("\\/:*?\"<>|");
    for (QChar& c : name)
        if (bad.contains(c) || c.unicode() < 0x20)
            c = u'_';
    name = name.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("release");
    if (row.isNzb())
        return name + QStringLiteral(".nzb");
    // a container keeps the extension its page gave; the fetch may correct it
    if (row.isContainer())
        return enodemeta::containerFormat(name).isEmpty() ? name + QStringLiteral(".dlc") : name;
    return name + QStringLiteral(".torrent");
}

QString fileFilter(const MetaResultActions::Row& row)
{
    if (row.isNzb())
        return MetaResultActions::tr("NZB files (*.nzb)");
    if (row.isContainer())
        return MetaResultActions::tr("Link containers (*.dlc *.ccf *.rsdf)");
    return MetaResultActions::tr("Torrent files (*.torrent)");
}

QString saveTitle(const MetaResultActions::Row& row)
{
    if (row.isNzb())
        return MetaResultActions::tr("Save NZB File");
    if (row.isContainer())
        return MetaResultActions::tr("Save Link Container");
    return MetaResultActions::tr("Save Torrent File");
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
        if (row.isNzb())
            sendDownload(searchID, row, category, /*force*/ false, model);
}

void MetaResultActions::saveMetaFiles(quint32 searchID, const QList<Row>& rows)
{
    if (rows.isEmpty())
        return;
    // asked up front: a dialog after the fetch would open from an IPC reply
    if (rows.size() == 1) {
        const Row& row = rows.first();
        const QString path = QFileDialog::getSaveFileName(
            m_parent, saveTitle(row), QDir(startDir()).filePath(safeName(row.name, row)), fileFilter(row));
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

void MetaResultActions::showWebLinks(quint32 searchID, const QList<Row>& rows)
{
    for (const Row& row : rows)
        fetchAndShowLinks(searchID, row);
}

void MetaResultActions::downloadWebLinks(quint32 searchID, const QList<Row>& rows)
{
    if (!Ed2kLinkImporter::hosterReady()) {
        showWebLinks(searchID, rows);
        return;
    }
    for (const Row& row : rows)
        fetchAndDownloadLinks(searchID, row);
}

void MetaResultActions::copyMagnets(const QList<Row>& rows)
{
    QStringList links;
    for (const Row& row : rows)
        if (!row.magnet.isEmpty())
            links << row.magnet;
    if (links.isEmpty())
        return;
    eMule::Clipboard::setText(links.join(u'\n'));
    StatusBarNotifier::post(tr("Copied %n magnet link(s) to the clipboard.", nullptr, int(links.size())));
}

void MetaResultActions::handOverMagnets(const QList<Row>& rows)
{
    QStringList links;
    for (const Row& row : rows)
        if (!row.magnet.isEmpty())
            links << row.magnet;
    if (!Ed2kLinkImporter::torrentsReady() || !Ed2kLinkImporter::sendTorrentLinks(links, m_parent))
        copyMagnets(rows);
}

void MetaResultActions::addTorrents(quint32 searchID, const QList<Row>& rows)
{
    if (!Ed2kLinkImporter::torrentsReady()) {
        saveMetaFiles(searchID, rows);
        return;
    }
    for (const Row& row : rows)
        fetchAndAddTorrent(searchID, row);
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
    msg.append(row.ref);

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

void MetaResultActions::fetchMeta(quint32 searchID, const Row& row, const QString& failText,
                                  std::function<void(const QCborMap& data)> onData,
                                  std::function<bool(const QString& reason)> onFail, bool inspect)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage msg(IpcMsgType::FetchMetaFile);
    msg.append(static_cast<qint64>(searchID));
    msg.append(row.hash);
    msg.append(row.ref);
    msg.append(inspect);

    QPointer<MetaResultActions> self(this);
    m_ipc->sendRequest(std::move(msg), [self, searchID, row, failText, onData, onFail, inspect](const IpcMessage& resp) {
        if (!self || !resp.isValid())
            return;
        if (!resp.fieldBool(0)) {
            const auto retry = [self, searchID, row, failText, onData, onFail, inspect] {
                if (self)
                    self->fetchMeta(searchID, row, failText, onData, onFail, inspect);
            };
            const QString reason = resp.fieldString(1);
            if (!self->handleAuth(resp.fieldMap(2), retry) && !(onFail && onFail(reason)))
                self->warn(failText.arg(row.name, reason));
            return;
        }
        onData(resp.fieldMap(1));
    });
}

void MetaResultActions::fetchAndSave(quint32 searchID, const Row& row, const QString& path, bool pathIsDir)
{
    QPointer<MetaResultActions> self(this);
    fetchMeta(searchID, row, tr("Could not download \"%1\": %2"), [self, row, path, pathIsDir](const QCborMap& data) {
        if (!self)
            return;
        const QByteArray content = data.value(QStringLiteral("content")).toByteArray();
        QString target = path;
        const QString served = data.value(QStringLiteral("fileName")).toString();
        if (pathIsDir) {
            QString name = served;
            if (name.isEmpty() || name.contains(u'/') || name.contains(u'\\'))
                name = safeName(row.name, row);
            target = QDir(path).filePath(name);
        } else if (row.isContainer()) {
            // the format is only known now: a .ccf saved as .dlc would not open
            const QString want = enodemeta::containerFormat(served);
            const QString have = enodemeta::containerFormat(target);
            if (!want.isEmpty() && !have.isEmpty() && want != have)
                target = target.chopped(have.size()) + want;
        }
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
            self->warn(tr("Could not write %1: %2").arg(QDir::toNativeSeparators(target), file.errorString()));
            return;
        }
        StatusBarNotifier::post(tr("Saved %1").arg(QDir::toNativeSeparators(target)));
    });
}

void MetaResultActions::fetchAndShowLinks(quint32 searchID, const Row& row)
{
    QPointer<MetaResultActions> self(this);
    fetchMeta(searchID, row, tr("Could not fetch the links of \"%1\": %2"), [self, row](const QCborMap& data) {
        if (!self)
            return;
        const QCborMap links = data.value(QStringLiteral("links")).toMap();
        if (links.value(QStringLiteral("links")).toArray().isEmpty()) {
            self->warn(tr("\"%1\" holds no links.").arg(row.name));
            return;
        }
        const QByteArray content = data.value(QStringLiteral("content")).toByteArray();
        const QString fileName = data.value(QStringLiteral("fileName")).toString();
        // one event-loop turn: never a dialog inside the reply's own stack
        QTimer::singleShot(0, self, [self, row, links, content, fileName] {
            if (!self)
                return;
            auto* dlg = new WebLinksDialog(row.name, links, content, fileName, self->m_ipc, self->m_parent);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        });
    });
}

void MetaResultActions::fetchAndDownloadLinks(quint32 searchID, const Row& row)
{
    QPointer<MetaResultActions> self(this);
    fetchMeta(searchID, row, tr("Could not fetch the links of \"%1\": %2"), [self, row](const QCborMap& data) {
        if (!self)
            return;
        // mirrors of one file share its number: one download a file, the core tries them in order
        const QList<QStringList> files = WebLinksDialog::mirrorsByFile(
            data.value(QStringLiteral("links")).toMap().value(QStringLiteral("links")).toArray());
        if (files.isEmpty()) {
            self->warn(tr("\"%1\" holds no links.").arg(row.name));
            return;
        }
        // one event-loop turn: switching tabs and any warning stay out of the reply's stack
        QTimer::singleShot(0, self, [self, row, files] {
            if (!self)
                return;
            // the row's name is the package: its files under one root, in one folder
            if (Ed2kLinkImporter::sendHosterLinks(files, self->m_parent, row.name))
                StatusBarNotifier::post(tr("Added %n file(s) of \"%1\" to the Hoster tab.", nullptr,
                                           int(files.size())).arg(row.name));
            else
                self->warn(tr("Could not add the links of \"%1\": downloads from hosters are off.")
                               .arg(row.name));
        });
    });
}

void MetaResultActions::fetchAndAddTorrent(quint32 searchID, const Row& row)
{
    QPointer<MetaResultActions> self(this);
    // the .torrent is gone or refused: the row's magnet still names it
    const auto viaMagnet = [self, row](const QString&) {
        return self && !row.magnet.isEmpty()
            && Ed2kLinkImporter::sendTorrentLinks({row.magnet}, self->m_parent);
    };
    fetchMeta(searchID, row, tr("Could not download \"%1\": %2"), [self, row](const QCborMap& data) {
        if (!self)
            return;
        const QByteArray content = data.value(QStringLiteral("content")).toByteArray();
        QString name = data.value(QStringLiteral("fileName")).toString();
        if (name.isEmpty() || name.contains(u'/') || name.contains(u'\\'))
            name = safeName(row.name, row);
        if (!Ed2kLinkImporter::sendTorrentFile(name, content, self->m_parent))
            self->warn(tr("Could not add \"%1\": the torrent is empty.").arg(row.name));
    }, viaMagnet);
}

void MetaResultActions::showDetails(quint32 searchID, const Row& row,
                                    const MetaResultDetailDialog::RowInfo& info,
                                    std::function<void()> onDownload)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    QPointer<MetaResultDetailDialog> dlg = openDetails(info, std::move(onDownload));
    fetchMeta(searchID, row, {}, [dlg, row](const QCborMap& data) {
        if (!dlg)
            return;
        QCborMap details = data;
        details.insert(QStringLiteral("kind"), row.isNzb() ? QStringLiteral("nzb") : QStringLiteral("torrent"));
        dlg->setDetails(details);
    }, [dlg](const QString& reason) {
        if (dlg)
            dlg->setError(reason);
        return true;
    }, true);
}

void MetaResultActions::showIndexerDetails(quint32 searchID, const QString& resultId,
                                           const MetaResultDetailDialog::RowInfo& info,
                                           std::function<void()> onDownload)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    QPointer<MetaResultDetailDialog> dlg = openDetails(info, std::move(onDownload));
    IpcMessage msg(IpcMsgType::InspectIndexerResult);
    msg.append(static_cast<qint64>(searchID));
    msg.append(resultId);
    m_ipc->sendRequest(std::move(msg), [dlg](const IpcMessage& resp) {
        if (!dlg || !resp.isValid())
            return;
        if (!resp.fieldBool(0)) {
            dlg->setError(resp.fieldString(1));
            return;
        }
        dlg->setDetails(resp.fieldMap(1));
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

MetaResultDetailDialog* MetaResultActions::openDetails(const MetaResultDetailDialog::RowInfo& info,
                                                       std::function<void()> onDownload)
{
    auto* dlg = new MetaResultDetailDialog(info, m_parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    if (onDownload)
        connect(dlg, &MetaResultDetailDialog::downloadRequested, this, std::move(onDownload));
    dlg->show();
    return dlg;
}

} // namespace eMule
