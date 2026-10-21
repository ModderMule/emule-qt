#include "pch.h"
/// @file TorrentLinks.cpp
/// @brief What counts as a BitTorrent link or .torrent file — implementation.

#include "utils/TorrentLinks.h"

#include <QFileInfo>
#include <QMimeData>
#include <QRegularExpression>
#include <QUrl>

namespace eMule::gui {

namespace {

bool startsWithCi(const QString& s, QLatin1StringView prefix)
{
    return s.startsWith(prefix, Qt::CaseInsensitive);
}

} // namespace

bool isBitTorrentMagnet(const QString& link)
{
    const QString trimmed = link.trimmed();
    if (!startsWithCi(trimmed, QLatin1StringView("magnet:")))
        return false;
    const qsizetype q = trimmed.indexOf(QLatin1Char('?'));
    if (q < 0)
        return false;

    bool bt = false;
    for (const QString& param : trimmed.mid(q + 1).split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
        const qsizetype eq = param.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = param.left(eq).toLower();
        const QString val = QUrl::fromPercentEncoding(param.mid(eq + 1).toUtf8());
        // xt, xt.1, xt.2 ... (BEP 9 multi-hash magnets)
        if (key == QLatin1String("xt") || key.startsWith(QLatin1String("xt."))) {
            if (startsWithCi(val, QLatin1StringView("urn:ed2k:"))
                || startsWithCi(val, QLatin1StringView("urn:ed2khash:")))
                return false;   // eD2K wins: Transfers keeps its magnets
            if (startsWithCi(val, QLatin1StringView("urn:btih:"))
                || startsWithCi(val, QLatin1StringView("urn:btmh:")))
                bt = true;
        } else if ((key == QLatin1String("xs") || key == QLatin1String("as"))
                   && startsWithCi(val, QLatin1StringView("ed2k://"))) {
            return false;   // ED2KLink parses an embedded eD2K file link out of these
        }
    }
    return bt;
}

QStringList bitTorrentMagnetsIn(const QString& text)
{
    QStringList out;
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    // A magnet has no spaces (they are percent-encoded), so whitespace splits safely
    for (const QString& token : text.split(ws, Qt::SkipEmptyParts)) {
        if (isBitTorrentMagnet(token))
            out << token;
    }
    return out;
}

QString magnetDisplayName(const QString& link)
{
    const qsizetype q = link.indexOf(QLatin1Char('?'));
    QString hash;
    if (q >= 0) {
        for (const QString& param : link.mid(q + 1).split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
            const qsizetype eq = param.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            const QString key = param.left(eq).toLower();
            // '+' is a space in dn, as browsers form-encode it
            const QString val = QUrl::fromPercentEncoding(
                param.mid(eq + 1).replace(QLatin1Char('+'), QLatin1Char(' ')).toUtf8());
            if (key == QLatin1String("dn") && !val.trimmed().isEmpty())
                return val.trimmed();
            if (hash.isEmpty() && (key == QLatin1String("xt") || key.startsWith(QLatin1String("xt.")))
                && (startsWithCi(val, QLatin1StringView("urn:btih:"))
                    || startsWithCi(val, QLatin1StringView("urn:btmh:"))))
                hash = val.mid(9);
        }
    }
    return hash.isEmpty() ? link : hash;
}

bool isTorrentFilePath(const QString& path)
{
    return path.endsWith(QLatin1String(".torrent"), Qt::CaseInsensitive);
}

QStringList torrentDropFiles(const QMimeData* mime)
{
    QStringList out;
    if (mime == nullptr || !mime->hasUrls())
        return out;
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (isTorrentFilePath(path) && QFileInfo(path).isFile())
            out << path;
    }
    return out;
}

QString torrentMagnetLink(const QString& hashV1, const QString& name)
{
    if (hashV1.isEmpty())
        return {};
    QString link = QStringLiteral("magnet:?xt=urn:btih:") + hashV1.toLower();
    if (!name.isEmpty())
        link += QStringLiteral("&dn=") + QString::fromLatin1(QUrl::toPercentEncoding(name));
    return link;
}

} // namespace eMule::gui
