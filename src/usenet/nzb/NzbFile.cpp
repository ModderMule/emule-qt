#include "nzb/NzbFile.h"

#include "nzb/SubjectParser.h"
#include "utils/Log.h"

#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

namespace eMule::usenet {

namespace {

/// Strip the angle brackets an NZB may or may not put around a message-id. The
/// wire form adds them back; carrying them here would produce "<<id>>" and a
/// silent 430.
QString normalizeMessageId(QString id)
{
    id = id.trimmed();
    if (id.startsWith(u'<'))
        id.remove(0, 1);
    if (id.endsWith(u'>'))
        id.chop(1);
    return id;
}

} // namespace

bool NzbFile::parse(const QByteArray& data, NzbInfo& out, QString& error)
{
    error.clear();
    out.files.clear();

    QXmlStreamReader xml(data);
    NzbFileInfo current;
    bool inFile = false;

    while (!xml.atEnd()) {
        const auto token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            // Compare on the local name only: NZBs carry the newzbin namespace,
            // a wrong one, or none, and refusing any of those helps nobody.
            const auto name = xml.name();

            if (name == QLatin1String("file")) {
                current = NzbFileInfo{};
                inFile = true;
                const auto attrs = xml.attributes();
                current.subject = attrs.value(QLatin1String("subject")).toString();
                current.poster = attrs.value(QLatin1String("poster")).toString();
                current.date = attrs.value(QLatin1String("date")).toLongLong();

                const SubjectInfo parsed = parseSubject(current.subject);
                current.fileName = parsed.fileName;
                current.partsTotal = parsed.total;

            } else if (name == QLatin1String("group") && inFile) {
                const QString group = xml.readElementText().trimmed();
                if (!group.isEmpty())
                    current.groups.append(group);

            } else if (name == QLatin1String("segment") && inFile) {
                const auto attrs = xml.attributes();
                NzbSegment segment;
                // Encoded size. Never a file offset -- see NzbInfo.h.
                segment.bytes = attrs.value(QLatin1String("bytes")).toLongLong();
                segment.number = attrs.value(QLatin1String("number")).toInt();
                segment.messageId = normalizeMessageId(xml.readElementText());
                if (!segment.messageId.isEmpty())
                    current.segments.append(segment);

            } else if (name == QLatin1String("meta")) {
                const auto type = xml.attributes().value(QLatin1String("type")).toString();
                const QString value = xml.readElementText();
                if (type.compare(QLatin1String("password"), Qt::CaseInsensitive) == 0
                    && !value.isEmpty()) {
                    out.password = value;
                } else if (type.compare(QLatin1String("name"), Qt::CaseInsensitive) == 0
                           && !value.isEmpty()) {
                    // The release's own name. Only parseFile() used to set this,
                    // from the file path, so an NZB arriving over IPC or from a
                    // URL always fell through to "Usenet download" even when it
                    // said what it was.
                    out.name = value;
                }
            }

        } else if (token == QXmlStreamReader::EndElement
                   && xml.name() == QLatin1String("file")) {
            if (!current.segments.isEmpty())
                out.files.append(current);
            inFile = false;
        }
    }

    if (xml.hasError()) {
        error = QStringLiteral("Line %1: %2").arg(xml.lineNumber()).arg(xml.errorString());
        return false;
    }
    if (out.files.isEmpty()) {
        // Almost always an indexer's HTML error page served with a .nzb name:
        // well-formed XML (or not) with nothing we can download. Calling that an
        // empty success would put a permanently stalled item in the queue.
        error = QStringLiteral("No files found — is this really an NZB?");
        return false;
    }
    return true;
}

bool NzbFile::parseFile(const QString& path, NzbInfo& out, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    QString baseName = QFileInfo(path).completeBaseName();
    const QString fromName = takePasswordFromName(baseName);

    if (!parse(data, out, error))
        return false;

    out.name = baseName;
    // An explicit <meta type="password"> wins: the filename is a convention,
    // the meta tag is the format saying so.
    if (out.password.isEmpty())
        out.password = fromName;
    return true;
}

QString NzbFile::takePasswordFromName(QString& baseName)
{
    const int open = baseName.indexOf(QLatin1String("{{"));
    if (open < 0)
        return {};
    const int close = baseName.indexOf(QLatin1String("}}"), open + 2);
    if (close < 0)
        return {};

    const QString password = baseName.mid(open + 2, close - open - 2);
    baseName.remove(open, close - open + 2);
    baseName = baseName.trimmed();
    return password;
}

} // namespace eMule::usenet
