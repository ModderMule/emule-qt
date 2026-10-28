/// @file Ed3kUploadsBridge.cpp
/// @brief The ed3k upload queue in GetUploads — implementation.

#include "Ed3kUploadsBridge.h"

#include <QCborMap>
#include <QDateTime>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kUploads.h"
#endif

namespace eMule::Ed3kUploadsBridge {

namespace {

ed3k::Ed3kUploads* s_uploads = nullptr;

#ifdef EMULE_HAVE_ED3K
QCborMap rowToCbor(const ed3k::Ed3kUploads::Row& r, qint64 nowMs)
{
    const QString node = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(r.node.bytes.data()),
                                                        qsizetype(r.node.bytes.size())).toHex());
    const qint64 held = std::max<qint64>(0, nowMs - r.since);
    return QCborMap{
        {QStringLiteral("network"), QStringLiteral("ed3k")},
        {QStringLiteral("userName"), node.left(8)},
        {QStringLiteral("userHash"), node},
        {QStringLiteral("software"), QStringLiteral("ed3k")},
        {QStringLiteral("uploadState"), r.holding ? QStringLiteral("Transferring") : QStringLiteral("OnQueue")},
        {QStringLiteral("uploadFileName"), r.fileName},
        {QStringLiteral("uploadFileSize"), qint64(std::min<std::uint64_t>(r.fileSize, INT64_MAX))},
        {QStringLiteral("root"), QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(r.root.bytes.data()),
                                                                qsizetype(r.root.bytes.size())).toHex())},
        {QStringLiteral("addr"), r.address},
        {QStringLiteral("port"), int(r.port)},
        {QStringLiteral("sessionUp"), qint64(std::min<std::uint64_t>(r.bytes, INT64_MAX))},
        {QStringLiteral("waitStartTime"), r.holding ? qint64(0) : held},
        {QStringLiteral("uploadStartDelay"), r.holding ? held : qint64(0)},
        {QStringLiteral("queueRank"), r.place},
        // was: {QStringLiteral("isConnected"), true},
        {QStringLiteral("isConnected"), r.connected},
        {QStringLiteral("lan"), r.lan},
        {QStringLiteral("due"), r.due},
    };
}
#endif

} // namespace

void setUploads(ed3k::Ed3kUploads* uploads)
{
    s_uploads = uploads;
}

void appendRows(QCborArray& uploading, QCborArray& waiting)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_uploads)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto& r : s_uploads->uploading())
        uploading.append(rowToCbor(r, now));
    for (const auto& r : s_uploads->waiting())
        waiting.append(rowToCbor(r, now));
#else
    (void)uploading;
    (void)waiting;
#endif
}

} // namespace eMule::Ed3kUploadsBridge
