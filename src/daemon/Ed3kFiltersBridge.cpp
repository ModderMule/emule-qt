/// @file Ed3kFiltersBridge.cpp
/// @brief ed3k filter lists over IPC — implementation.

#include "Ed3kFiltersBridge.h"

#include <QCborArray>
#include <QCborMap>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kFilters.h"

#include <QObject>
#endif

namespace eMule::Ed3kFiltersBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

ed3k::Ed3kFilters* s_filters = nullptr;

/// error 0: no ed3k node (not built or not running); else an ed3k::Error.
IpcMessage failure(int seqId, int error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

#ifdef EMULE_HAVE_ED3K
namespace ffi = ::ed3k;
using ed3k::Ed3kFilters;

QString hexOf(std::span<const std::uint8_t> bytes)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).toHex());
}

std::optional<Ed3kFilters::EntryId> idOf(const QString& hex)
{
    const QByteArray raw = QByteArray::fromHex(hex.trimmed().toLatin1());
    Ed3kFilters::EntryId id{};
    if (raw.size() != qsizetype(id.size()))
        return std::nullopt;
    std::copy(raw.cbegin(), raw.cend(), reinterpret_cast<char*>(id.data()));
    return id;
}

std::optional<ffi::FilterSubject> subjectOf(qint64 v)
{
    if (v < 1 || v > 3)
        return std::nullopt;
    return ffi::FilterSubject(v);
}

template <typename T>
IpcMessage result(int seq, const ffi::Result<T>& ok, QCborMap data = {})
{
    return ok ? IpcMessage::makeResult(seq, true, data) : failure(seq, int(ok.error()));
}

QCborMap judgementToCbor(const ffi::Engine::FilterJudgement& j)
{
    const auto text = [](const std::string& s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); };
    QCborArray said;
    for (const auto& s : j.said)
        said.append(QCborMap{{QStringLiteral("filter"), qint64(s.filter)},
                             {QStringLiteral("verdict"), int(s.verdict)},
                             {QStringLiteral("label"), text(s.label)},
                             {QStringLiteral("title"), text(s.title)},
                             {QStringLiteral("reason"), text(s.reason)}});
    return QCborMap{{QStringLiteral("hide"), j.hide},
                    {QStringLiteral("refuse"), j.refuse},
                    {QStringLiteral("shown"), j.shown},
                    {QStringLiteral("own"), j.own ? int(j.own->first) : -1},
                    {QStringLiteral("ownReason"), j.own ? text(j.own->second) : QString()},
                    {QStringLiteral("said"), said}};
}

void answer(const IpcMessage& msg, Ed3kFilters& f, const std::function<void(const IpcMessage&)>& reply)
{
    const int seq = msg.seqId();
    switch (msg.type()) {
    case IpcMsgType::GetEd3kFilters:
        reply(IpcMessage::makeResult(seq, true, filtersToCbor()));
        break;
    case IpcMsgType::FollowEd3kFilter: {
        const auto id = f.follow(msg.fieldString(0), msg.fieldString(1));
        reply(result(seq, id, id ? QCborMap{{QStringLiteral("id"), qint64(*id)}} : QCborMap{}));
        break;
    }
    case IpcMsgType::OpenEd3kFilterFile: {
        const auto id = f.openFile(msg.fieldString(0), msg.field(1).toByteArray());
        reply(result(seq, id, id ? QCborMap{{QStringLiteral("id"), qint64(*id)}} : QCborMap{}));
        break;
    }
    case IpcMsgType::UnfollowEd3kFilter:
        reply(result(seq, f.unfollow(std::uint32_t(msg.fieldInt(0)))));
        break;
    case IpcMsgType::SetEd3kFilterMode:
        reply(result(seq, f.setMode(std::uint32_t(msg.fieldInt(0)), msg.fieldBool(1), msg.fieldBool(2))));
        break;
    case IpcMsgType::SetEd3kFilterOwn: {
        const auto subject = subjectOf(msg.fieldInt(0));
        const auto id = idOf(msg.fieldString(1));
        const qint64 verdict = msg.fieldInt(2);
        if (!subject || !id || verdict < 0 || verdict > 4) {
            reply(failure(seq, int(ffi::Error::Argument)));
            break;
        }
        reply(result(seq, f.setOwn(*subject, *id, ffi::FilterVerdict(verdict), msg.fieldString(3))));
        break;
    }
    case IpcMsgType::JudgeEd3kFilter: {
        const auto subject = subjectOf(msg.fieldInt(0));
        const auto id = idOf(msg.fieldString(1));
        if (!subject || !id) {
            reply(failure(seq, int(ffi::Error::Argument)));
            break;
        }
        const auto j = f.judgeOne(*subject, *id);
        reply(result(seq, j, j ? judgementToCbor(*j) : QCborMap{}));
        break;
    }
    default:
        reply(IpcMessage::makeError(seq, 400, QStringLiteral("Unknown ed3k request")));
        break;
    }
}
#endif

} // namespace

