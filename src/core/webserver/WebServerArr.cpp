#include "pch.h"
/// @file WebServerArr.cpp
/// @brief Sonarr/Radarr bridge: a qBittorrent Web API v2 subset (/api/v2) over the
///        eD2K download queue, and a Torznab indexer (/torznab/api) over eD2K/Kad
///        searches plus the synthetic feed (ArrSearchBridge).
///
/// Arr setup: download client "qBittorrent", host/port of this web server, any user
/// name, password = the REST API key. Indexer "Torznab", URL http://host:port/torznab,
/// API key = the same key. An eD2K file poses as a torrent whose info hash is the
/// MD4 plus eight zeros (ArrCompat.h).

#include "webserver/WebServer.h"

#include "app/AppContext.h"
#include "app/CoreOps.h"
#include "crypto/SecretCompare.h"
#include "files/PartFile.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"
#include "webserver/ApiBackend.h"
#include "webserver/ApiOpBuilder.h"
#include "webserver/ArrCompat.h"
#include "webserver/ArrSearchBridge.h"
#include "webserver/FormParts.h"
#include "webserver/WebSessionManager.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QRandomGenerator>
#include <QUrlQuery>

#include <memory>

namespace eMule {

using api::build::s;

namespace {

using L = QLatin1StringView;

/// What Arr is told it talks to: qBittorrent 4.6, Web API 2.9
constexpr auto kQBitVersion = "v4.6.7";
constexpr auto kQBitApiVersion = "2.9.3";
constexpr qint64 kQBitSessionIdleMs = 60LL * 60 * 1000;
constexpr int kQBitMaxSessions = 64;
/// qBittorrent's "infinite" ETA
constexpr qint64 kEtaInfinite = 8640000;

[[nodiscard]] QHttpServerResponse text(const QByteArray& body,
                                       QHttpServerResponse::StatusCode code = QHttpServerResponse::StatusCode::Ok)
{
    return QHttpServerResponse(QByteArrayLiteral("text/plain; charset=UTF-8"), body, code);
}

[[nodiscard]] QHttpServerResponse json(const QJsonDocument& doc)
{
    return QHttpServerResponse(QByteArrayLiteral("application/json"), doc.toJson(QJsonDocument::Compact));
}

[[nodiscard]] QHttpServerResponse xml(const QByteArray& body, const QByteArray& type = "application/xml")
{
    return QHttpServerResponse(type + "; charset=UTF-8", body);
}

[[nodiscard]] QHttpServerResponse forbidden()
{
    return text(QByteArrayLiteral("Forbidden"), QHttpServerResponse::StatusCode::Forbidden);
}

[[nodiscard]] QByteArray cookieValue(const QHttpServerRequest& req, QByteArrayView name)
{
    const QByteArray header = req.headers().combinedValue(QByteArrayLiteral("Cookie"));
    for (const QByteArray& part : header.split(';')) {
        const QByteArray kv = part.trimmed();
        if (kv.startsWith(name) && kv.size() > name.size() && kv.at(name.size()) == '=')
            return kv.mid(name.size() + 1);
    }
    return {};
}

/// The URL's query; '+' is a space (form encoding, Prowlarr), a real '+' comes as %2B.
[[nodiscard]] QUrlQuery urlArgs(const QHttpServerRequest& req)
{
    return QUrlQuery(req.url().query(QUrl::FullyEncoded).replace(u'+', QStringLiteral("%20")));
}

/// Query string for GET, form body (urlencoded or multipart text fields) for POST.
[[nodiscard]] QUrlQuery requestArgs(const QHttpServerRequest& req)
{
    QUrlQuery args = urlArgs(req);
    if (req.method() != QHttpServerRequest::Method::Post)
        return args;
    const QList<FormPart> parts =
        multipartParts(req.headers().combinedValue(QByteArrayLiteral("Content-Type")), req.body());
    if (parts.isEmpty()) {
        const QUrlQuery form = urlencodedForm(req.body());
        for (const auto& [k, v] : form.queryItems(QUrl::FullyEncoded))
            args.addQueryItem(k, v);
        return args;
    }
    for (const FormPart& part : parts) {
        if (part.fileName)   // a .torrent upload: not an eD2K file
            continue;
        args.addQueryItem(QString::fromLatin1(QUrl::toPercentEncoding(part.name)),
                          QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(part.data))));
    }
    return args;
}

