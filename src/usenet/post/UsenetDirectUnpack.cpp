/// @file UsenetDirectUnpack.cpp
/// @brief Extracts an archive set while its volumes are still downloading.

#include "post/UsenetDirectUnpack.h"

#include "utils/Log.h"

#include <QDir>
#include <QFileInfo>

namespace eMule::usenet {

UsenetDirectUnpack::UsenetDirectUnpack(QObject* parent)
    : QObject(parent)
{
}

UsenetDirectUnpack::~UsenetDirectUnpack()
{
    cancel();
}

void UsenetDirectUnpack::offerVolume(int index, const QString& path)
{
    {
        QMutexLocker lock(&m_mutex);
        m_volumes.insert(index, path);
    }
    m_wake.wakeAll();
}

void UsenetDirectUnpack::endOfSet()
{
    {
        QMutexLocker lock(&m_mutex);
        m_ended = true;
    }
    m_wake.wakeAll();
}

void UsenetDirectUnpack::cancel()
{
    m_cancelled.store(true);
    m_wake.wakeAll();
}

bool UsenetDirectUnpack::volumePath(int index, QString& out)
{
    QMutexLocker lock(&m_mutex);
    for (;;) {
        if (m_cancelled.load())
            return false;

        const auto it = m_volumes.constFind(index);
        if (it != m_volumes.constEnd()) {
            out = *it;
            return true;
        }

        // Nothing more is coming and this index never arrived: the set ends here.
        // That is the normal exit — the reader asks for one volume past the last.
        if (m_ended)
            return false;

        m_wake.wait(&m_mutex);
    }
}

void UsenetDirectUnpack::run(const eMule::usenet::UsenetDirectUnpackJob& job)
{
    UsenetDirectUnpackResult result;
    result.itemId = job.itemId;
    result.setKey = job.setKey;

    QDir().mkpath(job.destDir);

    ArchiveReader reader;
    if (!job.password.isEmpty())
        reader.setPassphrase(job.password);

    const bool ok = reader.extractAllFrom(*this, job.destDir);

    // Read *after* the run, not before: run() is invoked queued and can reach
    // its first line before the caller has offered volume zero. Taken early,
    // this comes back empty and post-processing then fails to match the set and
    // unpacks it a second time.
    {
        QMutexLocker lock(&m_mutex);
        const auto first = m_volumes.constFind(0);
        if (first != m_volumes.constEnd())
            result.firstVolume = *first;
    }

    if (m_cancelled.load()) {
        // Half-written members are worse than none: post-processing would take
        // them for a finished extraction. Drop them and let it start over.
        for (const QString& path : reader.extractedFiles())
            QFile::remove(path);
        result.error = QStringLiteral("cancelled");
        emit finished(result);
        return;
    }

    for (const QString& rejected : reader.rejectedEntries()) {
        logWarning(QStringLiteral("Usenet: skipped unsafe archive member \"%1\" while unpacking \"%2\"")
                       .arg(rejected, job.setKey));
    }

    // libarchive detects RAR encryption and can do nothing about it, so an
    // encrypted set produces no usable output however far the read got.
    if (reader.hasEncryptedEntries()
        && reader.formatName().contains(QLatin1String("RAR"), Qt::CaseInsensitive)) {
        for (const QString& path : reader.extractedFiles())
            QFile::remove(path);
        result.error = QStringLiteral("password-protected RAR");
        emit finished(result);
        return;
    }

    if (!ok) {
        for (const QString& path : reader.extractedFiles())
            QFile::remove(path);
        result.error = QStringLiteral("extraction failed");
        logWarning(QStringLiteral("Usenet: direct unpack of \"%1\" failed; the normal "
                                  "unpack will run at the end").arg(job.setKey));
        emit finished(result);
        return;
    }

    {
        QMutexLocker lock(&m_mutex);
        result.consumed = QStringList(m_volumes.values());
    }
    result.extracted = reader.extractedFiles();
    result.ok = true;

    logInfo(QStringLiteral("Usenet: unpacked \"%1\" while downloading (%2 file(s))")
                .arg(job.setKey).arg(result.extracted.size()));
    emit finished(result);
}

} // namespace eMule::usenet
