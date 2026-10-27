#pragma once

/// @file Ed3kHttp.h
/// @brief Host IO of the ed3k node: carries out event::HttpRequest (libs/packmule/docs/host-io.md).
///
/// A small HTTP/1.1 client on QTcpSocket/QSslSocket, not QNetworkAccessManager: the
/// core decides the address, so the host must connect to exactly that address while
/// Host, SNI and the certificate check use the name of the URL (QNAM cannot pin an
/// address). No redirect is followed, no Content-Encoding decoded, no cookies; at
/// most maxLen body bytes; gives up after timeoutS. A name is resolved here and every
/// address is offered to the core (httpResolved) before connecting.
///
/// Every answer goes through Answers; the session calls the engine and pumps.
///
/// JSON-RPC (event::RpcRequest, host-io.md): startRpc() POSTs the body to a URL the user
/// configured (ed3k.creditRpc / ed3k.chaintubeRpc), as mode B does (packmule_udp::http):
/// application/json, at most 16 MiB back, 30 s; a 2xx body goes to rpcAnswer, the rest to
/// rpcFailed. The user's URL: its name is resolved here and not offered to the core.

#include "Ed3kFfi.h"

#include <QByteArray>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>

namespace eMule::ed3k {

class Ed3kHttp : public QObject {
    Q_OBJECT

public:
    /// What the client tells the core. resolved: true = connect there; false = the
    /// request is over (Denied/Stale), offer no other address.
    struct Answers {
        std::function<bool(std::uint64_t id, const ::ed3k::Address& addr)> resolved;
        std::function<void(std::uint64_t id)> unreachable;
        std::function<void(std::uint64_t id, std::uint16_t status, const QByteArray& headers,
                           const QByteArray& body)> answer;
        std::function<void(std::uint64_t id)> failed;
        /// JSON-RPC: the body of a 2xx answer / anything else (no connection, other status)
        std::function<void(std::uint64_t id, const QByteArray& body)> rpcAnswer{};
        std::function<void(std::uint64_t id)> rpcFailed{};
    };

    /// RPC limits of mode B (packmule_udp::http RPC_BODY_MAX, host_io::RPC_TIMEOUT_S)
    static constexpr std::uint64_t kRpcBodyMax = 16u << 20;
    static constexpr std::uint64_t kRpcTimeoutS = 30;

    Ed3kHttp(Answers answers, QString userAgent, QObject* parent = nullptr);
    ~Ed3kHttp() override;

    /// The user's proxy (NoProxy = direct). Requests out keep the old one.
    void setProxy(const QNetworkProxy& proxy) { m_proxy = proxy; }

    /// Starts @p req. Failures are answered through Answers, maybe at once.
    void start(const ::ed3k::event::HttpRequest& req);

    /// RpcRequest @p id: POSTs @p body to @p url (https, or http to a loopback host).
    /// Answered through rpcAnswer / rpcFailed, maybe at once.
    void startRpc(std::uint64_t id, const QString& url, const QByteArray& body);

    /// Whether @p url may take RPC calls: https, or http to a loopback host (a local node).
    [[nodiscard]] static bool rpcUrlUsable(const QString& url);

    /// IoCancel: drops the request (HTTP or RPC), no answer.
    void cancel(std::uint64_t id);

    /// Drops every request, no answers (engine going away).
    void clear();

    [[nodiscard]] std::size_t pending() const { return m_requests.size(); }

private:
    struct Request;

    /// Arms the timeout and connects (literal) or resolves the name.
    void begin(std::unique_ptr<Request> r);
    void resolveNext(Request& r);
    void connectTo(Request& r, const QHostAddress& ip, std::uint16_t port);
    void onConnected(std::uint64_t id);
    void onReadyRead(std::uint64_t id);
    void onSocketError(std::uint64_t id);
    void onDisconnected(std::uint64_t id);
    /// false: the response is broken or too long (failed answered).
    bool parse(Request& r, bool atEnd);
    void finish(std::uint64_t id);
    void fail(std::uint64_t id);
    void drop(std::uint64_t id);
    [[nodiscard]] QByteArray requestBytes(const Request& r) const;

    Answers m_answers;
    QString m_userAgent;
    QNetworkProxy m_proxy{QNetworkProxy::NoProxy};
    std::map<std::uint64_t, std::unique_ptr<Request>> m_requests;
};

} // namespace eMule::ed3k
