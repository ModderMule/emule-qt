#pragma once

/// @file UsenetReleaseName.h
/// @brief Which TV episode a release name is, for the episode duplicate check.
///
/// Two releases of one episode rarely share a name: `Show.S01E02.720p.WEB.x264-A`
/// and `Show.S01E02.1080p.HDTV-B` are different NZBs with different articles, so
/// the digest check never links them. The episode key does: the folded show
/// title plus season/episode, or plus the air date for daily shows.
///
/// A PROPER/REPACK/REAL/RERIP release is a fix of one already out, so it is
/// flagged and the check lets it through. The tag only counts after the episode
/// token: "Real Time" or "The Proper Way" are titles.

#include <QDate>
#include <QString>

#include <optional>

namespace eMule::usenet {

struct ReleaseEpisode {
    QString title;    ///< folded, usenetFoldedReleaseName()
    int season = -1;  ///< -1 for a dated episode
    int episode = -1; ///< first one of a multi-episode release
    QDate airDate;    ///< daily shows; invalid otherwise
    bool proper = false;

    /// `title/s/e` or `title/yyyy-mm-dd`. Same key = same episode.
    [[nodiscard]] QString key() const;

    /// "S01E02" or "2024-03-05", for messages.
    [[nodiscard]] QString label() const;
};

/// nullopt for anything that names no single episode: a movie, a season pack
/// (`S01` alone), or no title before the episode token.
[[nodiscard]] std::optional<ReleaseEpisode> parseReleaseEpisode(const QString& name);

} // namespace eMule::usenet
