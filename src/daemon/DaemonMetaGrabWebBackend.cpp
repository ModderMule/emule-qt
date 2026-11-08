/// @file DaemonMetaGrabWebBackend.cpp
/// @brief Grabs the web page's meta rows — implementation.

#include "DaemonMetaGrabWebBackend.h"

#include "BtBridge.h"
#include "Ed3kHosterBridge.h"
#include "IpcMessage.h"
#include "MetaSearchService.h"
#include "UsenetBridge.h"
#include "queue/UsenetHealth.h"

#include "app/AppContext.h"
#include "enodemeta/WebLinks.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"

#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>
#include <QMap>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

/// The handler's own sentences, already translated for the GUI.
QString handlerText(const char* text)
{
    return QCoreApplication::translate("eMule::IpcClientHandler", text);
}

/// One request answered in place by @p handler: [ok, data] of its Result.
template <typename Handler>
std::pair<bool, QCborValue> askBridge(Handler handler, IpcMsgType type, const QCborArray& fields)
{
    IpcMessage request(type, 1);
    for (const QCborValue& f : fields)
        request.append(f);
    IpcMessage answer;
    handler(request, [&answer](const IpcMessage& m) { answer = m; });
    const bool ok = answer.type() == IpcMsgType::Result && answer.fieldBool(0);
    return {ok, answer.fieldCount() > 1 ? answer.field(1) : QCborValue()};
}

/// An NZB file row's queue name: its whole-set row's, while the search is live.
QString releaseName(const SearchFile& row, const std::array<uint8, 16>& hash, uint32 searchID)
{
    if (row.meta().fileIndex == enodemeta::kFileIndexWholeSet32 || !theApp.searchList)
        return row.fileName();
    std::array<uint8, 16> whole = hash;
    whole[4] = 0xFF;
    whole[5] = 0xFF;
    const SearchFile* f = theApp.searchList->searchFileByHash(whole.data(), searchID);
    return f && f->isMetaResult() && !f->fileName().isEmpty() ? f->fileName() : row.fileName();
}

void addFetched(enodemeta::Kind kind, const QString& name, const QByteArray& content,
                const std::function<void(bool, const QString&)>& done)
{
    using enodemeta::Kind;
    switch (kind) {
    case Kind::Nzb: {
        // ToDo: usenetMetaSelectedFileOnly — a file row grabs the whole release here
        const UsenetBridge::AddResult r =
            UsenetBridge::addNzbData(content, name, {}, usenet::UsenetAddOrigin::IndexerGrab);
        done(!r.itemId.isEmpty(), r.error);
        return;
    }
    case Kind::BtV1:
    case Kind::BtV2: {
        // ToDo: btMetaSelectedFileOnly — a file row adds the whole torrent here
        const auto [ok, data] = askBridge(BtBridge::handleRequest, IpcMsgType::AddTorrentFile,
                                          {content, QString(), false});
        const QCborMap m = data.toMap();
        // 0 Added, 1 Duplicate: either way it is in the list
        const qint64 outcome = m.value(QStringLiteral("outcome")).toInteger(4);
        done(ok && outcome <= 1, ok ? m.value(QStringLiteral("message")).toString()
                                    : QStringLiteral("BitTorrent engine unavailable"));
        return;
    }
    case Kind::Web: {
        const auto set = enodemeta::parseWebLinks(content);
        if (!set) {
            done(false, set.error());
            return;
        }
        // one item per file, its other links as mirrors (WebLinksDialog::mirrorsByFile)
        QMap<uint32, QCborArray> byFile;
        for (const enodemeta::WebLink& l : set->links)
            byFile[l.file].append(QString::fromUtf8(l.url));
        QCborArray files;
        for (const QCborArray& urls : std::as_const(byFile))
            files.append(urls);
        const QString package = set->title.isEmpty() ? name : set->title;
        const auto [ok, data] = askBridge(Ed3kHosterBridge::handleRequest, IpcMsgType::AddHosterLinks,
                                          {QString(), files, package});
        const bool added = ok && !data.toMap().value(QStringLiteral("ids")).toArray().isEmpty();
        done(added, ok ? QString() : data.toMap().value(QStringLiteral("error")).toString());
        return;
    }
    default:
        done(false, QStringLiteral("unsupported result kind"));
        return;
    }
}

} // namespace

void DaemonMetaGrabWebBackend::grab(uint32 searchID, const std::array<uint8, 16>& hash,
                                    std::function<void(bool ok, const QString& message)> done)
{
    const SearchFile* row = theApp.searchList ? theApp.searchList->searchFileByHash(hash.data(), searchID)
                                              : nullptr;
    if (!row || !row->isMetaResult()) {
        done(false, handlerText("The search result is gone."));
        return;
    }
    auto& svc = MetaSearchService::instance();
    const auto target = svc.targetForResult(*row);
    if (!target) {
        done(false, handlerText("The server of this result offers no download service."));
        return;
    }
    const enodemeta::Kind kind = row->meta().kind;
    if (kind == enodemeta::Kind::Container) {
        done(false, QStringLiteral("a container opens in the app only"));
        return;
    }
    const QString name = kind == enodemeta::Kind::Nzb ? releaseName(*row, hash, searchID) : row->fileName();
    const QByteArray hash16(reinterpret_cast<const char*>(hash.data()), 16);
    svc.fetch(*target, hash16, row->meta().catalogId, 0,
              [kind, name, done = std::move(done)](Ipc::MetaStatus status, const QString& error,
                                                   const QCborMap& /*meta*/,
                                                   const enodemeta::pb::MetaFile& file) {
        if (status != Ipc::MetaStatus::Ok) {
            done(false, error);
            return;
        }
        addFetched(kind, name, file.content(), done);
    });
}

} // namespace eMule
