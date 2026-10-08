#include "pch.h"
/// @file OpenApiWriter.cpp
/// @brief OpenAPI document generator.

#include "webserver/OpenApiWriter.h"

#include <QJsonArray>

namespace eMule::api {

namespace {

[[nodiscard]] QString s(const char* text) { return QString::fromUtf8(text); }

[[nodiscard]] QJsonObject typed(const char* type, const QString& description)
{
    QJsonObject o{{QStringLiteral("type"), s(type)}};
    if (!description.isEmpty())
        o.insert(QStringLiteral("description"), description);
    return o;
}

[[nodiscard]] QLatin1StringView jsonType(ParamType t)
{
    switch (t) {
    case ParamType::String:  return QLatin1StringView("string");
    case ParamType::Integer: return QLatin1StringView("integer");
    case ParamType::Number:  return QLatin1StringView("number");
    case ParamType::Boolean: return QLatin1StringView("boolean");
    case ParamType::Array:   return QLatin1StringView("array");
    case ParamType::Object:  return QLatin1StringView("object");
    }
    return QLatin1StringView("string");
}

[[nodiscard]] QJsonObject errorResponse(const char* description)
{
    return QJsonObject{
        {s("description"), s(description)},
        {s("content"), QJsonObject{{s("application/json"),
            QJsonObject{{s("schema"), schemaRef(s("Error"))}}}}},
    };
}

const char* const kOverview =
    "Control and observe an eMuleQt core: eD2K and Kad file sharing plus Usenet downloads.\n\n"
    "**Authentication.** Send the API key in the `X-Api-Key` header (or as "
    "`Authorization: Bearer <key>`). `/health`, `/openapi.json` and `/docs` need none. The "
    "stream routes take `?token=` instead.\n\n"
    "**Errors.** Every failure is `{\"error\": {\"code\": <HTTP status>, \"message\": \"...\"}}`. "
    "An unknown field or query parameter is a 400, never silently ignored. 409 means the "
    "request is fine but the core is in the wrong state for it; 503 that a part of the core "
    "is not running.\n\n"
    "**Lists.** Answer `{items, total, offset, limit}`; page with `?limit=` (1-1000, "
    "default 100) and `?offset=`.\n\n"
    "**Destructive calls** need `\"confirm\": true` (in the body, or `?confirm=true` on "
    "DELETE).\n\n"
    "**Identifiers.** A file is named by its eD2K hash, 32 hexadecimal digits. Sizes are "
    "bytes. A category is an index into `GET /categories`.\n\n"
    "**Terms.** *HighID*: reachable from outside, the normal state. *LowID* / *firewalled*: "
    "not reachable, so fewer sources and slower transfers — check port forwarding. *Kad*: "
    "the serverless network; it works alongside the eD2K servers. *Source*: a peer that has "
    "(part of) a file.\n\n"
    "**Typical flow.** `GET /snapshot` → `POST /search` → `GET /search/{id}/results` until "
    "`state` is `finished` → `POST /search/{id}/download` → watch `GET /downloads/{hash}` "
    "or the `/events` stream.\n\n"
    "**Changes.** Every response carries `X-Contract-Version`. `GET /events` pushes changes "
    "so clients need not poll.";

} // namespace

QJsonObject schemaString(const QString& d)  { return typed("string", d); }
QJsonObject schemaInteger(const QString& d) { return typed("integer", d); }
QJsonObject schemaNumber(const QString& d)  { return typed("number", d); }
QJsonObject schemaBoolean(const QString& d) { return typed("boolean", d); }

static QJsonObject schemaStringList(const QString& d)
{
    return QJsonObject{{s("type"), s("array")}, {s("items"), schemaString()}, {s("description"), d}};
}

QJsonObject schemaObject(std::initializer_list<std::pair<QString, QJsonObject>> properties)
{
    QJsonObject props;
    for (const auto& [name, schema] : properties)
        props.insert(name, schema);
    return QJsonObject{{s("type"), s("object")}, {s("properties"), props}};
}

QJsonObject schemaRef(const QString& component)
{
    return QJsonObject{{s("$ref"), QStringLiteral("#/components/schemas/%1").arg(component)}};
}

QJsonObject schemaPage(const QString& component)
{
    return schemaObject({
        {s("items"), QJsonObject{{s("type"), s("array")}, {s("items"), schemaRef(component)}}},
        {s("total"), schemaInteger(s("Rows in the whole list."))},
        {s("offset"), schemaInteger(s("Index of the first row returned."))},
        {s("limit"), schemaInteger(s("Page size that was applied."))},
    });
}

QJsonObject componentSchemas()
{
    QJsonObject c;
    c.insert(s("Error"), schemaObject({
        {s("error"), schemaObject({
            {s("code"), schemaInteger(s("The HTTP status."))},
            {s("message"), schemaString(s("What went wrong, in plain words."))},
        })},
    }));
    c.insert(s("Download"), schemaObject({
        {s("hash"), schemaString(s("eD2K file hash, 32 hex digits."))},
        {s("fileName"), schemaString()},
        {s("fileSize"), schemaInteger(s("Bytes."))},
        {s("completedSize"), schemaInteger(s("Bytes downloaded and verified."))},
        {s("percentCompleted"), schemaNumber(s("0 to 100."))},
        {s("status"), schemaString(s("ready (running), empty, waitingforhash, hashing, error, "
                                     "insufficient (disk full), paused, completing, complete."))},
        {s("datarate"), schemaInteger(s("Current download speed in bytes per second."))},
        {s("sourceCount"), schemaInteger(s("Peers known to have the file."))},
        {s("transferringSrcCount"), schemaInteger(s("Peers sending right now."))},
        {s("downPriority"), schemaString(s("veryLow, low, normal, high, veryHigh or auto."))},
        {s("isAutoDownPriority"), schemaBoolean()},
        {s("isPaused"), schemaBoolean()},
        {s("isStopped"), schemaBoolean()},
        {s("category"), schemaInteger(s("Category index; 0 = none."))},
        {s("fakeScore"), schemaInteger(s("0 to 100: how strongly the signals say the file is not "
                                         "what its name claims."))},
        {s("confidence"), schemaString(s("spam, likely_fake, suspect, caution, looks_good or genuine; "
                                         "empty when nothing was judged."))},
        {s("fakeReasons"), schemaStringList(s("Why: multiple_names, names_span_kinds, "
                                         "bad_signal_name, bad_signal_comment, header_extension_mismatch, "
                                         "executable_masquerade, archive_masquerade, claimed_type_mismatch, "
                                         "spam_score, spam_status, bad_rating, fake_rating, multiple_aich, "
                                         "implausible_media_length, implausible_media_bitrate, "
                                         "media_size_mismatch, name_media_tag_mismatch, abuse_content_name."))},
    }));
    c.insert(s("Server"), schemaObject({
        {s("name"), schemaString()},
        {s("address"), schemaString(s("IP literal or host name; with port it identifies the server."))},
        {s("ip"), schemaInteger(s("IPv4 address as a number; 0 for an IPv6 server."))},
        {s("addr"), schemaString(s("Resolved IP address."))},
        {s("addr6"), schemaString(s("IPv6 address of a dual-stack server, else empty."))},
        {s("port"), schemaInteger()},
        {s("description"), schemaString()},
        {s("version"), schemaString()},
        {s("users"), schemaInteger()},
        {s("files"), schemaInteger()},
        {s("ping"), schemaInteger(s("Milliseconds."))},
        {s("failedCount"), schemaInteger(s("Connection failures in a row."))},
        {s("preference"), schemaInteger(s("0 normal, 1 high, 2 low."))},
        {s("isStatic"), schemaBoolean(s("Kept even when it stops answering."))},
        {s("disabled"), schemaBoolean(s("Failed too often; skipped when connecting."))},
    }));
    c.insert(s("SearchResult"), schemaObject({
        {s("hash"), schemaString(s("eD2K file hash; pass it to POST /search/{id}/download."))},
        {s("fileName"), schemaString()},
        {s("fileSize"), schemaInteger(s("Bytes."))},
        {s("sourceCount"), schemaInteger(s("Peers reported to have the file."))},
        {s("completeSourceCount"), schemaInteger(s("Of those, peers with the whole file."))},
        {s("isKadResult"), schemaBoolean()},
        {s("kadOrigin"), schemaBoolean()},
        {s("metaKind"), schemaString(s("\"torrent\" or \"nzb\": a release an eD2K server listed from "
                                       "its catalogue. Empty for an eD2K file. Not downloadable "
                                       "through POST /search/{id}/download."))},
        {s("magnet"), schemaString(s("Magnet link of a torrent result, when the server sent one; else empty."))},
        {s("spamRating"), schemaInteger(s("Spam filter score; 60 and above counts as spam."))},
        {s("seenBefore"), schemaBoolean(s("This file turned up in an earlier search."))},
        {s("seenNames"), schemaInteger(s("Different names seen for this hash."))},
        {s("firstSeen"), schemaInteger(s("Unix seconds; 0 = never."))},
        {s("fakeScore"), schemaInteger(s("0 to 100: how strongly the signals say the file is not "
                                         "what its name claims."))},
        {s("confidence"), schemaString(s("spam, likely_fake, suspect, caution, looks_good or genuine; "
                                         "empty when nothing was judged."))},
        {s("fakeReasons"), schemaStringList(s("Why: multiple_names, names_span_kinds, "
                                         "bad_signal_name, bad_signal_comment, header_extension_mismatch, "
                                         "executable_masquerade, archive_masquerade, claimed_type_mismatch, "
                                         "spam_score, spam_status, bad_rating, fake_rating, multiple_aich, "
                                         "implausible_media_length, implausible_media_bitrate, "
                                         "media_size_mismatch, name_media_tag_mismatch, abuse_content_name."))},
    }));
    c.insert(s("Client"), schemaObject({
        {s("userName"), schemaString()},
        {s("userHash"), schemaString()},
        {s("software"), schemaString(s("Client program and version."))},
        {s("addr"), schemaString()},
        {s("port"), schemaInteger()},
        {s("uploadState"), schemaString(s("Our upload to this peer."))},
        {s("downloadState"), schemaString(s("Our download from this peer."))},
        {s("upDatarate"), schemaInteger(s("Bytes per second to the peer."))},
        {s("downDatarate"), schemaInteger(s("Bytes per second from the peer."))},
        {s("sessionUp"), schemaInteger(s("Bytes sent this session."))},
        {s("sessionDown"), schemaInteger(s("Bytes received this session."))},
        {s("transferredUp"), schemaInteger(s("Bytes sent in total."))},
        {s("transferredDown"), schemaInteger(s("Bytes received in total."))},
        {s("queueScore"), schemaInteger(s("Position score in our upload queue; highest is next."))},
        {s("queueRating"), schemaInteger(s("The score without the waiting-time part."))},
        {s("waitTimeMs"), schemaInteger(s("Time spent waiting in our queue."))},
        {s("remoteQueueRank"), schemaInteger(s("Our position in the peer's queue; 0 = unknown."))},
        {s("isBanned"), schemaBoolean()},
        {s("isFriend"), schemaBoolean()},
        {s("hasLowID"), schemaBoolean()},
        {s("uploadFileName"), schemaString(s("File the peer wants from us."))},
        {s("reqFileName"), schemaString(s("File we want from the peer."))},
    }));
    c.insert(s("Friend"), schemaObject({
        {s("hash"), schemaString()},
        {s("name"), schemaString()},
        {s("ip"), schemaInteger()},
        {s("addr"), schemaString()},
        {s("port"), schemaInteger()},
        {s("lastSeen"), schemaInteger(s("Unix seconds."))},
        {s("hasFriendSlot"), schemaBoolean()},
        {s("hasKadID"), schemaBoolean()},
    }));
    return c;
}

QJsonObject paramSchema(const Param& p)
{
    QJsonObject schema{{s("type"), QString(jsonType(p.type))}};
    if (!p.description.isEmpty())
        schema.insert(s("description"), p.description);

    const bool numeric = p.type == ParamType::Integer || p.type == ParamType::Number;
    const QString lo = numeric ? s("minimum") : p.type == ParamType::Array ? s("minItems") : s("minLength");
    const QString hi = numeric ? s("maximum") : p.type == ParamType::Array ? s("maxItems") : s("maxLength");
    if (p.min)
        schema.insert(lo, *p.min);
    if (p.max)
        schema.insert(hi, *p.max);
    if (!p.enumValues.isEmpty())
        schema.insert(s("enum"), QJsonArray::fromStringList(p.enumValues));
    if (!p.defaultValue.isUndefined())
        schema.insert(s("default"), p.defaultValue);
    if (p.type == ParamType::Array)
        schema.insert(s("items"), QJsonObject{{s("type"), QString(jsonType(p.itemType))}});
    return schema;
}

QJsonObject inputSchema(const Operation& op)
{
    QJsonObject properties;
    QJsonArray required;
    for (const Param& p : op.params) {
        properties.insert(p.name, paramSchema(p));
        if (p.required)
            required.append(p.name);
    }
    QJsonObject schema{
        {s("type"), s("object")},
        {s("properties"), properties},
        {s("additionalProperties"), op.is(FreeBody)},
    };
    if (!required.isEmpty())
        schema.insert(s("required"), required);
    return schema;
}

QJsonObject buildOpenApi(const Registry& registry, const QString& version)
{
    QJsonObject paths;
    QStringList tagNames;

    for (const Operation& op : registry.operations()) {
        if (!tagNames.contains(op.tag))
            tagNames.append(op.tag);

        QJsonObject entry{
            {s("operationId"), op.id},
            {s("tags"), QJsonArray{op.tag}},
            {s("summary"), op.summary},
        };
        QString description = op.description;
        if (op.is(Destructive))
            description += s("\n\nDestructive: needs `confirm: true`.");
        if (!description.isEmpty())
            entry.insert(s("description"), description.trimmed());
        if (!op.mcpTool.isEmpty())
            entry.insert(s("x-mcp-tool"), op.mcpTool);
        if (op.is(NoAuth) || op.is(RawStream))
            entry.insert(s("security"), QJsonArray{});

        // Path and query parameters
        QJsonArray parameters;
        QJsonObject bodyProperties;
        QJsonArray bodyRequired;
        for (const Param& p : op.params) {
            if (p.in == ParamIn::Body) {
                bodyProperties.insert(p.name, paramSchema(p));
                if (p.required)
                    bodyRequired.append(p.name);
                continue;
            }
            QJsonObject parameter{
                {s("name"), p.name},
                {s("in"), p.in == ParamIn::Path ? s("path") : s("query")},
                {s("required"), p.required},
                {s("schema"), paramSchema(p)},
            };
            if (!p.description.isEmpty())
                parameter.insert(s("description"), p.description);
            parameters.append(parameter);
        }
        if (!parameters.isEmpty())
            entry.insert(s("parameters"), parameters);

        if (!bodyProperties.isEmpty() || op.is(FreeBody)) {
            QJsonObject bodySchema{
                {s("type"), s("object")},
                {s("properties"), bodyProperties},
                {s("additionalProperties"), op.is(FreeBody)},
            };
            if (!bodyRequired.isEmpty())
                bodySchema.insert(s("required"), bodyRequired);
            QJsonObject content{{s("application/json"), QJsonObject{{s("schema"), bodySchema}}}};
            if (op.is(RawBody)) {
                content.insert(s("application/x-nzb"), QJsonObject{
                    {s("schema"), QJsonObject{{s("type"), s("string")}, {s("format"), s("binary")}}}});
            }
            entry.insert(s("requestBody"), QJsonObject{
                {s("required"), !bodyRequired.isEmpty()},
                {s("content"), content},
            });
        }

        // Responses
        QJsonObject responses;
        if (op.is(RawStream)) {
            responses.insert(s("200"), QJsonObject{{s("description"),
                op.id == QLatin1StringView("events.stream") ? s("text/event-stream")
                                                           : s("The requested bytes or page.")}});
        } else {
            const QJsonObject schema = op.resultSchema.isEmpty()
                ? QJsonObject{{s("type"), s("object")}} : op.resultSchema;
            responses.insert(s("200"), QJsonObject{
                {s("description"), s("Success")},
                {s("content"), QJsonObject{{s("application/json"),
                    QJsonObject{{s("schema"), schema}}}}},
            });
            responses.insert(s("400"), errorResponse("A parameter is missing, unknown or out of range."));
            if (!op.is(NoAuth))
                responses.insert(s("401"), errorResponse("Missing or wrong API key."));
            if (op.path.contains(u'{'))
                responses.insert(s("404"), errorResponse("No such item."));
        }
        entry.insert(s("responses"), responses);

        QJsonObject item = paths.value(op.path).toObject();
        item.insert(methodName(op.method), entry);
        paths.insert(op.path, item);
    }

    QJsonArray tags;
    for (const QString& name : std::as_const(tagNames))
        tags.append(QJsonObject{{s("name"), name}});

    return QJsonObject{
        {s("openapi"), s("3.1.0")},
        {s("info"), QJsonObject{
            {s("title"), s("eMuleQt REST API")},
            {s("version"), version},
            {s("description"), s(kOverview)},
            {s("x-contract-version"), kContractVersion},
        }},
        {s("servers"), QJsonArray{QJsonObject{{s("url"), s("/")}}}},
        {s("tags"), tags},
        {s("security"), QJsonArray{QJsonObject{{s("ApiKey"), QJsonArray{}}}}},
        {s("paths"), paths},
        {s("components"), QJsonObject{
            {s("securitySchemes"), QJsonObject{{s("ApiKey"), QJsonObject{
                {s("type"), s("apiKey")}, {s("in"), s("header")}, {s("name"), s("X-Api-Key")}}}}},
            {s("schemas"), componentSchemas()},
        }},
    };
}

} // namespace eMule::api
