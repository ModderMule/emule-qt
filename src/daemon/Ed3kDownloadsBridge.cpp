/// @file Ed3kDownloadsBridge.cpp
/// @brief Plain ed3k downloads over IPC — implementation.

#include "Ed3kDownloadsBridge.h"
#include "Ed3kOffGate.h"
#include "Ed3kUploadsBridge.h"
#include "SingleTransfer.h"
#include "webserver/WebServer.h"

#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>
#include <QFileInfo>
#include <QUrl>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kDownloads.h"
#include "Ed3kSearch.h"
#include "Ed3kSession.h"
#include "media/MediaInfo.h"

#include "app/AppContext.h"
#include "files/KnownFile.h"
#include "files/SharedFileList.h"
#include "transfer/UploadQueue.h"
#include "utils/BlockRuns.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QObject>

#include <cstring>
#endif

namespace eMule::Ed3kDownloadsBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

#ifdef EMULE_HAVE_ED3K
ed3k::Ed3kDownloads* s_downloads = nullptr;
ed3k::Ed3kSearch* s_search = nullptr;
#endif

IpcMessage failure(int seqId, const QString& error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

QString noNode()
{
    return QCoreApplication::translate("eMule::Ed3kDownloadsBridge", "ed3k is not available.");
}

#ifdef EMULE_HAVE_ED3K
namespace ffi = ::ed3k;

/// Ed3kDownloads' block, and the piece of FileStatus.have (Protocol.md §12.5)
constexpr std::uint64_t kBarBlock = 256 * 1024;
constexpr std::uint64_t kBarPiece = 16 * kBarBlock;

QString hexOf(const ffi::Root& r)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(r.bytes.data()), qsizetype(r.bytes.size())).toHex());
}

std::optional<ffi::Root> rootOf(const QString& hex)
{
    const QByteArray raw = QByteArray::fromHex(hex.toLatin1());
    ffi::Root r;
    if (raw.size() != qsizetype(r.bytes.size()))
        return std::nullopt;
    std::memcpy(r.bytes.data(), raw.constData(), r.bytes.size());
    return r;
}

/// Cancel as eD2K's CancelDownload (MFC MP_CANCEL): a complete file goes from disk too,
/// and from the eD2K share list (Ed3kShared follows it) once that worked
bool cancelDeleting(ed3k::Ed3kDownloads& d, const ffi::Root& root)
{
    const auto info = d.info(root);
    if (!info || info->state != ed3k::Ed3kDownloadInfo::State::Complete || info->path.isEmpty())
        return d.cancel(root);
    const QString path = QDir::cleanPath(info->path);
    if (theApp.uploadQueue)
        theApp.uploadQueue->releaseUploadFile(path);
    const bool done = d.cancel(root);   // closes the own share first
    if (!QFile::remove(path) && QFileInfo::exists(path)) {
        logWarning(QStringLiteral("Could not delete completed file: %1").arg(path));
        return done;
    }
    if (theApp.sharedFileList) {
        KnownFile* hit = nullptr;
        theApp.sharedFileList->forEachFile([&](KnownFile* f) {
            if (!hit && QDir::cleanPath(f->filePath()) == path)
                hit = f;
        });
        if (hit)
            theApp.sharedFileList->removeFile(hit);
    }
    return done;
}