[[nodiscard]] QString arg(const QUrlQuery& args, const char* key)
{
    return args.queryItemValue(QString::fromLatin1(key), QUrl::FullyDecoded);
}

[[nodiscard]] bool truthy(const QString& v)
{
    return v.compare(L("true"), Qt::CaseInsensitive) == 0 || v == L("1");
}

/// "hash1|hash2" (40-hex padded or 32-hex) -> MD4 hex list; "all" -> {"all"}
[[nodiscard]] QStringList md4List(const QString& hashes)
{
    QStringList out;
    for (const QString& h : hashes.split(u'|', Qt::SkipEmptyParts)) {
        const QString t = h.trimmed().toLower();
        if (t == L("all"))
            return {QStringLiteral("all")};
        if (const QString md4 = arr::md4FromBtih(t); !md4.isEmpty())
            out.append(md4);
        else if (t.size() == 32)
            out.append(t);
    }
    return out;
}

[[nodiscard]] QString md4Hex(const PartFile& file)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(file.fileHash()), 16).toHex());
}

[[nodiscard]] QString savePathOf(const PartFile& file)
{
    if (file.status() == PartFileStatus::Complete)
        return QDir::toNativeSeparators(QFileInfo(file.filePath()).absolutePath());
    return QDir::toNativeSeparators(thePrefs.incomingDirForCategory(int(file.category())));
}

[[nodiscard]] QString contentPathOf(const PartFile& file)
{
    if (file.status() == PartFileStatus::Complete)
        return QDir::toNativeSeparators(file.filePath());
    return QDir::toNativeSeparators(thePrefs.incomingDirForCategory(int(file.category())) + u'/' + file.fileName());
}

} // namespace

// ---------------------------------------------------------------------------
// Routes
// ---------------------------------------------------------------------------

void WebServer::registerArrRoutes()
{
    for (const auto method : {QHttpServerRequest::Method::Get, QHttpServerRequest::Method::Post}) {
        m_server->route(QStringLiteral("/api/v2/<arg>/<arg>"), method,
            [this](const QString& group, const QString& name, const QHttpServerRequest& req) {
                return handleQBit(group, name, req);
            });
    }
    m_server->route(QStringLiteral("/torznab/api"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) { return handleTorznab(req); });
}

