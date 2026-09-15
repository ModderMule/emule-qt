#pragma once

/// @file FakeProxyServer.h
/// @brief A loopback SOCKS5 or HTTP CONNECT proxy that records where it was sent.
///
/// Enough of RFC 1928/1929 and of HTTP CONNECT for Qt's own proxy clients and no
/// more: one tunnel per connection, then bytes are piped both ways. It exists to
/// prove *that* a socket went through a proxy and *which name* it asked for — a
/// SOCKS5 request carrying a domain name (ATYP 3) is what keeps DNS off the local
/// resolver.
///
/// Every lambda uses the server as its context object, so ~QObject cuts them
/// before the sockets (children) are destroyed. No Q_OBJECT, like
/// FakeNntpServer.h, so it stays header-only.

#include <QByteArray>
#include <QHostAddress>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

#include <memory>
#include <vector>

namespace eMule::testing {

class FakeProxyServer : public QTcpServer {
public:
    enum class Kind { Socks5, HttpConnect };

    explicit FakeProxyServer(Kind kind, QObject* parent = nullptr)
        : QTcpServer(parent)
        , m_kind(kind)
    {
        connect(this, &QTcpServer::newConnection, this, [this] { onNewConnection(); });
    }

    /// Listen on loopback. Returns the port, or 0 on failure.
    quint16 start()
    {
        if (!listen(QHostAddress::LocalHost, 0))
            return 0;
        return serverPort();
    }

    /// Require these credentials: SOCKS5 method 2, or HTTP Basic after a 407.
    void setCredentials(QString user, QString pass)
    {
        m_user = std::move(user);
        m_pass = std::move(pass);
    }

    /// Every tunnel asked for, as "host:port", in arrival order.
    [[nodiscard]] QStringList targets() const { return m_targets; }

    /// Whether any SOCKS5 request named its target rather than an address.
    [[nodiscard]] bool sawDomainName() const { return m_sawDomain; }

    /// HTTP requests answered 407.
    [[nodiscard]] int challenges() const { return m_challenges; }

private:
    struct Session {
        QPointer<QTcpSocket> client;
        QPointer<QTcpSocket> upstream;
        QByteArray buffer;
        int stage = 0;   ///< SOCKS5: 0 greeting, 1 auth, 2 request; 3 piping (both kinds)
    };

    void onNewConnection()
    {
        while (QTcpSocket* client = nextPendingConnection()) {
            auto session = std::make_unique<Session>();
            Session* s = session.get();
            s->client = client;
            m_sessions.push_back(std::move(session));

            connect(client, &QTcpSocket::readyRead, this, [this, s] { onClientData(s); });
            connect(client, &QTcpSocket::disconnected, this, [s] {
                if (s->upstream)
                    s->upstream->disconnectFromHost();
            });
        }
    }

    void onClientData(Session* s)
    {
        if (!s->client)
            return;
        s->buffer += s->client->readAll();

        if (s->stage == 3) {
            if (s->upstream)
                s->upstream->write(s->buffer);
            s->buffer.clear();
            return;
        }
        if (m_kind == Kind::Socks5)
            advanceSocks5(s);
        else
            advanceHttp(s);
    }

