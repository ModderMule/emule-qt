#include "IpcStreamBridge.h"

#include "DaemonApp.h"
#include "Ed3kDownloadsBridge.h"
#include "IpcServer.h"

#include "app/AppContext.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "ipc/IpcTls.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"
#include "utils/OtherFunctions.h"
#include "webserver/WebServer.h"

#include <QCborMap>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <array>
#include <memory>

namespace eMule::IpcStreamBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

constexpr int kWaitPollMs = 250;
constexpr qint64 kWaitMaxMs = 15000;

/// One file to read from, looked up anew for each request: a download may finish or go
struct Source {
    bool found = false;
    QString name;
    QString path;
    qint64 size = 0;
    qint64 availableEnd = 0;   ///< readable up to here (seeking on and unfinished)
    bool complete = false;
    bool seekable = false;     ///< bytes are waited for and fetched first
};

Source fromFile(const QString& path, const QString& name, bool complete)
{
    Source s;
    const QFileInfo fi(path);
    if (path.isEmpty() || !fi.exists())
        return s;
    s.found = true;
    s.path = path;
    s.name = name;
    s.size = fi.size();
    s.availableEnd = s.size;
    s.complete = complete;
    return s;
}

/// ed3k download by its 64-hex root
Source ed3kSource(const QString& root, qint64 offset, bool seeking)
{
    if (seeking) {
        const UsenetStreamSource src = Ed3kDownloadsBridge::streamFrom(root, offset);
        if (src.found && !src.pieces.isEmpty()) {
            Source s;
            s.found = true;
            s.name = src.fileName;
            s.path = src.pieces.first().path;
            s.size = src.totalSize;
            s.availableEnd = src.complete ? src.totalSize : src.availableEnd;
            s.complete = src.complete;
            s.seekable = !src.complete;
            return s;
        }
    }
    QString path;
    QString name;
    if (!Ed3kDownloadsBridge::fileOf(root, path, name))
        return {};
    return fromFile(path, name, false);
}

/// eD2K download or any shared file by its 32-hex hash (WebServer::handlePreviewStream)
Source ed2kSource(const QString& hex, qint64 offset, bool seeking)
{
    std::array<uint8, 16> hash{};
    if (decodeBase16(hex, hash.data(), hash.size()) != hash.size())
        return {};
    if (theApp.downloadQueue) {
        if (PartFile* pf = theApp.downloadQueue->fileByID(hash.data())) {
            const bool complete = pf->status() == PartFileStatus::Complete;
            if (complete && !pf->filePath().isEmpty())
                return fromFile(pf->filePath(), pf->fileName(), true);
            if (seeking) {
                Source s;
                s.found = true;
                s.name = pf->fileName();
                s.path = pf->dataFilePath();
                s.size = static_cast<qint64>(static_cast<uint64>(pf->fileSize()));
                s.availableEnd = static_cast<qint64>(pf->playFrom(static_cast<uint64>(std::max<qint64>(offset, 0))));
                s.seekable = true;
                return s;
            }
            return fromFile(pf->dataFilePath(), pf->fileName(), false);
        }
    }
    if (theApp.sharedFileList) {
        if (const KnownFile* kf = theApp.sharedFileList->getFileByID(hash.data()))
            return fromFile(kf->filePath(), kf->fileName(), true);
    }
    return {};
}

Source resolve(const QString& id, qint64 offset)
{
    const bool seeking = thePrefs.downloadSeeking();
    if (id.size() == 64)
        return ed3kSource(id, offset, seeking);
    if (id.size() == 32)
        return ed2kSource(id, offset, seeking);
    return {};
}