// ---------------------------------------------------------------------------
// qBittorrent Web API v2
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handleQBit(const QString& group, const QString& name,
                                          const QHttpServerRequest& req)
{
    const QUrlQuery args = requestArgs(req);
    if (group == L("auth")) {
        if (name == L("login"))
            return qbitLogin(req, args);
        if (name == L("logout")) {
            m_qbitSessions.remove(cookieValue(req, "SID"));
            return text("Ok.");
        }
        return text({}, QHttpServerResponse::StatusCode::NotFound);
    }
    if (!qbitAuthorized(req))
        return forbidden();

    if (group == L("app")) {
        if (name == L("version"))
            return text(kQBitVersion);
        if (name == L("webapiVersion"))
            return text(kQBitApiVersion);
        if (name == L("buildInfo")) {
            return json(QJsonDocument(QJsonObject{{s("qt"), QString::fromLatin1(qVersion())},
                                                  {s("libtorrent"), s("eD2K")}, {s("bitness"), 64}}));
        }
        if (name == L("preferences")) {
            // no seeding limits: Arr decides by each row's ratio_limit
            return json(QJsonDocument(QJsonObject{
                {s("save_path"), QDir::toNativeSeparators(thePrefs.incomingDir())},
                {s("temp_path_enabled"), false},
                {s("max_ratio_enabled"), false},
                {s("max_ratio"), -1},
                {s("max_seeding_time_enabled"), false},
                {s("max_seeding_time"), -1},
                {s("max_inactive_seeding_time_enabled"), false},
                {s("max_inactive_seeding_time"), -1},
                {s("max_ratio_act"), 0},
                {s("queueing_enabled"), false},
                {s("dht"), true},
                {s("auto_tmm_enabled"), false},
            }));
        }
        if (name == L("setPreferences"))
            return text({});
        return text({}, QHttpServerResponse::StatusCode::NotFound);
    }
    if (group == L("torrents"))
        return qbitTorrents(name, args, req);
    if (group == L("transfer") && name == L("info")) {
        qint64 down = 0;
        if (m_downloadQueue) {
            for (const auto& f : m_downloadQueue->files())
                down += qint64(f->datarate());
        }
        return json(QJsonDocument(QJsonObject{{s("dl_info_speed"), down}, {s("up_info_speed"), 0},
                                              {s("connection_status"), s("connected")}}));
    }
    return text({}, QHttpServerResponse::StatusCode::NotFound);
}

bool WebServer::qbitAuthorized(const QHttpServerRequest& req)
{
    if (m_config.apiKey.isEmpty())
        return false;
    if (apiKeyMatches(req.headers()))
        return true;
    const QByteArray sid = cookieValue(req, "SID");
    const auto it = m_qbitSessions.find(sid);
    if (sid.isEmpty() || it == m_qbitSessions.end())
        return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - it.value() > kQBitSessionIdleMs) {
        m_qbitSessions.erase(it);
        return false;
    }
    it.value() = now;
    return true;
}

QHttpServerResponse WebServer::qbitLogin(const QHttpServerRequest& req, const QUrlQuery& form)
{
    const QString client = QStringLiteral("qbit:") + req.remoteAddress().toString();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_sessionManager) {
        if (m_sessionManager->loginWaitSeconds(client, now) > 0)
            return forbidden();   // qBittorrent bans with 403 too
    }
    const QByteArray password = arg(form, "password").toUtf8();
    if (m_config.apiKey.isEmpty() || password.isEmpty()
        || !constantTimeEquals(password, m_config.apiKey.toUtf8())) {
        if (m_sessionManager && !password.isEmpty())
            m_sessionManager->noteLoginFailure(client, now);
        logWarning(QStringLiteral("WebServer: qBittorrent API login refused for %1").arg(req.remoteAddress().toString()));
        return text("Fails.");
    }
    if (m_sessionManager)
        m_sessionManager->noteLoginSuccess(client);

    // oldest out when full
    m_qbitSessions.removeIf([now](const auto& it) { return now - it.value() > kQBitSessionIdleMs; });
    while (m_qbitSessions.size() >= kQBitMaxSessions)
        m_qbitSessions.erase(std::ranges::min_element(m_qbitSessions));
    QByteArray sid(32, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(sid.data()), sid.size() / 4);
    sid = sid.toHex();
    m_qbitSessions.insert(sid, now);

    QHttpServerResponse resp = text("Ok.");
    QHttpHeaders headers = resp.headers();
    headers.append(QHttpHeaders::WellKnownHeader::SetCookie,
                   QByteArrayLiteral("SID=") + sid + QByteArrayLiteral("; HttpOnly; SameSite=Strict; path=/"));
    resp.setHeaders(std::move(headers));
    return resp;
}

