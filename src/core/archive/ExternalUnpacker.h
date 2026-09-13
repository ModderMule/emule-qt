#pragma once

/// @file ExternalUnpacker.h
/// @brief Extraction by an external 7-Zip or unrar, for what libarchive cannot do.
///
/// libarchive decrypts **ZIP only**. For 7z it stops at "Crypto codec not
/// supported yet" and for RAR at "RAR encryption support unavailable" — both of
/// them detection, never decryption. Since most password-protected Usenet
/// releases are RAR, a password the user supplies would otherwise be inert, and
/// the release would fail with every byte of it already on disk.
///
/// So when ArchiveReader reports encryptionBlocked(), the set goes to whichever
/// of `7zz`, `7z`, `7za` or `unrar` is installed. Nothing else routes here:
/// a genuinely corrupt archive must keep failing as a corrupt archive.
///
/// **The tool's output is untrusted like any other archive's.** 7-Zip and unrar
/// both strip leading `/` and `..` themselves, but that is their policy and not
/// a guarantee we control, so extraction goes to a private staging directory
/// and each member is moved out through ArchiveReader::safeEntryPath().
///
/// **The password never reaches a log.** It rides on the command line, which is
/// what both tools offer and what every other client does; every message this
/// class produces names the redacted form.

#include <QString>
#include <QStringList>

#include <atomic>

namespace eMule {

class ExternalUnpacker {
public:
    /// Which command-line dialect a discovered binary speaks. They differ in
    /// enough places — output flag, stdout flag, listing format, exit codes —
    /// that guessing per call would be worse than recording it once.
    enum class Kind {
        None,
        SevenZip,   ///< 7zz / 7z / 7za: `x -o<dir>`, `-so`, `l -slt`
        Unrar,      ///< unrar: `x <archive> <dir>/`, `p -inul`
    };

    enum class Outcome {
        Ok,
        WrongPassword,   ///< the tool ran and refused the passphrase
        NoTool,          ///< nothing installed to run
        Failed,
    };

    struct Result {
        Outcome outcome = Outcome::Failed;
        QStringList extractedFiles;   ///< sanitised, under destDir
        QStringList rejectedEntries;  ///< members safeEntryPath() refused
        QString error;

        [[nodiscard]] bool ok() const { return outcome == Outcome::Ok; }
    };

    /// @p preferredTool is an explicit binary path — the `usenetExternalUnpacker`
    /// preference. Empty means search PATH and the usual install locations.
    ///
    /// A non-empty path is used or nothing: if it is missing, not executable, or
    /// not a 7-Zip/unrar binary, available() is false rather than a different
    /// tool being substituted. Quietly using something the user did not name
    /// turns a typo into behaviour they cannot account for later.
    explicit ExternalUnpacker(QString preferredTool = {});

    /// Whether any usable tool was found. Cheap: discovery is cached per path.
    [[nodiscard]] bool available() const;

    /// Absolute path of the binary that will run, empty when there is none.
    [[nodiscard]] QString toolPath() const;
    [[nodiscard]] Kind toolKind() const;

    /// A short name for messages, e.g. "7zz". Never the full path — a warning
    /// telling the user to install "/opt/homebrew/bin/7zz" helps nobody.
    [[nodiscard]] QString toolName() const;

    /// Extract every member of @p volumes into @p destDir.
    ///
    /// Only @p volumes.first() is passed to the tool: unlike libarchive, both
    /// tools follow a multi-volume set by name themselves. The rest of the list
    /// is still required, because a set whose later volumes are missing must be
    /// recognised as incomplete rather than half-extracted.
    Result extract(const QStringList& volumes, const QString& destDir,
                   const QString& password);

    /// One member of an archive, as the tool describes it.
    struct Member {
        QString name;
        qint64 size = 0;   ///< 0 when the tool did not say; see listMembers()
    };

    /// Members inside @p volumes, in archive order.
    ///
    /// Needed because `x -so` with no member name concatenates every member,
    /// which for a release with a sample and a subtitle track is not the movie.
    /// A header-encrypted archive needs the password even to list.
    ///
    /// **`size` is only filled by a 7-Zip-family binary**, whose `l -slt` is a
    /// machine-readable record per member. unrar's equivalents are column
    /// layouts that shift with locale, and a size parsed wrong is worse than no
    /// size at all — a preview would then advertise a length it cannot reach.
    /// So with only unrar installed the names come back and the sizes are 0,
    /// and the encrypted preview declines rather than guesses.
    [[nodiscard]] QList<Member> listMembers(const QStringList& volumes, const QString& password,
                                            QString& error) const;

    /// Write @p member's decrypted bytes to @p outPath, stopping wherever the
    /// volume set runs out, and return how many bytes were written.
    ///
    /// This is the encrypted-RAR preview: a partial RAR set yields a usable
    /// prefix because RAR headers sit at the front of each volume. A partial
    /// **7z** set yields nothing at all — its metadata lives at the end of the
    /// set — so callers must not offer this for 7z. Measured, not assumed:
    /// tst_UsenetPassword::anIncomplete7zSetCanNeverBePreviewed.
    ///
    /// A short return is normal and not an error: it is how far the volumes on
    /// disk reach. -1 means nothing could be run at all.
    [[nodiscard]] qint64 streamMemberTo(const QStringList& volumes, const QString& member,
                                        const QString& outPath, const QString& password);

    /// Kill a run in progress. Safe from another thread; a killed run reports
    /// Outcome::Failed with "cancelled".
    void cancel();

    /// Longest a single run may take before it is killed. Public so a test can
    /// shorten it rather than wait out the real ceiling.
    void setTimeoutMs(qint64 ms);

private:
    /// Run @p args, capturing stdout to @p stdoutPath when it is non-empty
    /// (streaming) or into @p captured when it is (listing). @p redactedArgs is
    /// what any message may show.
    int run(const QStringList& args, const QStringList& redactedArgs,
            const QString& stdoutPath, QByteArray* captured, QString& error);

    /// Move everything under @p staging into @p destDir through safeEntryPath().
    static void harvest(const QString& staging, const QString& destDir, Result& result);

    /// `-p<password>`, or the form that stops the tool prompting for one.
    [[nodiscard]] QString passwordArg(const QString& password) const;

    QString m_toolPath;
    Kind m_kind = Kind::None;
    qint64 m_timeoutMs = 6 * 60 * 60 * 1000;   ///< a 40 GB release is hours of CPU
    std::atomic<bool> m_cancelled{false};
};

} // namespace eMule
