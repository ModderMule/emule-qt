#pragma once

/// @file Scheduler.h
/// @brief Time-based scheduling of speed limits — port of MFC CScheduler.
///
/// Allows users to define time-based rules that override preferences
/// such as upload/download speed, connections, and sources.

#include "utils/Types.h"

#include <QObject>
#include <QString>

#include <array>
#include <ctime>
#include <memory>
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
    CatResume   = 7
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

    // State management
    void saveOriginals();
    void restoreOriginals();
    void activateSchedule(int index, bool makeDefault = false);

    // Component access
    void setDownloadQueue(DownloadQueue* dq) { m_downloadQueue = dq; }

private:
    std::vector<std::unique_ptr<ScheduleEntry>> m_schedules;
    DownloadQueue* m_downloadQueue = nullptr;
    int m_lastCheckedMinute = 60;

    // Original preference values (saved before schedule overrides)
    uint32 m_originalUpload = 0;
    uint32 m_originalDownload = 0;
    uint16 m_originalConnections = 0;
    uint16 m_originalSources = 0;
    uint16 m_originalConsPerFive = 0;
};

} // namespace eMule
