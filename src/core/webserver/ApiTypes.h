#pragma once

/// @file ApiTypes.h
/// @brief One REST operation, declared once: route, parameters, handler.
///
/// The same declaration drives HTTP routing and validation, the OpenAPI
/// document and the MCP tool list, so the three cannot disagree.

#include <QFuture>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <functional>
#include <optional>

class QHttpServerRequest;

namespace eMule::api {

inline constexpr int kContractVersion = 1;
inline constexpr int kDefaultPageSize = 100;
inline constexpr int kMaxPageSize = 1000;

enum class Method { Get, Post, Put, Patch, Delete };
enum class ParamIn { Path, Query, Body };
enum class ParamType { String, Integer, Number, Boolean, Array, Object };

/// One input of an operation. Path and query values arrive as text and are
/// converted to @c type; body values must already have it.
struct Param {
    QString name;
    ParamIn in = ParamIn::Query;
    ParamType type = ParamType::String;
    QString description;
    bool required = false;
    std::optional<double> min;      ///< numbers: value; strings and arrays: length
    std::optional<double> max;
    QStringList enumValues;
    QJsonValue defaultValue = QJsonValue(QJsonValue::Undefined);
    ParamType itemType = ParamType::String;   ///< for arrays

    Param& req() { required = true; return *this; }
    Param& range(double lo, double hi) { min = lo; max = hi; return *this; }
    Param& atLeast(double lo) { min = lo; return *this; }
    Param& oneOf(const QStringList& values) { enumValues = values; return *this; }
    Param& def(const QJsonValue& value) { defaultValue = value; return *this; }
    Param& of(ParamType item) { itemType = item; return *this; }
};

[[nodiscard]] inline Param param(ParamIn in, ParamType type, const QString& name,
                                 const QString& description)
{
    Param p;
    p.name = name;
    p.in = in;
    p.type = type;
    p.description = description;
    p.required = in == ParamIn::Path;
    return p;
}

[[nodiscard]] inline Param pathStr(const QString& n, const QString& d) { return param(ParamIn::Path, ParamType::String, n, d); }
[[nodiscard]] inline Param pathInt(const QString& n, const QString& d) { return param(ParamIn::Path, ParamType::Integer, n, d); }
[[nodiscard]] inline Param queryStr(const QString& n, const QString& d) { return param(ParamIn::Query, ParamType::String, n, d); }
[[nodiscard]] inline Param queryInt(const QString& n, const QString& d) { return param(ParamIn::Query, ParamType::Integer, n, d); }
[[nodiscard]] inline Param queryBool(const QString& n, const QString& d) { return param(ParamIn::Query, ParamType::Boolean, n, d); }
[[nodiscard]] inline Param bodyStr(const QString& n, const QString& d) { return param(ParamIn::Body, ParamType::String, n, d); }
[[nodiscard]] inline Param bodyInt(const QString& n, const QString& d) { return param(ParamIn::Body, ParamType::Integer, n, d); }
[[nodiscard]] inline Param bodyNum(const QString& n, const QString& d) { return param(ParamIn::Body, ParamType::Number, n, d); }
[[nodiscard]] inline Param bodyBool(const QString& n, const QString& d) { return param(ParamIn::Body, ParamType::Boolean, n, d); }
[[nodiscard]] inline Param bodyArray(const QString& n, const QString& d) { return param(ParamIn::Body, ParamType::Array, n, d); }
[[nodiscard]] inline Param bodyObject(const QString& n, const QString& d) { return param(ParamIn::Body, ParamType::Object, n, d); }

/// What a handler answers: an HTTP status and a JSON body.
struct Result {
    int status = 200;
    QJsonValue body;