IpcMessage failure(int seqId, const QString& error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

void handleStat(const IpcMessage& msg, const Reply& reply)
{
    const Source s = resolve(msg.fieldString(0), 0);
    if (!s.found) {
        reply(failure(msg.seqId(), QStringLiteral("File not found")));
        return;
    }
    reply(IpcMessage::makeResult(msg.seqId(), true, QCborMap{
        {QStringLiteral("name"), s.name},
        {QStringLiteral("size"), s.size},
        {QStringLiteral("complete"), s.complete},
        {QStringLiteral("seekable"), s.seekable},
    }));
}

/// Reads [offset, offset+len) in the pool, answers on @p owner's thread
void readAndReply(const Source& s, qint64 offset, qint64 len, int seqId, QObject* owner, const Reply& reply)
{
    QThreadPool::globalInstance()->start([s, offset, len, seqId, guard = QPointer(owner), reply] {
        QByteArray data;
        QString error;
        QFile f(s.path);
        if (!f.open(QIODevice::ReadOnly))
            error = f.errorString();
        else if (!f.seek(offset))
            error = QStringLiteral("Seek failed");
        else
            data = f.read(len);
        QMetaObject::invokeMethod(guard.data(), [s, seqId, guard, reply, data = std::move(data), error] {
            if (!guard)
                return;
            if (!error.isEmpty()) {
                reply(failure(seqId, error));
                return;
            }
            reply(IpcMessage::makeResult(seqId, true, QCborMap{
                {QStringLiteral("data"), data},
                {QStringLiteral("size"), s.size},
                {QStringLiteral("availableEnd"), s.availableEnd},
                {QStringLiteral("complete"), s.complete},
            }));
        }, Qt::QueuedConnection);
    });
}

/// State of one ReadStream that waits for its bytes
struct Wait {
    QString id;
    qint64 offset = 0;
    qint64 len = 0;
    int seqId = 0;
    QElapsedTimer clock;
};

void tryRead(const std::shared_ptr<Wait>& w, QObject* owner, const Reply& reply)
{
    const Source s = resolve(w->id, w->offset);
    if (!s.found) {
        reply(failure(w->seqId, QStringLiteral("File not found")));
        return;
    }
    if (w->offset >= s.size && s.size > 0) {
        // past the end: empty, not an error (a player probing the tail)
        reply(IpcMessage::makeResult(w->seqId, true, QCborMap{
            {QStringLiteral("data"), QByteArray()},
            {QStringLiteral("size"), s.size},
            {QStringLiteral("availableEnd"), s.availableEnd},
            {QStringLiteral("complete"), s.complete},
        }));
        return;
    }
    const qint64 readable = s.availableEnd - w->offset;
    if (readable > 0) {
        readAndReply(s, w->offset, std::min(w->len, readable), w->seqId, owner, reply);
        return;
    }
    if (!s.seekable || w->clock.elapsed() >= kWaitMaxMs) {
        reply(IpcMessage::makeResult(w->seqId, true, QCborMap{
            {QStringLiteral("data"), QByteArray()},
            {QStringLiteral("size"), s.size},
            {QStringLiteral("availableEnd"), s.availableEnd},
            {QStringLiteral("complete"), s.complete},
            {QStringLiteral("waiting"), true},
        }));
        return;
    }
    // bytes asked for first (playFrom); look again soon
    QTimer::singleShot(kWaitPollMs, owner, [w, owner, reply] { tryRead(w, owner, reply); });
}

void handleRead(const IpcMessage& msg, QObject* owner, const Reply& reply)
{
    auto w = std::make_shared<Wait>();
    w->id = msg.fieldString(0);
    w->offset = std::max<qint64>(msg.fieldInt(1), 0);
    w->len = std::clamp<qint64>(msg.fieldInt(2), 1, kMaxChunk);
    w->seqId = msg.seqId();
    w->clock.start();
    tryRead(w, owner, reply);
}

void handleIdentity(const IpcMessage& msg, const Reply& reply)
{
    QString fp;
    if (auto* da = DaemonApp::instance(); da && da->ipcServer()) {
        const QSslCertificate cert = da->ipcServer()->tlsCertificate();
        if (!cert.isNull())
            fp = Ipc::certFingerprint(cert);
    }
    reply(IpcMessage::makeResult(msg.seqId(), true, QCborMap{
        {QStringLiteral("fingerprint"), fp},
        {QStringLiteral("port"), static_cast<qint64>(thePrefs.ipcPort())},
    }));
}

} // namespace

bool handleRequest(const IpcMessage& msg, QObject* owner, const Reply& reply)
{
    switch (msg.type()) {
    case IpcMsgType::StatStream:     handleStat(msg, reply); return true;
    case IpcMsgType::ReadStream:     handleRead(msg, owner, reply); return true;
    case IpcMsgType::GetIpcIdentity: handleIdentity(msg, reply); return true;
    default:                         return false;
    }
}

} // namespace eMule::IpcStreamBridge
