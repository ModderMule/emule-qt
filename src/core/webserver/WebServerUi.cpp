#include "pch.h"
/// @file WebServerUi.cpp
/// @brief Web UI pages MFC never had: ed3k, Messages, Scheduler and Add Link; the header's
///        speed menu and status poll (live speeds, notifications); the app manifest.

#include "webserver/WebServer.h"
#include "webserver/ApiBackend.h"
#include "webserver/WebSessionManager.h"
#include "webserver/WebTemplateEngine.h"

#include "app/AppContext.h"
#include "app/CoreOps.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/Collection.h"
#include "files/CollectionFile.h"
#include "files/PartFile.h"
#include "friends/Friend.h"
#include "friends/FriendList.h"
#include "net/Address.h"
#include "net/EMSocket.h"
#include "prefs/PreferenceSchema.h"
#include "prefs/Preferences.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "stats/Statistics.h"
#include "transfer/DownloadQueue.h"
#include "transfer/Scheduler.h"
#include "utils/ArgStr.h"
#include "utils/OtherFunctions.h"
#include "utils/StringUtils.h"
#include "utils/UsenetDisplay.h"

#include <QCborArray>
#include <QCborMap>
#include <QHttpServerRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace eMule {

namespace {

using L = QLatin1StringView;

[[nodiscard]] QString htmlText(const QString& s)
{
    return WebTemplateEngine::htmlEscape(s);
}

[[nodiscard]] QHttpServerResponse uiJsonError(int code, const QString& message)
{
    return QHttpServerResponse(QJsonObject{{QStringLiteral("error"),
                                            QJsonObject{{QStringLiteral("code"), code},
                                                        {QStringLiteral("message"), message}}}},
                               static_cast<QHttpServerResponse::StatusCode>(code));
}

/// "12.3" for a percentage bar.
[[nodiscard]] QString pctText(qint64 done, qint64 size)
{
    const double pct = size > 0 ? 100.0 * double(qBound<qint64>(0, done, size)) / double(size) : 0.0;
    return QString::number(pct, 'f', 1);
}

[[nodiscard]] QString rateText(qint64 bytesPerSec)
{
    return bytesPerSec > 0 ? formatByteRate(double(bytesPerSec)) : QString();
}

/// A one-shot notice, emptied as it is shown.
[[nodiscard]] QString takeNotice(QString& notice, bool failed)
{
    return notice.isEmpty() ? QString()
        : QStringLiteral("<p id=\"ui-notice\" class=\"ui-notice%1\">%2</p>")
              .arg(failed ? QStringLiteral(" failed") : QString(), htmlText(std::exchange(notice, {})));
}

/// Non-empty trimmed lines of the `links` field, plus a single `link`.
[[nodiscard]] QStringList formLinks(const QUrlQuery& form)
{
    QStringList out;
    const QString text = form.queryItemValue(QStringLiteral("links"), QUrl::FullyDecoded) + u'\n'
                         + form.queryItemValue(QStringLiteral("link"), QUrl::FullyDecoded);
    for (const QString& line : text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts))
        if (const QString t = line.trimmed(); !t.isEmpty())
            out << t;
    return out;
}

// --- ed3k -------------------------------------------------------------------

/// Ed3kDownloadInfo::State as a filter key; index = wire value.
[[nodiscard]] QString ed3kStateKey(int state)
{
    static const QStringList keys{QStringLiteral("downloading"), QStringLiteral("paused"),
                                  QStringLiteral("hashing"), QStringLiteral("complete"),
                                  QStringLiteral("error")};
    return keys.value(state, QStringLiteral("waiting"));
}

[[nodiscard]] QString ed3kStateText(int state)
{
    switch (state) {
    case 0: return WebServer::tr("Downloading");
    case 1: return WebServer::tr("Paused");
    case 2: return WebServer::tr("Verifying");
    case 3: return WebServer::tr("Complete");
    case 4: return WebServer::tr("Failed");
    default: return {};
    }
}

/// eMule's kPr* as ed3k downloads take them: 0 low, 1 normal, 2 high, 3 very high, 4 very low.
[[nodiscard]] QString ed3kPriorityText(int priority)
{
    switch (priority) {
    case 0: return WebServer::tr("Low");
    case 2: return WebServer::tr("High");
    case 3: return WebServer::tr("Very High");
    case 4: return WebServer::tr("Very Low");
    default: return WebServer::tr("Normal");
    }
}

[[nodiscard]] QString fileTypeToken(const QString& name)
{
    const QString ext = name.section(u'.', -1).toLower();
    static const QStringList video{QStringLiteral("avi"), QStringLiteral("mkv"), QStringLiteral("mp4"),
                                   QStringLiteral("mov"), QStringLiteral("wmv"), QStringLiteral("mpg"),
                                   QStringLiteral("webm"), QStringLiteral("ts")};
    static const QStringList audio{QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"),
                                   QStringLiteral("wav"), QStringLiteral("m4a"), QStringLiteral("opus")};
    static const QStringList archive{QStringLiteral("zip"), QStringLiteral("rar"), QStringLiteral("7z"),
                                     QStringLiteral("gz"), QStringLiteral("tar"), QStringLiteral("bz2")};
    static const QStringList picture{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
                                     QStringLiteral("gif"), QStringLiteral("webp")};
    static const QStringList doc{QStringLiteral("pdf"), QStringLiteral("doc"), QStringLiteral("docx"),
                                 QStringLiteral("txt"), QStringLiteral("epub")};
    if (video.contains(ext)) return QStringLiteral("video");
    if (audio.contains(ext)) return QStringLiteral("audio");
    if (archive.contains(ext)) return QStringLiteral("archive");
    if (picture.contains(ext)) return QStringLiteral("picture");
    if (doc.contains(ext)) return QStringLiteral("document");
    if (ext == u"iso" || ext == u"bin" || ext == u"nrg") return QStringLiteral("cdimage");
    if (ext == u"exe" || ext == u"msi" || ext == u"dmg") return QStringLiteral("program");
    return QStringLiteral("other");
}