QHttpServerResponse WebServer::qbitTorrents(const QString& name, const QUrlQuery& args,
                                            const QHttpServerRequest& req)
{
    if (!m_downloadQueue)
        return text({}, QHttpServerResponse::StatusCode::ServiceUnavailable);

    if (name == L("info")) {
        const QString category = arg(args, "category");
        const bool anyCategory = !args.hasQueryItem(QStringLiteral("category"));
        const QString filter = arg(args, "filter");
        const QList<PartFile*> files = qbitFiles(args.hasQueryItem(QStringLiteral("hashes"))
                                                     ? arg(args, "hashes") : QStringLiteral("all"));
        QJsonArray rows;
        for (const PartFile* file : files) {
            const QJsonObject row = qbitTorrentJson(*file);
            if (!anyCategory && row.value(L("category")).toString() != category)
                continue;
            const bool done = file->status() == PartFileStatus::Complete;
            if ((filter == L("completed") && !done) || (filter == L("downloading") && done))
                continue;
            rows.append(row);
        }
        return json(QJsonDocument(rows));
    }
    if (name == L("categories")) {
        QJsonObject cats;
        for (int i = 1; i < thePrefs.categoryCount(); ++i) {
            const QString title = thePrefs.category(i).title;
            cats.insert(title, QJsonObject{{s("name"), title},
                                           {s("savePath"), QDir::toNativeSeparators(thePrefs.incomingDirForCategory(i))}});
        }
        return json(QJsonDocument(cats));
    }
    if (name == L("createCategory")) {
        const QString title = arg(args, "category").trimmed();
        if (title.isEmpty())
            return text("Invalid category name", QHttpServerResponse::StatusCode::BadRequest);
        if (qbitCategoryIndex(title, false) >= 0)
            return text("Category already exists", QHttpServerResponse::StatusCode::Conflict);
        return qbitCategoryIndex(title, true) >= 0
            ? text({}) : text("Fails.", QHttpServerResponse::StatusCode::Conflict);
    }
    if (name == L("add"))
        return qbitAdd(args, req);

    if (name == L("properties") || name == L("files")) {
        const QList<PartFile*> files = qbitFiles(arg(args, "hash"));
        if (files.isEmpty() || arg(args, "hash").compare(L("all"), Qt::CaseInsensitive) == 0)
            return text("Not Found", QHttpServerResponse::StatusCode::NotFound);
        const PartFile& f = *files.first();
        if (name == L("files")) {
            const double progress = f.status() == PartFileStatus::Complete ? 1.0 : f.percentCompleted() / 100.0;
            return json(QJsonDocument(QJsonArray{QJsonObject{
                {s("index"), 0}, {s("name"), f.fileName()}, {s("size"), qint64(f.fileSize())},
                {s("progress"), progress}, {s("priority"), 1},
                {s("is_seed"), f.status() == PartFileStatus::Complete}}}));
        }
        const QJsonObject row = qbitTorrentJson(f);
        QJsonObject props{
            {s("save_path"), row.value(L("save_path"))},
            {s("total_size"), row.value(L("size"))},
            {s("seeding_time"), 0},
            {s("share_ratio"), 0},
            {s("addition_date"), row.value(L("added_on"))},
            {s("completion_date"), row.value(L("completion_on"))},
            {s("dl_speed"), row.value(L("dlspeed"))},
            {s("eta"), row.value(L("eta"))},
            {s("pieces_have"), 0},
            {s("hash"), row.value(L("hash"))},
            {s("name"), f.fileName()},
        };
        return json(QJsonDocument(props));
    }

    const QStringList hashes = md4List(arg(args, "hashes"));
    if (name == L("delete")) {
        const bool deleteFiles = truthy(arg(args, "deleteFiles"));
        for (PartFile* file : qbitFiles(arg(args, "hashes"))) {
            const QString hash = md4Hex(*file);
            if (file->status() == PartFileStatus::Complete) {
                if (!deleteFiles) {
                    ops::clearCompletedDownloads({hash});
                } else if (const ops::Status st = ops::deleteSharedFile(hash); !st.ok()) {
                    logWarning(QStringLiteral("Arr: delete %1 failed: %2").arg(file->fileName(), st.message));
                    ops::clearCompletedDownloads({hash});
                }
            } else {
                // eD2K has no "remove, keep the part file": cancel deletes it
                const ops::Status st = ops::cancelDownload(hash);
                if (!st.ok())
                    logWarning(QStringLiteral("Arr: cancel %1 failed: %2").arg(file->fileName(), st.message));
            }
        }
        return text({});
    }
    if (name == L("pause") || name == L("stop") || name == L("resume") || name == L("start")) {
        const bool pause = name == L("pause") || name == L("stop");
        for (PartFile* file : qbitFiles(arg(args, "hashes"))) {
            if (file->status() == PartFileStatus::Complete)
                continue;
            const QString hash = md4Hex(*file);
            (void)(pause ? ops::pauseDownload(hash) : ops::resumeDownload(hash));
        }
        return text({});
    }
    if (name == L("setCategory")) {
        const QString title = arg(args, "category");
        const int index = title.isEmpty() ? 0 : qbitCategoryIndex(title, false);
        if (index < 0)
            return text("Category does not exist", QHttpServerResponse::StatusCode::Conflict);
        for (PartFile* file : qbitFiles(arg(args, "hashes")))
            (void)ops::setDownloadCategory(md4Hex(*file), uint32(index));
        return text({});
    }
    if (name == L("topPrio") || name == L("bottomPrio") || name == L("increasePrio")
        || name == L("decreasePrio")) {
        // no queue order in eD2K: the priority stands in for it
        const bool up = name == L("topPrio") || name == L("increasePrio");
        for (PartFile* file : qbitFiles(arg(args, "hashes"))) {
            if (file->status() != PartFileStatus::Complete)
                (void)ops::setDownloadPriority(md4Hex(*file), up ? 2 : 0, false);   // PR_HIGH / PR_LOW
        }
        return text({});
    }
    // accepted, nothing to do for eD2K
    if (name == L("setShareLimits") || name == L("setForceStart") || name == L("setSuperSeeding")
        || name == L("addTags") || name == L("removeTags") || name == L("setAutoManagement")
        || name == L("recheck") || name == L("reannounce") || name == L("setLocation")) {
        Q_UNUSED(hashes);
        return text({});
    }
    return text({}, QHttpServerResponse::StatusCode::NotFound);
}