QCborMap toCbor(const ed3k::Ed3kDownloadInfo& i)
{
    QCborMap m{
        {QStringLiteral("root"), hexOf(i.root)},
        {QStringLiteral("name"), i.name},
        {QStringLiteral("size"), qint64(i.size)},
        {QStringLiteral("done"), qint64(i.done)},
        {QStringLiteral("rate"), qint64(i.rate)},
        {QStringLiteral("transferred"), qint64(i.transferred)},
        {QStringLiteral("sources"), i.sources},
        {QStringLiteral("transferring"), i.transferring},
        {QStringLiteral("a4af"), i.a4af},
        {QStringLiteral("state"), int(i.state)},
        {QStringLiteral("priority"), i.priority},
        {QStringLiteral("category"), i.category},
        {QStringLiteral("owner"), i.owner},
        {QStringLiteral("path"), i.path},
        {QStringLiteral("addedOn"), i.addedOn},
        {QStringLiteral("completedOn"), i.completedOn},
        {QStringLiteral("error"), i.error},
        {QStringLiteral("previewPrio"), i.previewPrio},
        {QStringLiteral("isPreviewPossible"), i.previewPossible},
        {QStringLiteral("maxSources"), i.maxSources},
        // batch 15: "Node" / "Anonymous" (hops.md §1)
        {QStringLiteral("key"), s_downloads ? ed3k::Ed3kHide::keyName(s_downloads->keyOfRoot(i.root)) : QString()},
    };
    if (i.completionPermille >= 0) {
        m.insert(QStringLiteral("completionPermille"), i.completionPermille);
        m.insert(QStringLiteral("copying"), i.copying);
    }
    // other names of the root known here (tooltip, Details); its row stays in Transfers only
    if (QCborArray aliases = Ed3kUploadsBridge::aliasesOf(QByteArray(reinterpret_cast<const char*>(i.root.bytes.data()),
                                                                      qsizetype(i.root.bytes.size())));
        !aliases.isEmpty())
        m.insert(QStringLiteral("aliases"), aliases);
    // HTTP sources not used while hiding (tooltip, Details); absent: none dropped
    if (i.webDropped > 0)
        m.insert(QStringLiteral("webDropped"), i.webDropped);
    // the progress bar: held blocks as runs (BlockRuns.h), those asked for, sources per piece
    m.insert(QStringLiteral("blockSize"), qint64(kBarBlock));
    // HTTP whole read: its bytes on disk show as held, root checked at the end
    std::vector<bool> blocks = i.blocks;
    if (i.unchecked > 0) {
        std::uint64_t done = i.done;
        std::vector<bool> unverified(blocks.size(), false);
        const auto upTo = std::min(blocks.size(), std::size_t(i.unchecked / kBarBlock));
        for (std::size_t b = 0; b < upTo; ++b)
            if (!blocks[b]) {
                blocks[b] = true;
                unverified[b] = true;
                done += std::min(kBarBlock, i.size - std::uint64_t(b) * kBarBlock);
            }
        m.insert(QStringLiteral("done"), qint64(std::min(done, i.size)));
        // part detail (Display) draws them apart
        m.insert(QStringLiteral("unverified"), encodeBlockRuns(unverified));
    }
    m.insert(QStringLiteral("blocks"), encodeBlockRuns(blocks));
    if (!i.pending.empty())
        m.insert(QStringLiteral("pending"), encodeBlockRuns(i.pending));
    if (!i.availability.empty()) {
        m.insert(QStringLiteral("pieceSize"), qint64(kBarPiece));
        m.insert(QStringLiteral("avail"),
                 QByteArray(reinterpret_cast<const char*>(i.availability.data()), qsizetype(i.availability.size())));
    }
    return m;
}

