/// @file DaemonMetaGrabWebBackend.cpp
/// @brief Grabs the web page's meta rows — implementation.

#include "DaemonMetaGrabWebBackend.h"

#include "BtBridge.h"
#include "Ed3kHosterBridge.h"
#include "IpcMessage.h"
#include "MetaRowFiles.h"
#include "MetaSearchService.h"
#include "UsenetBridge.h"
#include "nzb/NzbFile.h"
#include "queue/UsenetHealth.h"

#include "app/AppContext.h"
#include "enodemeta/WebLinks.h"
#include "prefs/Preferences.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"

#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>
#include <QMap>

#include <algorithm>
#include <vector>

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

/// NZB files the picked rows keep; empty = all (a whole-set row, the pref off, no match).
QList<int> keptNzbFiles(const QByteArray& content, const std::vector<MetaRow>& picked)
{
    QList<int> kept;
    if (picked.empty() || !thePrefs.usenetMetaSelectedFileOnly())
        return kept;
    usenet::NzbInfo nzb;
    QString parseError;
    if (!usenet::NzbFile::parse(content, nzb, parseError))
        return kept;
    for (const MetaRow& r : picked)
        if (const auto f = resolveNzbFile(nzb, r); f && !kept.contains(*f))
            kept.append(*f);
    return kept;
}

/// Torrent files the picked rows download; empty = all, as the GUI's grab: any row
/// without its own file means the whole torrent.
QCborArray wantedTorrentFiles(const QByteArray& content, const std::vector<MetaRow>& picked)
{
    if (picked.empty() || !thePrefs.btMetaSelectedFileOnly())
        return {};
    QString error;
    const QCborArray files = BtBridge::inspectTorrent(content, error).value(QStringLiteral("files")).toArray();
    QCborArray wanted;
    for (const MetaRow& r : picked) {
        const auto f = resolveTorrentFile(files, r);
        if (!f)
            return {};
        if (!wanted.contains(*f))
            wanted.append(*f);
    }
    return wanted;
}

void addFetched(const MetaRow& row, const std::vector<MetaRow>& picked, const QString& name,
                const enodemeta::pb::MetaFile& file, const std::function<void(bool, const QString&)>& done)
{
    using enodemeta::Kind;
    const QByteArray& content = file.content();
    switch (row.kind) {
    case Kind::Nzb: {
        usenet::UsenetAddOptions options;
        options.keptFiles = keptNzbFiles(content, picked);
        const UsenetBridge::AddResult r =
            UsenetBridge::addNzbData(content, name, options, usenet::UsenetAddOrigin::IndexerGrab);
        done(!r.itemId.isEmpty(), r.error);
        return;
    }
    case Kind::BtV1:
    case Kind::BtV2: {
        // fields 3, 4 null: I2P and stop-after-metadata as the prefs say
        const auto [ok, data] = askBridge(BtBridge::handleRequest, IpcMsgType::AddTorrentFile,
                                          {content, QString(), false, QCborValue(), QCborValue(),
                                           wantedTorrentFiles(content, picked)});
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
    case Kind::Container:
        Ed3kHosterBridge::openContainerAndAdd(content, suggestedFileName(row, file.contentType()),
                                              [done](int added, const QString& error) { done(added > 0, error); });
        return;
    default:
        done(false, QStringLiteral("unsupported result kind"));
        return;
    }
}

} // namespace

void DaemonMetaGrabWebBackend::grab(uint32 searchID, const QList<std::array<uint8, 16>>& rows,
                                    std::function<void(bool ok, const QString& message)> done)
{
    // the first row is fetched; all of them say which files to keep
    std::vector<MetaRow> picked;
    for (const auto& hash : rows) {
        const SearchFile* f = theApp.searchList ? theApp.searchList->searchFileByHash(hash.data(), searchID)
                                                : nullptr;
        if (!f || !f->isMetaResult())
            continue;
        picked.push_back(metaRowOf(*f, QByteArray(reinterpret_cast<const char*>(hash.data()), 16)));
    }
    if (picked.empty()) {
        done(false, handlerText("The search result is gone."));
        return;
    }
    const MetaRow row = picked.front();
    if (!row.target) {
        done(false, handlerText("The server of this result offers no download service."));
        return;
    }
    // a whole-set row among them: everything
    if (std::ranges::any_of(picked, &MetaRow::isWholeSet))
        picked.clear();
    const QString name = row.isNzb() ? releaseName(row, searchID) : row.name;
    MetaSearchService::instance().fetch(*row.target, row.hash16, row.catalogId, 0,
              [row, picked, name, done = std::move(done)](Ipc::MetaStatus status, const QString& error,
                                                          const QCborMap& /*meta*/,
                                                          const enodemeta::pb::MetaFile& file) {
        if (status != Ipc::MetaStatus::Ok) {
            done(false, error);
            return;
        }
        addFetched(row, picked, name, file, done);
    });
}

} // namespace eMule