QHttpServerResponse WebServer::qbitAdd(const QUrlQuery& args, const QHttpServerRequest& req)
{
    Q_UNUSED(req);
    const QString category = arg(args, "category").trimmed();
    int categoryIndex = 0;
    if (!category.isEmpty()) {
        // qBittorrent creates a missing category on add
        categoryIndex = qbitCategoryIndex(category, true);
        if (categoryIndex < 0)
            categoryIndex = 0;
    }
    const bool paused = truthy(arg(args, "paused")) || truthy(arg(args, "stopped"));

    int added = 0;
    int failed = 0;
    for (const QString& line : arg(args, "urls").split(u'\n', Qt::SkipEmptyParts)) {
        const auto ref = arr::parseArrLink(line);
        if (!ref) {
            ++failed;
            logWarning(QStringLiteral("Arr: not an eD2K link: %1").arg(line.left(120)));
            continue;
        }
        const ops::AddOutcome out = ops::addDownloadFromLink(arr::ed2kLinkFor(*ref), categoryIndex, paused);
        if (!out.status.ok()) {
            ++failed;
            logWarning(QStringLiteral("Arr: %1 not added: %2").arg(ref->name, out.status.message));
            continue;
        }
        ++added;
        logInfo(QStringLiteral("Arr: added %1%2").arg(ref->name, out.added ? QString()
                                                                           : QStringLiteral(" (already queued)")));
        // queued already under another category: Arr looks for it in its own
        if (!out.added && categoryIndex > 0)
            (void)ops::setDownloadCategory(ref->hashHex, uint32(categoryIndex));
    }
    if (added == 0 && failed > 0)
        return text("Fails.");
    if (added == 0)
        return text("Fails.", QHttpServerResponse::StatusCode::UnsupportedMediaType);
    return text("Ok.");
}

