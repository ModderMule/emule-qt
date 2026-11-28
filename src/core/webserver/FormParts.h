#pragma once

/// @file FormParts.h
/// @brief multipart/form-data and urlencoded form bodies (template web UI, qBittorrent API).

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrlQuery>

#include <optional>

namespace eMule {

/// A urlencoded form body; '+' is a space.
[[nodiscard]] inline QUrlQuery urlencodedForm(const QByteArray& body)
{
    return QUrlQuery(QString::fromUtf8(body).replace(QLatin1Char('+'), QStringLiteral("%20")));
}

/// One part of a multipart/form-data body.
struct FormPart {
    QString name;
    std::optional<QString> fileName;   ///< set: a file input
    QByteArray data;
};

/// The parts of a multipart/form-data body (RFC 7578); empty when it is none.
[[nodiscard]] inline QList<FormPart> multipartParts(const QByteArray& contentType, const QByteArray& body)
{
    QList<FormPart> parts;
    const qsizetype at = contentType.indexOf("boundary=");
    if (!contentType.trimmed().toLower().startsWith("multipart/form-data") || at < 0)
        return parts;
    QByteArray boundary = contentType.mid(at + 9);
    if (const qsizetype semi = boundary.indexOf(';'); semi >= 0)
        boundary.truncate(semi);
    boundary = boundary.trimmed();
    if (boundary.size() > 1 && boundary.startsWith('"') && boundary.endsWith('"'))
        boundary = boundary.mid(1, boundary.size() - 2);
    if (boundary.isEmpty())
        return parts;
    const QByteArray delimiter = "--" + boundary;
    const QByteArray nextDelimiter = "\r\n" + delimiter;
    qsizetype pos = body.indexOf(delimiter);
    while (pos >= 0) {
        pos += delimiter.size();
        if (body.mid(pos, 2) == "--")
            break;   // closing delimiter
        if (body.mid(pos, 2) == "\r\n")
            pos += 2;
        const qsizetype headEnd = body.indexOf("\r\n\r\n", pos);
        if (headEnd < 0)
            break;
        const qsizetype next = body.indexOf(nextDelimiter, headEnd + 4);
        if (next < 0)
            break;
        FormPart part;
        for (const QByteArray& line : body.mid(pos, headEnd - pos).split('\n')) {
            if (!line.trimmed().toLower().startsWith("content-disposition:"))
                continue;
            // name="…"; filename="…" — quoted, as browsers send them
            const auto param = [&line](QByteArrayView key) -> std::optional<QString> {
                const QByteArray want = "; " + key.toByteArray() + "=\"";
                const qsizetype a = line.indexOf(want);
                if (a < 0)
                    return std::nullopt;
                const qsizetype b = line.indexOf('"', a + want.size());
                return b < 0 ? std::nullopt
                             : std::optional(QString::fromUtf8(line.mid(a + want.size(), b - a - want.size())));
            };
            part.name = param("name").value_or(QString());
            part.fileName = param("filename");
        }
        part.data = body.mid(headEnd + 4, next - headEnd - 4);
        if (!part.name.isEmpty())
            parts.append(std::move(part));
        pos = next + 2;
    }
    return parts;
}

} // namespace eMule
