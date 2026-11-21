/// @file SingleTransfer.cpp
/// @brief Single transfer mode (ed3k only) — implementation.

#include "SingleTransfer.h"

#include "BtBridge.h"
#include "Ed3kDownloadsBridge.h"
#include "Ed3kHosterBridge.h"
#include "IpcMessage.h"
#include "UsenetBridge.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QTimer>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kJoinBase.h"
#include "Ed3kSession.h"
#endif

namespace eMule::SingleTransfer {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

constexpr int kTickMs = 2000;
const QString kTorrent = QStringLiteral("torrent");
const QString kUsenet = QStringLiteral("usenet");
const QString kHoster = QStringLiteral("hoster");

ed3k::Ed3kSession* s_session = nullptr;
std::function<void(const IpcMessage&)> s_broadcast;
QTimer* s_timer = nullptr;
bool s_listed = false;   ///< the last push had rows of the mode

bool modeOn(const QString& tag)
{
    if (tag == kTorrent)
        return thePrefs.btSingleTransferOn();
    if (tag == kUsenet)
        return thePrefs.usenetSingleTransferOn();
    if (tag == kHoster)
        return thePrefs.hosterSingleTransferOn();
    return false;
}

/// joined with ed3k; nullopt: the join is not started, nothing to judge yet
std::optional<bool> joined([[maybe_unused]] const QString& tag, [[maybe_unused]] const QString& owner)
{
#ifdef EMULE_HAVE_ED3K
    if (s_session)
        return ed3k::Ed3kJoinBase::joinedAt(*s_session, tag, owner);
#endif
    return std::nullopt;
}

std::optional<Job> jobOf(const QString& tag, const QString& owner)
{
    if (tag == kTorrent)
        return BtBridge::transferJob(owner);
    if (tag == kUsenet)
        return UsenetBridge::transferJob(owner);
    if (tag == kHoster)
        return Ed3kHosterBridge::transferJob(owner);
    return std::nullopt;
}

bool controlOf(const QString& tag, const QString& owner, int action, qint64 value)
{
    if (tag == kTorrent)
        return BtBridge::transferControl(owner, action, value);
    if (tag == kUsenet)
        return UsenetBridge::transferControl(owner, action, value);
    if (tag == kHoster)
        return Ed3kHosterBridge::transferControl(owner, action, value);
    return false;
}

/// the jobs of @p tag; @p downloadingOnly: those fetching now (hoster: all, every item is a join)
QStringList jobsOf(const QString& tag, bool downloadingOnly)
{
    if (tag == kTorrent)
        return BtBridge::jobIds(downloadingOnly);
    if (tag == kUsenet)
        return UsenetBridge::jobIds(downloadingOnly);
    return Ed3kHosterBridge::owners();
}

std::pair<QString, QString> split(const QString& key)
{
    const qsizetype bar = key.indexOf(u'|');
    return bar > 0 ? std::pair(key.left(bar), key.mid(bar + 1)) : std::pair(QString(), QString());
}

void tick()
{
    enforce();
    if (!s_broadcast)
        return;
    QCborArray rows;
    appendRows(rows);
    // the rates move without an ed3k event; one more push clears the last rows
    if (rows.isEmpty() && !s_listed)
        return;
    s_listed = !rows.isEmpty();
    IpcMessage m(IpcMsgType::PushEd3kDownloads, 0);
    m.append(Ed3kDownloadsBridge::listToCbor());
    s_broadcast(m);
}

} // namespace

void setSession(ed3k::Ed3kSession* session, std::function<void(const IpcMessage&)> broadcast)
{
    s_session = session;
    s_broadcast = std::move(broadcast);
    s_listed = false;
    if (!session) {
        delete s_timer;
        s_timer = nullptr;
        return;
    }
    if (!s_timer) {
        s_timer = new QTimer;
        s_timer->setInterval(kTickMs);
        QObject::connect(s_timer, &QTimer::timeout, s_timer, &tick);
        s_timer->start();
    }
}

QString keyOf(const QString& tag, const QString& owner)
{
    return tag + u'|' + owner;
}

bool isKey(const QString& key)
{
    const auto [tag, owner] = split(key);
    return !owner.isEmpty() && (tag == kTorrent || tag == kUsenet || tag == kHoster);
}