int WebServer::qbitCategoryIndex(const QString& title, bool create)
{
    for (int i = 1; i < thePrefs.categoryCount(); ++i) {
        if (thePrefs.category(i).title.compare(title, Qt::CaseInsensitive) == 0)
            return i;
    }
    if (!create || !m_apiBackend)
        return -1;
    QJsonArray list;
    const QJsonArray current = m_apiBackend->categories();
    for (qsizetype i = 0; i < current.size(); ++i) {
        QJsonObject entry = current.at(i).toObject();
        entry.insert(s("oldIndex"), i);
        entry.remove(L("resolvedIncoming"));
        list.append(entry);
    }
    list.append(QJsonObject{{s("title"), title}});
    if (const ops::Status st = m_apiBackend->setCategories(list); !st.ok()) {
        logWarning(QStringLiteral("Arr: category %1 not created: %2").arg(title, st.message));
        return -1;
    }
    logInfo(QStringLiteral("Arr: created download category %1").arg(title));
    return qbitCategoryIndex(title, false);
}

QJsonObject WebServer::qbitTorrentJson(const PartFile& file) const
{
    const bool complete = file.status() == PartFileStatus::Complete;
    arr::DownloadState state;
    state.complete = complete;
    state.completing = file.status() == PartFileStatus::Completing;
    state.hashing = file.status() == PartFileStatus::Hashing || file.status() == PartFileStatus::WaitingForHash;
    state.error = file.status() == PartFileStatus::Error || file.status() == PartFileStatus::Insufficient;
    state.paused = file.isPaused() || file.isStopped();
    state.datarate = qint64(file.datarate());
    state.sources = int(file.sourceCount());

    const qint64 size = qint64(file.fileSize());
    const qint64 done = complete ? size : qint64(file.completedSize());
    const double progress = size > 0 ? double(done) / double(size) : 0.0;
    const qint64 remaining = file.timeRemaining();
    const QString hash = md4Hex(file);
    const int cat = int(file.category());
    const QString catTitle = cat > 0 && cat < thePrefs.categoryCount() ? thePrefs.category(cat).title : QString();
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    return QJsonObject{
        {s("hash"), arr::btihForMd4(hash)},
        {s("infohash_v1"), arr::btihForMd4(hash)},
        {s("name"), file.fileName()},
        {s("size"), size},
        {s("total_size"), size},
        {s("progress"), complete ? 1.0 : progress},
        {s("downloaded"), done},
        {s("amount_left"), size - done},
        {s("completed"), done},
        {s("dlspeed"), state.datarate},
        {s("upspeed"), 0},
        {s("eta"), complete ? 0 : (state.datarate > 0 && remaining > 0 ? remaining : kEtaInfinite)},
        {s("state"), arr::qbitState(state)},
        {s("category"), catTitle},
        {s("tags"), QString()},
        {s("save_path"), savePathOf(file)},
        {s("content_path"), contentPathOf(file)},
        {s("download_path"), QString()},
        {s("ratio"), 0},
        // 0: reached at once, so Arr may remove a finished file after import
        {s("ratio_limit"), complete ? 0 : -2},
        {s("seeding_time"), 0},
        {s("seeding_time_limit"), -2},
        {s("inactive_seeding_time_limit"), -2},
        {s("num_seeds"), int(file.transferringSrcCount())},
        {s("num_complete"), int(file.sourceCount())},
        {s("num_leechs"), 0},
        {s("num_incomplete"), 0},
        {s("priority"), 0},
        {s("added_on"), now},
        {s("completion_on"), complete ? now : -1},
        {s("last_activity"), now},
        {s("auto_tmm"), false},
        {s("force_start"), false},
        {s("super_seeding"), false},
        {s("magnet_uri"), arr::magnetFor(hash, file.fileName(), quint64(size))},
    };
}

