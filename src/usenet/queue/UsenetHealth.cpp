#include "queue/UsenetHealth.h"

#include "nzb/NzbInfo.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QStringList>

namespace eMule::usenet {

UsenetHealthCheck usenetHealthCheckFromInt(int value)
{
    switch (value) {
    case 1:  return UsenetHealthCheck::Sample;
    case 2:  return UsenetHealthCheck::Full;
    default: return UsenetHealthCheck::Off;
    }
}

QString nzbArticleDigest(const NzbInfo& nzb)
{
    QStringList ids;
    ids.reserve(nzb.segmentCount());
    for (const NzbFileInfo& file : nzb.files) {
        for (const NzbSegment& seg : file.segments) {
            if (!seg.messageId.isEmpty())
                ids.append(seg.messageId);
        }
    }
    if (ids.isEmpty())
        return {};

    ids.sort();
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const QString& id : std::as_const(ids)) {
        hash.addData(id.toUtf8());
        // A separator, or "ab" + "c" and "a" + "bc" would digest identically.
        hash.addData(QByteArrayLiteral("\n"));
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString nzbReleaseKey(const QString& name, qint64 totalEncodedBytes)
{
    const QString folded = name.trimmed().toCaseFolded();
    if (folded.isEmpty())
        return {};
    return folded + QLatin1Char('|') + QString::number(totalEncodedBytes);
}

} // namespace eMule::usenet