    [[nodiscard]] bool ok() const { return status >= 200 && status < 300; }
};

[[nodiscard]] inline Result ok(const QJsonObject& body) { return {200, body}; }
[[nodiscard]] inline Result ok(const QJsonArray& body) { return {200, body}; }
[[nodiscard]] inline Result accepted(const QJsonObject& body) { return {202, body}; }

/// The one error envelope: {"error": {"code": N, "message": "..."}}.
[[nodiscard]] inline Result error(int code, const QString& message)
{
    return {code, QJsonObject{{QStringLiteral("error"),
                               QJsonObject{{QStringLiteral("code"), code},
                                           {QStringLiteral("message"), message}}}}};
}

/// A validated call. @c args holds every declared parameter that was sent (or has
/// a default), whatever part of the request it came from.
struct Call {
    QJsonObject args;
    const QHttpServerRequest* request = nullptr;   ///< null when called as an MCP tool

    [[nodiscard]] bool has(QLatin1StringView name) const { return args.contains(name); }
    [[nodiscard]] QString str(QLatin1StringView name) const { return args.value(name).toString(); }
    [[nodiscard]] qint64 integer(QLatin1StringView name, qint64 fallback = 0) const
    {
        const QJsonValue v = args.value(name);
        return v.isDouble() ? static_cast<qint64>(v.toDouble()) : fallback;
    }
    [[nodiscard]] bool flag(QLatin1StringView name) const { return args.value(name).toBool(); }
    [[nodiscard]] int limit() const { return static_cast<int>(integer(QLatin1StringView("limit"), kDefaultPageSize)); }
    [[nodiscard]] int offset() const { return static_cast<int>(integer(QLatin1StringView("offset"), 0)); }
};

using Handler = std::function<Result(const Call&)>;
using AsyncHandler = std::function<QFuture<Result>(const Call&)>;

enum Flag : int {
    ReadOnly    = 1 << 0,   ///< changes nothing
    Destructive = 1 << 1,   ///< needs "confirm": true
    NoAuth      = 1 << 2,   ///< answered without an API key
    RawStream   = 1 << 3,   ///< documented only: routed and authenticated elsewhere
    RawBody     = 1 << 4,   ///< the handler reads the request body itself
    Paged       = 1 << 5,   ///< takes limit/offset, answers {items,total,offset,limit}
    NoMcpWrite  = 1 << 6,   ///< never offered as an MCP tool
    FreeBody    = 1 << 7,   ///< the body is a free-form object; the handler checks its keys
};

struct Operation {
    QString id;             ///< "downloads.list"
    Method method = Method::Get;
    QString path;           ///< "/api/v1/downloads/{hash}"
    QString tag;            ///< OpenAPI group
    QString summary;
    QString description;
    QList<Param> params;
    int flags = 0;
    QString mcpTool;        ///< tool name; empty = not an MCP tool
    QJsonObject resultSchema;
    Handler handler;
    AsyncHandler asyncHandler;

    [[nodiscard]] bool is(Flag f) const { return (flags & f) != 0; }
};

/// The page envelope every list answers with. @p rowAt is called only for the
/// rows inside the window.
template <typename RowAt>
[[nodiscard]] Result page(const Call& call, qsizetype total, RowAt&& rowAt)
{
    const qsizetype offset = std::clamp<qsizetype>(call.offset(), 0, std::max<qsizetype>(total, 0));
    const qsizetype limit = std::clamp<qsizetype>(call.limit(), 1, kMaxPageSize);
    QJsonArray items;
    for (qsizetype i = offset; i < total && i < offset + limit; ++i)
        items.append(rowAt(i));
    return ok(QJsonObject{{QStringLiteral("items"), items},
                          {QStringLiteral("total"), static_cast<qint64>(total)},
                          {QStringLiteral("offset"), static_cast<qint64>(offset)},
                          {QStringLiteral("limit"), static_cast<qint64>(limit)}});
}

[[nodiscard]] inline Result pageOf(const Call& call, const QJsonArray& rows)
{
    return page(call, rows.size(), [&rows](qsizetype i) { return rows.at(i); });
}

} // namespace eMule::api
