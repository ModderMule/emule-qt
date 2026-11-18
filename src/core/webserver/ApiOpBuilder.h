#pragma once

/// @file ApiOpBuilder.h
/// @brief Shorthands the WebServer*.cpp operation tables share.

#include "webserver/ApiRegistry.h"

#include <QString>

namespace eMule::api::build {

[[nodiscard]] inline QString s(const char* text) { return QString::fromUtf8(text); }

[[nodiscard]] inline Operation make(const char* id, Method method, const char* path, const char* tag,
                                    const char* summary, const char* description = "")
{
    Operation op;
    op.id = s(id);
    op.method = method;
    op.path = s(path);
    op.tag = s(tag);
    op.summary = s(summary);
    op.description = s(description);
    return op;
}

} // namespace eMule::api::build
