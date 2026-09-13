#pragma once

/// @file UsenetEncryptedPreview.h
/// @brief Previewing a password-protected release while it downloads.
///
/// The two byte sources UsenetQueue already has both fail here. The byte map
/// cannot work in principle — the bytes on disk are encrypted, so a slice of a
/// volume is not a slice of the movie — and direct unpack cannot work in
/// practice, because a keep-pace extraction is fed one volume at a time through
/// ArchiveVolumeSource and neither 7-Zip nor unrar will open a set that is not
/// already complete on disk.
///
/// What is left is to run the external tool over the volumes that *have*
/// landed, let it fail at the first one that has not, and keep the prefix it
/// produced. RAR allows that: its headers sit at the front of each volume, so a
/// partial set decodes to a partial file.
///
/// **7z does not, and never will.** A 7z set keeps its metadata at the end, so
/// an incomplete one opens as "Cannot open the file as archive" and yields zero
/// bytes — measured, not assumed. Callers must not offer this for 7z.
///
/// The cost is the design constraint: a run has no resume, so every refresh
/// re-decrypts from volume one. Nothing here starts on its own. UsenetQueue
/// runs it only while a preview is actually open, no more often than
/// kMinRerunMs, and only when new volumes have sealed since the last run.

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

namespace eMule {
class ExternalUnpacker;
}

namespace eMule::usenet {

/// One re-run: the volumes as they stood when it was scheduled.
struct UsenetEncryptedPreviewJob {
    QString itemId;
    QString setKey;          ///< the volume set's base name

    /// Contiguously sealed volumes, in volume order, starting at volume one.
    ///
    /// Sealed, and contiguous, both matter. A target file is preallocated to its
    /// full length, so an in-progress volume reads as a run of zeros rather than
    /// as a short file — hand one to the tool and the prefix it produces is
    /// quietly wrong instead of quietly short.
    QStringList volumes;

    /// Which member to extract; never "all of them", which is what `x -so`
    /// with no name would give. Empty asks the run to choose the biggest
    /// playable one — done on this thread because listing spawns the tool, and
    /// the queue's thread is the daemon's event loop.
    QString member;
    qint64  memberSize = 0;  ///< the tool's declared size, filled by the run
    QString password;
    QString externalTool;    ///< the usenetExternalUnpacker preference
    QString outPath;         ///< where the prefix is written
};

struct UsenetEncryptedPreviewResult {
    QString itemId;
    QString setKey;
    QString member;
    QString path;            ///< outPath, whatever came of it
    qint64  bytes = 0;       ///< how far the prefix reaches, 0 when nothing came out
    qint64  memberSize = 0;
    bool    cancelled = false;

    /// There is nothing here to preview and there never will be: no tool
    /// installed, or the archive listed cleanly and holds nothing playable with
    /// a known size. The queue turns this into a refusal — and only this, because
    /// the route treats a refusal as final and a set whose first volume has not
    /// landed yet must keep saying "wait".
    bool    refused = false;
};

/// Runs one job on its own thread. One instance per item, reused across runs.
class UsenetEncryptedPreview : public QObject {
    Q_OBJECT

public:
    explicit UsenetEncryptedPreview(QObject* parent = nullptr);
    ~UsenetEncryptedPreview() override;

    /// Ask the running tool to stop. Safe from the queue's thread while the run
    /// is on its own; that is the only reason ExternalUnpacker::cancel() exists.
    void cancel();

public slots:
    /// Extract @p job.member as far as its volumes reach. Queued from the queue
    /// thread; blocking here, which is why it has a thread of its own.
    void run(const eMule::usenet::UsenetEncryptedPreviewJob& job);

signals:
    void finished(const eMule::usenet::UsenetEncryptedPreviewResult& result);

private:
    std::unique_ptr<ExternalUnpacker> m_running;
    std::atomic<bool> m_cancelled{false};
};

/// Pick the member of @p volumes worth previewing: the biggest playable one
/// whose size the tool was willing to state.
///
/// @p refused comes back true when the answer will never change — no tool, or a
/// listing that worked and holds nothing playable. A listing that simply failed
/// leaves it false, because the usual reason is that volume one has not landed.
///
/// Free function so a test can drive the choice without a thread.
[[nodiscard]] QString chooseEncryptedPreviewMember(const QStringList& volumes,
                                                   const QString& password,
                                                   const QString& externalTool,
                                                   qint64& memberSize, bool& refused);

} // namespace eMule::usenet

Q_DECLARE_METATYPE(eMule::usenet::UsenetEncryptedPreviewJob)
Q_DECLARE_METATYPE(eMule::usenet::UsenetEncryptedPreviewResult)