void answer(const IpcMessage& msg, ed3k::Ed3kDownloads& d, const std::function<void(const IpcMessage&)>& reply)
{
    const int seq = msg.seqId();
    const auto ok = [&](const QCborValue& data = {}) { reply(IpcMessage::makeResult(seq, true, data)); };
    const auto bad = [&] {
        reply(failure(seq, QCoreApplication::translate("eMule::Ed3kDownloadsBridge", "Unknown ed3k download.")));
    };
    switch (msg.type()) {
    case IpcMsgType::GetEd3kDownloads:
        ok(listToCbor());
        break;
    case IpcMsgType::AddEd3kDownload: {
        const auto root = rootOf(msg.fieldString(0));
        if (!root) {
            bad();
            break;
        }
        ed3k::Ed3kDownloadSpec spec;
        spec.root = *root;
        spec.size = std::uint64_t(std::max<qint64>(0, msg.fieldInt(1)));
        spec.name = msg.fieldString(2);
        spec.category = int(msg.fieldInt(3));
        spec.paused = msg.fieldBool(4);
        if (auto r = d.add(spec))
            ok(QCborMap{{QStringLiteral("root"), hexOf(*root)}});
        else
            reply(failure(seq, r.error()));
        break;
    }
    case IpcMsgType::AddEd3kDownloadLink:
        if (auto r = d.addLink(msg.fieldString(0), int(msg.fieldInt(1)), msg.fieldBool(2)))
            ok(QCborMap{{QStringLiteral("root"), hexOf(*r)}});
        else
            reply(failure(seq, r.error()));
        break;
    case IpcMsgType::Ed3kDownloadControl: {
        const int action = int(msg.fieldInt(1));
        // a torrent/usenet/hoster row of single transfer mode: its own network acts
        if (SingleTransfer::isKey(msg.fieldString(0))) {
            if (SingleTransfer::control(msg.fieldString(0), action, msg.fieldInt(2)))
                ok(true);
            else
                bad();
            break;
        }
        if (action == 5) {
            d.clearCompleted();
            ok();
            break;
        }
        const auto root = rootOf(msg.fieldString(0));
        bool done = false;
        if (root) {
            switch (action) {
            case 0: done = d.pause(*root); break;
            case 1: done = d.resume(*root); break;
            case 2: done = d.cancel(*root); break;
            case 3: done = d.setPriority(*root, int(msg.fieldInt(2))); break;
            case 4: done = d.setCategory(*root, int(msg.fieldInt(2))); break;
            case 6: done = d.setPreviewPrio(*root, msg.fieldInt(2) != 0); break;   // preview parts first
            case 7: done = cancelDeleting(d, *root); break;   // Cancel/Delete: complete file off disk
            default: break;
            }
        }
        // pause of a paused one is no error worth a box: the list says what it is;
        // a done cancel has no row left to find
        if (root && (done || d.contains(*root)))
            ok(done);
        else
            bad();
        break;
    }
    default:
        reply(IpcMessage::makeError(seq, 400, QStringLiteral("Unknown ed3k request")));
        break;
    }
}
#endif

} // namespace

void setDownloads(ed3k::Ed3kDownloads* downloads)
{
#ifdef EMULE_HAVE_ED3K
    s_downloads = downloads;
#else
    (void)downloads;
#endif
}

bool isDownloadsRequest(IpcMsgType type)
{
    const int t = static_cast<int>(type);
    return t >= 1140 && t <= 1149;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isDownloadsRequest(msg.type()))
        return false;
    // ed3k off: nothing under config/ed3k is written
    if (Ed3kBridge::refuseWhileOff(msg, {IpcMsgType::GetEd3kDownloads}, reply))
        return true;
#ifdef EMULE_HAVE_ED3K
    if (s_downloads) {
        answer(msg, *s_downloads, reply);
        return true;
    }
#endif
    if (msg.type() == IpcMsgType::GetEd3kDownloads)
        reply(IpcMessage::makeResult(msg.seqId(), true, listToCbor()));
    else
        reply(failure(msg.seqId(), noNode()));
    return true;
}

bool downloadOf([[maybe_unused]] const QByteArray& root, bool& inProgress, QString& folder)
{
#ifdef EMULE_HAVE_ED3K
    ffi::Root r;
    if (!s_downloads || root.size() != qsizetype(r.bytes.size()))
        return false;
    std::memcpy(r.bytes.data(), root.constData(), r.bytes.size());
    const auto info = s_downloads->info(r);
    if (!info || info->state == ed3k::Ed3kDownloadInfo::State::Failed)
        return false;
    inProgress = info->state != ed3k::Ed3kDownloadInfo::State::Complete;
    folder = QFileInfo(info->path).absolutePath();
    return true;
#else
    (void)inProgress;
    (void)folder;
    return false;
#endif
}

