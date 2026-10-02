#include "nntp/ArticleFetcher.h"

#include "nntp/NntpCommand.h"
#include "nntp/NntpSocket.h"
#include "queue/ArticleWriter.h"

#include <algorithm>
#include <limits>

namespace eMule::usenet {

ArticleFetcher::ArticleFetcher(QObject* parent)
    : QObject(parent)
{
}

ArticleFetcher::~ArticleFetcher() = default;

void ArticleFetcher::fetch(NntpSocket* socket, const NzbSegment& segment,
                           ArticleWriter* writer, const QString& group)
{
    m_mode = Mode::Body;
    m_writer = writer;
    if (!beginRun(socket, segment, group))
        return;

    // Checked after beginRun() cleared state but before anything is sent: a
    // closed writer is a local fault, and reporting it as a protocol error keeps
    // the queue from climbing the failover ladder over a full disk.
    if (!m_writer || !m_writer->isOpen()) {
        finish(NntpError::WriteFailed, QStringLiteral("Output file is not open"));
        return;
    }

    startVerb();
}

void ArticleFetcher::stat(NntpSocket* socket, const NzbSegment& segment,
                          const QString& group)
{
    m_mode = Mode::Stat;
    m_writer = nullptr;   // deliberately: a probe must not be able to write
    if (!beginRun(socket, segment, group))
        return;
    startVerb();
}

qint64 ArticleFetcher::pipelineLookahead(qint64 bytesPerSecond, qint64 bufferCap,
                                         qint64 latencyMs)
{
    if (bytesPerSecond <= 0)
        return std::numeric_limits<qint64>::max();
    return 4 * bufferCap + bytesPerSecond * 2 * std::max<qint64>(latencyMs, 0) / 1000;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

bool ArticleFetcher::beginRun(NntpSocket* socket, const NzbSegment& segment,
                              const QString& group)
{
    m_socket = socket;
    m_segment = segment;
    m_group = group;
    m_articleFileName.clear();
    m_declaredFileSize = 0;
    m_decodedBytes = 0;
    m_decodedOffset = 0;
    m_writeError.clear();
    m_positioned = false;
    m_articleExists = false;
    m_nearlyDone = false;
    m_groupCommand.reset();
    m_bodyCommand.reset();
    m_statCommand.reset();

    // Busy is fine: the command is pipelined behind the running one.
    if (!m_socket || !m_socket->acceptsCommands()) {
        finish(NntpError::Disconnected, QStringLiteral("Connection is not ready"));
        return false;
    }

    connect(m_socket, &NntpSocket::commandFinished,
            this, &ArticleFetcher::onCommandFinished, Qt::UniqueConnection);
    return true;
}

void ArticleFetcher::startVerb()
{
    // GROUP once per connection and group, not per article: the round trip also
    // kept a follower's BODY from being pipelined. BODY by message-id does not
    // depend on the group, so a GROUP still in flight ahead of us is harmless.
    if (!m_group.isEmpty() && m_socket->selectedGroup() != m_group) {
        m_groupCommand = std::make_unique<GroupCommand>(m_group);
        m_socket->setSelectedGroup(m_group);
        m_socket->sendCommand(m_groupCommand.get());
        return;
    }
    if (m_mode == Mode::Stat)
        startStat();
    else
        startBody();
}

void ArticleFetcher::startStat()
{
    m_statCommand = std::make_unique<StatCommand>(m_segment.messageId);
    m_socket->sendCommand(m_statCommand.get());
}

void ArticleFetcher::startBody()
{
    // The sink runs inside the socket's readyRead, once per decoded line. It
    // seeks lazily: the offset only becomes known when =ypart arrives, which is
    // after =ybegin and before any payload.
    m_bodyCommand = std::make_unique<BodyCommand>(
        m_segment.messageId, [this](QByteArrayView chunk) {
            if (!m_writeError.isEmpty())
                return;

            if (!m_positioned) {
                // YencDecoder::offset() has already applied the 1-based
                // correction; a single-part post with no =ypart reports 0.
                m_decodedOffset = m_bodyCommand->decoder().offset();
                if (!m_writer->seekTo(m_decodedOffset, m_writeError))
                    return;
                m_positioned = true;
            }
            if (!m_writer->write(chunk, m_writeError))
                return;
            m_decodedBytes += chunk.size();
        });

    m_socket->sendCommand(m_bodyCommand.get());

    if (m_socket) {
        connect(m_socket, &NntpSocket::bodyProgress,
                this, &ArticleFetcher::onBodyProgress, Qt::UniqueConnection);
        onBodyProgress();
    }
}

void ArticleFetcher::onBodyProgress()
{
    if (!m_bodyCommand || m_bodyCommand->failed())
        return;
    // Still queued behind another command: nothing of ours drained yet.
    const qint64 drained = m_socket->currentCommand() == m_bodyCommand.get()
                               ? m_socket->commandBodyBytes()
                               : 0;
    if (m_progress) {
        m_progress->store(m_segment.bytes > 0 ? std::min(drained, m_segment.bytes) : drained,
                          std::memory_order_relaxed);
    }
    if (m_nearlyDone)
        return;
    const qint64 lookahead = pipelineLookahead(m_socket->readRateLimit(),
                                               m_socket->readBufferCapBytes(),
                                               m_socket->responseLatencyMs());
    if (m_segment.bytes > 0 && m_segment.bytes - drained > lookahead)
        return;
    m_nearlyDone = true;
    emit nearlyDone();
}

void ArticleFetcher::onCommandFinished(NntpCommand* command)
{
    if (command == m_groupCommand.get()) {
        if (m_groupCommand->failed()) {
            if (m_socket && m_socket->selectedGroup() == m_group)
                m_socket->setSelectedGroup({});
            finish(m_groupCommand->error(), m_groupCommand->errorText());
            return;
        }
        if (m_mode == Mode::Stat)
            startStat();
        else
            startBody();
        return;
    }

    if (command == m_statCommand.get()) {
        m_articleExists = m_statCommand->exists();
        if (m_statCommand->failed()) {
            finish(m_statCommand->error(), m_statCommand->errorText());
            return;
        }
        finish(NntpError::None, QString{});
        return;
    }

    if (command != m_bodyCommand.get())
        return;

    m_articleFileName = m_bodyCommand->decoder().fileName();
    m_declaredFileSize = m_bodyCommand->decoder().fileSize();

    // A write failure outranks a damaged article: a full disk is not something a
    // different server can fix, and reporting it as "article not found" would
    // send the queue climbing the failover ladder for nothing.
    if (!m_writeError.isEmpty()) {
        finish(NntpError::WriteFailed, m_writeError);
        return;
    }
    if (m_bodyCommand->failed()) {
        finish(m_bodyCommand->error(), m_bodyCommand->errorText());
        return;
    }
    finish(NntpError::None, QString{});
}

void ArticleFetcher::finish(NntpError error, const QString& text)
{
    if (m_socket) {
        disconnect(m_socket, &NntpSocket::commandFinished,
                   this, &ArticleFetcher::onCommandFinished);
        disconnect(m_socket, &NntpSocket::bodyProgress,
                   this, &ArticleFetcher::onBodyProgress);
    }
    emit finished(error, text);
}

} // namespace eMule::usenet
