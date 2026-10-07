#include "pch.h"
/// @file ApiRegistry.cpp
/// @brief REST operation table — validation and dispatch.

#include "webserver/ApiRegistry.h"

#include <QJsonDocument>
#include <QPromise>
#include <QRegularExpression>

#include <cmath>

namespace eMule::api {

namespace {

[[nodiscard]] Registry::Parsed fail(int code, const QString& message)
{
    return {false, error(code, message), {}};
}

/// Text from the path or the query, converted to the declared type.
[[nodiscard]] QJsonValue fromText(const Param& p, const QString& text, bool& ok)
{
    ok = true;
    switch (p.type) {
    case ParamType::String:
        return text;
    case ParamType::Integer: {
        const qint64 v = text.toLongLong(&ok);
        return ok ? QJsonValue(v) : QJsonValue();
    }
    case ParamType::Number: {
        const double v = text.toDouble(&ok);
        return ok ? QJsonValue(v) : QJsonValue();
    }
    case ParamType::Boolean:
        if (text == QLatin1StringView("true") || text == QLatin1StringView("1"))
            return true;
        if (text == QLatin1StringView("false") || text == QLatin1StringView("0"))
            return false;
        ok = false;
        return {};
    case ParamType::Array:
        // a,b,c
        return QJsonArray::fromStringList(text.split(u',', Qt::SkipEmptyParts));
    case ParamType::Object:
        break;
    }
    ok = false;
    return {};
}

[[nodiscard]] bool hasType(ParamType type, const QJsonValue& v)
{
    switch (type) {
    case ParamType::String:  return v.isString();
    case ParamType::Integer: return v.isDouble() && v.toDouble() == std::floor(v.toDouble());
    case ParamType::Number:  return v.isDouble();
    case ParamType::Boolean: return v.isBool();
    case ParamType::Array:   return v.isArray();
    case ParamType::Object:  return v.isObject();
    }
    return false;
}

/// Empty when @p v is acceptable for @p p.
[[nodiscard]] QString check(const Param& p, const QJsonValue& v)
{
    if (!hasType(p.type, v))
        return QStringLiteral("'%1' must be %2").arg(p.name, typeName(p.type));

    if (p.type == ParamType::Array) {
        const QJsonArray items = v.toArray();
        for (const QJsonValue& item : items) {
            if (!hasType(p.itemType, item))
                return QStringLiteral("every item of '%1' must be %2")
                    .arg(p.name, typeName(p.itemType));
        }
    }

    const bool numeric = p.type == ParamType::Integer || p.type == ParamType::Number;
    const double measure = numeric ? v.toDouble()
                         : p.type == ParamType::String ? double(v.toString().size())
                         : p.type == ParamType::Array ? double(v.toArray().size())
                         : 0.0;
    const QLatin1StringView what = numeric ? QLatin1StringView("")
                                 : p.type == ParamType::Array ? QLatin1StringView(" items")
                                                              : QLatin1StringView(" characters");
    if (p.min && measure < *p.min)
        return QStringLiteral("'%1' must be at least %2%3").arg(p.name).arg(*p.min).arg(what);
    if (p.max && measure > *p.max)
        return QStringLiteral("'%1' must be at most %2%3").arg(p.name).arg(*p.max).arg(what);

    if (!p.enumValues.isEmpty() && !p.enumValues.contains(v.toString()))
        return QStringLiteral("'%1' must be one of: %2")
            .arg(p.name, p.enumValues.join(QStringLiteral(", ")));
    return {};
}

[[nodiscard]] const Param* findParam(const Operation& op, QStringView name, ParamIn in)
{
    for (const Param& p : op.params) {
        if (p.in == in && p.name == name)
            return &p;
    }
    return nullptr;
}

[[nodiscard]] const Param* findParam(const Operation& op, QStringView name)
{
    for (const Param& p : op.params) {
        if (p.name == name)
            return &p;
    }
    return nullptr;
}

} // namespace

QLatin1StringView methodName(Method m)
{
    switch (m) {
    case Method::Get:    return QLatin1StringView("get");
    case Method::Post:   return QLatin1StringView("post");
    case Method::Put:    return QLatin1StringView("put");
    case Method::Patch:  return QLatin1StringView("patch");
    case Method::Delete: return QLatin1StringView("delete");
    }
    return QLatin1StringView("get");
}

QLatin1StringView typeName(ParamType t)
{
    switch (t) {
    case ParamType::String:  return QLatin1StringView("a string");
    case ParamType::Integer: return QLatin1StringView("an integer");
    case ParamType::Number:  return QLatin1StringView("a number");
    case ParamType::Boolean: return QLatin1StringView("true or false");
    case ParamType::Array:   return QLatin1StringView("an array");
    case ParamType::Object:  return QLatin1StringView("an object");
    }
    return QLatin1StringView("a value");
}

void Registry::add(Operation op)
{
    if (op.is(Paged)) {
        op.params.append(queryInt(QStringLiteral("limit"),
                                  QStringLiteral("Rows per page."))
                             .range(1, kMaxPageSize).def(kDefaultPageSize));
        op.params.append(queryInt(QStringLiteral("offset"),
                                  QStringLiteral("Rows to skip.")).atLeast(0).def(0));
    }
    if (op.is(Destructive)) {
        // DELETE has no body worth relying on
        Param confirm = param(op.method == Method::Delete ? ParamIn::Query : ParamIn::Body,
                              ParamType::Boolean, QStringLiteral("confirm"),
                              QStringLiteral("Must be true. Guards against an accidental call; "
                                             "this cannot be undone."));
        confirm.required = true;
        op.params.append(confirm);
    }
    m_operations.append(std::move(op));
}

const Operation* Registry::findById(QStringView id) const
{
    for (const Operation& op : m_operations) {
        if (op.id == id)
            return &op;
    }
    return nullptr;
}

const Operation* Registry::findTool(QStringView tool) const
{
    if (tool.isEmpty())
        return nullptr;
    for (const Operation& op : m_operations) {
        if (op.mcpTool == tool)
            return &op;
    }
    return nullptr;
}

Registry::Parsed Registry::parseHttp(const Operation& op, const QStringList& pathValues,
                                     const QUrlQuery& query, const QByteArray& body) const
{
    QJsonObject args;

    const QStringList names = pathParamNames(op.path);
    for (qsizetype i = 0; i < names.size() && i < pathValues.size(); ++i) {
        const Param* p = findParam(op, names.at(i), ParamIn::Path);
        if (!p)
            continue;
        bool ok = false;
        const QJsonValue v = fromText(*p, pathValues.at(i), ok);
        if (!ok)
            return fail(400, QStringLiteral("'%1' must be %2").arg(p->name, typeName(p->type)));
        args.insert(p->name, v);
    }

    // RawBody: the handler takes the request apart itself, query string included.
    const auto items = op.is(RawBody) ? QList<std::pair<QString, QString>>{}
                                      : query.queryItems(QUrl::FullyDecoded);
    for (const auto& [key, text] : items) {
        const Param* p = findParam(op, key, ParamIn::Query);
        if (!p)
            return fail(400, QStringLiteral("Unknown query parameter '%1'").arg(key));
        bool ok = false;
        const QJsonValue v = fromText(*p, text, ok);
        if (!ok)
            return fail(400, QStringLiteral("'%1' must be %2").arg(p->name, typeName(p->type)));
        args.insert(p->name, v);
    }

    if (!op.is(RawBody)) {
        const bool takesBody = std::ranges::any_of(
            op.params, [](const Param& p) { return p.in == ParamIn::Body; });
        if (!body.trimmed().isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(body);
            if (!doc.isObject())
                return fail(400, QStringLiteral("Invalid JSON body"));
            const QJsonObject object = doc.object();
            for (auto it = object.begin(); it != object.end(); ++it) {
                if (!op.is(FreeBody) && (!takesBody || !findParam(op, it.key(), ParamIn::Body)))
                    return fail(400, QStringLiteral("Unknown field '%1'").arg(it.key()));
                args.insert(it.key(), it.value());
            }
        }
    }
    return finish(op, std::move(args));
}

Registry::Parsed Registry::parseArgs(const Operation& op, const QJsonObject& args) const
{
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (!op.is(FreeBody) && !findParam(op, it.key()))
            return fail(400, QStringLiteral("Unknown argument '%1'").arg(it.key()));
    }
    return finish(op, args);
}

