#pragma once

/// @file OpenApiWriter.h
/// @brief The OpenAPI 3.1 document and JSON Schemas, generated from the operation table.

#include "webserver/ApiRegistry.h"

#include <QJsonObject>

#include <initializer_list>
#include <utility>

namespace eMule::api {

// Small JSON Schema builders, for Operation::resultSchema.
[[nodiscard]] QJsonObject schemaString(const QString& description = {});
[[nodiscard]] QJsonObject schemaInteger(const QString& description = {});
[[nodiscard]] QJsonObject schemaNumber(const QString& description = {});
[[nodiscard]] QJsonObject schemaBoolean(const QString& description = {});
[[nodiscard]] QJsonObject schemaObject(std::initializer_list<std::pair<QString, QJsonObject>> properties);
/// A named row type from componentSchemas().
[[nodiscard]] QJsonObject schemaRef(const QString& component);
/// The list envelope {items, total, offset, limit} around a named row type.
[[nodiscard]] QJsonObject schemaPage(const QString& component);

/// The row types the API returns, keyed by name.
[[nodiscard]] QJsonObject componentSchemas();

/// JSON Schema of one parameter.
[[nodiscard]] QJsonObject paramSchema(const Param& param);

/// One object schema holding every parameter of @p op: an MCP tool's inputSchema.
[[nodiscard]] QJsonObject inputSchema(const Operation& op);

[[nodiscard]] QJsonObject buildOpenApi(const Registry& registry, const QString& version);

} // namespace eMule::api
