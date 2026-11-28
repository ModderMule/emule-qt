#pragma once

/// @file ArrWanted.h
/// @brief Polls the Wanted > Missing list of each configured Sonarr/Radarr
///        (Preferences::arrInstances) and hands the titles to ArrSearchBridge.

#include "prefs/ArrInstance.h"
#include "webserver/ArrCompat.h"

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>

namespace eMule {

class ArrSearchBridge;

class ArrWanted : public QObject {
    Q_OBJECT

public:
    ArrWanted(ArrSearchBridge* bridge, QObject* parent = nullptr);

    /// Reads arrApiEnabled, arrInstances and arrWantedPollMinutes; polls at once
    /// when that turned the feed on.
    void applyPreferences();

    /// Episodes / movies asked per instance and poll.
    static constexpr int kItemsPerInstance = 20;

private:
    void poll();
    void collect(const QList<arr::WantedTerm>& terms);

    ArrSearchBridge* m_bridge;
    QNetworkAccessManager m_nam;
    QTimer m_timer;
    QList<arr::WantedTerm> m_round;   ///< answers of the poll in progress
    QList<ArrInstance> m_instances;   ///< as last applied: a change polls soon
    int m_pending = 0;
    bool m_on = false;
};

} // namespace eMule
