#include "pch.h"
/// @file Scheduler.cpp
/// @brief Time-based scheduling of speed limits — port of MFC Scheduler.cpp.
///
/// Uses QSettings for INI persistence and QDateTime for time checks.

#include "transfer/Scheduler.h"
#include "transfer/DownloadQueue.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QSettings>

#include <algorithm>
#include <iterator>
#include <optional>

namespace eMule {

bool isKnownScheduleAction(int id)
{
    return (id >= static_cast<int>(ScheduleAction::SetUpload) && id <= static_cast<int>(ScheduleAction::CatResume))
           || (id >= static_cast<int>(ScheduleAction::UsenetPause) && id <= static_cast<int>(ScheduleAction::BtUpload));
}

bool scheduleActionHasValue(ScheduleAction action)
{
    switch (action) {
    case ScheduleAction::None:
    case ScheduleAction::UsenetPause:
    case ScheduleAction::UsenetResume:
    case ScheduleAction::BtPause:
    case ScheduleAction::BtResume:
        return false;
    default:
        return true;
    }
}

// ===========================================================================
// ScheduleEntry
// ===========================================================================

void ScheduleEntry::resetActions()
{
    for (int i = 0; i < 16; ++i) {
        actions[static_cast<size_t>(i)] = ScheduleAction::None;
        values[static_cast<size_t>(i)].clear();
    }
}

// ===========================================================================
// Scheduler
// ===========================================================================

Scheduler::Scheduler(QObject* parent)
    : QObject(parent)
{
}

Scheduler::~Scheduler() = default;

int Scheduler::addSchedule(std::unique_ptr<ScheduleEntry> entry)
{
    m_schedules.push_back(std::move(entry));
    return static_cast<int>(m_schedules.size()) - 1;
}

void Scheduler::updateSchedule(int index, std::unique_ptr<ScheduleEntry> entry)
{
    releaseNetworks();
    if (index >= 0 && index < static_cast<int>(m_schedules.size()))
        m_schedules[static_cast<size_t>(index)] = std::move(entry);
}

ScheduleEntry* Scheduler::schedule(int index) const
{
    if (index >= 0 && index < static_cast<int>(m_schedules.size()))
        return m_schedules[static_cast<size_t>(index)].get();
    return nullptr;
}

void Scheduler::removeSchedule(int index)
{
    releaseNetworks();   // indices shift
    if (index >= 0 && index < static_cast<int>(m_schedules.size()))
        m_schedules.erase(m_schedules.begin() + index);
}

void Scheduler::removeAll()
{
    releaseNetworks();
    m_schedules.clear();
}

int Scheduler::count() const
{
    return static_cast<int>(m_schedules.size());
}

// ===========================================================================
// Persistence — INI format matching MFC preferences.ini layout
// ===========================================================================

int Scheduler::loadFromFile(const QString& configDir)
{
    const QString filePath = configDir + QStringLiteral("/preferences.ini");
    QSettings ini(filePath, QSettings::IniFormat);

    ini.beginGroup(QStringLiteral("Scheduler"));
    int max = ini.value(QStringLiteral("Count"), 0).toInt();
    ini.endGroup();

    int loaded = 0;
    for (int i = 0; i < max; ++i) {
        const QString section = QStringLiteral("Schedule#%1").arg(i);
        ini.beginGroup(section);

        QString title = ini.value(QStringLiteral("Title")).toString();
        if (title.isEmpty()) {
            ini.endGroup();
            break;
        }

        auto entry = std::make_unique<ScheduleEntry>();
        entry->title = title;
        entry->day = static_cast<ScheduleDay>(ini.value(QStringLiteral("Day"), 0).toUInt());
        entry->enabled = ini.value(QStringLiteral("Enabled"), false).toBool();
        entry->startTime = static_cast<time_t>(ini.value(QStringLiteral("StartTime"), 0).toLongLong());
        entry->endTime = static_cast<time_t>(ini.value(QStringLiteral("EndTime"), 0).toLongLong());

        // Unknown ids (a mod's, e.g. MorphXT 8-16) are dropped and the rest close up:
        // the first None ends the list.
        size_t slot = 0;
        for (int a = 0; a < 16; ++a) {
            const int id = ini.value(QStringLiteral("Action%1").arg(a), 0).toInt();
            if (!isKnownScheduleAction(id))
                continue;
            entry->actions[slot] = static_cast<ScheduleAction>(id);
            entry->values[slot] = ini.value(QStringLiteral("Value%1").arg(a)).toString();
            ++slot;
        }

        ini.endGroup();
        addSchedule(std::move(entry));
        ++loaded;
    }

    return loaded;
}

void Scheduler::saveToFile(const QString& configDir)
{
    const QString filePath = configDir + QStringLiteral("/preferences.ini");
    QSettings ini(filePath, QSettings::IniFormat);

    ini.beginGroup(QStringLiteral("Scheduler"));
    ini.setValue(QStringLiteral("Count"), count());
    ini.endGroup();

    for (int i = 0; i < count(); ++i) {
        const ScheduleEntry* entry = schedule(i);
        if (!entry)
            continue;

        const QString section = QStringLiteral("Schedule#%1").arg(i);
        ini.beginGroup(section);

        ini.setValue(QStringLiteral("Title"), entry->title);
        ini.setValue(QStringLiteral("Day"), static_cast<uint32>(entry->day));
        ini.setValue(QStringLiteral("Enabled"), entry->enabled);
        ini.setValue(QStringLiteral("StartTime"), static_cast<qlonglong>(entry->startTime));
        ini.setValue(QStringLiteral("EndTime"), static_cast<qlonglong>(entry->endTime));

        for (int a = 0; a < 16; ++a) {
            ini.setValue(QStringLiteral("Action%1").arg(a),
                         static_cast<int>(entry->actions[static_cast<size_t>(a)]));
            ini.setValue(QStringLiteral("Value%1").arg(a),
                         entry->values[static_cast<size_t>(a)]);
        }

        ini.endGroup();
    }
}

// ===========================================================================
// check — MFC CScheduler::Check (called periodically)
// ===========================================================================

int Scheduler::check(bool forceCheck)
{
    // Only act when at least one schedule entry is enabled
    bool anyEnabled = false;
    for (int i = 0; i < count(); ++i) {
        if (auto* e = schedule(i); e && e->enabled) {
            anyEnabled = true;
            break;
        }
    }
    if (!anyEnabled) {
        stop();
        return -1;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const int currentMinute = now.time().minute();

    if (!forceCheck && currentMinute == m_lastCheckedMinute)
        return -1;

    m_lastCheckedMinute = currentMinute;
    Preferences::ScheduledLimits before = thePrefs.scheduledLimits();
    clearOverrides();
    std::set<int> open;

    for (int i = 0; i < count(); ++i) {
        const ScheduleEntry* entry = schedule(i);
        if (!entry || !entry->enabled)
            continue;
        if (entry->actions[0] == ScheduleAction::None)
            continue;

        // Check day of week
        if (entry->day != ScheduleDay::Daily) {
            int dow = now.date().dayOfWeek(); // 1=Mon, 7=Sun

            switch (entry->day) {
            case ScheduleDay::Monday:
                if (dow != 1) continue; break;
            case ScheduleDay::Tuesday:
                if (dow != 2) continue; break;
            case ScheduleDay::Wednesday:
                if (dow != 3) continue; break;
            case ScheduleDay::Thursday:
                if (dow != 4) continue; break;
            case ScheduleDay::Friday:
                if (dow != 5) continue; break;
            case ScheduleDay::Saturday:
                if (dow != 6) continue; break;
            case ScheduleDay::Sunday:
                if (dow != 7) continue; break;
            case ScheduleDay::MonToFri:
                if (dow > 5) continue; break;
            case ScheduleDay::MonToSat:
                if (dow > 6) continue; break;
            case ScheduleDay::SatSun:
                if (dow < 6) continue; break;
            default:
                break;
            }
        }

        // Check time window
        QDateTime t1 = QDateTime::fromSecsSinceEpoch(entry->startTime);
        QDateTime t2 = QDateTime::fromSecsSinceEpoch(entry->endTime);
        int it1 = t1.time().hour() * 60 + t1.time().minute();
        int it2 = t2.time().hour() * 60 + t2.time().minute();
        int itn = now.time().hour() * 60 + now.time().minute();

        if (it1 <= it2) {
            // Normal timespan
            if (itn < it1 || itn >= it2)
                continue;
        } else {
            // Reversed timespan (e.g. 23:30 to 05:10)
            if (itn < it1 && itn >= it2)
                continue;
        }

        // Activate this schedule
        activateSchedule(i, entry->endTime == 0);
        open.insert(i);
    }

    // Window edges: ends first, so a window opening in the same minute wins
    std::vector<int> ended;
    std::ranges::set_difference(m_openWindows, open, std::back_inserter(ended));
    std::erase_if(m_openWindows, [&open](int i) { return !open.contains(i); });
    for (int i : ended)
        windowEnded(i);
    for (int i : open) {
        if (m_openWindows.insert(i).second)
            activateWindowStart(i, schedule(i)->endTime == 0);
    }

    const Preferences::ScheduledLimits after = thePrefs.scheduledLimits();
    const auto kb = [](const std::optional<uint32>& v) {
        return v ? QString::number(*v) : QStringLiteral("default");
    };
    if (before.usenetMaxDownloadKb != after.usenetMaxDownloadKb)
        logInfo(QStringLiteral("Scheduler: Usenet download limit %1").arg(kb(after.usenetMaxDownloadKb)));
    if (before.btMaxDownloadKb != after.btMaxDownloadKb || before.btMaxUploadKb != after.btMaxUploadKb)
        logInfo(QStringLiteral("Scheduler: BitTorrent download limit %1, upload limit %2")
                    .arg(kb(after.btMaxDownloadKb), kb(after.btMaxUploadKb)));
    if (before.maxUpload != after.maxUpload || before.maxDownload != after.maxDownload) {
        logInfo(QStringLiteral("Scheduler: upload limit %1, download limit %2")
                    .arg(after.maxUpload ? QString::number(*after.maxUpload)
                                         : QStringLiteral("default"),
                         after.maxDownload ? QString::number(*after.maxDownload)
                                           : QStringLiteral("default")));
    }

    return -1;
}

// ===========================================================================
// clearOverrides — MFC CScheduler::RestoreOriginals
// ===========================================================================

void Scheduler::clearOverrides()
{
    thePrefs.setScheduledLimits({});
}

void Scheduler::releaseNetworks()
{
    if (m_network) {
        if (m_usenetHolder >= 0)
            m_network->usenetRestore();
        if (m_btHolder >= 0)
            m_network->btRestore();
    }
    m_usenetHolder = -1;
    m_btHolder = -1;
    m_openWindows.clear();
    m_lastCheckedMinute = 60;   // next check() runs
}

void Scheduler::stop()
{
    clearOverrides();
    releaseNetworks();
}

// ===========================================================================
// activateSchedule — MFC CScheduler::ActivateSchedule
// ===========================================================================

void Scheduler::activateSchedule(int index, bool makeDefault)
{
    ScheduleEntry* entry = schedule(index);
    if (!entry)
        return;

    // makeDefault (no end time): MFC also changes the originals, so the value
    // stays once the schedule is gone. Here: the stored setting.
    Preferences::ScheduledLimits limits = thePrefs.scheduledLimits();
    for (int ai = 0; ai < 16; ++ai) {
        ScheduleAction action = entry->actions[static_cast<size_t>(ai)];
        if (action == ScheduleAction::None)
            break;

        const QString& value = entry->values[static_cast<size_t>(ai)];
        if (value.isEmpty())
            continue;

        bool ok = false;
        int intVal = value.toInt(&ok);
        if (!ok)
            continue;

        switch (action) {
        case ScheduleAction::SetUpload:
            limits.maxUpload = static_cast<uint32>(intVal);
            if (makeDefault)
                thePrefs.setMaxUpload(static_cast<uint32>(intVal));
            break;
        case ScheduleAction::SetDownload:
            limits.maxDownload = static_cast<uint32>(intVal);
            if (makeDefault)
                thePrefs.setMaxDownload(static_cast<uint32>(intVal));
            break;
        case ScheduleAction::SetSources:
            limits.maxSourcesPerFile = static_cast<uint16>(intVal);
            if (makeDefault)
                thePrefs.setMaxSourcesPerFile(static_cast<uint16>(intVal));
            break;
        case ScheduleAction::SetCon5Sec:
            limits.maxConsPerFive = static_cast<uint16>(intVal);
            break;
        case ScheduleAction::SetConnections:
            limits.maxConnections = static_cast<uint16>(intVal);
            if (makeDefault)
                thePrefs.setMaxConnections(static_cast<uint16>(intVal));
            break;
        case ScheduleAction::CatStop:
            if (m_downloadQueue)
                m_downloadQueue->stopCategory(intVal);   // MFC MP_STOP, incl. -1 / -2
            if (m_network)
                m_network->ed3kCategory(intVal, true);
            break;
        case ScheduleAction::CatResume:
            if (m_downloadQueue)
                m_downloadQueue->setCatStatus(intVal, false);
            if (m_network)
                m_network->ed3kCategory(intVal, false);
            break;
        case ScheduleAction::UsenetDownload:
            limits.usenetMaxDownloadKb = static_cast<uint32>(std::max(intVal, 0));
            break;
        case ScheduleAction::BtDownload:
            limits.btMaxDownloadKb = static_cast<uint32>(std::max(intVal, 0));
            if (makeDefault)
                thePrefs.setBtMaxDownloadKb(static_cast<uint32>(std::max(intVal, 0)));
            break;
        case ScheduleAction::BtUpload:
            limits.btMaxUploadKb = static_cast<uint32>(std::max(intVal, 0));
            if (makeDefault)
                thePrefs.setBtMaxUploadKb(static_cast<uint32>(std::max(intVal, 0)));
            break;
        default:
            break;
        }
    }
    thePrefs.setScheduledLimits(limits);
}

// ===========================================================================
// activateWindowStart — once per window: pause / resume, Usenet categories
// ===========================================================================

void Scheduler::activateWindowStart(int index, bool makeDefault)
{
    const ScheduleEntry* entry = schedule(index);
    if (!entry || !m_network)
        return;

    applyNetworkActions(index, Network::Usenet, makeDefault);
    applyNetworkActions(index, Network::Bt, makeDefault);

    for (size_t ai = 0; ai < entry->actions.size(); ++ai) {
        const ScheduleAction action = entry->actions[ai];
        if (action == ScheduleAction::None)
            break;
        if (action != ScheduleAction::UsenetCatPause && action != ScheduleAction::UsenetCatResume)
            continue;
        bool ok = false;
        const int category = entry->values[ai].toInt(&ok);
        if (ok)
            m_network->usenetCategory(category, action == ScheduleAction::UsenetCatPause);
    }
}

// ===========================================================================
// Private
// ===========================================================================

void Scheduler::windowEnded(int index)
{
    for (const Network net : {Network::Usenet, Network::Bt}) {
        int& holder = net == Network::Usenet ? m_usenetHolder : m_btHolder;
        if (holder != index)
            continue;
        holder = -1;
        if (m_network) {
            if (net == Network::Usenet)
                m_network->usenetRestore();
            else
                m_network->btRestore();
        }
        // another timed window still open for this network takes over (last one wins)
        for (auto it = m_openWindows.rbegin(); it != m_openWindows.rend(); ++it) {
            const ScheduleEntry* e = schedule(*it);
            if (e && e->endTime != 0 && entryTouches(*it, net)) {
                applyNetworkActions(*it, net, false);
                break;
            }
        }
    }
}

bool Scheduler::entryTouches(int index, Network net) const
{
    const ScheduleEntry* entry = schedule(index);
    if (!entry)
        return false;
    const ScheduleAction pause = net == Network::Usenet ? ScheduleAction::UsenetPause : ScheduleAction::BtPause;
    const ScheduleAction resume = net == Network::Usenet ? ScheduleAction::UsenetResume : ScheduleAction::BtResume;
    for (const ScheduleAction action : entry->actions) {
        if (action == ScheduleAction::None)
            break;
        if (action == pause || action == resume)
            return true;
    }
    return false;
}

void Scheduler::applyNetworkActions(int index, Network net, bool makeDefault)
{
    const ScheduleEntry* entry = schedule(index);
    if (!entry || !m_network)
        return;
    const ScheduleAction pause = net == Network::Usenet ? ScheduleAction::UsenetPause : ScheduleAction::BtPause;
    const ScheduleAction resume = net == Network::Usenet ? ScheduleAction::UsenetResume : ScheduleAction::BtResume;

    // the entry's last pause/resume for this network decides
    std::optional<bool> paused;
    for (const ScheduleAction action : entry->actions) {
        if (action == ScheduleAction::None)
            break;
        if (action == pause)
            paused = true;
        else if (action == resume)
            paused = false;
    }
    if (!paused)
        return;

    const bool usenet = net == Network::Usenet;
    logInfo(QStringLiteral("Scheduler: %1 %2 (%3)")
                .arg(usenet ? QStringLiteral("Usenet") : QStringLiteral("BitTorrent"),
                     *paused ? QStringLiteral("paused") : QStringLiteral("resumed"), entry->title));
    if (usenet)
        m_network->usenetPause(*paused, makeDefault);
    else
        m_network->btPause(*paused, makeDefault);
    // a saved change has nothing to undo
    (usenet ? m_usenetHolder : m_btHolder) = makeDefault ? -1 : index;
}

} // namespace eMule
