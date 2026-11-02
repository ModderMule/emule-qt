/// @file Ed3kLinksBridge.cpp
/// @brief The other ed3k links over IPC (node, cache, node list file) — implementation.

#include "Ed3kLinksBridge.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCborMap>
#include <QObject>
#include <QPointer>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kSession.h"

#include <string>
#include <vector>
#endif

namespace eMule::Ed3kLinksBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

/// error 0: no ed3k node (not built or not running); else an ed3k::Error.
IpcMessage failure(int seqId, int error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

#ifdef EMULE_HAVE_ED3K
namespace ffi = ::ed3k;

QPointer<ed3k::Ed3kSession> s_session;
QMetaObject::Connection s_statusConn;
/// the pref applied in this run of the node
bool s_applied = false;

/// cacheConfigure with every link of @p links (the upload keys: never logged)
ffi::Result<> configure(ed3k::Ed3kSession& session, const QStringList& links)
{
    auto* e = session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    std::vector<std::string> all;
    for (const QString& l : links)
        all.push_back(l.toStdString());
    const auto ok = e->cacheConfigure(session.now(), all);
    session.pump();
    return ok;
}

void onStatus()
{
    if (!s_session)
        return;
    if (!s_session->isRunning()) {
        s_applied = false;
        return;
    }
    if (s_applied)
        return;
    s_applied = true;
    const QStringList links = thePrefs.ed3kCacheServers();
    if (links.isEmpty())
        return;
    if (auto ok = configure(*s_session, links); !ok)
        logWarning(QStringLiteral("ed3k: the %1 cache server(s) were not configured (error %2)").arg(links.size()).arg(int(ok.error())));
}
#endif

} // namespace

void setSession(ed3k::Ed3kSession* session)
{
#ifdef EMULE_HAVE_ED3K
    QObject::disconnect(s_statusConn);   // ours only
    s_session = session;
    s_applied = false;
    if (session) {
        s_statusConn = QObject::connect(session, &ed3k::Ed3kSession::statusChanged, session, [] { onStatus(); });
        onStatus();
    }
#else
    (void)session;
#endif
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    const IpcMsgType t = msg.type();
    if (t != IpcMsgType::OpenEd3kNodeLink && t != IpcMsgType::AddEd3kCacheLink && t != IpcMsgType::OpenEd3kNodeListFile)
        return false;
    const int seq = msg.seqId();
#ifdef EMULE_HAVE_ED3K
    auto* e = s_session ? s_session->engine() : nullptr;
    if (!e) {
        reply(failure(seq, 0));
        return true;
    }
    if (t == IpcMsgType::OpenEd3kNodeLink) {
        // bootstrap.md §1 source 3: the user said yes in the GUI
        const QByteArray text = msg.fieldString(0).trimmed().toUtf8();
        const auto link = ffi::parseNodeLink(std::string_view(text.constData(), std::size_t(text.size())));
        if (!link) {
            reply(failure(seq, int(link.error())));
            return true;
        }
        std::vector<ffi::Node> nodes;
        for (const auto& ep : link->endpoints)
            nodes.push_back(ffi::Node{link->id, ep});
        const auto started = e->bootstrap(s_session->now(), nodes);
        s_session->pump();
        if (!started) {
            reply(failure(seq, int(started.error())));
            return true;
        }
        logInfo(QStringLiteral("ed3k: node link opened, %1 lookup(s) started").arg(*started));
        reply(IpcMessage::makeResult(seq, true, QCborMap{{QStringLiteral("lookups"), qint64(*started)}}));
    } else if (t == IpcMsgType::AddEd3kCacheLink) {
        // http-source.md §4.1: asked in the GUI; the key is never logged
        const QString link = msg.fieldString(0).trimmed();
        QStringList links = thePrefs.ed3kCacheServers();
        const bool known = links.contains(link);
        if (!known)
            links << link;
        if (auto ok = configure(*s_session, links); !ok) {
            // the pref's links again, without this one
            (void)configure(*s_session, thePrefs.ed3kCacheServers());
            reply(failure(seq, int(ok.error())));
            return true;
        }
        if (!known)
            thePrefs.setEd3kCacheServers(links);
        logInfo(QStringLiteral("ed3k: cache server link added (%1 in all)").arg(links.size()));
        reply(IpcMessage::makeResult(seq, true, QCborMap{{QStringLiteral("servers"), qint64(links.size())}}));
    } else {
        // bootstrap.md §3 "By hand": opening the file is the decision to trust it
        const QByteArray file = msg.field(0).toByteArray();
        const auto nodes = e->bootstrapOpenFile(s_session->now(), std::span(reinterpret_cast<const std::byte*>(file.constData()),
                                                                            std::size_t(file.size())));
        s_session->pump();
        if (!nodes) {
            reply(failure(seq, int(nodes.error())));
            return true;
        }
        logInfo(QStringLiteral("ed3k: node list file opened, %1 node(s) added").arg(*nodes));
        reply(IpcMessage::makeResult(seq, true, QCborMap{{QStringLiteral("nodes"), qint64(*nodes)}}));
    }
#else
    reply(failure(seq, 0));
#endif
    return true;
}

} // namespace eMule::Ed3kLinksBridge
