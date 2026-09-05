#pragma once

/// @file UsenetDirectUnpack.h
/// @brief Extracts an archive set while its volumes are still downloading.
///
/// Unpacking at the end of a download reads every volume and writes the payload
/// out again, with the user watching. Nothing forces that wait: a volume is
/// complete the moment its last article lands, and libarchive reads a set as one
/// stream front to back. So the extraction can simply keep pace with the
/// download and be finished when the last article is.
///
/// The only thing that differs from the end-of-download path is where the next
/// volume comes from, so that is the only thing this adds — it *is* an
/// ArchiveVolumeSource, and the extraction itself is ArchiveReader's, unchanged.
/// Every format libarchive reads is therefore covered, compressed and solid RAR
/// included; nothing here knows what a RAR is.
///
/// This runs on its own thread, not UsenetPostProcessor's: a run is blocked for
/// as long as the download takes, and that thread has to stay free for other
/// items' repairs.

#include "archive/ArchiveReader.h"

#include <QMap>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QWaitCondition>

#include <atomic>

namespace eMule::usenet {

struct UsenetDirectUnpackJob {
    QString itemId;
    QString setKey;      ///< the archive set's base name, as UsenetUnpacker groups it
    QString destDir;     ///< where members land; the same dir post-processing uses
    QString password;
};

struct UsenetDirectUnpackResult {
    QString itemId;
    QString setKey;
    QString firstVolume;    ///< identifies the set to the post-processing skip list
    QStringList extracted;
    QStringList consumed;   ///< the volumes it read, for cleanup accounting
    bool ok = false;
    QString error;
};

class UsenetDirectUnpack : public QObject, public ArchiveVolumeSource {
    Q_OBJECT

public:
    explicit UsenetDirectUnpack(QObject* parent = nullptr);
    ~UsenetDirectUnpack() override;

    /// Hand over a sealed volume. Any thread; wakes a run waiting for it.
    /// Volumes may arrive in any order — the run waits for the index it needs.
    void offerVolume(int index, const QString& path);

    /// No further volumes are coming. A run waiting past the last one then ends
    /// the stream cleanly instead of blocking forever.
    void endOfSet();

    /// Abandon the run. Unwinds libarchive through a short read rather than
    /// killing the thread mid-write.
    void cancel();

    [[nodiscard]] bool cancelled() const { return m_cancelled.load(); }

    // ArchiveVolumeSource
    [[nodiscard]] bool volumePath(int index, QString& out) override;

public slots:
    void run(const eMule::usenet::UsenetDirectUnpackJob& job);

signals:
    void finished(const eMule::usenet::UsenetDirectUnpackResult& result);

private:
    mutable QMutex m_mutex;
    QWaitCondition m_wake;
    QMap<int, QString> m_volumes;
    bool m_ended = false;
    std::atomic<bool> m_cancelled{false};
};

} // namespace eMule::usenet

Q_DECLARE_METATYPE(eMule::usenet::UsenetDirectUnpackJob)
Q_DECLARE_METATYPE(eMule::usenet::UsenetDirectUnpackResult)
