#pragma once

/// @file ArrCompat.h
/// @brief Pure helpers of the Sonarr/Radarr bridge: eD2K <-> qBittorrent/Torznab
///        translation, Torznab XML, Arr Wanted lists, Gluetun answers.
///
/// Arr only knows torrent clients and Torznab indexers. An eD2K file is passed off
/// as a torrent: its info hash is the MD4 padded to 40 hex digits with zeros, its
/// magnet carries name (dn) and size (xl), so a qBit `add` can rebuild the ed2k link.

#include <QDateTime>
#include <QJsonDocument>
#include <QList>
#include <QString>

#include <optional>

namespace eMule::arr {

/// The eD2K file an Arr link names.
struct Ed2kRef {
    QString hashHex;   ///< 32 lowercase hex
    QString name;
    quint64 size = 0;
};

/// 40-hex info hash for an MD4: the hash plus eight zeros.
[[nodiscard]] QString btihForMd4(const QString& md4Hex);
/// The MD4 inside a padded info hash; empty when @p btih is not one.
[[nodiscard]] QString md4FromBtih(const QString& btih);

/// magnet:?xt=urn:btih:<md4>00000000&dn=<name>&xl=<size>
[[nodiscard]] QString magnetFor(const QString& md4Hex, const QString& name, quint64 size);
/// ed2k://|file|name|size|hash|/
[[nodiscard]] QString ed2kLinkFor(const Ed2kRef& ref);

/// An ed2k file link, or a magnet with a padded btih (or xt=urn:ed2k:) plus dn and xl.
[[nodiscard]] std::optional<Ed2kRef> parseArrLink(const QString& link);

/// What an eD2K download looks like to Arr (qBittorrent 4.x state names).
struct DownloadState {
    bool complete = false;
    bool completing = false;   ///< moving to incoming
    bool hashing = false;
    bool error = false;
    bool paused = false;       ///< paused or stopped
    qint64 datarate = 0;
    int sources = 0;
};
[[nodiscard]] QString qbitState(const DownloadState& s);

// --- Torznab -------------------------------------------------------------------

struct TorznabItem {
    QString title;
    QString hashHex;           ///< MD4
    QString fileName;
    quint64 size = 0;
    int category = 8000;       ///< newznab id
    int seeders = 0;           ///< complete sources
    int peers = 0;             ///< all sources
    QDateTime published;
};

/// t=caps
[[nodiscard]] QByteArray torznabCaps();
/// RSS 2.0 with torznab:attr rows; @p selfUrl is the feed's own address (atom:link).
[[nodiscard]] QByteArray torznabFeed(const QList<TorznabItem>& items, const QString& selfUrl);
/// <error code=".." description=".."/>; 100 = bad key, 200 = missing parameter,
/// 202 = no such function, 900 = other.
[[nodiscard]] QByteArray torznabError(int code, const QString& description);

/// eD2K file type for newznab categories: Video, Audio, Doc, Pro or empty (any).
[[nodiscard]] QString ed2kTypeForCategories(const QList<int>& cats);
/// A row's newznab category: the first requested one in the same group, else one
/// guessed from the name (SxxEyy = TV).
[[nodiscard]] int categoryForResult(const QString& fileName, const QList<int>& requested);
/// "1,2000,5030" -> ints
[[nodiscard]] QList<int> parseCategories(const QString& text);
/// q plus SxxEyy (or Sxx without an episode) for t=tvsearch.
[[nodiscard]] QString tvQuery(const QString& q, const QString& season, const QString& episode);
/// Title without the extension, dots kept: what Arr parses.
[[nodiscard]] QString releaseTitle(const QString& fileName);
/// Every word of @p query is in @p fileName (case and separators ignored).
[[nodiscard]] bool nameMatchesQuery(const QString& fileName, const QString& query);

// --- Arr Wanted lists ----------------------------------------------------------

struct WantedTerm {
    QString query;      ///< what eD2K is asked
    int category = 0;   ///< 5000 TV, 2000 movie
    QString source;     ///< instance name, for the log
};

/// Sonarr GET /api/v3/wanted/missing?includeSeries=true: "Title SxxEyy" per record.
[[nodiscard]] QList<WantedTerm> parseSonarrMissing(const QJsonDocument& doc, const QString& source);
/// Radarr GET /api/v3/movie: monitored, no file, available; "Title Year". At most @p max.
[[nodiscard]] QList<WantedTerm> parseRadarrMovies(const QJsonDocument& doc, const QString& source, int max);

// --- Gluetun -------------------------------------------------------------------

/// {"port":N} (v1/openvpn/portforwarded, v1/portforward) or {"ports":[N,..]}; 0 = none.
[[nodiscard]] quint16 parseGluetunPort(const QByteArray& json);
/// {"status":"running"} -> "running"
[[nodiscard]] QString parseGluetunStatus(const QByteArray& json);
/// {"public_ip":"…"}
[[nodiscard]] QString parseGluetunPublicIp(const QByteArray& json);

} // namespace eMule::arr