// --- Scheduler --------------------------------------------------------------

/// What a rule's action takes as value
enum class SchedValue { Rate, Count, Category, None };
struct SchedActionInfo {
    int id;
    const char* name;
    SchedValue value;
};
/// The GUI's list (OptionsDialog kSchedActions), same English words
constexpr SchedActionInfo kSchedActions[] = {
    {1, QT_TRANSLATE_NOOP("eMule::WebServer", "Upload Limit"), SchedValue::Rate},
    {2, QT_TRANSLATE_NOOP("eMule::WebServer", "Download Limit"), SchedValue::Rate},
    {3, QT_TRANSLATE_NOOP("eMule::WebServer", "Source Limit"), SchedValue::Count},
    {4, QT_TRANSLATE_NOOP("eMule::WebServer", "Con/5sec Limit"), SchedValue::Count},
    {5, QT_TRANSLATE_NOOP("eMule::WebServer", "Max Connections"), SchedValue::Count},
    {6, QT_TRANSLATE_NOOP("eMule::WebServer", "Stop Category"), SchedValue::Category},
    {7, QT_TRANSLATE_NOOP("eMule::WebServer", "Resume Category"), SchedValue::Category},
    {32, QT_TRANSLATE_NOOP("eMule::WebServer", "Usenet: Pause"), SchedValue::None},
    {33, QT_TRANSLATE_NOOP("eMule::WebServer", "Usenet: Resume"), SchedValue::None},
    {34, QT_TRANSLATE_NOOP("eMule::WebServer", "Usenet: Download Limit"), SchedValue::Rate},
    {35, QT_TRANSLATE_NOOP("eMule::WebServer", "Usenet: Pause Category"), SchedValue::Category},
    {36, QT_TRANSLATE_NOOP("eMule::WebServer", "Usenet: Resume Category"), SchedValue::Category},
    {37, QT_TRANSLATE_NOOP("eMule::WebServer", "BitTorrent: Pause"), SchedValue::None},
    {38, QT_TRANSLATE_NOOP("eMule::WebServer", "BitTorrent: Resume"), SchedValue::None},
    {39, QT_TRANSLATE_NOOP("eMule::WebServer", "BitTorrent: Download Limit"), SchedValue::Rate},
    {40, QT_TRANSLATE_NOOP("eMule::WebServer", "BitTorrent: Upload Limit"), SchedValue::Rate},
};

[[nodiscard]] const SchedActionInfo* schedAction(int id)
{
    for (const SchedActionInfo& info : kSchedActions)
        if (info.id == id)
            return &info;
    return nullptr;
}

[[nodiscard]] QString schedDayText(int day)
{
    switch (day) {
    case 1: return WebServer::tr("Monday");
    case 2: return WebServer::tr("Tuesday");
    case 3: return WebServer::tr("Wednesday");
    case 4: return WebServer::tr("Thursday");
    case 5: return WebServer::tr("Friday");
    case 6: return WebServer::tr("Saturday");
    case 7: return WebServer::tr("Sunday");
    case 8: return WebServer::tr("Mon-Fri");
    case 9: return WebServer::tr("Mon-Sat");
    case 10: return WebServer::tr("Sat-Sun");
    default: return WebServer::tr("Daily");
    }
}

[[nodiscard]] QString hhmm(time_t t)
{
    return QDateTime::fromSecsSinceEpoch(t).time().toString(QStringLiteral("HH:mm"));
}

/// Stops the scheduler, runs @p change, saves, and starts it again (IPC SaveSchedules).
template <typename F>
void changeSchedules(Preferences& prefs, F&& change)
{
    if (!theApp.scheduler)
        return;
    theApp.scheduler->stop();
    std::forward<F>(change)(*theApp.scheduler);
    theApp.scheduler->saveToFile(prefs.configDir());
    if (prefs.schedulerEnabled())
        theApp.scheduler->check(true);
    prefs.saveInBackground();
}

} // namespace

// ---------------------------------------------------------------------------
// Shared
// ---------------------------------------------------------------------------

void WebServer::noteChatMessage(const QString& userHash, const QString& name, const QString& text, bool incoming)
{
    m_chatLog.push_back({userHash.toUpper(), name, text, QDateTime::currentDateTime(), incoming});
    if (incoming)
        ++m_chatIncoming;
    if (qsizetype(m_chatLog.size()) > kChatLogMax)
        m_chatLog.erase(m_chatLog.begin(), m_chatLog.begin() + (qsizetype(m_chatLog.size()) - kChatLogMax));
}

QString WebServer::downloadStatusText(const QString& statusKey)
{
    if (statusKey == u"downloading") return tr("Downloading");
    if (statusKey == u"paused") return tr("Paused");
    if (statusKey == u"stopped") return tr("Stopped");
    if (statusKey == u"complete") return tr("Complete");
    if (statusKey == u"error") return tr("Error");
    if (statusKey == u"hashing") return tr("Hashing");
    return tr("Waiting");
}

void WebServer::addUiHeaderVars(QHash<QString, QString>& vars)
{
    if (m_preferences) {
        vars[QStringLiteral("LimitDown")] = QString::number(m_preferences->configuredMaxDownload());
        vars[QStringLiteral("LimitUp")] = QString::number(m_preferences->configuredMaxUpload());
        vars[QStringLiteral("CapDown")] = QString::number(m_preferences->maxGraphDownloadRate());
        vars[QStringLiteral("CapUp")] = QString::number(m_preferences->maxGraphUploadRate());
        vars[QStringLiteral("AltSpeed")] = m_preferences->altSpeed() ? QStringLiteral("1") : QStringLiteral("0");
    }
    // one-shot: the page after the action shows it, the live poll after that no more
    vars[QStringLiteral("UiNotice")] = takeNotice(m_webUiNotice, m_webUiFailed);
    // unread: the page script counts from the last visit of the Messages page
    vars[QStringLiteral("ChatIn")] = QString::number(m_chatIncoming);
    vars[QStringLiteral("Ed3kTabHidden")] =
        m_ed3kBackend && m_ed3kBackend->available() ? QString() : QStringLiteral("hidden");
}

