#pragma once

/// @file UsenetReleaseChecks.h
/// @brief What a finished release must pass before it is published, besides PAR2.
///
/// Both checks are pure and run on the post-processing thread:
///
///   - **SFV.** A release with no PAR2 set — or with PAR2 off, or on a build
///     without it — used to be published unchecked. Many ship an `.sfv`
///     instead, and a CRC32 per file is enough to refuse one that arrived
///     damaged.
///   - **Unwanted files.** A "movie" whose archive holds `setup.exe` is the
///     commonest fake on Usenet. Only a *media* release is judged: a software
///     download is supposed to carry executables.

#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace eMule::usenet {

using CheckProgressFn = std::function<void(int percent, const QString& fileName)>;
using CheckCancelFn = std::function<bool()>;

// -- SFV ----------------------------------------------------------------------

/// One line of an .sfv.
struct SfvEntry {
    QString fileName;   ///< bare name: any path the line carried is dropped
    quint32 crc = 0;
};

/// The entries of one .sfv. `;` comments, blank lines and lines that do not end
/// in eight hex digits are skipped rather than failing the file.
[[nodiscard]] QList<SfvEntry> parseSfv(QByteArrayView text);

struct SfvCheck {
    enum class Outcome : quint8 {
        NoSfv,            ///< nothing to check against
        NothingToCheck,   ///< an .sfv, but none of the files it lists is here
        Clean,
        Damaged,          ///< a CRC mismatch, or an expected file missing
        Cancelled,
    };

    Outcome outcome = Outcome::NoSfv;
    QStringList sfvFiles;   ///< absolute paths of the .sfv files read
    QStringList damaged;    ///< names that failed
    QStringList unposted;   ///< listed and never posted — usually a sample; logged only
    int checked = 0;
};

/// Verify every `.sfv` in @p dir against the files beside it.
///
/// @p expectedNames are the files the download delivered. A listed file that is
/// absent and was never expected is a sample or an .nfo nobody posted, not
/// damage; one that was expected and is absent is.
[[nodiscard]] SfvCheck verifySfv(const QString& dir, const QStringList& expectedNames,
                                 const CheckProgressFn& progress = {},
                                 const CheckCancelFn& cancel = {});

/// CRC32 of a whole file, streamed; nullopt when unreadable or cancelled.
[[nodiscard]] std::optional<quint32> fileCrc32(const QString& path,
                                               const CheckCancelFn& cancel = {});

// -- Unwanted files -----------------------------------------------------------

/// "exe, .COM; *.scr" -> {"exe", "com", "scr"}: lower case, no dots, no blanks.
[[nodiscard]] QStringList parseExtensionList(const QString& text);

/// Whether a release name carries a video tag: a resolution, codec, source or
/// episode marker.
[[nodiscard]] bool looksLikeVideoReleaseName(const QString& name);

/// The names in @p names a media release must not carry, bare.
///
/// @p mediaRelease is what is already known about the release — its NZB names,
/// its title; a playable name among @p names counts too. A disguised double
/// extension, `movie.mkv.exe`, is unwanted even without media: nothing
/// legitimate is named like that. Empty @p extensions turns the check off.
[[nodiscard]] QStringList unwantedFileNames(const QStringList& names,
                                           const QStringList& extensions, bool mediaRelease);

/// A file named as media whose first bytes are a program or an archive — the
/// `.mkv` that is really an executable. A container of the wrong kind (an MP4
/// named .mkv) still plays, and a container nobody recognises may be real, so
/// neither is reported.
[[nodiscard]] bool isFakeMediaFile(const QString& path);

/// "a, b, c and 2 more": how a message names the files it is about.
[[nodiscard]] QString describeFileList(const QStringList& names);

} // namespace eMule::usenet