    void advanceSocks5(Session* s)
    {
        const QByteArray& b = s->buffer;

        if (s->stage == 0) {
            if (b.size() < 2 || b.size() < 2 + quint8(b.at(1)))
                return;
            const QByteArray methods = b.mid(2, quint8(b.at(1)));
            s->buffer.remove(0, 2 + quint8(b.at(1)));

            const bool wantAuth = !m_user.isEmpty();
            char method = '\xFF';
            if (wantAuth && methods.contains('\x02'))
                method = '\x02';
            else if (!wantAuth && methods.contains('\x00'))
                method = '\x00';
            s->client->write(QByteArray("\x05", 1) + QByteArray(1, method));
            if (method == '\xFF') {
                s->client->disconnectFromHost();
                return;
            }
            s->stage = method == '\x02' ? 1 : 2;
        }

        if (s->stage == 1) {
            if (b.size() < 2)
                return;
            const int ulen = quint8(b.at(1));
            if (b.size() < 3 + ulen)
                return;
            const int plen = quint8(b.at(2 + ulen));
            if (b.size() < 3 + ulen + plen)
                return;
            const QString user = QString::fromUtf8(b.mid(2, ulen));
            const QString pass = QString::fromUtf8(b.mid(3 + ulen, plen));
            s->buffer.remove(0, 3 + ulen + plen);

            const bool ok = user == m_user && pass == m_pass;
            s->client->write(QByteArray("\x01", 1) + QByteArray(1, ok ? '\x00' : '\x01'));
            if (!ok) {
                s->client->disconnectFromHost();
                return;
            }
            s->stage = 2;
        }

        if (s->stage == 2) {
            if (b.size() < 5)
                return;
            QString host;
            int consumed = 0;
            switch (b.at(3)) {
            case '\x01':
                if (b.size() < 10)
                    return;
                host = QStringLiteral("%1.%2.%3.%4")
                           .arg(quint8(b.at(4))).arg(quint8(b.at(5)))
                           .arg(quint8(b.at(6))).arg(quint8(b.at(7)));
                consumed = 8;
                break;
            case '\x03': {
                const int len = quint8(b.at(4));
                if (b.size() < 5 + len + 2)
                    return;
                host = QString::fromLatin1(b.mid(5, len));
                consumed = 5 + len;
                m_sawDomain = true;
                break;
            }
            case '\x04':
                if (b.size() < 22)
                    return;
                host = QHostAddress(reinterpret_cast<const quint8*>(b.constData() + 4)).toString();
                consumed = 20;
                break;
            default:
                s->client->disconnectFromHost();
                return;
            }
            const quint16 port = quint16((quint8(b.at(consumed)) << 8) | quint8(b.at(consumed + 1)));
            s->buffer.remove(0, consumed + 2);

            openTunnel(s, host, port,
                       QByteArray("\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10),
                       QByteArray("\x05\x05\x00\x01\x00\x00\x00\x00\x00\x00", 10));
        }
    }

    void advanceHttp(Session* s)
    {
        const qsizetype end = s->buffer.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QByteArray head = s->buffer.left(end);
        s->buffer.remove(0, end + 4);

        const QList<QByteArray> lines = head.split('\n');
        const QList<QByteArray> request = lines.value(0).trimmed().split(' ');
        if (request.value(0) != "CONNECT") {
            s->client->write("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n");
            s->client->disconnectFromHost();
            return;
        }

        if (!m_user.isEmpty()) {
            const QByteArray expected =
                "Basic " + (m_user + QLatin1Char(':') + m_pass).toUtf8().toBase64();
            bool authorised = false;
            for (const QByteArray& line : lines) {
                const qsizetype colon = line.indexOf(':');
                if (colon > 0 && line.left(colon).trimmed().toLower() == "proxy-authorization"
                    && line.mid(colon + 1).trimmed() == expected) {
                    authorised = true;
                }
            }
            if (!authorised) {
                ++m_challenges;
                s->client->write("HTTP/1.1 407 Proxy Authentication Required\r\n"
                                 "Proxy-Authenticate: Basic realm=\"fake\"\r\n"
                                 "Content-Length: 0\r\n\r\n");
                // Another request may follow on this connection.
                if (!s->buffer.isEmpty())
                    advanceHttp(s);
                return;
            }
        }

        const QByteArray target = request.value(1);
        const qsizetype colon = target.lastIndexOf(':');
        openTunnel(s, QString::fromLatin1(target.left(colon)),
                   quint16(target.mid(colon + 1).toUInt()),
                   QByteArray("HTTP/1.1 200 Connection established\r\n\r\n"),
                   QByteArray("HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n"));
    }

    void openTunnel(Session* s, const QString& host, quint16 port, const QByteArray& okReply,
                    const QByteArray& failReply)
    {
        m_targets.append(QStringLiteral("%1:%2").arg(host).arg(port));

        auto* upstream = new QTcpSocket(this);
        s->upstream = upstream;
        connect(upstream, &QTcpSocket::connected, this, [s, okReply] {
            if (!s->client)
                return;
            s->client->write(okReply);
            s->stage = 3;
            if (!s->buffer.isEmpty()) {
                s->upstream->write(s->buffer);
                s->buffer.clear();
            }
        });
        connect(upstream, &QTcpSocket::readyRead, this, [s] {
            if (s->client && s->upstream)
                s->client->write(s->upstream->readAll());
        });
        connect(upstream, &QTcpSocket::errorOccurred, this, [s, failReply] {
            if (s->stage != 3 && s->client) {
                s->client->write(failReply);
                s->client->disconnectFromHost();
            }
        });
        connect(upstream, &QTcpSocket::disconnected, this, [s] {
            if (s->client)
                s->client->disconnectFromHost();
        });
        upstream->connectToHost(host, port);
    }

    Kind m_kind;
    QString m_user;
    QString m_pass;
    QStringList m_targets;
    bool m_sawDomain = false;
    int m_challenges = 0;
    std::vector<std::unique_ptr<Session>> m_sessions;
};

} // namespace eMule::testing
