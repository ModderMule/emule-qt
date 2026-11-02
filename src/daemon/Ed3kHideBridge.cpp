/// @file Ed3kHideBridge.cpp
/// @brief Hiding over IPC — implementation.

#include "Ed3kHideBridge.h"
#include "Ed3kBridge.h"
#include "Ed3kOffGate.h"

#include <QCoreApplication>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kDownloads.h"
#include "Ed3kHide.h"
#include "Ed3kSession.h"
#include "Ed3kShared.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#endif

namespace eMule::Ed3kHideBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

IpcMessage failure(int seqId, const QString& error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

#ifdef EMULE_HAVE_ED3K
namespace ffi = ::ed3k;

ed3k::Ed3kSession* s_session = nullptr;
ed3k::Ed3kShared* s_shared = nullptr;
ed3k::Ed3kDownloads* s_downloads = nullptr;

std::optional<ffi::Root> rootOf(const QByteArray& raw)
{
    if (raw.size() != 32)
        return std::nullopt;
    ffi::Root r;
    std::copy_n(reinterpret_cast<const std::uint8_t*>(raw.constData()), 32, r.bytes.begin());
    return r;
}

void moveKey(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    // a root, or the MD4 of a shared file (the Shared Files list knows no root)
    const QByteArray raw = QByteArray::fromHex(msg.fieldString(0).toLatin1());
    const auto root = raw.size() == 16 && s_shared ? s_shared->hashedRootOf(raw) : rootOf(raw);
    if (!root) {
        reply(failure(msg.seqId(), QCoreApplication::translate("eMule::Ed3kHideBridge", "Not an ed3k root.")));
        return;
    }
    const ffi::KeyKind to = msg.fieldBool(1) ? ffi::KeyKind::Anon : ffi::KeyKind::Node;
    // the anonymous key takes files only while it runs (nothing goes there behind the user)
    if (to == ffi::KeyKind::Anon && !s_session->hide().hiding()) {
        reply(failure(msg.seqId(), QCoreApplication::translate("eMule::Ed3kHideBridge", "Hiding is off: turn on \"Hide my node\" first.")));
        return;
    }
    // hops.md §1: the GUI asked; closed under one key, opened under the other
    const bool moved = (s_shared && s_shared->moveFileKey(*root, to)) || (s_downloads && s_downloads->moveKey(*root, to));
    if (!moved) {
        reply(failure(msg.seqId(), QCoreApplication::translate("eMule::Ed3kHideBridge", "Not shared or downloaded over ed3k, or already under that key.")));
        return;
    }
    reply(IpcMessage::makeResult(msg.seqId(), true, QCborMap{{QStringLiteral("key"), ed3k::Ed3kHide::keyName(to)}}));
}
#endif

} // namespace

void setTargets([[maybe_unused]] ed3k::Ed3kSession* session, [[maybe_unused]] ed3k::Ed3kShared* shared,
                [[maybe_unused]] ed3k::Ed3kDownloads* downloads)
{
#ifdef EMULE_HAVE_ED3K
    s_session = session;
    s_shared = shared;
    s_downloads = downloads;
#endif
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    const IpcMsgType t = msg.type();
    if (t != IpcMsgType::NewEd3kAnonKey && t != IpcMsgType::MoveEd3kFileKey)
        return false;
    if (Ed3kBridge::refuseWhileOff(msg, {}, reply))
        return true;
#ifdef EMULE_HAVE_ED3K
    if (!s_session || !s_session->isRunning()) {
        reply(failure(msg.seqId(), QCoreApplication::translate("eMule::Ed3kHideBridge", "ed3k is not running.")));
        return true;
    }
    if (t == IpcMsgType::NewEd3kAnonKey) {
        QString err;
        if (s_session->hide().newAnonKey(&err))
            reply(IpcMessage::makeResult(msg.seqId(), true));
        else
            reply(failure(msg.seqId(), QCoreApplication::translate("eMule::Ed3kHideBridge", "No new anonymous key: %1").arg(err)));
    } else {
        moveKey(msg, reply);
    }
#else
    reply(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ed3k is not built in")));
#endif
    return true;
}

QCborMap statusToCbor()
{
    return Ed3kBridge::hideToCbor();
}

QString sharedKey([[maybe_unused]] const QByteArray& md4)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_shared || !s_session || !s_session->isRunning())
        return {};
    const auto k = s_shared->keyOfFile(md4);
    return k ? ed3k::Ed3kHide::keyName(*k) : QString();
#else
    return {};
#endif
}

void connectPushes([[maybe_unused]] ed3k::Ed3kSession& session,
                   [[maybe_unused]] std::function<void(const IpcMessage&)> broadcast)
{
#ifdef EMULE_HAVE_ED3K
    // a start shares many files: one push for a burst
    auto* timer = new QTimer(&session.hide());
    timer->setSingleShot(true);
    timer->setInterval(500);
    QObject::connect(timer, &QTimer::timeout, &session, [s = QPointer(&session), broadcast] {
        if (!s)
            return;
        IpcMessage msg(IpcMsgType::PushEd3kHideChanged, 0);
        msg.append(statusToCbor());
        broadcast(msg);
        emit s->statusChanged();   // PushEd3kStatus: the status bar's tooltip
    });
    QObject::connect(&session.hide(), &ed3k::Ed3kHide::changed, timer, [timer] {
        if (!timer->isActive())
            timer->start();
    });
#endif
}

} // namespace eMule::Ed3kHideBridge