QHttpServerResponse WebServer::webManifest()
{
    const QJsonObject manifest{
        {QStringLiteral("name"), QStringLiteral("eMule Qt Web Control")},
        {QStringLiteral("short_name"), QStringLiteral("eMule")},
        {QStringLiteral("start_url"), QStringLiteral("/?w=transfer")},
        {QStringLiteral("scope"), QStringLiteral("/")},
        {QStringLiteral("display"), QStringLiteral("standalone")},
        {QStringLiteral("background_color"), QStringLiteral("#005784")},
        {QStringLiteral("theme_color"), QStringLiteral("#003C6E")},
        {QStringLiteral("icons"), QJsonArray{
            QJsonObject{{QStringLiteral("src"), QStringLiteral("app-icon-192.png")},
                        {QStringLiteral("sizes"), QStringLiteral("192x192")},
                        {QStringLiteral("type"), QStringLiteral("image/png")}},
            QJsonObject{{QStringLiteral("src"), QStringLiteral("app-icon-512.png")},
                        {QStringLiteral("sizes"), QStringLiteral("512x512")},
                        {QStringLiteral("type"), QStringLiteral("image/png")}},
        }},
        // an installed app opens magnet links on the Add Link page (Chromium)
        {QStringLiteral("protocol_handlers"), QJsonArray{
            QJsonObject{{QStringLiteral("protocol"), QStringLiteral("magnet")},
                        {QStringLiteral("url"), QStringLiteral("/?w=addlink&link=%s")}},
        }},
    };
    QHttpServerResponse resp(QByteArrayLiteral("application/manifest+json"),
                             QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    return resp;
}

QHttpServerResponse WebServer::handleUiStatus(const QHttpServerRequest& request)
{
    const WebSessionCheck ses = webSession(request);
    if (!ses.valid)
        return uiJsonError(401, QStringLiteral("Session expired"));

    QJsonObject out;
    if (m_statistics) {
        out.insert(L("down"), QString::number(m_statistics->rateDown(), 'f', 1));
        out.insert(L("up"), QString::number(m_statistics->rateUp(), 'f', 1));
    }
    if (m_preferences) {
        out.insert(L("limitDown"), qint64(m_preferences->configuredMaxDownload()));
        out.insert(L("limitUp"), qint64(m_preferences->configuredMaxUpload()));
        out.insert(L("altSpeed"), m_preferences->altSpeed());
    }
    if (m_serverConnect)
        out.insert(L("server"), m_serverConnect->currentServer() ? m_serverConnect->currentServer()->name()
                                                                 : tr("Not connected"));
    out.insert(L("chatIn"), qint64(m_chatIncoming));

    // Finished and failed downloads of every network: the page notifies the new ones.
    // [key, name]; keys stay stable while the download is listed.
    QJsonArray done, failed;
    constexpr qsizetype kMax = 1000;
    const auto add = [&](QJsonArray& list, const QString& key, const QString& name) {
        if (list.size() < kMax)
            list.append(QJsonArray{key, name});
    };
    if (m_downloadQueue) {
        for (const auto* file : m_downloadQueue->files()) {
            if (file->status() == PartFileStatus::Complete)
                add(done, QStringLiteral("e:") + md4str(file->fileHash()), file->fileName());
            else if (file->status() == PartFileStatus::Error)
                add(failed, QStringLiteral("e:") + md4str(file->fileHash()), file->fileName());
        }
    }
    if (m_ed3kBackend && m_ed3kBackend->available()) {
        for (const QCborValue& v : m_ed3kBackend->downloads()) {
            const QCborMap r = v.toMap();
            const auto state = r.value(L("state")).toInteger();
            const QString key = QStringLiteral("3:") + r.value(L("root")).toString();
            if (state == 3)
                add(done, key, r.value(L("name")).toString());
            else if (state == 4)
                add(failed, key, r.value(L("name")).toString());
        }
    }
    if (m_torrentBackend && m_torrentBackend->running()) {
        for (const QCborValue& v : m_torrentBackend->torrents()) {
            const QCborMap r = v.toMap();
            const QString key = QStringLiteral("t:") + r.value(L("id")).toString();
            const auto state = r.value(L("state")).toInteger();
            if (!r.value(L("error")).toString().isEmpty())
                add(failed, key, r.value(L("name")).toString());
            else if (state == 4 || state == 5)
                add(done, key, r.value(L("name")).toString());
        }
    }
    if (usenetAvailable()) {
        for (const QCborValue& v : m_usenetBackend->queue()) {
            const QCborMap r = v.toMap();
            const QString key = QStringLiteral("u:") + r.value(L("id")).toString();
            const auto status = r.value(L("status")).toInteger();
            if (status == UsenetWireStatus::Complete)
                add(done, key, r.value(L("name")).toString());
            else if (status == UsenetWireStatus::Failed)
                add(failed, key, r.value(L("name")).toString());
        }
    }
    if (m_hosterBackend && m_hosterBackend->available()) {
        for (const QCborValue& v : m_hosterBackend->items()) {
            const QCborMap r = v.toMap();
            const QString key = QStringLiteral("h:") + QString::number(r.value(L("id")).toInteger());
            QString name = r.value(L("name")).toString();
            if (name.isEmpty())
                name = r.value(L("url")).toString();
            const auto state = r.value(L("state")).toInteger();
            if (state == 8)
                add(done, key, name);
            else if (state == 12)
                add(failed, key, name);
        }
    }
    out.insert(L("done"), done);
    out.insert(L("failed"), failed);
    return QHttpServerResponse(out);
}

// ---------------------------------------------------------------------------
// Speed menu
// ---------------------------------------------------------------------------

void WebServer::webSpeedAction(const QUrlQuery& form)
{
    if (!m_preferences)
        return;
    bool okDown = false, okUp = false;
    const uint down = form.queryItemValue(QStringLiteral("maxdown")).toUInt(&okDown);
    const uint up = form.queryItemValue(QStringLiteral("maxup")).toUInt(&okUp);
    const bool hasAlt = form.hasQueryItem(QStringLiteral("alt"));
    const bool alt = form.queryItemValue(QStringLiteral("alt")) == u"1";
    // same bounds as the Options form
    if ((form.hasQueryItem(QStringLiteral("maxdown")) && (!okDown || down > 1'000'000))
        || (form.hasQueryItem(QStringLiteral("maxup")) && (!okUp || up > 1'000'000))) {
        m_webUiFailed = true;
        m_webUiNotice = tr("Nothing was saved: a value is missing or out of range.");
        return;
    }
    PrefChanges changes;
    if (okDown)
        changes.append({QStringLiteral("maxDownload"), qint64{down}});
    if (okUp)
        changes.append({QStringLiteral("maxUpload"), qint64{up}});
    if (hasAlt)
        changes.append({QStringLiteral("altSpeed"), alt});
    if (changes.isEmpty())
        return;

    if (m_apiBackend) {
        const ApiBackend::PrefApplyReport report = m_apiBackend->applyPreferences(changes);
        m_webUiFailed = !report.status.ok();
        m_webUiNotice = m_webUiFailed ? tr("Not saved: %1").arg(report.status.message)
                                      : tr("Speed limits changed.");
        return;
    }
    // No daemon behind it (tests): store only
    if (okDown)
        m_preferences->setMaxDownload(down);
    if (okUp)
        m_preferences->setMaxUpload(up);
    if (hasAlt)
        m_preferences->setAltSpeed(alt);
    m_webUiFailed = false;
    m_webUiNotice = tr("Speed limits changed.");
}

// ---------------------------------------------------------------------------
// Add Link page, drag and drop, paste
// ---------------------------------------------------------------------------

QString WebServer::buildAddLinkPage(bool isAdmin, const QUrlQuery& query)
{
    QHash<QString, QString> vars;
    vars[QStringLiteral("AddLinkText")] = htmlText(query.queryItemValue(QStringLiteral("link"), QUrl::FullyDecoded));
    vars[QStringLiteral("FormOff")] = isAdmin ? QString() : QStringLiteral("disabled");
    return WebTemplateEngine::substitute(m_templateEngine->section(QStringLiteral("ADDLINK")), vars);
}

void WebServer::webAddAction(const QUrlQuery& form, const QList<WebUpload>& uploads)
{
    const bool paused = form.queryItemValue(QStringLiteral("paused")) == u"1";
    int added = 0;
    int pending = 0;
    QStringList errors;
    const auto refuse = [&errors](const QString& what, const QString& why) {
        errors << tr("%1: %2").arg(what.left(80), why);
    };

    for (const QString& link : formLinks(form)) {
        const QString lower = link.left(10).toLower();
        if (lower.startsWith(u"ed2k://")) {
            if (m_downloadQueue && m_downloadQueue->addDownloadFromED2KLink(link, DownloadQueue::defaultTempDir()))
                ++added;
            else
                refuse(link, tr("not added"));
        } else if (lower.startsWith(u"ed3k://")) {
            if (!m_ed3kBackend || !m_ed3kBackend->available()) {
                refuse(link, tr("ed3k is off."));
                continue;
            }
            const Ed3kWebBackend::AddResult r = m_ed3kBackend->addLink(link, 0, paused);
            if (r.root.isEmpty())
                refuse(link, r.error.isEmpty() ? tr("not added") : r.error);
            else
                ++added;
        } else if (lower.startsWith(u"magnet:")) {
            if (!m_torrentBackend || !m_torrentBackend->running()) {
                refuse(link, tr("BitTorrent is off."));
                continue;
            }
            const TorrentWebAddResult r = m_torrentBackend->addMagnet(link, {}, paused);
            if (r.outcome == TorrentWebAddOutcome::Added)
                ++added;
            else
                refuse(link, r.message.isEmpty() ? tr("not added") : r.message);
        } else if (lower.startsWith(u"http://") || lower.startsWith(u"https://")) {
            // an NZB link is Usenet's, any other web link the hoster's
            if (QUrl(link).path().endsWith(QStringLiteral(".nzb"), Qt::CaseInsensitive) && usenetAvailable()) {
                UsenetWebAddOptions options;
                options.paused = paused;
                ++pending;
                m_usenetBackend->addNzbUrl(link, options, [this, link](const UsenetWebAddResult& r) {
                    if (r.ok())
                        return;
                    m_webUiFailed = true;
                    m_webUiNotice = tr("%1: %2").arg(link.left(80), r.error);
                });
            } else if (m_hosterBackend && m_hosterBackend->available()) {
                const int n = m_hosterBackend->addLinks(link, {});
                if (n > 0)
                    added += n;
                else
                    refuse(link, tr("not added"));
            } else {
                refuse(link, tr("Hoster downloads are off."));
            }
        } else {
            refuse(link, tr("not a link eMule knows"));
        }
    }

    QList<WebUpload> containers;
    for (const WebUpload& file : uploads) {
        const QString ext = file.fileName.section(u'.', -1).toLower();
        if (ext == u"torrent") {
            if (!m_torrentBackend || !m_torrentBackend->running()) {
                refuse(file.fileName, tr("BitTorrent is off."));
                continue;
            }
            const TorrentWebAddResult r = m_torrentBackend->addTorrentFile(file.data, {}, paused);
            if (r.outcome == TorrentWebAddOutcome::Added)
                ++added;
            else
                refuse(file.fileName, r.message.isEmpty() ? tr("not added") : r.message);
        } else if (ext == u"nzb") {
            if (!usenetAvailable()) {
                refuse(file.fileName, tr("Usenet engine unavailable"));
                continue;
            }
            UsenetWebAddOptions options;
            options.paused = paused;
            const UsenetWebAddResult r = m_usenetBackend->addNzb(file.data, file.fileName, options);
            if (r.ok())
                ++added;
            else
                refuse(file.fileName, r.error.isEmpty() ? tr("not added") : r.error);
        } else if (ext == u"dlc" || ext == u"rsdf" || ext == u"ccf") {
            if (!m_hosterBackend || !m_hosterBackend->available())
                refuse(file.fileName, tr("Hoster downloads are off."));
            else
                containers.append(file);
        } else if (ext == u"emulecollection") {
            // Collection reads files only
            QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/webdrop-XXXXXX.emulecollection"));
            Collection collection;
            if (!tmp.open() || tmp.write(file.data) != file.data.size() || !tmp.flush()
                || !collection.initFromFile(tmp.fileName(), file.fileName)) {
                refuse(file.fileName, tr("not a collection"));
                continue;
            }
            for (const auto& [key, entry] : collection.files()) {
                const ops::AddOutcome r = ops::addDownloadFromSearch(
                    md4str(entry->fileHash()), entry->fileName(), static_cast<uint64>(entry->fileSize()), {},
                    0, 0, paused);
                if (r.status.ok())
                    ++added;
                else
                    refuse(entry->fileName(), r.status.message);
            }
        } else {
            refuse(file.fileName, tr("not a file eMule can add"));
        }
    }
    if (!containers.isEmpty())
        openWebContainers(containers);

    if (added == 0 && pending == 0 && errors.isEmpty() && containers.isEmpty())
        return;
    m_webUiFailed = added == 0 && pending == 0 && containers.isEmpty();
    m_webUiNotice = tr("%n download(s) added", nullptr, added);
    if (pending > 0)
        m_webUiNotice += QStringLiteral(" — ") + tr("%n NZB link(s) being fetched", nullptr, pending);
    if (!containers.isEmpty())
        m_webUiNotice += QStringLiteral(" — ") + tr("%n container(s) opened on the Hoster page", nullptr,
                                                    int(containers.size()));
    if (!errors.isEmpty())
        m_webUiNotice += QStringLiteral(" — ") + errors.join(QStringLiteral("; "));
}

// ---------------------------------------------------------------------------
// ed3k page
// ---------------------------------------------------------------------------

QString WebServer::buildEd3kPage(bool isAdmin)
{
    if (!m_ed3kBackend || !m_ed3kBackend->available())
        return WebTemplateEngine::substitute(m_templateEngine->section(QStringLiteral("ED3K_UNAVAILABLE")));

    const QCborMap st = m_ed3kBackend->status();
    QHash<QString, QString> vars;
    const bool running = st.value(L("running")).toBool();
    vars[QStringLiteral("Ed3kState")] = htmlText(running ? tr("Running") : tr("Not running"));
    vars[QStringLiteral("Ed3kNodeId")] = htmlText(st.value(L("nodeId")).toString());
    vars[QStringLiteral("Ed3kContacts")] = QString::number(st.value(L("contacts")).toInteger());
    vars[QStringLiteral("Ed3kUsers")] = QString::number(st.value(L("users")).toInteger());
    vars[QStringLiteral("Ed3kReach")] = htmlText(!running ? QString()
                                                 : st.value(L("router")).toBool() ? tr("Router (reachable)")
                                                                                  : tr("Leaf (through a relay)"));
    QStringList endpoints;
    for (const char* key : {"endpoint4", "endpoint6"})
        if (const QString e = st.value(QLatin1String(key)).toString(); !e.isEmpty())
            endpoints << e;
    vars[QStringLiteral("Ed3kEndpoints")] = htmlText(endpoints.join(QStringLiteral(", ")));
    vars[QStringLiteral("Ed3kRates")] = htmlText(tr("Down: %1, up: %2").arg(
        formatByteRate(double(st.value(L("rateDown")).toInteger())),
        formatByteRate(double(st.value(L("rateUp")).toInteger()))));

    // only ed3k's own rows: a single-transfer join of another network has no 64-hex root
    static const QRegularExpression hexRoot(QStringLiteral("^[0-9a-fA-F]{64}$"));
    QList<QCborMap> rows;
    for (const QCborValue& v : m_ed3kBackend->downloads())
        if (hexRoot.match(v.toMap().value(L("root")).toString()).hasMatch())
            rows.append(v.toMap());
    std::ranges::sort(rows, [](const QCborMap& a, const QCborMap& b) {
        return a.value(L("name")).toString().compare(b.value(L("name")).toString(), Qt::CaseInsensitive) < 0;
    });
    const QString lineTmpl = m_templateEngine->section(QStringLiteral("ED3K_LINE"));
    QString lines;
    qint64 rate = 0;
    for (const QCborMap& r : std::as_const(rows)) {
        const qint64 size = r.value(L("size")).toInteger();
        const int state = int(r.value(L("state")).toInteger());
        rate += r.value(L("rate")).toInteger();
        QHash<QString, QString> v;
        const QString name = r.value(L("name")).toString();
        v[QStringLiteral("Ed3kRoot")] = htmlText(r.value(L("root")).toString());
        v[QStringLiteral("Ed3kName")] = htmlText(name);
        v[QStringLiteral("Ed3kFileType")] = fileTypeToken(name);
        v[QStringLiteral("Ed3kPath")] = htmlText(r.value(L("path")).toString());
        v[QStringLiteral("Ed3kSize")] = size > 0 ? formatByteSize(size) : QString();
        v[QStringLiteral("Ed3kPercent")] = pctText(r.value(L("done")).toInteger(), size);
        v[QStringLiteral("Ed3kStatusKey")] = ed3kStateKey(state);
        QString status = ed3kStateText(state);
        if (const QString err = r.value(L("error")).toString(); state == 4 && !err.isEmpty())
            status += QStringLiteral(": ") + err;
        v[QStringLiteral("Ed3kStatus")] = htmlText(status);
        v[QStringLiteral("Ed3kStatusName")] = htmlText(ed3kStateText(state));
        v[QStringLiteral("Ed3kSpeed")] = rateText(r.value(L("rate")).toInteger());
        v[QStringLiteral("Ed3kSources")] = QStringLiteral("%1 (%2)").arg(
            argStr(r.value(L("transferring")).toInteger()), argStr(r.value(L("sources")).toInteger()));
        v[QStringLiteral("Ed3kPriority")] = htmlText(ed3kPriorityText(int(r.value(L("priority")).toInteger())));
        lines += WebTemplateEngine::substitute(lineTmpl, v);
    }
    vars[QStringLiteral("Ed3kLines")] = lines;
    vars[QStringLiteral("Ed3kSummary")] = htmlText(
        tr("%n download(s)", nullptr, int(rows.size())) + QStringLiteral(" — ") + formatByteRate(double(rate)));
    vars[QStringLiteral("FormOff")] = isAdmin ? QString() : QStringLiteral("disabled");
    return WebTemplateEngine::substitute(m_templateEngine->section(QStringLiteral("ED3K")), vars);
}

void WebServer::webEd3kAction(const QUrlQuery& form)
{
    if (!m_ed3kBackend || !m_ed3kBackend->available())
        return;
    using C = Ed3kWebBackend::Control;
    const QString op = form.queryItemValue(QStringLiteral("op"));
    if (op == u"add") {
        webAddAction(form, {});
        return;
    }
    if (op == u"clearcompleted") {
        m_ed3kBackend->control({}, C::ClearCompleted, 0);
        return;
    }
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{64}$"));
    int done = 0;
    const QStringList ids = form.allQueryItemValues(QStringLiteral("ids"));
    for (const QString& root : ids) {
        if (!hex.match(root).hasMatch())
            continue;
        bool ok = false;
        if (op == u"pause")
            ok = m_ed3kBackend->control(root, C::Pause, 0);
        else if (op == u"resume")
            ok = m_ed3kBackend->control(root, C::Resume, 0);
        else if (op == u"cancel")
            ok = m_ed3kBackend->control(root, C::Cancel, 0);
        else if (op == u"priolow")
            ok = m_ed3kBackend->control(root, C::Priority, 0);
        else if (op == u"prionormal")
            ok = m_ed3kBackend->control(root, C::Priority, 1);
        else if (op == u"priohigh")
            ok = m_ed3kBackend->control(root, C::Priority, 2);
        done += ok ? 1 : 0;
    }
    if (!ids.isEmpty() && done < ids.size()) {
        m_webUiFailed = true;
        m_webUiNotice = tr("%1 of %2 download(s) did not take it.").arg(argStr(ids.size() - done), argStr(ids.size()));
    }
}

// ---------------------------------------------------------------------------
// Messages page
// ---------------------------------------------------------------------------

QString WebServer::buildMessagesPage(bool isAdmin, const QUrlQuery& query)
{
    const QString peer = query.queryItemValue(QStringLiteral("peer")).toUpper();
    static const QRegularExpression hashRe(QStringLiteral("^[0-9A-F]{32}$"));
    const bool hasPeer = hashRe.match(peer).hasMatch();

    // Conversations: every friend, then whoever else wrote or was written to
    struct Peer {
        QString hash;
        QString name;
        bool isFriend = false;
        bool online = false;
        QString lastSeen;
        int lines = 0;
    };
    QList<Peer> peers;
    const auto findPeer = [&peers](const QString& hash) -> Peer* {
        for (Peer& p : peers)
            if (p.hash == hash)
                return &p;
        return nullptr;
    };
    if (m_friendList) {
        for (const auto& f : m_friendList->friends()) {
            Peer p;
            p.isFriend = true;
            p.hash = f->hasUserhash() ? md4str(f->userHash().data()).toUpper() : QString();
            p.name = f->name().isEmpty() ? f->lastUsedAddress().toString() : f->name();
            const UpDownClient* c = f->linkedClient();
            p.online = c && c->socket() && c->socket()->isConnected();
            p.lastSeen = f->lastSeen() > 0
                ? QDateTime::fromSecsSinceEpoch(f->lastSeen()).toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                : QString();
            peers.append(p);
        }
    }
    for (const WebChatLine& line : m_chatLog) {
        if (line.userHash.isEmpty())
            continue;
        if (Peer* p = findPeer(line.userHash)) {
            ++p->lines;
            if (p->name.isEmpty())
                p->name = line.name;
            continue;
        }
        Peer p;
        p.hash = line.userHash;
        p.name = line.name;
        p.lines = 1;
        if (theApp.clientList) {
            std::array<uint8, 16> h{};
            if (decodeBase16(line.userHash, h.data(), 16) == 16)
                if (const auto* c = theApp.clientList->findByUserHash(h.data(), 0, 0))
                    p.online = c->socket() && c->socket()->isConnected();
        }
        peers.append(p);
    }

    const QString friendTmpl = m_templateEngine->section(QStringLiteral("FRIEND_LINE"));
    QString friendLines;
    QString peerName;
    for (const Peer& p : std::as_const(peers)) {
        QHash<QString, QString> v;
        v[QStringLiteral("FriendHash")] = p.hash;
        v[QStringLiteral("FriendName")] = htmlText(p.name.isEmpty() ? p.hash : p.name);
        v[QStringLiteral("FriendIcon")] = p.isFriend ? QStringLiteral("l_friend") : QStringLiteral("l_users");
        v[QStringLiteral("FriendStatusKey")] = p.online ? QStringLiteral("online") : QStringLiteral("offline");
        v[QStringLiteral("FriendStatus")] = htmlText(p.online ? tr("Connected") : tr("Offline"));
        v[QStringLiteral("FriendLastSeen")] = htmlText(p.lastSeen);
        v[QStringLiteral("FriendLines")] = p.lines > 0 ? QString::number(p.lines) : QString();
        v[QStringLiteral("FriendActive")] = hasPeer && p.hash == peer ? QStringLiteral("active") : QString();
        if (hasPeer && p.hash == peer)
            peerName = p.name;
        friendLines += WebTemplateEngine::substitute(friendTmpl, v);
    }

    const QString chatTmpl = m_templateEngine->section(QStringLiteral("CHAT_LINE"));
    QString chatLines;
    if (hasPeer) {
        for (const WebChatLine& line : m_chatLog) {
            if (line.userHash != peer)
                continue;
            QHash<QString, QString> v;
            v[QStringLiteral("ChatDir")] = line.incoming ? QStringLiteral("in") : QStringLiteral("out");
            v[QStringLiteral("ChatTime")] = line.when.toString(QStringLiteral("HH:mm:ss"));
            v[QStringLiteral("ChatName")] = htmlText(line.incoming ? (line.name.isEmpty() ? peerName : line.name)
                                                                   : (m_preferences ? m_preferences->nick() : tr("Me")));
            v[QStringLiteral("ChatText")] = htmlText(line.text);
            chatLines += WebTemplateEngine::substitute(chatTmpl, v);
        }
    }

    QHash<QString, QString> vars;
    vars[QStringLiteral("FriendLines")] = friendLines;
    vars[QStringLiteral("FriendCount")] = QString::number(m_friendList ? m_friendList->count() : 0);
    vars[QStringLiteral("ChatLines")] = chatLines;
    vars[QStringLiteral("ChatPeer")] = hasPeer ? peer : QString();
    vars[QStringLiteral("ChatTitle")] = htmlText(hasPeer ? (peerName.isEmpty() ? peer : peerName)
                                                         : tr("Pick a friend to chat with."));
    vars[QStringLiteral("ChatHidden")] = hasPeer ? QString() : QStringLiteral("hidden");
    vars[QStringLiteral("ChatIsFriend")] =
        hasPeer && m_friendList && m_friendList->isAlreadyFriend(peer) ? QStringLiteral("1") : QStringLiteral("0");
    vars[QStringLiteral("FormOff")] = isAdmin ? QString() : QStringLiteral("disabled");
    return WebTemplateEngine::substitute(m_templateEngine->section(QStringLiteral("MESSAGES")), vars);
}

void WebServer::webMessagesAction(const QUrlQuery& form)
{
    const QString op = form.queryItemValue(QStringLiteral("op"));
    const QString peer = form.queryItemValue(QStringLiteral("peer")).toUpper();
    if (op == u"send") {
        const QString text = form.queryItemValue(QStringLiteral("text"), QUrl::FullyDecoded).trimmed();
        if (text.isEmpty())
            return;
        // eMule's chat window caps a line; a longer one is cut by the client anyway
        const QString message = text.left(450);
        const ops::Status st = ops::sendChatMessage(peer, message);
        if (!st.ok()) {
            m_webUiFailed = true;
            m_webUiNotice = tr("Not sent: %1").arg(st.message);
            return;
        }
        QString name;
        std::array<uint8, 16> h{};
        if (theApp.clientList && decodeBase16(peer, h.data(), 16) == 16)
            if (const auto* c = theApp.clientList->findByUserHash(h.data(), 0, 0))
                name = c->userName();
        noteChatMessage(peer, name, message, /*incoming*/ false);
        return;
    }
    if (!m_friendList)
        return;
    if (op == u"addfriend") {
        const QString hashStr = form.queryItemValue(QStringLiteral("hash")).trimmed();
        const Address addr = Address::fromString(form.queryItemValue(QStringLiteral("addr"), QUrl::FullyDecoded).trimmed());
        const uint16 port = form.queryItemValue(QStringLiteral("port")).toUShort();
        std::array<uint8, 16> hash{};
        const bool hasHash = hashStr.size() == 32 && decodeBase16(hashStr, hash.data(), 16) == 16;
        Friend* f = (hasHash || (!addr.isNull() && port > 0))
            ? m_friendList->addFriend(hasHash ? hash.data() : nullptr, addr, port,
                                      form.queryItemValue(QStringLiteral("name"), QUrl::FullyDecoded).trimmed().left(255),
                                      hasHash)
            : nullptr;
        if (!f) {
            m_webUiFailed = true;
            m_webUiNotice = tr("Failed to add friend: give a user hash, or an address and port.");
            return;
        }
        saveFriends();
        m_webUiFailed = false;
        m_webUiNotice = tr("Friend added.");
        return;
    }
    if (op == u"removefriend") {
        std::array<uint8, 16> hash{};
        if (peer.size() != 32 || decodeBase16(peer, hash.data(), 16) != 16)
            return;
        if (Friend* f = m_friendList->searchFriend(hash.data())) {
            m_friendList->removeFriend(f);
            saveFriends();
        }
        return;
    }
    if (op == u"clearchat") {
        std::erase_if(m_chatLog, [&peer](const WebChatLine& l) { return l.userHash == peer; });
        return;
    }
}

// ---------------------------------------------------------------------------
// Scheduler page
// ---------------------------------------------------------------------------

QString WebServer::buildSchedulerPage(bool isAdmin)
{
    const QString lineTmpl = m_templateEngine->section(QStringLiteral("SCHEDULE_LINE"));
    QString lines;
    const int n = theApp.scheduler ? theApp.scheduler->count() : 0;
    for (int i = 0; i < n; ++i) {
        const ScheduleEntry* e = theApp.scheduler->schedule(i);
        if (!e)
            continue;
        QStringList actions;
        for (size_t a = 0; a < e->actions.size() && e->actions[a] != ScheduleAction::None; ++a) {
            const SchedActionInfo* info = schedAction(static_cast<int>(e->actions[a]));
            QString text = info ? tr(info->name) : QString::number(static_cast<int>(e->actions[a]));
            if (info && info->value != SchedValue::None)
                text += QStringLiteral(": ") + e->values[a]
                        + (info->value == SchedValue::Rate ? QStringLiteral(" ") + tr("KB/s") : QString());
            actions << text;
        }
        QHash<QString, QString> v;
        v[QStringLiteral("ScheduleIndex")] = QString::number(i);
        v[QStringLiteral("ScheduleTitle")] = htmlText(e->title);
        v[QStringLiteral("ScheduleDay")] = htmlText(schedDayText(static_cast<int>(e->day)));
        v[QStringLiteral("ScheduleTime")] = e->endTime == 0
            ? htmlText(tr("from %1").arg(hhmm(e->startTime)))
            : htmlText(QStringLiteral("%1 – %2").arg(hhmm(e->startTime), hhmm(e->endTime)));
        v[QStringLiteral("ScheduleActions")] = htmlText(actions.join(QStringLiteral(", ")));
        v[QStringLiteral("ScheduleEnabled")] = e->enabled ? QStringLiteral("checked") : QString();
        v[QStringLiteral("ScheduleStatusKey")] = e->enabled ? QStringLiteral("enabled") : QStringLiteral("disabled");
        v[QStringLiteral("FormOff")] = isAdmin ? QString() : QStringLiteral("disabled");
        lines += WebTemplateEngine::substitute(lineTmpl, v);
    }

    QString dayOptions;
    for (int d = 0; d <= 10; ++d)
        dayOptions += QStringLiteral("<option value=\"%1\">%2</option>").arg(QString::number(d), htmlText(schedDayText(d)));
    QString actionOptions;
    for (const SchedActionInfo& info : kSchedActions)
        actionOptions += QStringLiteral("<option value=\"%1\" data-value=\"%2\">%3</option>")
                             .arg(QString::number(info.id),
                                  info.value == SchedValue::None ? QStringLiteral("0") : QStringLiteral("1"),
                                  htmlText(tr(info.name)));

    QHash<QString, QString> vars;
    vars[QStringLiteral("ScheduleLines")] = lines;
    vars[QStringLiteral("SchedulerChecked")] =
        m_preferences && m_preferences->schedulerEnabled() ? QStringLiteral("checked") : QString();
    vars[QStringLiteral("ScheduleDayOptions")] = dayOptions;
    vars[QStringLiteral("ScheduleActionOptions")] = actionOptions;
    vars[QStringLiteral("ScheduleCount")] = QString::number(n);
    vars[QStringLiteral("FormOff")] = isAdmin ? QString() : QStringLiteral("disabled");
    return WebTemplateEngine::substitute(m_templateEngine->section(QStringLiteral("SCHEDULER")), vars);
}

void WebServer::webSchedulerAction(const QUrlQuery& form)
{
    if (!theApp.scheduler || !m_preferences)
        return;
    Preferences& prefs = *m_preferences;
    const QString op = form.queryItemValue(QStringLiteral("op"));
    bool okIndex = false;
    const int index = form.queryItemValue(QStringLiteral("i")).toInt(&okIndex);
    const bool validIndex = okIndex && index >= 0 && index < theApp.scheduler->count();

    if (op == u"enable") {
        const bool on = form.queryItemValue(QStringLiteral("on")) == u"1";
        prefs.setSchedulerEnabled(on);
        if (on)
            theApp.scheduler->check(true);
        else
            theApp.scheduler->stop();
        prefs.saveInBackground();
        return;
    }
    if (op == u"toggle" && validIndex) {
        changeSchedules(prefs, [index](Scheduler& s) {
            if (ScheduleEntry* e = s.schedule(index))
                e->enabled = !e->enabled;
        });
        return;
    }
    if (op == u"remove" && validIndex) {
        changeSchedules(prefs, [index](Scheduler& s) { s.removeSchedule(index); });
        return;
    }
    if (op == u"add") {
        const QTime start = QTime::fromString(form.queryItemValue(QStringLiteral("start"), QUrl::FullyDecoded), QStringLiteral("HH:mm"));
        const QString endText = form.queryItemValue(QStringLiteral("end"), QUrl::FullyDecoded);
        const QTime end = QTime::fromString(endText, QStringLiteral("HH:mm"));
        const int day = form.queryItemValue(QStringLiteral("day")).toInt();
        const int actionId = form.queryItemValue(QStringLiteral("action")).toInt();
        const QString value = form.queryItemValue(QStringLiteral("value"), QUrl::FullyDecoded).trimmed();
        const SchedActionInfo* info = schedAction(actionId);
        bool valueOk = true;
        if (info && info->value != SchedValue::None) {
            const int n = value.toInt(&valueOk);
            valueOk = valueOk && (n >= 0 || info->value == SchedValue::Category);
        }
        if (!start.isValid() || (!endText.isEmpty() && !end.isValid()) || day < 0 || day > 10 || !info
            || !isKnownScheduleAction(actionId) || !valueOk) {
            m_webUiFailed = true;
            m_webUiNotice = tr("Nothing was saved: a value is missing or out of range.");
            return;
        }
        auto entry = std::make_unique<ScheduleEntry>();
        entry->title = form.queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded).trimmed().left(100);
        if (entry->title.isEmpty())
            entry->title = tr("New Schedule");
        // only the time of day counts (Scheduler::check)
        entry->startTime = QDateTime(QDate::currentDate(), start).toSecsSinceEpoch();
        entry->endTime = endText.isEmpty() ? 0 : QDateTime(QDate::currentDate(), end).toSecsSinceEpoch();
        entry->day = static_cast<ScheduleDay>(day);
        entry->actions[0] = static_cast<ScheduleAction>(actionId);
        entry->values[0] = info->value == SchedValue::None ? QString() : value;
        entry->enabled = true;
        changeSchedules(prefs, [&entry](Scheduler& s) { s.addSchedule(std::move(entry)); });
        m_webUiFailed = false;
        m_webUiNotice = tr("Schedule added.");
        return;
    }
}

} // namespace eMule