QCborMap transferRow(const QString& tag, const QString& owner, const Job& j, bool joined)
{
    return QCborMap{
        {QStringLiteral("root"), keyOf(tag, owner)},
        {QStringLiteral("network"), tag},
        {QStringLiteral("name"), j.name},
        {QStringLiteral("size"), j.size},
        {QStringLiteral("done"), j.done},
        {QStringLiteral("rate"), j.rate},
        {QStringLiteral("sources"), j.sources},
        {QStringLiteral("transferring"), j.transferring},
        {QStringLiteral("state"), int(j.state)},
        {QStringLiteral("priority"), j.priority},
        {QStringLiteral("category"), j.category},
        {QStringLiteral("owner"), owner},
        {QStringLiteral("path"), j.path},
        {QStringLiteral("addedOn"), j.addedOn},
        {QStringLiteral("error"), j.error},
        {QStringLiteral("joined"), joined},
    };
}

void appendRows(QCborArray& downloads)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_session)
        return;
    for (const QString& tag : {kTorrent, kUsenet}) {
        if (!modeOn(tag))
            continue;
        // the joined jobs: those with an ed3k member (Ed3kJoinBase::networkJobsAt) and the
        // others the join holds (a set still searched, members not open yet)
        QStringList owners;
        for (const auto& job : ed3k::Ed3kJoinBase::networkJobsAt(*s_session))
            if (job.tag == tag && !owners.contains(job.owner))
                owners << job.owner;
        for (const QString& owner : jobsOf(tag, false))
            if (!owners.contains(owner) && joined(tag, owner).value_or(false))
                owners << owner;
        for (const QString& owner : std::as_const(owners))
            if (const auto j = jobOf(tag, owner))
                downloads.append(transferRow(tag, owner, *j));
        // unjoined torrents done: not paused, they seed; listed so they show somewhere
        if (tag == kTorrent)
            for (const QString& owner : jobsOf(tag, false))
                if (!owners.contains(owner))
                    if (const auto j = jobOf(tag, owner); j && j->state == RowState::Complete)
                        downloads.append(transferRow(tag, owner, *j, false));
    }
    if (modeOn(kHoster))
        for (const QString& owner : Ed3kHosterBridge::owners())
            if (const auto j = jobOf(kHoster, owner))
                downloads.append(transferRow(kHoster, owner, *j));
#else
    Q_UNUSED(downloads)
#endif
}

bool control(const QString& key, int action, qint64 value)
{
    const auto [tag, owner] = split(key);
    if (!isKey(key))
        return false;
    const bool done = controlOf(tag, owner, action, value);
    // the user took it over: the mode resumes it no more
    if (done && (action == 1 || action == 2 || action == 7)) {
        QStringList paused = thePrefs.singleTransferPaused();
        if (paused.removeAll(key) > 0)
            thePrefs.setSingleTransferPaused(paused);
    }
    return done;
}

void enforce()
{
    QStringList paused = thePrefs.singleTransferPaused();
    const QStringList before = paused;
    for (const QString& tag : {kTorrent, kUsenet}) {
        if (!modeOn(tag))
            continue;
        // the mode lasts: an unjoined job fetching now is paused
        for (const QString& owner : jobsOf(tag, true)) {
            if (joined(tag, owner) != false)
                continue;   // joined, or not judged yet (ed3k starting)
            if (controlOf(tag, owner, 0, 0)) {
                logInfo(QStringLiteral("Single transfer mode: paused %1 \"%2\" (not joined with ed3k)")
                            .arg(tag, jobOf(tag, owner).value_or(Job{}).name));
                if (const QString key = keyOf(tag, owner); !paused.contains(key))
                    paused << key;
            }
        }
    }
    // the mode ended, or the job joined since: resumed; a job gone is forgotten
    for (auto it = paused.begin(); it != paused.end();) {
        const auto [tag, owner] = split(*it);
        const auto job = jobOf(tag, owner);
        const bool engineUp = tag == kTorrent ? BtBridge::running() : UsenetBridge::queue() != nullptr;
        if (engineUp && !job) {
            it = paused.erase(it);
            continue;
        }
        // a torrent done after all (paused mid-check) seeds like any done torrent
        const bool doneTorrent = tag == kTorrent && job && job->state == RowState::Complete;
        if (job && (!modeOn(tag) || joined(tag, owner) == true || doneTorrent)) {
            if (job->state == RowState::Paused || doneTorrent)   // done: state reads Complete while paused
                (void)controlOf(tag, owner, 1, 0);
            logInfo(QStringLiteral("Single transfer mode: resumed %1 \"%2\"").arg(tag, job->name));
            it = paused.erase(it);
            continue;
        }
        ++it;
    }
    if (paused != before)
        thePrefs.setSingleTransferPaused(paused);
}

} // namespace eMule::SingleTransfer
