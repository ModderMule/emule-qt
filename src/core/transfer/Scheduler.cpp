#include "pch.h"
/// @file Scheduler.cpp
/// @brief Time-based scheduling of speed limits — port of MFC Scheduler.cpp.
///
/// Uses QSettings for INI persistence and QDateTime for time checks.

#include "transfer/Scheduler.h"
#include "transfer/DownloadQueue.h"
#include "prefs/Preferences.h"

#include <QDateTime>
#include <QSettings>

namespace eMule {

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
    if (index >= 0 && index < static_cast<int>(m_schedules.size()))
        m_schedules.erase(m_schedules.begin() + index);
}

void Scheduler::removeAll()
{
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

        for (int a = 0; a < 16; ++a) {
            entry->actions[static_cast<size_t>(a)] = static_cast<ScheduleAction>(
                ini.value(QStringLiteral("Action%1").arg(a), 0).toInt());
            entry->values[static_cast<size_t>(a)] =
                ini.value(QStringLiteral("Value%1").arg(a)).toString();
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
    if (count() == 0)
        return -1;

    const QDateTime now = QDateTime::currentDateTime();
    const int currentMinute = now.time().minute();

    if (!forceCheck && currentMinute == m_lastCheckedMinute)
        return -1;

    m_lastCheckedMinute = currentMinute;
    restoreOriginals();

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
    }

    return -1;
}

// ===========================================================================
// saveOriginals / restoreOriginals
// ===========================================================================

void Scheduler::saveOriginals()
{
    m_originalUpload = thePrefs.maxUpload();
    m_originalDownload = thePrefs.maxDownload();
    m_originalConnections = thePrefs.maxConnections();
    m_originalSources = thePrefs.maxSourcesPerFile();
    m_originalConsPerFive = thePrefs.maxConsPerFive();
}

void Scheduler::restoreOriginals()
{
    thePrefs.setMaxUpload(m_originalUpload);
    thePrefs.setMaxDownload(m_originalDownload);
    thePrefs.setMaxConnections(m_originalConnections);
    thePrefs.setMaxSourcesPerFile(m_originalSources);
    thePrefs.setMaxConsPerFive(m_originalConsPerFive);
}

// ===========================================================================
// activateSchedule — MFC CScheduler::ActivateSchedule
// ===========================================================================

void Scheduler::activateSchedule(int index, bool makeDefault)
{
    ScheduleEntry* entry = schedule(index);
    if (!entry)
        return;

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
            thePrefs.setMaxUpload(static_cast<uint32>(intVal));
            if (makeDefault)
                m_originalUpload = static_cast<uint32>(intVal);
            break;
        case ScheduleAction::SetDownload:
            thePrefs.setMaxDownload(static_cast<uint32>(intVal));
            if (makeDefault)
                m_originalDownload = static_cast<uint32>(intVal);
            break;
        case ScheduleAction::SetSources:
            thePrefs.setMaxSourcesPerFile(static_cast<uint16>(intVal));
            if (makeDefault)
                m_originalSources = static_cast<uint16>(intVal);
            break;
        case ScheduleAction::SetCon5Sec:
            thePrefs.setMaxConsPerFive(static_cast<uint16>(intVal));
            break;
        case ScheduleAction::SetConnections:
            thePrefs.setMaxConnections(static_cast<uint16>(intVal));
            if (makeDefault)
                m_originalConnections = static_cast<uint16>(intVal);
            break;
        case ScheduleAction::CatStop:
            if (m_downloadQueue)
                m_downloadQueue->setCatStatus(static_cast<uint32>(intVal), true);
            break;
        case ScheduleAction::CatResume:
            if (m_downloadQueue)
                m_downloadQueue->setCatStatus(static_cast<uint32>(intVal), false);
            break;
        default:
            break;
        }
    }
}

} // namespace eMule
