#pragma once

/// @file PreferenceSchema.h
/// @brief Metadata for the preferences a remote client may read and change.
///
/// One table: type, range, unit and a description per key. It answers
/// GET /preferences/schema, validates REST PATCH and range-checks the IPC setter.
/// Values are read through Preferences::toIpcMap(), so a key here is always an
/// IPC key too. Credentials and anything naming a program to run are not listed
/// and so cannot be reached over REST or MCP.

#include <QCborValue>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

namespace eMule {

class Preferences;

struct PrefSpec {
    enum class Kind { Bool, Int, String };

    QString key;
    Kind kind = Kind::Bool;
    QString description;
    std::optional<double> min;      ///< Int: value; String: length
    std::optional<double> max;
    QString unit;
    bool restartRequired = false;
    bool advanced = false;
    bool writable = true;           ///< false: shown, but not changeable remotely
};

[[nodiscard]] const QList<PrefSpec>& preferenceSchema();
[[nodiscard]] const PrefSpec* findPreferenceSpec(QStringView key);

/// The schema as JSON, each row with its default.
[[nodiscard]] QJsonArray preferenceSchemaJson();

/// Current values of the schema's keys.
[[nodiscard]] QJsonObject preferenceValues(const Preferences& prefs);

/// Empty when @p value may be stored under @p spec.
[[nodiscard]] QString preferenceProblem(const PrefSpec& spec, const QCborValue& value);

using PrefChanges = QList<std::pair<QString, QCborValue>>;

/// Checks a REST PATCH body: every key known, writable, of the right type and in
/// range. Returns the problem, or empty with @p out filled.
[[nodiscard]] QString checkPreferencePatch(const QJsonObject& body, PrefChanges& out);

} // namespace eMule
