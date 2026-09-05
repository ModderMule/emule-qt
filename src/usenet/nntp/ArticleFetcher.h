#pragma once

/// @file ArticleFetcher.h
/// @brief One segment, end to end: BODY -> decode -> place in the file.
///
/// The join between the three layers, and deliberately the only place that
/// knows about all of them. It holds no policy: which server to ask and what to
/// do about a failure belong to the pool and the queue respectively. What it
/// owns is the sequence — optional GROUP, then BODY, decode straight into the
/// writer at the offset the article states, and report a typed outcome.
///
/// Nothing buffers the article. The decoded bytes go from the socket's line
/// through the decoder into the file, and the only copy that exists is the
/// decoder's per-line scratch buffer.

#include "nntp/NntpError.h"
#include "nzb/NzbInfo.h"

#include <QObject>
#include <QString>

#include <memory>

namespace eMule::usenet {

class ArticleWriter;
class BodyCommand;
class GroupCommand;
class NntpCommand;
class NntpSocket;

class ArticleFetcher : public QObject {
    Q_OBJECT

public:
    explicit ArticleFetcher(QObject* parent = nullptr);
    ~ArticleFetcher() override;

    /// Fetch @p segment over @p socket and write it through @p writer.
    ///
    /// @p socket must already be ready(); the fetcher never connects, because
    /// connection lifetime belongs to the pool. @p writer must be open, and
    /// must outlive the finished() signal.
    ///
    /// @p group is issued first when non-empty. Almost no provider needs it for
    /// message-id access and it costs a round trip, so it is driven by
    /// NewsServer::joinGroup rather than done unconditionally.
    void fetch(NntpSocket* socket, const NzbSegment& segment,
               ArticleWriter* writer, const QString& group = {});

    /// Filename the article declared in `=ybegin name=`. For an obfuscated post
    /// this is the only place the real name appears, so the queue reads it from
    /// the first segment of each file.
    [[nodiscard]] const QString& articleFileName() const { return m_articleFileName; }

    /// Total size of the whole file, from `=ybegin size=`.
    [[nodiscard]] qint64 declaredFileSize() const { return m_declaredFileSize; }

    /// Decoded bytes this segment contributed.
    [[nodiscard]] qint64 decodedBytes() const { return m_decodedBytes; }

    /// Zero-based offset into the *final file* where those bytes landed, taken
    /// from the article's own `=ypart begin` (already `begin - 1`).
    ///
    /// It is the only authority on the question: an NZB's `<segment bytes>` is
    /// the encoded size, so no offset can be derived from the NZB at all. The
    /// queue needs it to know which byte ranges of a half-downloaded file are
    /// actually readable — see UsenetFileState::written.
    [[nodiscard]] qint64 decodedOffset() const { return m_decodedOffset; }

signals:
    /// The segment is done. @p error is NntpError::None on success.
    ///
    /// escalatesToNextLevel(error) distinguishes "this server does not have it"
    /// from "this connection went wrong" — the distinction the queue routes on.
    void finished(eMule::usenet::NntpError error, const QString& text);

private:
    void onCommandFinished(NntpCommand* command);
    void startBody();
    void finish(NntpError error, const QString& text);

    NntpSocket* m_socket = nullptr;
    ArticleWriter* m_writer = nullptr;
    NzbSegment m_segment;
    QString m_group;

    std::unique_ptr<GroupCommand> m_groupCommand;
    std::unique_ptr<BodyCommand> m_bodyCommand;

    QString m_articleFileName;
    qint64 m_declaredFileSize = 0;
    qint64 m_decodedBytes = 0;
    qint64 m_decodedOffset = 0;

    /// Set when a write fails mid-article. The decode still runs to completion
    /// so the connection stays in sync, but the outcome is the write error.
    QString m_writeError;
    bool m_positioned = false;
};

} // namespace eMule::usenet
