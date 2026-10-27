#pragma once

/// @file Ed3kSiteServer.h
/// @brief The local website server of name.md §6.2: its own QHttpServer on 127.0.0.1 and
/// ::1 (never the web interface's), one origin per set (`<id>.<key>.localhost:<port>`).
///
/// Per request: siteHost → siteGate (421, 405, 403) → a readable host is resolved and
/// answered with a 302 only; a set's origin is looked up with sitePage over the newest
/// manifest held (followed for the user only) and its member sent with sitePage's headers.
/// Only verified bytes go out: members not held are fetched first (Ed3kNames, whole, at most
/// kSiteMemberMax) and answered 504 after 60 s (§6.3). Listens only while the node runs.

#include "Ed3kNames.h"

#include <QHttpServerResponder>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <vector>

class QHttpServer;
class QHttpServerRequest;

namespace eMule::ed3k {

class Ed3kSession;

class Ed3kSiteServer : public QObject {
    Q_OBJECT

public:
    /// §6.3: a member not there within this long is answered 504.
    static constexpr int kMemberWaitMs = 60'000;

    Ed3kSiteServer(Ed3kSession& session, Ed3kNames& names, QObject* parent = nullptr);
    ~Ed3kSiteServer() override;

    /// The port (pref ed3k.sitePort, picked once and kept: it is part of every origin);
    /// 0 = any free one (tests). A change while listening rebinds.
    void setPort(std::uint16_t port);
    /// The port listened on; 0 while not listening.
    [[nodiscard]] std::uint16_t port() const;
    [[nodiscard]] bool isListening() const { return port() != 0; }
    /// http://<id>.<key>.localhost:<port>/<path> of a set; empty while not listening.
    [[nodiscard]] QString originUrl(const ::ed3k::Key32& key, const QByteArray& id, const QString& path = {}) const;
    /// How long a request waits for a manifest, a member or a name (tests shorten it).
    void setWaitMs(int ms) { m_waitMs = ms; }

signals:
    /// A readable host the user typed resolved to a name that needs the user's yes first
    /// (§4.4 confirm, §5.3 DNS unverified): the GUI asks, then opens it.
    void confirmNeeded(const eMule::ed3k::Ed3kNames::Resolved& r);

private:
    struct Pending;

    void onSessionStatus();
    void listen();
    void close();
    void onRequest(const QHttpServerRequest& request, QHttpServerResponder& responder);
    /// A set's request with its manifest: the page, or a member fetch to wait for.
    void servePage(std::unique_ptr<Pending> p, const QByteArray& manifest);
    void onSiteChanged(const QByteArray& key, const QByteArray& id);
    void onMemberReady(const QByteArray& root);
    void onResolved(const Ed3kNames::Resolved& r);
    void onFailed(quint64 id, const QString& name, int why, const QList<QByteArray>& ties);
    void onTick();
    void park(std::unique_ptr<Pending> p);
    /// Takes every parked request @p match accepts.
    std::vector<std::unique_ptr<Pending>> take(const std::function<bool(const Pending&)>& match);
    static void answer(QHttpServerResponder& r, int status, const QByteArray& body = {}, const QByteArray& headerLines = {},
                       bool head = false);

    Ed3kSession& m_session;
    Ed3kNames& m_names;
    std::unique_ptr<QHttpServer> m_server;
    std::uint16_t m_wantPort = 0;
    std::uint16_t m_port = 0;
    int m_waitMs = kMemberWaitMs;
    bool m_toldFetchSite = false;
    std::list<std::unique_ptr<Pending>> m_pending;
    QTimer m_tick;
};

} // namespace eMule::ed3k
