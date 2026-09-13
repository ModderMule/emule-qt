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

#include <QList>
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

/// One member of a set being extracted, as far as it has got.
struct UsenetDirectUnpackEntry {
    int index = -1;             ///< archive order — the ordinal a preview addresses
    QString name;               ///< as the archive named it
    QString path;               ///< inside the job's destDir
    qint64 entrySize = 0;       ///< declared unpacked size, 0 when none was declared
    qint64 bytesReadable = 0;   ///< flushed, so a reader really can get these back
    bool finished = false;
};

/// How far a run has got, pushed out as it goes.
///
/// A signal rather than a getter the queue could call: the queue deletes the
/// worker and its thread from its own event loop, and a snapshot it already
/// holds cannot dangle behind that. It is also read on every queue push, once
/// per file, where a lock would be the wrong shape entirely.
struct UsenetDirectUnpackProgress {
    QString itemId;
    QString setKey;
    QList<UsenetDirectUnpackEntry> entries;   ///< in archive order
};

struct UsenetDirectUnpackResult {
    QString itemId;
    QString setKey;
    QString firstVolume;    ///< identifies the set to the post-processing skip list
    QStringList extracted;
    QStringList consumed;   ///< the volumes it read, for cleanup accounting
    bool ok = false;

    /// The run stopped because the set is encrypted, not because anything is
    /// wrong with it. The queue needs the distinction: this is the one failure
    /// that a *password* fixes, and the one that must not make the preview
    /// answer "never" while the download is still coming.
    bool encrypted = false;

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

    /// The volume the run is parked on, or -1 when it is not waiting.
    ///
    /// Atomic rather than carried in the progress signal, because the interesting
    /// moment is exactly the one where nothing is being written and so nothing is
    /// being reported. Whoever wants the extraction to continue fetches this
    /// volume next.
    [[nodiscard]] int waitingForVolume() const { return m_waitingFor.load(); }

    // ArchiveVolumeSource
    [[nodiscard]] bool volumePath(int index, QString& out) override;

public slots:
    void run(const eMule::usenet::UsenetDirectUnpackJob& job);

signals:
    void finished(const eMule::usenet::UsenetDirectUnpackResult& result);

    /// Emitted as members are written, roughly once per MiB. Queued to the
    /// queue's thread.
    void progress(const eMule::usenet::UsenetDirectUnpackProgress& state);

private:
    mutable QMutex m_mutex;
    QWaitCondition m_wake;
    QMap<int, QString> m_volumes;
    bool m_ended = false;
    std::atomic<bool> m_cancelled{false};
    std::atomic<int> m_waitingFor{-1};

    /// Built on the worker thread from ArchiveReader's sink and shipped out
    /// whole, so a receiver never sees a half-updated member list.
    QList<UsenetDirectUnpackEntry> m_entries;
};

} // namespace eMule::usenet

Q_DECLARE_METATYPE(eMule::usenet::UsenetDirectUnpackJob)
Q_DECLARE_METATYPE(eMule::usenet::UsenetDirectUnpackResult)
Q_DECLARE_METATYPE(eMule::usenet::UsenetDirectUnpackProgress)
