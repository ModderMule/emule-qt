#pragma once

/// @file ClientIcons.h
/// @brief Client software icon (MFC GetDisplayImage) for every list showing peers.

#include <QByteArray>
#include <QCborArray>
#include <QCborMap>
#include <QIcon>
#include <QList>
#include <QString>

#include <optional>

namespace eMule {

/// "Plus" variants mean the client has credit (scoreRatio > 1.0). Friends keep
/// the software icon with a small friend badge at the bottom-right.
[[nodiscard]] QIcon clientSoftwareIcon(int softwareId, bool hasCredit, bool isFriend);

/// The torrent/usenet/hoster join an ed3k transfer belongs to (Ed3kJoinBase tag).
enum class Ed3kJoin { None, Torrent, Usenet, Hoster };
/// "torrent", "usenet", "hoster" → the join; anything else None.
[[nodiscard]] Ed3kJoin ed3kJoinOf(const QString& tag);

/// ed3k node: the eMule client icon with a Mars-red mane (no MFC original).
/// @p anonymous: the transfer runs on our anonymous key (hops.md §1), drawn as a grey
/// shadow. Says nothing about the peer's key. @p join: that network's icon as a badge.
[[nodiscard]] QIcon ed3kClientIcon(bool anonymous = false, Ed3kJoin join = Ed3kJoin::None);

/// Tooltip line for an ed3k row: "ed3k, anonymous key, for a torrent".
[[nodiscard]] QString ed3kNetworkText(bool anonymous, Ed3kJoin join);

/// A client of a torrent/usenet/hoster job's own network (a BitTorrent peer, a news server,
/// a hoster) in the Transfers Downloading list: that network's icon.
[[nodiscard]] QIcon networkClientIcon(Ed3kJoin network);
/// The resource path of networkClientIcon (badges); None: empty.
[[nodiscard]] QString networkIconPath(Ed3kJoin network);
/// "BitTorrent", "Usenet", "Hoster"; None: empty.
[[nodiscard]] QString networkName(Ed3kJoin network);

/// One other name of an ed3k root ("aliases" entry: network, name, id): "Torrent: <name> (btih:…)".
[[nodiscard]] QString ed3kAliasText(const QCborMap& alias);
/// One "ed3kJoins" row (Ed3kUploadsBridge::joinsOf: network, state, reason, key, jobs) as a
/// line: "BitTorrent: Skipped (the file is under the anonymous key; BitTorrent is off under …)".
[[nodiscard]] QString ed3kJoinText(const QCborMap& join);
/// Every row of @p joins, one per line; empty: none.
[[nodiscard]] QString ed3kJoinsText(const QCborArray& joins);

/// An ed3k source of a torrent/usenet/hoster job (Ed3kUploadsBridge::jobSources row): a
/// peer row in the Torrents, Usenet and Hoster tabs.
struct Ed3kJobSource {
    QString key;        ///< userHash: unique per row
    QString name;       ///< nick, else the node ID's start
    QString software;
    QString address;    ///< "addr:port"; empty: not known (relayed, anonymous key)
    QString cc;         ///< address's country code
    QString fileName;   ///< the job's file it serves
    QString state;      ///< "Downloading" / "OnQueue"
    qint64 rate = 0;
    qint64 received = 0;
    int have = 0;       ///< its pieces of ours
    int pieces = 0;
    QByteArray parts;   ///< per 4 MiB piece, sourcePartMap codes (0 lacks, 1 both, 2 it has, 3 asked, 4 receiving)
    bool anonymous = false;
    Ed3kJoin join = Ed3kJoin::None;

    [[nodiscard]] static Ed3kJobSource fromCbor(const QCborMap& m);
    [[nodiscard]] QIcon icon() const { return ed3kClientIcon(anonymous, join); }
    [[nodiscard]] QString addressText() const;   ///< address, else "(hidden)"
    [[nodiscard]] QString stateText() const;     ///< "Downloading" / "On Queue"
    [[nodiscard]] QString toolTip() const;       ///< network line, client, file
};

/// The I2P icon: I2P torrent peers, the Options > BitTorrent > I2P page.
[[nodiscard]] QIcon i2pIcon();

/// MFC's five source state icons, in its image-list order
/// (DownloadListCtrl.cpp:181-185).
enum class SourceStateIcon { Downloading, OnQueue, Connecting, NoNeededOrFull, Unknown };

/// Which of them a source row shows (MFC DownloadListCtrl.cpp:569-594).
/// @param stateToken the daemon's download state token ("OnQueue", ...)
[[nodiscard]] SourceStateIcon sourceStateIconKind(const QString& stateToken, bool remoteQueueFull,
                                                  bool a4af);
[[nodiscard]] QIcon sourceStateIcon(SourceStateIcon kind);

/// Two 16 px icons side by side, 20 px apart as MFC draws them.
[[nodiscard]] QIcon iconPair(const QIcon& first, const QIcon& second);

/// One network a transfer runs on (Networks column of Transfers and Torrents, #261).
/// Public/I2p: the transports a torrent may use; Ed3kAnon: ed3k under our anonymous key.
enum class TransferNet : quint8 { Ed2k, Ed3k, Ed3kAnon, Torrent, Usenet, Hoster, Public, I2p };
/// The network of a torrent/usenet/hoster job; None: nullopt.
[[nodiscard]] std::optional<TransferNet> transferNetOf(Ed3kJoin network);
/// @p nets side by side, 16 px each (cached). Empty: a null icon.
[[nodiscard]] QIcon transferNetsIcon(const QList<TransferNet>& nets);
/// "eD2K, ed3k": tooltip and sort text of the same.
[[nodiscard]] QString transferNetsText(const QList<TransferNet>& nets);

} // namespace eMule
