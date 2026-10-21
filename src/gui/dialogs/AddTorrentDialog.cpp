#include "pch.h"
/// @file AddTorrentDialog.cpp
/// @brief Add BitTorrent magnet links or .torrent files — implementation.

#include "dialogs/AddTorrentDialog.h"

#include "app/IpcClient.h"
#include "utils/StatusBarNotifier.h"
#include "utils/TorrentAdd.h"
#include "utils/TorrentLinks.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>

#include <memory>

namespace eMule {

namespace {

PasteTextDialog::Chrome chromeFor(AddTorrentDialog::Mode mode)
{
    const bool magnets = mode == AddTorrentDialog::Mode::Magnets;
    PasteTextDialog::Chrome c;
    c.title = magnets ? AddTorrentDialog::tr("Add Magnet Links")
                      : AddTorrentDialog::tr("Add Torrent Files");
    c.iconPath = QStringLiteral(":/icons/Torrent.ico");
    c.label = magnets ? AddTorrentDialog::tr("Magnet links:")
                      : AddTorrentDialog::tr("Torrent files:");
    c.placeholder = magnets
        ? AddTorrentDialog::tr("Paste one or more magnet:?xt=urn:btih:… links here, one per line...")
        : QString();
    c.acceptText = AddTorrentDialog::tr("Download");
    c.defaultSize = QSize(500, 300);
    c.readOnlyText = !magnets;
    c.savePathLabel = AddTorrentDialog::tr("Save to:");
    c.pausedOption = true;
    return c;
}

} // namespace

AddTorrentDialog::AddTorrentDialog(IpcClient* ipc, Mode mode, const QStringList& items,
                                   QWidget* parent)
    : PasteTextDialog(chromeFor(mode), parent)
    , m_ipc(ipc)
    , m_mode(mode)
{
    // A remote core's folders are not this machine's: type, don't browse
    setSavePathBrowsable(m_ipc && m_ipc->isLocalConnection());

    if (m_mode == Mode::Files) {
        m_paths = items;
        QStringList names;
        for (const QString& p : items)
            names << QFileInfo(p).fileName();
        setLines(names);
    } else if (!items.isEmpty()) {
        setLines(items);
    }
}

void AddTorrentDialog::onAccepted()
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(this, tr("Not Connected"), tr("Not connected to the eMule core."));
        return;
    }

    QStringList keys;
    QHash<QString, QByteArray> fileData;
    QStringList unreadable;

    if (m_mode == Mode::Magnets) {
        QStringList notMagnets;
        for (const QString& line : lines()) {
            if (gui::isBitTorrentMagnet(line))
                keys << line;
            else
                notMagnets << line;
        }
        keys.removeDuplicates();
        if (!notMagnets.isEmpty()) {
            QMessageBox::warning(this, windowTitle(),
                                 tr("These lines are not BitTorrent magnet links:\n\n%1")
                                     .arg(notMagnets.join(QLatin1Char('\n'))));
            setLines(notMagnets + keys);
            return;
        }
    } else {
        for (const QString& path : std::as_const(m_paths)) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                unreadable << QFileInfo(path).fileName();
                continue;
            }
            fileData.insert(path, file.readAll());
            keys << path;
        }
        if (!unreadable.isEmpty()) {
            QMessageBox::warning(this, windowTitle(),
                                 tr("Cannot read:\n\n%1").arg(unreadable.join(QLatin1Char('\n'))));
        }
    }

    if (keys.isEmpty())
        return;
    if (keys.size() > kMaxItems) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Please add at most %1 torrents at a time.").arg(kMaxItems));
        return;
    }
    sendAll(keys, fileData);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void AddTorrentDialog::sendAll(const QStringList& keys, const QHash<QString, QByteArray>& fileData)
{
    const QString savePath = this->savePath();
    const bool paused = queuePaused();
    const bool files = m_mode == Mode::Files;

    auto added = std::make_shared<int>(0);
    auto problems = std::make_shared<QStringList>();
    const QPointer<AddTorrentDialog> self(this);

    auto titleOf = [files](const QString& key) {
        return files ? QFileInfo(key).fileName() : gui::magnetDisplayName(key);
    };

    setBusy(true);
    m_ipc->sendBatchRequest(
        keys,
        [files, fileData, savePath, paused](const QString& key) {
            return files ? gui::torrentFileRequest(fileData.value(key), savePath, paused)
                         : gui::magnetRequest(key, savePath, paused);
        },
        this,
        [self, added, problems] {
            // Off the reply's stack before anything modal opens (NzbAdd.h)
            QTimer::singleShot(0, qApp, [self, added, problems] {
                if (!self)
                    return;
                self->setBusy(false);
                if (*added > 0)
                    StatusBarNotifier::post(tr("Added %n torrent(s).", "", *added), 5000);
                if (!problems->isEmpty()) {
                    QMessageBox::warning(self, self->windowTitle(),
                                         problems->join(QStringLiteral("\n\n")));
                }
                self->accept();
            });
        },
        [added, problems, titleOf](const QString& key, const Ipc::IpcMessage& reply) {
            const gui::BtAddReply r = gui::decodeTorrentAddReply(reply);
            if (r.outcome == gui::BtAddOutcome::Added) {
                ++(*added);
                return;
            }
            problems->append(r.dropped ? tr("Could not reach the eMule core.")
                                       : gui::btAddOutcomeText(r.outcome, titleOf(key), r.detail));
        });
}

} // namespace eMule