bool fileOf([[maybe_unused]] const QString& hexRoot, [[maybe_unused]] QString& path, [[maybe_unused]] QString& name)
{
#ifdef EMULE_HAVE_ED3K
    const auto r = rootOf(hexRoot);
    if (!s_downloads || !r)
        return false;
    const auto info = s_downloads->info(*r);
    if (!info || info->state == ed3k::Ed3kDownloadInfo::State::Failed || info->path.isEmpty())
        return false;
    path = info->path;
    name = info->name;
    return true;
#else
    return false;
#endif
}

void setSearch([[maybe_unused]] ed3k::Ed3kSearch* search)
{
#ifdef EMULE_HAVE_ED3K
    s_search = search;
#endif
}

bool searchKad([[maybe_unused]] const QByteArray& root)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_search || root.size() != 32)
        return false;
    bool started = s_search->searchNotes(root);
    if (s_downloads) {
        ffi::Root r;
        std::copy_n(reinterpret_cast<const std::uint8_t*>(root.constData()), r.bytes.size(), r.bytes.begin());
        if (const auto info = s_downloads->info(r); info && !info->name.isEmpty())
            started = s_search->searchNames(root, info->name, info->size) || started;
    }
    return started;
#else
    return false;
#endif
}

std::optional<QCborMap> detailsOf([[maybe_unused]] const QString& hexRoot, [[maybe_unused]] bool generalOnly)
{
#ifdef EMULE_HAVE_ED3K
    const auto r = rootOf(hexRoot);
    if (!s_downloads || !r)
        return std::nullopt;
    const auto info = s_downloads->info(*r);
    if (!info)
        return std::nullopt;
    using State = ed3k::Ed3kDownloadInfo::State;
    // eD2K's tokens (CborSerializers statusToString, priorityToString)
    const auto status = [&] {
        switch (info->state) {
        case State::Paused:    return QStringLiteral("paused");
        case State::Verifying: return QStringLiteral("hashing");
        case State::Complete:  return QStringLiteral("complete");
        case State::Failed:    return QStringLiteral("error");
        default:               return QStringLiteral("ready");
        }
    }();
    static const char* const kPrio[] = {"low", "normal", "high", "veryHigh", "veryLow"};
    const QString prio = info->priority >= 0 && info->priority <= 4 ? QLatin1String(kPrio[info->priority])
                                                                    : QStringLiteral("normal");
    const qint64 done = qint64(std::min(info->done, info->size));
    QCborMap m{
        {QStringLiteral("network"), QStringLiteral("ed3k")},
        {QStringLiteral("hash"), hexOf(info->root)},
        {QStringLiteral("fileName"), info->name},
        {QStringLiteral("filePath"), info->path},
        {QStringLiteral("dataPath"), info->path},
        {QStringLiteral("fileSize"), qint64(info->size)},
        {QStringLiteral("completedSize"), done},
        {QStringLiteral("percentCompleted"), info->size > 0 ? 100.0 * double(done) / double(info->size) : 0.0},
        {QStringLiteral("downTransferred"), done},
        {QStringLiteral("sizeOnDisk"), QFileInfo(info->path).size()},
        {QStringLiteral("status"), status},
        {QStringLiteral("downPriority"), prio},
        {QStringLiteral("datarate"), qint64(info->rate)},
        {QStringLiteral("sourceCount"), info->sources},
        {QStringLiteral("transferringSrcCount"), info->transferring},
        {QStringLiteral("a4afSrcCount"), info->a4af},
        {QStringLiteral("addedOn"), info->addedOn},
        {QStringLiteral("ed3kRoot"), hexOf(info->root)},
        {QStringLiteral("ed3kKey"), ed3k::Ed3kHide::keyName(s_downloads->keyOfRoot(info->root))},
    };
    if (QCborArray aliases = Ed3kUploadsBridge::aliasesOf(QByteArray(reinterpret_cast<const char*>(info->root.bytes.data()),
                                                                      qsizetype(info->root.bytes.size())));
        !aliases.isEmpty())
        m.insert(QStringLiteral("ed3kAliases"), aliases);
    if (QCborArray joins = Ed3kUploadsBridge::joinsOf(QByteArray(reinterpret_cast<const char*>(info->root.bytes.data()),
                                                                 qsizetype(info->root.bytes.size())));
        !joins.isEmpty())
        m.insert(QStringLiteral("ed3kJoins"), joins);
    // HTTP sources not used while hiding (hops.md §1); absent: none dropped
    if (info->webDropped > 0)
        m.insert(QStringLiteral("ed3kWebDropped"), info->webDropped);
    if (generalOnly)
        return m;
    // Comments: NOTE records found by "Search Kad" (Ed3kSearch::searchNotes)
    const QByteArray rootBytes(reinterpret_cast<const char*>(info->root.bytes.data()), qsizetype(info->root.bytes.size()));
    QCborArray comments;
    if (s_search)
        for (const auto& n : s_search->notesOf(rootBytes)) {
            if (n.text.isEmpty() && n.rating == 0)
                continue;
            const QString author = ed3k::Ed3kSearch::noteAuthor(n.nick, n.publisher);
            comments.append(QCborMap{{QStringLiteral("userName"), author.isEmpty() ? QStringLiteral("ed3k") : author},
                                     {QStringLiteral("origin"), QStringLiteral("ed3k")},
                                     {QStringLiteral("rating"), qint64(n.rating)},
                                     {QStringLiteral("comment"), n.text}});
        }
    m.insert(QStringLiteral("comments"), comments);
    m.insert(QStringLiteral("notesSearchRunning"), s_search && s_search->notesRunning(rootBytes));
    // File Names and Metadata: what KEYWORD records of this root carry (Ed3kSearch::searchNames)
    QCborArray names;
    QCborArray tags;
    if (s_search) {
        const auto found = s_search->namesOf(rootBytes);
        for (const auto& n : found)
            names.append(QCborMap{{QStringLiteral("name"), n.name}, {QStringLiteral("count"), qint64(n.publishers)}});
        // per tag the value of the most published name that has one; eD2K's tag types (2 string, 3 uint32)
        const auto text = [&tags, &found](const QString& tag, QString ed3k::Ed3kSearch::FoundName::*field) {
            for (const auto& n : found)
                if (!(n.*field).isEmpty()) {
                    tags.append(QCborMap{{QStringLiteral("name"), tag}, {QStringLiteral("type"), 2}, {QStringLiteral("strValue"), n.*field}});
                    return;
                }
        };
        const auto number = [&tags, &found](const QString& tag, std::uint32_t ed3k::Ed3kSearch::FoundName::*field) {
            for (const auto& n : found)
                if (n.*field != 0) {
                    tags.append(QCborMap{{QStringLiteral("name"), tag}, {QStringLiteral("type"), 3}, {QStringLiteral("intValue"), qint64(n.*field)}});
                    return;
                }
        };
        text(QStringLiteral("Artist"), &ed3k::Ed3kSearch::FoundName::artist);
        text(QStringLiteral("Album"), &ed3k::Ed3kSearch::FoundName::album);
        text(QStringLiteral("Title"), &ed3k::Ed3kSearch::FoundName::title);
        number(QStringLiteral("Length"), &ed3k::Ed3kSearch::FoundName::duration);
        number(QStringLiteral("Bitrate"), &ed3k::Ed3kSearch::FoundName::bitrate);
        text(QStringLiteral("Codec"), &ed3k::Ed3kSearch::FoundName::codec);
    }
    m.insert(QStringLiteral("sourceNames"), names);
    m.insert(QStringLiteral("tags"), tags);
    m.insert(QStringLiteral("namesSearchRunning"), s_search && s_search->namesRunning(rootBytes));
    m.insert(QStringLiteral("kadConnected"), theApp.ed3kConnected && theApp.ed3kConnected());
    // Media Info: read from the file once its head is here (eD2K reads it at completion)
    if (info->state == State::Complete || info->previewPossible) {
        MediaInfo mi;
        if (extractMediaInfo(info->path, mi))
            insertMediaInfoFields(m, mi);
    }
    // ed3k Link tab
    ffi::FileLink link;
    link.root = info->root;
    link.size = info->size;
    if (!info->name.isEmpty())
        link.name = info->name.toStdString();
    if (const auto written = ffi::writeFileLink(link))
        m.insert(QStringLiteral("ed3kLink"), QString::fromStdString(*written));
    // Archive Preview: the missing ranges, eD2K's gap pairs (end inclusive)
    if (info->state != State::Complete) {
        QCborArray gaps;
        const auto& held = info->blocks;
        for (std::size_t b = 0; b < held.size();) {
            if (held[b]) {
                ++b;
                continue;
            }
            const std::size_t from = b;
            while (b < held.size() && !held[b])
                ++b;
            gaps.append(qint64(from * kBarBlock));
            gaps.append(qint64(std::min<std::uint64_t>(b * kBarBlock, info->size) - 1));
        }
        m.insert(QStringLiteral("archiveGaps"), gaps);
    }
    m.insert(QStringLiteral("canRecoverArchive"), false);
    return m;
#else
    return std::nullopt;
#endif
}

