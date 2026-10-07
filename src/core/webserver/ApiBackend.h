#pragma once

/// @file ApiBackend.h
/// @brief What the REST API needs from the daemon and core cannot reach itself.
///
/// The log ring, the category store (it renumbers Usenet and feed entries too),
/// the preference setters and shutdown all live in the daemon. Same seam as
/// UsenetWebBackend: core declares it, DaemonApp installs the implementation.

#include "app/CoreOps.h"
#include "prefs/PreferenceSchema.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace eMule {

class ApiBackend {
public:
    virtual ~ApiBackend() = default;

    struct LogRecord {
        qint64 id = 0;          ///< rises by one per record; restarts with the daemon
        QString category;
        int severity = 0;       ///< QtMsgType
        QString message;
        qint64 timestamp = 0;   ///< unix seconds
    };

    /// Records with an id above @p sinceId, oldest first.
    [[nodiscard]] virtual QList<LogRecord> logs(qint64 sinceId) const = 0;

    /// Download categories, index order; entry 0 is the default category.
    [[nodiscard]] virtual QJsonArray categories() const = 0;

    /// Replaces the list. An entry carrying `oldIndex` is an existing category
    /// (moved or edited); one without is new; a missing old index was deleted.
    virtual ops::Status setCategories(const QJsonArray& categories) = 0;

    /// Stores @p changes exactly as a GUI save does, side effects included.
    virtual ops::Status applyPreferences(const PrefChanges& changes) = 0;

    /// Quits the daemon once the reply is out.
    virtual void requestShutdown() = 0;
};

} // namespace eMule
