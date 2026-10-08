#include "nntp/NntpCommand.h"

#include <QStringList>

#include <algorithm>

namespace eMule::usenet {

namespace {

/// "The command was fine, I cannot do it for this article/group" — a routing
/// fact, not a malfunction: 430/423/420/412 no such article, 411 no such group,
/// 451 taken down, 403 try elsewhere. NZBGet's CheckResponse reads 41x-43x the
/// same way.
///
/// Excluded on purpose: 400 (server is closing), 480-483 (auth/TLS wanted, the
/// socket's business) and 499. These must not be ProtocolError: that is fatal to
/// the connection, and UsenetWorker::finishJob() turns it into blockServer() —
/// one missing article backing the whole account off.
constexpr bool cannotSupply(int code)
{
    return code > 400 && code < 480;
}

/// "<id>" for the plain 430, with the server's own words for anything rarer.
QString refusalText(const QString& messageId, int code, const QString& text)
{
    if (code == 430)
        return QStringLiteral("<%1>").arg(messageId);
    return QStringLiteral("<%1> (%2 %3)").arg(messageId).arg(code).arg(text);
}

} // namespace

void NntpCommand::fail(NntpError e, QString text)
{
    // First failure wins. A command that already knows it got 430 must not have
    // that replaced by the "connection closed" that follows when the socket is
    // torn down as a result.
    if (m_error != NntpError::None)
        return;

    m_error = e;
    m_errorText = text.isEmpty() ? describeNntpError(e) : std::move(text);
}

// ---------------------------------------------------------------------------
// CAPABILITIES
// ---------------------------------------------------------------------------

QByteArray CapabilitiesCommand::requestLine() const
{
    return QByteArrayLiteral("CAPABILITIES");
}

bool CapabilitiesCommand::hasBodyFor(int code) const
{
    return code == 101;
}

void CapabilitiesCommand::onStatus(int code, const QString& text)
{
    if (code == 101)
        return;

    // 480/500 here is not fatal: plenty of providers predate RFC 3977 and
    // answer "500 command not recognized". The caller falls back to trying
    // the command it wanted anyway.
    fail(NntpError::ProtocolError,
         QStringLiteral("CAPABILITIES refused: %1 %2").arg(code).arg(text));
}

void CapabilitiesCommand::onBodyLine(QByteArrayView line)
{
    const auto token = QString::fromLatin1(line.data(), line.size()).trimmed();
    if (!token.isEmpty())
        m_capabilities.append(token);
}

bool CapabilitiesCommand::has(QLatin1StringView token) const
{
    // A capability line is "NAME arg arg", so compare the first word only.
    return std::ranges::any_of(m_capabilities, [token](const QString& cap) {
        return cap.startsWith(token, Qt::CaseInsensitive)
            && (cap.size() == token.size() || cap.at(token.size()).isSpace());
    });
}

// ---------------------------------------------------------------------------
// GROUP
// ---------------------------------------------------------------------------

GroupCommand::GroupCommand(QString group)
    : m_group(std::move(group))
{
}

QByteArray GroupCommand::requestLine() const
{
    return QByteArrayLiteral("GROUP ") + m_group.toLatin1();
}

void GroupCommand::onStatus(int code, const QString& text)
{
    // 411 is the answer; any other "cannot" is the same routing fact.
    if (cannotSupply(code)) {
        fail(NntpError::GroupNotFound,
             code == 411 ? QStringLiteral("No such newsgroup: %1").arg(m_group)
                         : QStringLiteral("No such newsgroup: %1 (%2 %3)")
                               .arg(m_group).arg(code).arg(text));
        return;
    }
    if (code != 211) {
        fail(NntpError::ProtocolError,
             QStringLiteral("GROUP %1 failed: %2 %3").arg(m_group).arg(code).arg(text));
        return;
    }

    // "211 <count> <low> <high> <group>"
    const auto parts = text.split(u' ', Qt::SkipEmptyParts);
    if (parts.size() < 3) {
        fail(NntpError::ProtocolError,
             QStringLiteral("Malformed GROUP response: %1").arg(text));
        return;
    }
    m_count = parts.at(0).toLongLong();
    m_low   = parts.at(1).toLongLong();
    m_high  = parts.at(2).toLongLong();
}

// ---------------------------------------------------------------------------
// STAT
// ---------------------------------------------------------------------------

StatCommand::StatCommand(QString messageId)
    : m_messageId(std::move(messageId))
{
}

QByteArray StatCommand::requestLine() const
{
    return QByteArrayLiteral("STAT ") + bracketMessageId(m_messageId);
}

void StatCommand::onStatus(int code, const QString& text)
{
    if (code == 223) {
        m_exists = true;
        return;
    }
    // Only 430 can arise from the message-id form, but see cannotSupply().
    if (cannotSupply(code)) {
        fail(NntpError::ArticleNotFound, refusalText(m_messageId, code, text));
        return;
    }
    fail(NntpError::ProtocolError,
         QStringLiteral("STAT failed: %1 %2").arg(code).arg(text));
}

// ---------------------------------------------------------------------------
// BODY
// ---------------------------------------------------------------------------

BodyCommand::BodyCommand(QString messageId, YencDecoder::Sink sink)
    : m_messageId(std::move(messageId))
    , m_decoder(std::move(sink))
{
}

QByteArray BodyCommand::requestLine() const
{
    return QByteArrayLiteral("BODY ") + bracketMessageId(m_messageId);
}

bool BodyCommand::hasBodyFor(int code) const
{
    // 222 introduces the article; 430 does not. Whether a body follows is a
    // property of the response, not of the request.
    return code == 222;
}

void BodyCommand::onStatus(int code, const QString& text)
{
    if (code == 222) {
        m_decoder.reset();
        return;
    }
    if (cannotSupply(code)) {
        // The routing signal that sends this article to the next priority
        // level. A normal outcome, not a malfunction — 430 and its rarer
        // relatives alike (423, 451 takedown, ...).
        fail(NntpError::ArticleNotFound, refusalText(m_messageId, code, text));
        return;
    }
    fail(NntpError::ProtocolError,
         QStringLiteral("BODY failed: %1 %2").arg(code).arg(text));
}

void BodyCommand::onBodyLine(QByteArrayView line)
{
    m_decoder.feedLine(line);
}

qsizetype BodyCommand::onBodyData(QByteArrayView wire, bool& ended)
{
    return m_decoder.feedRaw(wire, ended);
}

void BodyCommand::onComplete()
{
    if (failed())
        return;

    switch (m_decoder.status()) {
    case YencDecoder::Status::Ok:
        return;
    // Every way a body can be undecodable, and all of them mean the same thing:
    // what this server stores is unusable to us, and it will hand out the same
    // copy next time. So exclude it and ask the next server, as for a 430 —
    // NZBGet's RetryOnCrcError=no default.
    //
    // It is emphatically *not* a transport fault. This runs after the
    // terminating "." on a connection that is back to Ready, so calling it one
    // dropped a healthy connection and backed the account off for a minute.
    case YencDecoder::Status::CrcMismatch:
    case YencDecoder::Status::SizeMismatch:
    case YencDecoder::Status::NoBinaryData:
    case YencDecoder::Status::Incomplete:
    case YencDecoder::Status::Malformed:
        fail(NntpError::ArticleCorrupt, m_decoder.statusText());
        return;
    }
}

// ---------------------------------------------------------------------------

QByteArray bracketMessageId(const QString& messageId)
{
    const auto trimmed = QStringView(messageId).trimmed();
    if (trimmed.startsWith(u'<') && trimmed.endsWith(u'>'))
        return trimmed.toLatin1();
    return QByteArrayLiteral("<") + trimmed.toLatin1() + QByteArrayLiteral(">");
}

} // namespace eMule::usenet