UsenetStreamSource streamFrom([[maybe_unused]] const QString& hexRoot, [[maybe_unused]] qint64 offset)
{
    UsenetStreamSource src;
#ifdef EMULE_HAVE_ED3K
    const auto r = rootOf(hexRoot);
    if (!s_downloads || !r || offset < 0)
        return src;
    const auto info = s_downloads->info(*r);
    if (!info || info->state == ed3k::Ed3kDownloadInfo::State::Failed || info->path.isEmpty())
        return src;
    const auto held = s_downloads->playFrom(*r, std::uint64_t(offset));
    if (!held)
        return src;
    src.found = true;
    src.fileName = info->name;
    src.totalSize = qint64(info->size);
    src.availableEnd = qint64(*held);
    src.complete = info->state == ed3k::Ed3kDownloadInfo::State::Complete;
    src.pieces = {{info->path, 0, 0, qint64(info->size)}};
#endif
    return src;
}

QString moveTo([[maybe_unused]] const QString& hexRoot, [[maybe_unused]] const QString& dir)
{
#ifdef EMULE_HAVE_ED3K
    const auto r = rootOf(hexRoot);
    if (!s_downloads || !r)
        return QStringLiteral("no such download");
    return s_downloads->moveTo(*r, dir);
#else
    return QStringLiteral("no ed3k downloads");
#endif
}