QFuture<Result> Registry::invoke(const Operation& op, const Call& call) const
{
    if (op.asyncHandler)
        return op.asyncHandler(call);
    if (op.handler)
        return finished(op.handler(call));
    return finished(error(501, QStringLiteral("Not implemented")));
}

QStringList Registry::pathParamNames(const QString& path)
{
    static const QRegularExpression rx(QStringLiteral("\\{([^}]+)\\}"));
    QStringList names;
    auto it = rx.globalMatch(path);
    while (it.hasNext())
        names.append(it.next().captured(1));
    return names;
}

QFuture<Result> Registry::finished(Result result)
{
    QPromise<Result> promise;
    QFuture<Result> future = promise.future();
    promise.start();
    promise.addResult(std::move(result));
    promise.finish();
    return future;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

Registry::Parsed Registry::finish(const Operation& op, QJsonObject args) const
{
    for (const Param& p : op.params) {
        if (!args.contains(p.name)) {
            if (p.required)
                return fail(400, QStringLiteral("Missing '%1'").arg(p.name));
            if (!p.defaultValue.isUndefined())
                args.insert(p.name, p.defaultValue);
            continue;
        }
        if (const QString problem = check(p, args.value(p.name)); !problem.isEmpty())
            return fail(400, problem);
    }
    if (op.is(Destructive) && !args.value(QLatin1StringView("confirm")).toBool())
        return fail(400, QStringLiteral("Set 'confirm' to true to carry this out"));
    return {true, {}, args};
}

} // namespace eMule::api
