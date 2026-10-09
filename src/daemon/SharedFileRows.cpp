/// @file SharedFileRows.cpp
/// @brief One shared-files row as the GUI gets it — in the list reply and in a push.

#include "SharedFileRows.h"

#include "app/AppContext.h"
#include "client/UpDownClient.h"
#include "files/Collection.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "media/MediaInfo.h"
#include "transfer/UploadQueue.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QCborArray>
#include <QFileInfo>

namespace eMule {

std::unordered_map<MD4Key, SharedFileLoad> sharedFileLoads()
{
    std::unordered_map<MD4Key, SharedFileLoad> loads;
    if (!theApp.uploadQueue)
        return loads;
    theApp.uploadQueue->forEachWaiting([&](UpDownClient* c) {
        ++loads[MD4Key(c->reqUpFileId())].queuedClients;
    });
    theApp.uploadQueue->forEachUploading([&](UpDownClient* c) {
        loads[MD4Key(c->reqUpFileId())].uploadDataRate += c->upDatarate();
    });
    return loads;
}

SharedFileLoad sharedFileLoad(const uint8* fileHash)
{
    SharedFileLoad load;
    if (!theApp.uploadQueue)
        return load;
    theApp.uploadQueue->forEachWaiting([&](UpDownClient* c) {
        if (md4equ(c->reqUpFileId(), fileHash))
            ++load.queuedClients;
    });
    theApp.uploadQueue->forEachUploading([&](UpDownClient* c) {
        if (md4equ(c->reqUpFileId(), fileHash))
            load.uploadDataRate += c->upDatarate();
    });
    return load;
}

QCborMap sharedFileRow(KnownFile* kf, const SharedFileList::ShareRules& rules,
                       const SharedFileLoad& load)
{
    // A part file is a shared file as soon as it has a part to offer; it is listed
    // with what it has, not as if it were whole.
    const bool isPartFile = kf->isPartFile();
    const qint64 completedSz = isPartFile
        ? static_cast<qint64>(static_cast<PartFile*>(kf)->completedSize())
        : static_cast<qint64>(kf->fileSize());

    QCborMap m;
    const QString hash = md4str(kf->fileHash());
    m.insert(QStringLiteral("hash"), hash);
    m.insert(QStringLiteral("fileName"), kf->fileName());
    m.insert(QStringLiteral("fileSize"), static_cast<qint64>(kf->fileSize()));
    m.insert(QStringLiteral("fileType"), kf->fileType());
    // Comment/rating marks, same shape as the download rows.
    m.insert(QStringLiteral("hasComment"), kf->hasComment());
    m.insert(QStringLiteral("userRating"), static_cast<int>(kf->userRating(true)));
    // Separately, whether *we* commented or rated it — MFC's shared list draws that
    // as an overlay on the type icon rather than as a rating mark
    // (srchybrid/SharedFilesCtrl.cpp:561-562). Costs one lazy fileinfo.ini read per
    // file, once, exactly as the original's per-file CIni does.
    m.insert(QStringLiteral("ownComment"),
             !kf->getFileComment().isEmpty() || kf->getFileRating() > 0);
    // The fake-file mark. Only what has already been settled: this loop walks a
    // share that can hold tens of thousands of files, so it may not open any of
    // them. SharedFileList::warmContainerChecks() does the reading a slice per
    // tick, so a file the sweep has not reached yet reads as clean for now —
    // Unchecked is not suspect, and claiming otherwise would be worse than late.
    const ContainerCheck& cc = kf->containerCheckIfResolved();
    m.insert(QStringLiteral("containerSuspect"), cc.isSuspect());
    m.insert(QStringLiteral("containerExpected"), cc.expected);
    m.insert(QStringLiteral("containerActual"), cc.actual);
    m.insert(QStringLiteral("upPriority"), static_cast<int>(kf->upPriority()));
    m.insert(QStringLiteral("isAutoUpPriority"), kf->isAutoUpPriority());
    m.insert(QStringLiteral("requests"), static_cast<qint64>(kf->statistic.requests()));
    m.insert(QStringLiteral("acceptedUploads"), static_cast<qint64>(kf->statistic.accepts()));
    m.insert(QStringLiteral("transferred"), static_cast<qint64>(kf->statistic.transferred()));
    m.insert(QStringLiteral("allTimeRequests"), static_cast<qint64>(kf->statistic.allTimeRequests()));
    m.insert(QStringLiteral("allTimeAccepted"), static_cast<qint64>(kf->statistic.allTimeAccepts()));
    m.insert(QStringLiteral("allTimeTransferred"), static_cast<qint64>(kf->statistic.allTimeTransferred()));
    m.insert(QStringLiteral("completeSources"), static_cast<int>(kf->completeSourcesCount()));
    m.insert(QStringLiteral("publishedED2K"), kf->publishedED2K());
    m.insert(QStringLiteral("completeSourcesLo"), static_cast<int>(kf->completeSourcesCountLo()));
    m.insert(QStringLiteral("completeSourcesHi"), static_cast<int>(kf->completeSourcesCountHi()));
    m.insert(QStringLiteral("kadPublished"), kf->isSharedInKad());
    // Media columns; absent keys read as empty / 0 in the GUI
    if (const QString& v = kf->getStrTagValue(FT_MEDIA_ARTIST); !v.isEmpty())
        m.insert(QStringLiteral("artist"), v);
    if (const QString& v = kf->getStrTagValue(FT_MEDIA_ALBUM); !v.isEmpty())
        m.insert(QStringLiteral("album"), v);
    if (const QString& v = kf->getStrTagValue(FT_MEDIA_TITLE); !v.isEmpty())
        m.insert(QStringLiteral("title"), v);
    if (const uint32 v = kf->getIntTagValue(FT_MEDIA_LENGTH))
        m.insert(QStringLiteral("length"), static_cast<qint64>(v));
    if (const uint32 v = kf->getIntTagValue(FT_MEDIA_BITRATE))
        m.insert(QStringLiteral("bitrate"), static_cast<qint64>(v));
    if (const QString& v = kf->getStrTagValue(FT_MEDIA_CODEC); !v.isEmpty())
        m.insert(QStringLiteral("codec"), codecDisplayName(v));
    m.insert(QStringLiteral("filePath"), kf->filePath());
    // Whether the user is allowed to unshare it — the incoming directory is not
    // unshareable by accident, it is unshareable by design (MFC ShouldBeShared with
    // bMustBeShared, srchybrid/SharedFilesCtrl.cpp:1598). The GUI greys the menu
    // entry with this rather than guessing at the incoming path from the rows.
    m.insert(QStringLiteral("canUnshare"),
             theApp.sharedFileList
                 && !theApp.sharedFileList->shouldBeShared(rules, kf->path(), kf->filePath(), true));
    m.insert(QStringLiteral("path"), QFileInfo(kf->filePath()).absolutePath());
    m.insert(QStringLiteral("ed2kLink"), kf->getED2kLink());
    m.insert(QStringLiteral("isPartFile"), isPartFile);
    if (isPartFile)   // the Incomplete Files node splits by it
        m.insert(QStringLiteral("category"), static_cast<qint64>(static_cast<const PartFile*>(kf)->category()));
    m.insert(QStringLiteral("uploadingClients"), kf->uploadingClientCount());

    m.insert(QStringLiteral("queuedClients"), load.queuedClients);
    m.insert(QStringLiteral("uploadDataRate"), load.uploadDataRate);

    // Whether a link with the part hashes can be built; the link itself is asked for
    // when wanted (GetEd2kLink), not sent with every row.
    const auto& fid = kf->fileIdentifier();
    m.insert(QStringLiteral("hasPartHashes"),
             fid.getAvailableMD4PartHashCount() > 0 && fid.hasExpectedMD4HashCount());

    m.insert(QStringLiteral("partCount"), static_cast<int>(kf->partCount()));
    m.insert(QStringLiteral("completedSize"), completedSz);

    // Build per-part availability map for share status bar
    {
        QCborArray partMapArr;
        const auto& availFreq = kf->availPartFrequency();
        const int pc = static_cast<int>(kf->partCount());

        if (isPartFile) {
            auto* pf = static_cast<PartFile*>(kf);
            const auto& srcFreq = pf->srcPartFrequency();
            const bool hasSources = kf->hasUploadingClients() || kf->completeSourcesCountHi() > 0;

            if (hasSources || pf->status() != PartFileStatus::Paused) {
                const uint16 baseSources = kf->completeSourcesCountLo()
                                           ? kf->completeSourcesCountLo() - 1 : 0;
                for (int i = 0; i < pc; ++i) {
                    if (!pf->isComplete(static_cast<uint32>(i))) {
                        partMapArr.append(255); // gap — light grey
                    } else {
                        // Use srcPartFrequency when actively downloading, availPartFrequency otherwise
                        uint16 freq = 0;
                        if (pf->status() != PartFileStatus::Paused && i < static_cast<int>(srcFreq.size()))
                            freq = srcFreq[static_cast<size_t>(i)];
                        else if (i < static_cast<int>(availFreq.size()))
                            freq = std::max(availFreq[static_cast<size_t>(i)], baseSources);
                        // Encode: 0 → 1(red), else clamp(freq+1, 2, 254)
                        partMapArr.append(freq == 0 ? 1 : std::clamp<int>(freq + 1, 2, 254));
                    }
                }
            } else {
                // Paused with no sources — complete=0, incomplete=255
                for (int i = 0; i < pc; ++i)
                    partMapArr.append(pf->isComplete(static_cast<uint32>(i)) ? 0 : 255);
            }
        } else {
            // Complete KnownFile
            if (kf->hasUploadingClients() || kf->completeSourcesCountHi() > 1) {
                const uint16 baseSources = kf->completeSourcesCountLo()
                                           ? kf->completeSourcesCountLo() - 1 : 0;
                for (int i = 0; i < pc; ++i) {
                    uint16 freq = baseSources;
                    if (i < static_cast<int>(availFreq.size()))
                        freq = std::max(availFreq[static_cast<size_t>(i)], baseSources);
                    partMapArr.append(freq == 0 ? 1 : std::clamp<int>(freq + 1, 2, 254));
                }
            }
            // else: empty array → delegate draws solid dark grey
        }

        if (!partMapArr.isEmpty())
            m.insert(QStringLiteral("sharePartMap"), partMapArr);
    }

    // Collection metadata
    const bool isColl = Collection::hasCollectionExtension(kf->fileName());
    m.insert(QStringLiteral("isCollection"), isColl);
    m.insert(QStringLiteral("hasCollectionAuthorKey"),
             isColl && kf->collection() && !kf->collection()->m_authorKey.isEmpty());

    return m;
}

} // namespace eMule