QString rename([[maybe_unused]] const QString& hexRoot, [[maybe_unused]] const QString& name)
{
#ifdef EMULE_HAVE_ED3K
    const auto r = rootOf(hexRoot);
    if (!s_downloads || !r)
        return QStringLiteral("no such download");
    const auto done = s_downloads->rename(*r, name);
    return done ? QString() : done.error();
#else
    return QStringLiteral("no ed3k downloads");
#endif
}

bool addWebSource([[maybe_unused]] const QString& hexRoot, [[maybe_unused]] const QString& url)
{
#ifdef EMULE_HAVE_ED3K
    const auto r = rootOf(hexRoot);
    return s_downloads && r && s_downloads->addWebSource(*r, url);
#else
    return false;
#endif
}

bool setSourceLimit([[maybe_unused]] const QString& hexRoot, [[maybe_unused]] int limit)
{
#ifdef EMULE_HAVE_ED3K
    const auto r = rootOf(hexRoot);
    return s_downloads && r && s_downloads->setSourceLimit(*r, limit);
#else
    return false;
#endif
}

std::optional<int> swapA4af([[maybe_unused]] const QString& hexRoot)
{
#ifdef EMULE_HAVE_ED3K
    const auto r = rootOf(hexRoot);
    if (!s_downloads || !r)
        return std::nullopt;
    return s_downloads->swapA4af(*r);
#else
    return std::nullopt;
#endif
}

