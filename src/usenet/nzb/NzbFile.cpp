#include "nzb/NzbFile.h"

#include "utils/OtherFunctions.h"

#include "nzb/SubjectParser.h"
#include "utils/Log.h"

#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

namespace eMule::usenet {

bool NzbFile::parse(const QByteArray& data, NzbInfo& out, QString& error)
{
    error.clear();
    out.files.clear();
    out.meta.clear();
    out.filesWithoutSegments = 0;

    QXmlStreamReader xml(data);
    NzbFileInfo current;
    bool inFile = false;
    int segmentPosition = 0;   // <segment>s seen in the current <file>

    // Taken once for the whole document, not per <file>. It costs a refcount and
    // it buys the property that one NZB is read by one rule set even if the
    // preference changes while this runs.
    const std::shared_ptr<const SubjectRuleSet> rules = subjectRules();

    while (!xml.atEnd()) {
        const auto token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            // Compare on the local name only: NZBs carry the newzbin namespace,
            // a wrong one, or none, and refusing any of those helps nobody.
            const auto name = xml.name();

            if (name == QLatin1String("file")) {
                current = NzbFileInfo{};
                inFile = true;
                segmentPosition = 0;
                const auto attrs = xml.attributes();
                current.subject = attrs.value(QLatin1String("subject")).toString();
                current.poster = attrs.value(QLatin1String("poster")).toString();
                current.date = attrs.value(QLatin1String("date")).toLongLong();

                const SubjectInfo parsed = rules->parse(current.subject);
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
                segment.numberText = attrs.value(QLatin1String("number")).toString();
                segment.number = segment.numberText.toInt();
                // Entities resolved by the reader: usenet.md §1 mᵢ.
                segment.messageId = normalizeMessageId(xml.readElementText());
                if (!segment.messageId.isEmpty()) {
                    current.segments.append(segment);
                } else {
                    // Not fetchable, but it costs the file its usenet.md file id.
                    segment.position = segmentPosition;
                    current.emptyIdSegments.append(segment);
                }
                ++segmentPosition;

            } else if (name == QLatin1String("meta")) {
                const auto type = xml.attributes().value(QLatin1String("type")).toString();
                const QString value = xml.readElementText();
                out.meta.append({type, value});
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
            else
                ++out.filesWithoutSegments;
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
    // The convention itself lives in core: the indexer's feed parser reads the
    // same marker out of a release *title*, and it does not link this module.
    return takeBracedPassword(baseName);
}

} // namespace eMule::usenet
