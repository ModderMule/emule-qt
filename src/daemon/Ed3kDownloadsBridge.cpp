/// @file Ed3kDownloadsBridge.cpp
/// @brief Plain ed3k downloads over IPC — implementation.

#include "Ed3kDownloadsBridge.h"

#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kDownloads.h"
#include "Ed3kSession.h"

#include <QObject>

#include <cstring>
#endif

namespace eMule::Ed3kDownloadsBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

#ifdef EMULE_HAVE_ED3K
ed3k::Ed3kDownloads* s_downloads = nullptr;
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

QCborMap toCbor(const ed3k::Ed3kDownloadInfo& i)
{
    return QCborMap{
        {QStringLiteral("root"), hexOf(i.root)},
        {QStringLiteral("name"), i.name},
        {QStringLiteral("size"), qint64(i.size)},
        {QStringLiteral("done"), qint64(i.done)},
        {QStringLiteral("rate"), qint64(i.rate)},
        {QStringLiteral("sources"), i.sources},
        {QStringLiteral("transferring"), i.transferring},
        {QStringLiteral("state"), int(i.state)},
        {QStringLiteral("priority"), i.priority},
        {QStringLiteral("category"), i.category},
        {QStringLiteral("owner"), i.owner},
        {QStringLiteral("path"), i.path},
        {QStringLiteral("addedOn"), i.addedOn},
        {QStringLiteral("completedOn"), i.completedOn},
        {QStringLiteral("error"), i.error},
    };
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
            default: break;
            }
        }
        // pause of a paused one is no error worth a box: the list says what it is
        if (root && d.contains(*root))
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