std::optional<QCborMap> webSourceRow([[maybe_unused]] const QByteArray& root, [[maybe_unused]] qint64 peerRate)
{
#ifdef EMULE_HAVE_ED3K
    ffi::Root r;
    if (!s_downloads || root.size() != qsizetype(r.bytes.size()))
        return std::nullopt;
    std::memcpy(r.bytes.data(), root.constData(), r.bytes.size());
    const auto web = s_downloads->webSourceOf(r);
    const auto info = s_downloads->info(r);
    if (!web || !info)
        return std::nullopt;
    // buildSourcePartMap's codes: 1 both have, 2 it has / we need, 4 receiving
    const int pieces = int((info->size + kBarPiece - 1) / kBarPiece);
    constexpr std::size_t kPer = std::size_t(kBarPiece / kBarBlock);
    QCborArray partMap;
    for (int p = 0; p < pieces; ++p) {
        const auto first = std::size_t(p) * kPer;
        bool ours = first < web->have.size();
        for (std::size_t b = first; ours && b < std::min(first + kPer, web->have.size()); ++b)
            ours = web->have[b];
        if (!ours && web->unchecked > 0)
            ours = std::min(info->size, std::uint64_t(p + 1) * kBarPiece) <= web->unchecked;
        const bool active = web->fetching && std::uint64_t(p) == web->at / kBarPiece;
        partMap.append(ours ? 1 : active ? 4 : 2);
    }
    const qint64 rate = web->fetching ? std::max<qint64>(0, qint64(info->rate) - peerRate) : 0;
    const QUrl url(web->url);
    return QCborMap{
        {QStringLiteral("network"), QStringLiteral("ed3k")},
        {QStringLiteral("isUrl"), true},
        {QStringLiteral("userName"), web->url},
        {QStringLiteral("userHash"), QString(QStringLiteral("ed3k:web:") + hexOf(r))},
        {QStringLiteral("software"), QStringLiteral("HTTP")},
        {QStringLiteral("softwareId"), -1},
        {QStringLiteral("downloadState"), web->fetching ? QStringLiteral("Downloading") : QStringLiteral("OnQueue")},
        {QStringLiteral("transferredDown"), qint64(web->unchecked)},
        {QStringLiteral("sessionDown"), qint64(web->unchecked)},
        {QStringLiteral("datarate"), rate},
        {QStringLiteral("availPartCount"), pieces},
        {QStringLiteral("partCount"), pieces},
        {QStringLiteral("addr"), url.host()},
        {QStringLiteral("port"), url.port(url.scheme() == QLatin1String("https") ? 443 : 80)},
        {QStringLiteral("noViewSharedFiles"), true},
        {QStringLiteral("sourceFrom"), -1},
        {QStringLiteral("sourcePartMap"), partMap},
    };
#else
    return std::nullopt;
#endif
}

QCborMap listToCbor()
{
    QCborArray list;
    bool running = false;
#ifdef EMULE_HAVE_ED3K
    if (s_downloads) {
        running = s_downloads->isStarted();
        for (const auto& i : s_downloads->list())
            list.append(toCbor(i));
    }
#endif
    SingleTransfer::appendRows(list);
    return QCborMap{{QStringLiteral("running"), running}, {QStringLiteral("downloads"), list}};
}

void connectPushes(ed3k::Ed3kDownloads& downloads, std::function<void(const IpcMessage&)> broadcast)
{
#ifdef EMULE_HAVE_ED3K
    QObject::connect(&downloads, &ed3k::Ed3kDownloads::changed, &downloads, [broadcast] {
        IpcMessage m(IpcMsgType::PushEd3kDownloads, 0);
        m.append(listToCbor());
        broadcast(m);
    });
#else
    (void)downloads;
    (void)broadcast;
#endif
}

} // namespace eMule::Ed3kDownloadsBridge
