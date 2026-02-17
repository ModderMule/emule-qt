/// @file PartFileWriteThread.cpp
/// @brief Async buffered I/O thread for PartFile disk writes.

#include "files/PartFileWriteThread.h"
#include "utils/Log.h"

#include <QFile>

namespace eMule {

PartFileWriteThread::PartFileWriteThread(QObject* parent)
    : QThread(parent)
{
}

void PartFileWriteThread::enqueueWrite(WriteJob job)
{
    QMutexLocker locker(&m_mutex);
    m_queue.push_back(std::move(job));
    m_condition.wakeOne();
}

void PartFileWriteThread::requestStop()
{
    QMutexLocker locker(&m_mutex);
    m_stopRequested = true;
    m_condition.wakeAll();
}

void PartFileWriteThread::run()
{
    while (true) {
        WriteJob job;

        {
            QMutexLocker locker(&m_mutex);
            while (m_queue.empty() && !m_stopRequested)
                m_condition.wait(&m_mutex);

            if (m_stopRequested) {
                // Drain remaining jobs
                m_queue.clear();
                return;
            }

            job = std::move(m_queue.front());
            m_queue.pop_front();
        }

        QFile file(job.filePath);
        if (!file.open(QIODevice::ReadWrite)) {
            emit writeError(job.partFile,
                            QStringLiteral("Cannot open file: %1").arg(job.filePath));
            continue;
        }

        if (!file.seek(static_cast<qint64>(job.offset))) {
            emit writeError(job.partFile,
                            QStringLiteral("Seek failed at offset %1").arg(job.offset));
            continue;
        }

        qint64 written = file.write(
            reinterpret_cast<const char*>(job.data.data()),
            static_cast<qint64>(job.data.size()));

        if (written != static_cast<qint64>(job.data.size())) {
            emit writeError(job.partFile,
                            QStringLiteral("Write incomplete: %1 of %2 bytes")
                                .arg(written)
                                .arg(job.data.size()));
            continue;
        }

        file.flush();
        emit writeCompleted(job.partFile);
    }
}

} // namespace eMule