void setFilters(ed3k::Ed3kFilters* filters)
{
    s_filters = filters;
}

bool isFiltersRequest(IpcMsgType type)
{
    const int t = static_cast<int>(type);
    return t >= 1130 && t <= 1139;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isFiltersRequest(msg.type()))
        return false;
#ifdef EMULE_HAVE_ED3K
    if (s_filters && s_filters->isRunning()) {
        answer(msg, *s_filters, reply);
        return true;
    }
    if (msg.type() == IpcMsgType::GetEd3kFilters) {
        reply(IpcMessage::makeResult(msg.seqId(), true, filtersToCbor()));
        return true;
    }
#endif
    reply(failure(msg.seqId(), 0));
    return true;
}

QCborMap filtersToCbor()
{
#ifdef EMULE_HAVE_ED3K
    if (!s_filters)
        return {{QStringLiteral("running"), false}};
    QCborArray lists;
    if (s_filters->isRunning())
        for (const auto& l : s_filters->lists())
            lists.append(QCborMap{{QStringLiteral("id"), qint64(l.id)},
                                  {QStringLiteral("label"), l.label},
                                  {QStringLiteral("mode"), (l.hide ? 1 : 0) | (l.refuse ? 2 : 0)},
                                  {QStringLiteral("byHand"), l.byHand},
                                  {QStringLiteral("set"), l.set ? hexOf(*l.set) : QString()},
                                  {QStringLiteral("seq"), l.seq ? qint64(std::min<std::uint64_t>(*l.seq, INT64_MAX)) : qint64(-1)},
                                  {QStringLiteral("files"), qint64(l.files)},
                                  {QStringLiteral("entries"), qint64(std::min<std::uint64_t>(l.entries, INT64_MAX))},
                                  {QStringLiteral("waiting"), qint64(l.waiting)}});
    QCborArray own;
    for (const auto& o : s_filters->own())
        own.append(QCborMap{{QStringLiteral("subject"), int(o.subject)},
                            {QStringLiteral("id"), hexOf(o.id)},
                            {QStringLiteral("verdict"), int(o.verdict)},
                            {QStringLiteral("reason"), o.reason}});
    return QCborMap{{QStringLiteral("running"), s_filters->isRunning()},
                    {QStringLiteral("lists"), lists},
                    {QStringLiteral("ownMode"), (s_filters->ownHide() ? 1 : 0) | (s_filters->ownRefuse() ? 2 : 0)},
                    {QStringLiteral("own"), own}};
#else
    return {{QStringLiteral("running"), false}};
#endif
}

void connectPushes(ed3k::Ed3kFilters& filters, std::function<void(const IpcMessage&)> broadcast)
{
#ifdef EMULE_HAVE_ED3K
    QObject::connect(&filters, &ed3k::Ed3kFilters::filtersChanged, &filters,
                     [broadcast] { broadcast(IpcMessage(IpcMsgType::PushEd3kFiltersChanged, 0)); });
#else
    (void)filters;
    (void)broadcast;
#endif
}

} // namespace eMule::Ed3kFiltersBridge
