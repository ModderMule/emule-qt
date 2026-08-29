#pragma once

/// @file NntpCommand.h
/// @brief One NNTP command, as a state machine separate from the transport.
///
/// This split exists on purpose and from day one. ngPost hit exactly this
/// problem — same transport, different command sequence — and answered it by
/// copy-pasting `NntpConnection` into `NntpCheckCon` with a different state
/// enum. Two near-identical 500-line files then have to be fixed twice.
///
/// So `NntpSocket` owns *transport, greeting and authentication* and knows
/// nothing about GROUP or BODY; everything after "connection is ready" is an
/// `NntpCommand` subclass. Adding a verb means adding a subclass, never
/// touching the socket.

#include "decode/YencDecoder.h"
#include "nntp/NntpError.h"

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace eMule::usenet {

/// Base for every post-authentication command.
///
/// Lifecycle, driven by NntpSocket:
///   requestLine()  -> written to the socket
///   onStatus()     -> the 3-digit response line
///   onBodyLine()   -> zero or more dot-unstuffed lines, iff hasBodyFor(code)
///   onComplete()   -> exactly once, whether it succeeded or failed
///
/// Not a QObject: commands are short-lived, owned by whoever issued them, and
/// signal completion through the socket. Keeping them plain keeps them cheap
/// enough to allocate per article.
class NntpCommand {
public:
    NntpCommand() = default;
    virtual ~NntpCommand() = default;

    NntpCommand(const NntpCommand&) = delete;
    NntpCommand& operator=(const NntpCommand&) = delete;

    /// The command text, without the trailing CRLF.
    [[nodiscard]] virtual QByteArray requestLine() const = 0;

    /// Whether @p code introduces a dot-terminated multi-line block.
    /// In NNTP this is a property of the response, not the request: BODY
    /// answers 222 with a body and 430 without one.
    [[nodiscard]] virtual bool hasBodyFor(int code) const { Q_UNUSED(code); return false; }

    /// The status line arrived. @p text is the line with the code stripped.
    /// Call fail() here for any code this command treats as an error.
    virtual void onStatus(int code, const QString& text) = 0;

    /// One body line, already dot-unstuffed, without its line terminator.
    /// A view, not a copy: article bodies reach hundreds of KB and the whole
    /// point of streaming them is not to hold one twice.
    virtual void onBodyLine(QByteArrayView line) { Q_UNUSED(line); }

    /// Called once when the command is done, successfully or not.
    virtual void onComplete() {}

    [[nodiscard]] bool failed() const { return m_error != NntpError::None; }
    [[nodiscard]] NntpError error() const { return m_error; }
    [[nodiscard]] const QString& errorText() const { return m_errorText; }

    /// Record a failure. The first one wins — a later transport error must not
    /// overwrite the specific reason the command already knows.
    void fail(NntpError e, QString text = {});

private:
    NntpError m_error = NntpError::None;
    QString m_errorText;
};

// ---------------------------------------------------------------------------
// Phase 1 commands. BODY arrives with the article fetcher in phase 2.
// ---------------------------------------------------------------------------

/// CAPABILITIES (RFC 3977 §5.2). Used to discover whether the server offers
/// STARTTLS, and by the Options "Test" button to show what the account supports.
class CapabilitiesCommand final : public NntpCommand {
public:
    [[nodiscard]] QByteArray requestLine() const override;
    [[nodiscard]] bool hasBodyFor(int code) const override;
    void onStatus(int code, const QString& text) override;
    void onBodyLine(QByteArrayView line) override;

    [[nodiscard]] const QStringList& capabilities() const { return m_capabilities; }
    [[nodiscard]] bool has(QLatin1StringView token) const;

private:
    QStringList m_capabilities;
};

/// GROUP (RFC 3977 §6.1.1). Only needed for servers that refuse message-id
/// access without a selected group; see NewsServer::joinGroup.
class GroupCommand final : public NntpCommand {
public:
    explicit GroupCommand(QString group);

    [[nodiscard]] QByteArray requestLine() const override;
    void onStatus(int code, const QString& text) override;

    [[nodiscard]] qint64 articleCount() const { return m_count; }
    [[nodiscard]] qint64 lowWaterMark() const { return m_low; }
    [[nodiscard]] qint64 highWaterMark() const { return m_high; }

private:
    QString m_group;
    qint64 m_count = 0;
    qint64 m_low = 0;
    qint64 m_high = 0;
};

/// STAT (RFC 3977 §6.2.4). Asks whether an article exists without transferring
/// it — the cheap way to check an NZB's health, and how a fill server is probed
/// before an article is escalated to it.
class StatCommand final : public NntpCommand {
public:
    explicit StatCommand(QString messageId);

    [[nodiscard]] QByteArray requestLine() const override;
    void onStatus(int code, const QString& text) override;

    [[nodiscard]] bool exists() const { return m_exists; }

private:
    QString m_messageId;
    bool m_exists = false;
};

/// BODY (RFC 3977 §6.2.3) — one article, streamed straight into a yEnc decoder.
///
/// Nothing buffers the article. Bodies run to roughly 750 KB encoded, and the
/// whole point of a per-line API from the socket down to the decoder is not to
/// hold one twice. The sink receives decoded bytes as they are produced.
class BodyCommand final : public NntpCommand {
public:
    /// @p sink receives decoded payload in order. It is called from the socket's
    /// thread, so it must not block.
    BodyCommand(QString messageId, YencDecoder::Sink sink);

    [[nodiscard]] QByteArray requestLine() const override;
    [[nodiscard]] bool hasBodyFor(int code) const override;
    void onStatus(int code, const QString& text) override;
    void onBodyLine(QByteArrayView line) override;
    void onComplete() override;

    /// Header fields and the CRC verdict. Valid once the command completes;
    /// `fileName()` is the only place an obfuscated post's real name appears.
    [[nodiscard]] const YencDecoder& decoder() const { return m_decoder; }

    [[nodiscard]] const QString& messageId() const { return m_messageId; }

private:
    QString m_messageId;
    YencDecoder m_decoder;
};

/// Wraps a bare message-id in the angle brackets the wire format requires.
/// NZBs store them both ways, and a doubled bracket is a silent 430.
[[nodiscard]] QByteArray bracketMessageId(const QString& messageId);

} // namespace eMule::usenet

Q_DECLARE_METATYPE(eMule::usenet::NntpCommand*)
