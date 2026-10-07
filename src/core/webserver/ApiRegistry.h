#pragma once

/// @file ApiRegistry.h
/// @brief The table of REST operations, and the validation every call goes through.

#include "webserver/ApiTypes.h"

#include <QByteArray>
#include <QUrlQuery>

namespace eMule::api {

class Registry {
public:
    /// Adds @p op. A Paged operation gains limit/offset, a Destructive one "confirm".
    void add(Operation op);

    [[nodiscard]] const QList<Operation>& operations() const { return m_operations; }
    [[nodiscard]] const Operation* findById(QStringView id) const;
    [[nodiscard]] const Operation* findTool(QStringView tool) const;

    struct Parsed {
        bool ok = false;
        Result failure;     ///< set when !ok
        QJsonObject args;
    };

    /// An HTTP request: @p pathValues in the order the path declares them.
    [[nodiscard]] Parsed parseHttp(const Operation& op, const QStringList& pathValues,
                                   const QUrlQuery& query, const QByteArray& body) const;

    /// One JSON object holding every parameter, as an MCP tool call sends it.
    [[nodiscard]] Parsed parseArgs(const Operation& op, const QJsonObject& args) const;

    /// Runs the handler; a synchronous one comes back as a finished future.
    [[nodiscard]] QFuture<Result> invoke(const Operation& op, const Call& call) const;

    /// "/api/v1/x/{id}" -> names of the path parameters, in order.
    [[nodiscard]] static QStringList pathParamNames(const QString& path);

    [[nodiscard]] static QFuture<Result> finished(Result result);

private:
    [[nodiscard]] Parsed finish(const Operation& op, QJsonObject args) const;

    QList<Operation> m_operations;
};

[[nodiscard]] QLatin1StringView methodName(Method m);
[[nodiscard]] QLatin1StringView typeName(ParamType t);

} // namespace eMule::api
