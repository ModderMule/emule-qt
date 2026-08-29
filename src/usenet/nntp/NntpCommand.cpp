#include "nntp/NntpCommand.h"

#include <QStringList>

#include <algorithm>

namespace eMule::usenet {

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
    if (code == 411) {
        fail(NntpError::GroupNotFound,
             QStringLiteral("No such newsgroup: %1").arg(m_group));
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
    // 430 is the expected "this server does not have it" answer, and the whole
    // reason STAT exists. It is a routing fact, not a malfunction, so it must
    // not be logged as an error anywhere up the stack.
    if (code == 430) {
        fail(NntpError::ArticleNotFound, QStringLiteral("<%1>").arg(m_messageId));
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
    if (code == 430) {
        // The routing signal that sends this article to the next priority
        // level. A normal outcome, not a malfunction.
        fail(NntpError::ArticleNotFound, QStringLiteral("<%1>").arg(m_messageId));
        return;
    }
    fail(NntpError::ProtocolError,
         QStringLiteral("BODY failed: %1 %2").arg(code).arg(text));
}

void BodyCommand::onBodyLine(QByteArrayView line)
{
    m_decoder.feedLine(line);
}

void BodyCommand::onComplete()
{
    if (failed())
        return;

    switch (m_decoder.status()) {
    case YencDecoder::Status::Ok:
        return;
    case YencDecoder::Status::CrcMismatch:
    case YencDecoder::Status::SizeMismatch:
        // Corruption is a *transport* fault, not a missing article: the server
        // has it, this copy arrived damaged. Retrying here is right; escalating
        // to a fill server for it would waste the block account.
        fail(NntpError::ProtocolError, m_decoder.statusText());
        return;
    case YencDecoder::Status::NoBinaryData:
    case YencDecoder::Status::Incomplete:
    case YencDecoder::Status::Malformed:
        fail(NntpError::ProtocolError, m_decoder.statusText());
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
