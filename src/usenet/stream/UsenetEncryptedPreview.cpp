#include "pch.h"

/// @file UsenetEncryptedPreview.cpp

#include "stream/UsenetEncryptedPreview.h"

#include "stream/UsenetStreamIndex.h"

#include "archive/ExternalUnpacker.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace eMule::usenet {

UsenetEncryptedPreview::UsenetEncryptedPreview(QObject* parent)
    : QObject(parent)
{
}

UsenetEncryptedPreview::~UsenetEncryptedPreview() = default;

void UsenetEncryptedPreview::cancel()
{
    m_cancelled.store(true);
    // The pointer is only ever cleared on this object's own thread after the
    // tool has exited, so a cancel arriving mid-run always finds a live one.
    if (ExternalUnpacker* running = m_running.get())
        running->cancel();
}

void UsenetEncryptedPreview::run(const eMule::usenet::UsenetEncryptedPreviewJob& job)
{
    UsenetEncryptedPreviewResult result;
    result.itemId = job.itemId;
    result.setKey = job.setKey;
    result.member = job.member;
    result.memberSize = job.memberSize;

    m_cancelled.store(false);

    if (job.volumes.isEmpty() || job.outPath.isEmpty()) {
        emit finished(result);
        return;
    }

    QString member = job.member;
    qint64 memberSize = job.memberSize;
    if (member.isEmpty()) {
        bool refused = false;
        member = chooseEncryptedPreviewMember(job.volumes, job.password, job.externalTool,
                                              memberSize, refused);
        result.member = member;
        result.memberSize = memberSize;
        result.refused = refused;
    }
    if (member.isEmpty() || memberSize <= 0) {
        emit finished(result);
        return;
    }

    QDir().mkpath(QFileInfo(job.outPath).absolutePath());

    // Write to a sibling and only promote it if it beats what is already being
    // served. A run always restarts at byte 0, so writing straight to outPath
    // would truncate the file a player is reading from — and the queue's
    // high-water rule cannot help once the bytes are already gone.
    const QString partial = job.outPath + QStringLiteral(".partial");

    m_running = std::make_unique<ExternalUnpacker>(job.externalTool);
    const qint64 bytes =
        m_running->streamMemberTo(job.volumes, member, partial, job.password);
    const bool cancelled = m_cancelled.load();
    m_running.reset();

    result.cancelled = cancelled;
    result.path = job.outPath;

    if (cancelled || bytes <= 0) {
        QFile::remove(partial);
        emit finished(result);
        return;
    }

    const qint64 existing = QFileInfo(job.outPath).size();
    if (bytes <= existing) {
        // Nothing new. Usually because no further volume has sealed since the
        // last run, which is exactly the case the queue's rate limit exists to
        // avoid paying for twice.
        QFile::remove(partial);
        result.bytes = existing;
        emit finished(result);
        return;
    }

    QFile::remove(job.outPath);
    if (!QFile::rename(partial, job.outPath)) {
        logWarning(QStringLiteral("Usenet: could not publish the preview prefix for \"%1\"")
                       .arg(member));
        QFile::remove(partial);
        emit finished(result);
        return;
    }

    result.bytes = bytes;
    logInfo(QStringLiteral("Usenet: encrypted preview of \"%1\" reaches %2 of %3 byte(s)")
                .arg(member).arg(bytes).arg(memberSize));
    emit finished(result);
}

QString chooseEncryptedPreviewMember(const QStringList& volumes, const QString& password,
                                     const QString& externalTool, qint64& memberSize,
                                     bool& refused)
{
    memberSize = 0;
    refused = false;
    if (volumes.isEmpty() || password.isEmpty())
        return {};

    const ExternalUnpacker tool(externalTool);
    if (!tool.available()) {
        refused = true;
        return {};
    }

    QString error;
    const auto members = tool.listMembers(volumes, password, error);
    if (members.isEmpty())
        return {};   // usually volume one has not landed; ask again later

    // Biggest playable member wins. A release's sample and its subtitles are
    // playable names too, and neither is what somebody pressed Preview for.
    QString best;
    for (const ExternalUnpacker::Member& m : members) {
        if (!isPlayableName(m.name) || m.size <= memberSize)
            continue;
        best = m.name;
        memberSize = m.size;
    }

    // A size of 0 means the tool would not say — unrar, which has no
    // machine-readable listing. Without a total the route can only answer
    // "bytes X-Y/*", and a player given that for a file it cannot seek in is
    // worse off than one told to wait for the download.
    //
    // The listing worked either way, so this is final: nothing about it changes
    // when more volumes land.
    if (best.isEmpty() || memberSize <= 0) {
        refused = true;
        return {};
    }

    return best;
}

} // namespace eMule::usenet
