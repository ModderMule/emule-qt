#pragma once

/// @file UsenetHealth.h
/// @brief Release identity, and what an .nzb is worth before it costs anything.
///
/// Two questions are asked at add time, and neither may ever stop an article
/// being fetched:
///
///   - **Is this already here?** Answered exactly, from the message-ids.
///   - **How much of it is actually there?** Answered in two independent parts:
///     what the NZB itself lists (NzbInfo::shortfall(), free) and what the
///     servers still hold (the availability probe, one STAT per sampled
///     article). They are different numbers about different things and are
///     deliberately not merged.
///
/// The rule this file exists under, stated in docs/usenet-module.md alongside
/// its two siblings: retention falls back to asking anyway, an allowance falls
/// back to waiting, and a health check falls back to **downloading anyway**.
/// NNTP cannot say *why* an article is absent -- 430 is expired, taken down,
/// never propagated, or simply on a different server -- so every figure here is
/// advice, and the only actor allowed to act on it is the user.

#include <QList>
#include <QString>
#include <QtTypes>

namespace eMule::usenet {

struct NzbInfo;

/// Where an add came from. Decides which existing items block a re-add, exactly
/// as Ed2kLinkImporter::Source does for eD2K links: a release still downloading
/// is refused either way, but re-adding a *completed* one is a deliberate act
/// when a person did it and almost certainly a mistake when a feed did.
enum class UsenetAddSource : quint8 {
    Manual = 0,     ///< a file dialog, a pasted URL, a drop, a search grab
    Automatic = 1,  ///< the watch folder, or a feed
};

/// Where an NZB came from, for the intake statistics only. Policy keys on
/// UsenetAddSource, which folds these into manual and automatic.
enum class UsenetAddOrigin : quint8 {
    File,         ///< file dialog, drop, command line
    Url,
    WatchFolder,
    Feed,
    IndexerGrab,
};

/// Why addNzb() did what it did.
///
/// An automatic actor -- the watch folder, a feed -- has to tell "we already
/// have this" apart from "that did not work", because the first is terminal and
/// the second is worth one more attempt. Without it a duplicate is retried
/// forever, and matching on the error *text* to avoid that would break the first
/// time a sentence is reworded or translated.
enum class UsenetAddOutcome : quint8 {
    Added = 0,
    Duplicate,   ///< in the queue and still on its way -- terminal, not a failure
    Invalid,     ///< not a parseable NZB, or it lists no files -- never worth retrying
    Failed,      ///< could not be created on disk -- worth one more attempt later

    /// Finished, or in the history. Terminal for an automatic actor, and a
    /// *question* for a person, which addNzb()'s force answers. Deliberately not
    /// Duplicate: nothing overrides that one, and one value could not say both.
    ///
    /// Appended rather than inserted -- the value travels on the wire now.
    AlreadyDownloaded,
};

/// What an add may choose, beside the bytes and the name.
///
/// Every field defaults to what the intake paths did before there was any way to
/// say otherwise, so a caller states only what it cares about:
/// `addNzb(data, name, error, {.category = 3, .paused = true})`. It is a struct
/// rather than more trailing parameters because the positional form had already
/// reached the point where `password` sat last only to avoid re-ordering five
/// call sites -- the same reason setPostProcessingOptions() takes one.
struct UsenetAddOptions {
    /// Which existing items refuse this re-add.
    UsenetAddSource source = UsenetAddSource::Manual;

    /// Suppress the "you already downloaded this" refusal and nothing else. A
    /// release whose articles are still arriving cannot usefully arrive twice,
    /// so no caller may override that one.
    bool force = false;

    /// An archive passphrase from whoever is adding the release. A manual add
    /// wins outright; otherwise the NZB's own metadata is authoritative.
    QString password;

    /// Index into Preferences::categories(). 0 means "the caller did not pick
    /// one", which is when auto-categorisation runs.
    int category = 0;

    /// -2 very low .. +2 very high, clamped by addNzb(). Higher runs first.
    int priority = 0;

    /// Queue it without starting it. Independent of usenetAutoAddPaused(),
    /// which pauses an automatic add whatever the caller asked for.
    bool paused = false;

    /// NZB file indices to leave out, as the Add NZB dialog unchecked them.
    /// Widened to whole archive sets; a list skipping a par2 file or every
    /// payload file refuses the add.
    QList<int> skippedFiles;
};

/// How hard to ask before spending anything. Mirrors the integer stored in
/// Preferences::usenetHealthCheck(), which is an int there because core owns the
/// file format and may not depend on eMule::Usenet — the same split NewsServer
/// and IndexerConfig already make.
enum class UsenetHealthCheck : quint8 {
    Off = 0,

    /// The first listed article of each file. Expiry is wholesale — a provider
    /// retires articles by post date and every article of one posted file shares
    /// that date — so one article stands for its file, and a release of 50-100
    /// files costs 50-100 status lines.
    Sample = 1,

    /// Every article. Certain, and honest about the cost: a 15 GB release is
    /// tens of thousands of round trips.
    Full = 2,
};

[[nodiscard]] UsenetHealthCheck usenetHealthCheckFromInt(int value);

/// What a probe concluded. Every field is advisory; nothing in the download path
/// reads any of it.
struct UsenetHealthVerdict {
    /// Percentage of the release believed obtainable, combining what the NZB
    /// never listed with what no account still holds. -1 when nothing was
    /// assessed, which is **not** the same as 100.
    int percent = -1;

    qint64 missingBytes = 0;

    /// Recovery data that survived the probe. A volume the servers no longer
    /// hold cannot repair anything, so it does not count here.
    qint64 recoveryBytes = 0;

    /// Whether a server was actually asked, as against the figure being the
    /// NZB's own arithmetic.
    bool probed = false;

    [[nodiscard]] bool likelyRecoverable() const
    { return missingBytes == 0 || recoveryBytes >= missingBytes; }
};

/// Exact identity of a release: a digest over its sorted message-ids.
///
/// This is identity rather than a heuristic. Two NZBs sharing message-ids fetch
/// literally the same articles from literally the same servers, whatever their
/// names say. A *repost* of the same content carries fresh ids and is correctly
/// not a match -- that case belongs to nzbReleaseKey().
///
/// Sorted before hashing because two indexers may list the same articles in a
/// different order, and an order-dependent digest would call that two releases.
[[nodiscard]] QString nzbArticleDigest(const NzbInfo& nzb);

/// One release name, folded to the one form everything compares on.
///
/// Case-folded, a trailing ".nzb" removed, and every run of spaces, dots,
/// underscores and dashes collapsed to a single space. An indexer's title, an
/// .nzb filename and the name inside the NZB are three spellings of the same
/// release; without one folding, matching them is three different guesses.
///
/// Heuristic, and only ever used to *colour a row and ask a question* -- what
/// actually gets refused is decided by nzbArticleDigest().
[[nodiscard]] QString usenetFoldedReleaseName(const QString& name);

/// Heuristic identity: folded name plus total encoded size.
///
/// The same shape as IndexerResult::dedupKey(), and honest about the same
/// limitation -- it will occasionally merge two distinct releases that share a
/// name and a size, and miss a repost whose title differs by a tag. It is
/// therefore only ever *reported*, never acted on.
[[nodiscard]] QString nzbReleaseKey(const QString& name, qint64 totalEncodedBytes);

} // namespace eMule::usenet
