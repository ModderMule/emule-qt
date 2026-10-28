#pragma once

/// @file Ed3kLive.h
/// @brief Watching live streams (live.md) in the daemon: an `ed3k:live` link watched
/// (liveWatchLink), its init data and segments served as one progressive HTTP stream on the
/// website server's port (Ed3kSiteServer, loopback only) at
/// `http://live-<stream hex>.localhost:<port>/`, which the GUI opens in the system's player
/// or browser.
///
/// - The response is chunked: the init data, the segments held (the last kWindow), then
///   every new segment as it is verified (fragmented MP4 plays as it grows, §2.3).
/// - States: joining (no segment yet), playing, interrupted (LiveInterrupted: no record for
///   60 s), no peers (LiveGone took the last neighbour), ended (LiveEnded: the responses end).
/// - leave(): liveLeave; the responses end.
///
/// ToDo: broadcasting (liveBroadcast, livePushSegment, the channel key) is out of scope.
/// ToDo: a viewer that cannot play `media`/`codecs` should say so and not join (§2.3); the
/// system player decides now.

#include "Ed3kFfi.h"

#include <QByteArray>
#include <QHttpServerResponder>
#include <QObject>
#include <QString>

#include <cstdint>
#include <deque>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class QHttpServerRequest;

namespace eMule::ed3k {

class Ed3kSession;
class Ed3kSiteServer;

class Ed3kLive : public QObject {
    Q_OBJECT

public:
    enum class State : int { Joining = 0, Playing = 1, Interrupted = 2, NoPeers = 3, Ended = 4 };
    /// Segments kept for a viewer that comes later.
    static constexpr std::size_t kWindow = 4;
    /// Responses per stream at most (the oldest goes).
    static constexpr std::size_t kViewersMax = 8;

    struct Info {
        QString id;               ///< the stream, 32 hex digits
        QString title;            ///< dn of the link
        QString media, codecs;
        State state = State::Joining;
        int neighbours = 0;
        std::uint64_t segments = 0;
        std::uint64_t bytes = 0;
        int viewers = 0;
        QString url;              ///< empty while the website server does not listen
    };

    /// Registers itself as the host handler of @p sites.
    Ed3kLive(Ed3kSession& session, Ed3kSiteServer& sites, QObject* parent = nullptr);
    ~Ed3kLive() override;

    /// Watches the channel of an `ed3k:live` link; its id. The error is user-visible.
    std::expected<QString, QString> watch(const QString& link);
    bool leave(const QString& id);
    [[nodiscard]] std::vector<Info> list() const;
    [[nodiscard]] std::optional<Info> info(const QString& id) const;
    /// http://live-<id>.localhost:<port>/; empty while not listening.
    [[nodiscard]] QString urlOf(const QString& id) const;

    /// Peers by hand (tests: no Kad).
    bool addPeer(const QString& id, const ::ed3k::Source& peer);

signals:
    /// A stream's state, neighbours or counters changed (coalesced).
    void changed();

private:
    struct Viewer;
    struct Watched {
        ::ed3k::Stream stream;
        QString title;
        QString media = QStringLiteral("video/mp4");
        QString codecs;
        State state = State::Joining;
        bool left = false;   ///< LiveLeft came: the handle is stale
        std::set<::ed3k::NodeId> neighbours;
        std::optional<::ed3k::Root> initRoot;
        QByteArray init;
        std::deque<QByteArray> window;
        std::uint64_t segments = 0;
        std::uint64_t bytes = 0;
        std::vector<std::unique_ptr<Viewer>> viewers;
    };

    void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onSessionStatus();
    bool onRequest(const QHttpServerRequest& request, QHttpServerResponder& responder);
    void onSegment(Watched& w, const ::ed3k::event::LiveSegment& s);
    /// Headers, init data and the window to a viewer not begun.
    void begin(Watched& w, Viewer& v);
    /// Ends every response of @p w.
    void endViewers(Watched& w);
    Watched* find(const ::ed3k::Stream& stream);
    [[nodiscard]] Info infoOf(const Watched& w) const;
    void markChanged();
    [[nodiscard]] static QString idOf(const ::ed3k::Stream& stream);

    Ed3kSession& m_session;
    Ed3kSiteServer& m_sites;
    std::map<::ed3k::Stream, Watched> m_streams;
    bool m_changePending = false;
};

} // namespace eMule::ed3k