QList<PartFile*> WebServer::qbitFiles(const QString& hashes) const
{
    QList<PartFile*> out;
    if (!m_downloadQueue)
        return out;
    const QStringList wanted = md4List(hashes);
    const bool all = wanted == QStringList{QStringLiteral("all")};
    for (const auto& f : m_downloadQueue->files()) {
        PartFile* file = &*f;
        if (all || wanted.contains(md4Hex(*file)))
            out.append(file);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Torznab
// ---------------------------------------------------------------------------

QFuture<QHttpServerResponse> WebServer::handleTorznab(const QHttpServerRequest& req)
{
    const auto done = [](QHttpServerResponse&& r) {
        QPromise<QHttpServerResponse> promise;
        QFuture<QHttpServerResponse> future = promise.future();
        promise.start();
        promise.addResult(std::move(r));
        promise.finish();
        return future;
    };
    const QUrlQuery args = urlArgs(req);
    QByteArray key = arg(args, "apikey").toUtf8();
    if (key.isEmpty())
        key = requestApiKey(req.headers());
    if (m_config.apiKey.isEmpty() || key.isEmpty() || !constantTimeEquals(key, m_config.apiKey.toUtf8()))
        return done(xml(arr::torznabError(100, QStringLiteral("Incorrect user credentials"))));

    const QString t = arg(args, "t");
    if (t == L("caps"))
        return done(xml(arr::torznabCaps()));
    if (t != L("search") && t != L("tvsearch") && t != L("movie") && t != L("music") && t != L("book"))
        return done(xml(arr::torznabError(202, QStringLiteral("No such function"))));

    const QList<int> cats = arr::parseCategories(arg(args, "cat"));
    const int limit = std::clamp(arg(args, "limit").toInt() > 0 ? arg(args, "limit").toInt() : 100, 1, 100);
    const QString self = req.url().toString(QUrl::RemoveQuery);
    QString query = arg(args, "q").trimmed();
    if (t == L("tvsearch"))
        query = arr::tvQuery(query, arg(args, "season"), arg(args, "ep"));
    // one page only: eD2K has no paging
    if (arg(args, "offset").toInt() > 0)
        return done(xml(arr::torznabFeed({}, self), "application/rss+xml"));

    if (query.isEmpty() || (t == L("tvsearch") && arg(args, "q").trimmed().isEmpty())) {
        // the synthetic feed (Arr RSS sync)
        QList<arr::TorznabItem> items = m_arrBridge ? m_arrBridge->feed(cats, limit) : QList<arr::TorznabItem>{};
        if (items.isEmpty()) {
            // Sonarr's indexer test fails on an empty feed: one row it cannot parse
            arr::TorznabItem placeholder;
            placeholder.title = QStringLiteral("eMule Qt feed - no eD2K hits yet");
            placeholder.fileName = placeholder.title;
            placeholder.hashHex = QStringLiteral("31d6cfe0d16ae931b73c59d7e0c089c0");   // MD4 of nothing
            placeholder.size = 1;
            placeholder.category = cats.isEmpty() ? 8000 : cats.first();
            placeholder.published = QDateTime::currentDateTimeUtc();
            items.append(placeholder);
        }
        return done(xml(arr::torznabFeed(items, self), "application/rss+xml"));
    }

    if (!m_arrBridge)
        return done(xml(arr::torznabFeed({}, self), "application/rss+xml"));
    auto promise = std::make_shared<QPromise<QHttpServerResponse>>();
    promise->start();
    QFuture<QHttpServerResponse> future = promise->future();
    m_arrBridge->search({query, cats, limit}, m_config.arrSearchWaitSeconds,
                        [promise, self](const QList<arr::TorznabItem>& items) {
                            promise->addResult(xml(arr::torznabFeed(items, self), "application/rss+xml"));
                            promise->finish();
                        });
    return future;
}

} // namespace eMule
