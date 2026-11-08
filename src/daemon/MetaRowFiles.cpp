/// @file MetaRowFiles.cpp
/// @brief A meta search row and which file of its metafile it stands for — implementation.

#include "MetaRowFiles.h"

#include "nzb/NzbFile.h"

#include "app/AppContext.h"
#include "enodemeta/LinkContainer.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "utils/OtherFunctions.h"

#include <QCborMap>

namespace eMule {

MetaRow metaRowOf(const SearchFile& f, const QByteArray& hash16)
{
    MetaRow r;
    r.hash16 = hash16;
    r.catalogId = f.meta().catalogId;
    r.name = f.fileName();
    r.kind = f.meta().kind;
    r.target = MetaSearchService::instance().targetForResult(f);
    r.fileIndex = f.meta().fileIndex;
    r.filePath = f.meta().filePath;
    return r;
}

uint32 hashFileIndex(const QByteArray& hash16)
{
    const auto idx = static_cast<uint16>(uint8(hash16.at(4)) | (uint8(hash16.at(5)) << 8));
    return idx == enodemeta::kFileIndexWholeSet ? enodemeta::kFileIndexWholeSet32 : idx;
}

MetaRow siblingRow(const QString& hashHex, uint32 searchID)
{
    MetaRow r;
    uint8 hash[16]{};
    if (decodeBase16(hashHex, hash, 16) != 16)
        return r;   // whole set: keeps everything
    r.hash16 = QByteArray(reinterpret_cast<const char*>(hash), 16);
    r.fileIndex = hashFileIndex(r.hash16);
    if (theApp.searchList) {
        if (const SearchFile* f = theApp.searchList->searchFileByHash(hash, searchID); f && f->isMetaResult()) {
            r.fileIndex = f->meta().fileIndex;
            r.filePath = f->meta().filePath;
        }
    }
    return r;
}

std::optional<int> resolveNzbFile(const usenet::NzbInfo& nzb, const MetaRow& r)
{
    if (r.isWholeSet())
        return std::nullopt;
    std::optional<int> hit;
    if (!r.filePath.isEmpty()) {
        for (int f = 0; f < nzb.files.size() && !hit; ++f)
            if (nzb.files.at(f).subject == r.filePath)
                hit = f;
    }
    if (!hit && r.fileIndex < uint32(nzb.files.size()))
        hit = int(r.fileIndex);
    if (hit && nzb.files.at(*hit).isPar2())
        return std::nullopt;
    return hit;
}

std::optional<int> resolveTorrentFile(const QCborArray& files, const MetaRow& r)
{
    if (r.isWholeSet())
        return std::nullopt;
    const auto real = [](const QCborMap& f) { return !f.value(QStringLiteral("pad")).toBool(); };
    if (!r.filePath.isEmpty()) {
        const QString want = QString(r.filePath).replace(u'\\', u'/');
        for (const QCborValue& v : files) {
            const QCborMap f = v.toMap();
            const QString path = f.value(QStringLiteral("path")).toString();
            // with or without the torrent's own folder in front
            if (real(f) && (path == want || path.endsWith(u'/' + want) || want.endsWith(u'/' + path)))
                return int(f.value(QStringLiteral("index")).toInteger());
        }
    }
    for (const QCborValue& v : files) {
        const QCborMap f = v.toMap();
        if (f.value(QStringLiteral("index")).toInteger(-1) == qint64(r.fileIndex))
            return real(f) ? std::optional(int(r.fileIndex)) : std::nullopt;
    }
    return std::nullopt;
}

QString releaseName(const MetaRow& row, uint32 searchID)
{
    if (row.isWholeSet() || row.hash16.size() != 16 || !theApp.searchList)
        return row.name;
    QByteArray whole = row.hash16;
    whole[4] = char(0xFF);
    whole[5] = char(0xFF);
    const SearchFile* f = theApp.searchList->searchFileByHash(
        reinterpret_cast<const uint8*>(whole.constData()), searchID);
    return f && f->isMetaResult() && !f->fileName().isEmpty() ? f->fileName() : row.name;
}

QString suggestedFileName(const MetaRow& r, const QString& contentType)
{
    QString base = stripInvalidFilenameChars(r.name).trimmed();
    if (base.isEmpty())
        base = md4str(reinterpret_cast<const uint8*>(r.hash16.constData()));
    switch (r.kind) {
    case enodemeta::Kind::Nzb:
        return base + QStringLiteral(".nzb");
    case enodemeta::Kind::Web:
        return base + QStringLiteral(".txt");
    case enodemeta::Kind::Container: {
        QString format = enodemeta::containerFormatOf(contentType);
        const QString own = enodemeta::containerFormat(base);
        if (format.isEmpty())
            format = own.isEmpty() ? QStringLiteral("dlc") : own;
        if (!own.isEmpty())
            base.chop(own.size() + 1);
        return base + u'.' + format;
    }
    default:
        return base + QStringLiteral(".torrent");
    }
}

} // namespace eMule
