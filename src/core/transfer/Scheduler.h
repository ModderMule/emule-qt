#pragma once

/// @file Scheduler.h
/// @brief Time-based scheduling of speed limits — port of MFC CScheduler.
///
/// Allows users to define time-based rules that override preferences
/// such as upload/download speed, connections, and sources. Beyond MFC it also
/// pauses / resumes / caps Usenet and BitTorrent (ids from 32 up).

#include "utils/Types.h"

#include <QObject>
#include <QString>

#include <array>
#include <ctime>
#include <memory>
#include <set>
#include <vector>

namespace eMule {

class DownloadQueue;

/// Action codes for schedule entries.
enum class ScheduleAction : int {
    None        = 0,
    SetUpload   = 1,
    SetDownload = 2,
    SetSources  = 3,
    SetCon5Sec  = 4,
    SetConnections = 5,
    CatStop     = 6,
    CatResume   = 7,
    // 8-16 are MorphXT's (ACTION_USSMAXPING..ACTION_RELOAD): left out on purpose
    UsenetPause     = 32,
    UsenetResume    = 33,
    UsenetDownload  = 34,   // KB/s, 0 = no cap
    UsenetCatPause  = 35,   // value: category, -1 all, -2 uncategorized
    UsenetCatResume = 36,
    BtPause         = 37,
    BtResume        = 38,
    BtDownload      = 39,   // KB/s, 0 = unlimited
    BtUpload        = 40
};

/// An id this build knows; anything else loads as None.
[[nodiscard]] bool isKnownScheduleAction(int id);

/// Actions that take no value (pause / resume a network).
[[nodiscard]] bool scheduleActionHasValue(ScheduleAction action);

/// What the scheduler does to networks the core can't see; the daemon implements it.
/// Non-persistent changes are runtime only and undone by the matching restore.
class ScheduleNetworkActions {
public:
    virtual ~ScheduleNetworkActions() = default;
    virtual void usenetPause(bool paused, bool persistent) = 0;
    virtual void usenetRestore() = 0;
    virtual void usenetCategory(int category, bool paused) = 0;
    virtual void btPause(bool paused, bool persistent) = 0;
    virtual void btRestore() = 0;
    /// eMule's Stop/Resume Category, applied to ed3k downloads too
    virtual void ed3kCategory(int category, bool paused) = 0;
};

/// Day-of-week codes.
enum class ScheduleDay : uint32 {
    Daily    = 0,
    Monday   = 1,
    Tuesday  = 2,
    Wednesday = 3,
    Thursday = 4,
    Friday   = 5,
    Saturday = 6,
    Sunday   = 7,
    MonToFri = 8,
    MonToSat = 9,
    SatSun   = 10
};

/// A single schedule entry with up to 16 actions.
struct ScheduleEntry {
    QString title;
    time_t startTime = 0;
    time_t endTime = 0;
    ScheduleDay day = ScheduleDay::Daily;
    std::array<ScheduleAction, 16> actions{};
    std::array<QString, 16> values{};
    bool enabled = false;

    void resetActions();
};

class Scheduler : public QObject {
    Q_OBJECT
public:
    explicit Scheduler(QObject* parent = nullptr);
    ~Scheduler() override;

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // Schedule management
    int addSchedule(std::unique_ptr<ScheduleEntry> entry);
    void updateSchedule(int index, std::unique_ptr<ScheduleEntry> entry);
    [[nodiscard]] ScheduleEntry* schedule(int index) const;
    void removeSchedule(int index);
    void removeAll();
    [[nodiscard]] int count() const;

    // Persistence
    int loadFromFile(const QString& configDir);
    void saveToFile(const QString& configDir);

    // Runtime — called periodically
    int check(bool forceCheck = false);

    // State management. Limits go into the Preferences scheduler layer, so the
    // stored values stay the user's own and come back when no schedule applies.
    void clearOverrides();
    void activateSchedule(int index, bool makeDefault = false);

    /// Pause / resume actions fire once when a window opens (a manual click inside
    /// the window then sticks) and are undone when it closes.
    void activateWindowStart(int index, bool makeDefault);

    /// Undoes every runtime network change and forgets the open windows, so the
    /// next check() starts them again. Called when schedules change or stop.
    void releaseNetworks();

    /// clearOverrides() + releaseNetworks(): no schedule applies any more
    void stop();

    // Component access
    void setDownloadQueue(DownloadQueue* dq) { m_downloadQueue = dq; }
    void setNetworkActions(ScheduleNetworkActions* actions) { m_network = actions; }

private:
    enum class Network { Usenet, Bt };
    void windowEnded(int index);
    [[nodiscard]] bool entryTouches(int index, Network net) const;
    void applyNetworkActions(int index, Network net, bool makeDefault);

    std::vector<std::unique_ptr<ScheduleEntry>> m_schedules;
    DownloadQueue* m_downloadQueue = nullptr;
    ScheduleNetworkActions* m_network = nullptr;
    int m_lastCheckedMinute = 60;
    std::set<int> m_openWindows;   // entries whose window was open at the last check
    int m_usenetHolder = -1;       // entry whose runtime Usenet pause/resume is in force
    int m_btHolder = -1;
};

} // namespace eMule
