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
/// - Restarts (live.md §2.3, ruling #15 of batch 12): one fMP4 stream can't carry a second
///   `moov`. On a segment with `gap`, another `init` or a new broadcast (`start`), every begun
///   response ends (with a filler box, never an empty last chunk) and the window starts anew;
///   the next request gets the new init data and the window. A browser asking for HTML gets
///   a small player page at `/` that reconnects to `/stream` when a response ends.
///
/// was: ToDo: broadcasting (liveBroadcast, livePushSegment, the channel key) is out of scope.
/// (done: Ed3kLiveBroadcast)
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
        std::uint64_t restarts = 0;   ///< responses ended for a gap, a new init or broadcast
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
    /// a segment held for later viewers, with the init data it needs
    struct Held {
        QByteArray data;
        std::optional<::ed3k::Root> init;
    };
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
        // std::deque<QByteArray> window;   (was: one init for all; now per segment, #15)
        std::deque<Held> window;
        /// init data by root (the newest few: a viewer begins with its window's)
        std::deque<std::pair<::ed3k::Root, QByteArray>> inits;
        /// the broadcast and init of the window; a change restarts the viewers (#15)
        std::optional<std::uint64_t> start;
        std::optional<::ed3k::Root> windowInit;
        std::uint64_t restarts = 0;
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
    /// #15: ends the begun responses (the player asks again) and empties the window.
    void restartViewers(Watched& w);
    /// The last chunk of a begun response: never empty (Qt refuses that), a box the
    /// container ignores.
    void endResponse(const Watched& w, Viewer& v);
    /// The init data of root @p r held, empty when none.
    [[nodiscard]] static QByteArray initOf(const Watched& w, const std::optional<::ed3k::Root>& r);
    /// The player page (HTML) of a stream: it reconnects when a response ends.
    void answerPage(QHttpServerResponder& r, const QString& id) const;
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
