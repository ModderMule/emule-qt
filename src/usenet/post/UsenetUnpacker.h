#pragma once

/// @file UsenetUnpacker.h
/// @brief Turning a downloaded volume set back into the file it encodes.
///
/// A Usenet release is almost never posted as the payload. It is posted as a
/// multi-volume archive plus PAR2 recovery volumes, so what phase 3 assembled is
/// forty `.partNN.rar` files, none of which is any use to anybody.
///
/// The whole job is picking the *first* volume and handing it to libarchive.
/// Both its RAR4 and RAR5 readers follow the remaining volumes themselves once
/// opened on volume 1 — but only on volume 1. Open `.part07.rar` and you get an
/// error about a file part split across volumes, which is why volume detection
/// is the bulk of this file rather than an afterthought.
///
/// Extraction itself is ArchiveReader's, including the member-name sanitising
/// that stops a hostile archive writing outside the destination. Nothing here
/// joins a path by hand.
///
/// Blocking and synchronous, like Par2Verifier and for the same reason:
/// UsenetPostProcessor owns the thread.

#include <QString>
#include <QSet>
#include <QStringList>

#include <QList>

#include <functional>

namespace eMule::usenet {

/// One archive and every volume belonging to it.
struct ArchiveSet {
    /// Absolute path of the volume to open. The only one libarchive may be given.
    QString firstVolume;

    /// Every member of the set, first volume included, in volume order. These
    /// are what cleanup deletes once the payload is safely extracted.
    QStringList volumes;

    /// Display name, without the volume suffix.
    QString baseName;
};

class UsenetUnpacker {
public:
    struct Result {
        bool ok = false;

        /// No archive in the directory at all. Not a failure: a release posted
        /// as bare files is normal, and the caller just publishes them as they
        /// are.
        bool nothingToDo = false;

        /// The archive is encrypted and libarchive cannot open it. True only for
        /// RAR, where detection is as far as libarchive goes; ZIP and 7z decrypt
        /// from NzbInfo::password like any other member.
        bool encryptedUnsupported = false;

        /// Absolute paths of what came out.
        QStringList extractedFiles;

        /// Volume files consumed, across every set. Cleanup deletes these.
        QStringList consumedArchives;

        QString error;
    };

    using ProgressFn = std::function<void(int percent, const QString& fileName)>;

    UsenetUnpacker() = default;

    void setProgressCallback(ProgressFn fn) { m_progress = std::move(fn); }

    /// Extract every archive set in @p sourceDir into @p destDir.
    ///
    /// @p password comes from NzbInfo::password — the `<meta type="password">`
    /// tag or the release name. Empty is the normal case.
    /// @p skipFirstVolumes names sets already extracted elsewhere, by the path
    /// of their first volume. A skipped set is still *found* — it just is not
    /// unpacked again — so nothingToDo keeps meaning "no archives here at all".
    Result unpack(const QString& sourceDir, const QString& destDir,
                  const QString& password = {},
                  const QSet<QString>& skipFirstVolumes = {});

    /// Group the archive files in @p dir into sets, one entry per set, each
    /// naming the volume that must be opened.
    [[nodiscard]] static QList<ArchiveSet> findArchiveSets(const QString& dir);

    /// Whether @p fileName looks like any volume of any supported archive.
    /// Used by cleanup as much as by detection.
    [[nodiscard]] static bool isArchiveVolume(const QString& fileName);

    /// Where @p fileName sits in its archive set. `index` is -1 when the name is
    /// not an archive volume at all; otherwise the lowest index opens the set.
    ///
    /// Exposed because the streaming index has to order the volumes of a set
    /// from *NZB filenames*, before anything is on disk, and re-deriving these
    /// four naming schemes somewhere else is how the two drift apart.
    struct VolumePosition {
        QString baseName;   ///< lowercased, the key that groups a set
        int index = -1;
    };
    [[nodiscard]] static VolumePosition volumePositionOf(const QString& fileName);

private:
    ProgressFn m_progress;
};

} // namespace eMule::usenet
