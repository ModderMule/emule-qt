#pragma once

/// @file FakeHttpServer.h
/// @brief In-process HTTP/1.1 responder for download tests: records every request,
/// answers from a handler. Connection: close, one request per connection.

#include <QByteArray>
#include <QByteArrayList>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <functional>
#include <utility>

class FakeHttpServer : public QObject {
public:
    struct Response {
        int status = 200;
        QByteArray body;
        QList<std::pair<QByteArray, QByteArray>> headers{};
        bool silent = false;   ///< never answer (for abort / timeout tests)
    };
    using Handler = std::function<Response(const QByteArray& path, const QByteArray& request)>;

    explicit FakeHttpServer(const QHostAddress& bind = QHostAddress::LocalHost)
    {
        m_ok = m_server.listen(bind, 0);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = m_server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    m_buffer[s] += s->readAll();
                    if (!m_buffer[s].contains("\r\n\r\n"))
                        return;   // headers still arriving
                    const QByteArray request = m_buffer.take(s);
                    m_requests.append(request);
                    const QByteArray path = request.split(' ').value(1);
                    const Response r = handler ? handler(path, request) : response;
                    if (r.silent)
                        return;
                    QByteArray out = "HTTP/1.1 " + QByteArray::number(r.status) + ' '
                                     + reason(r.status) + "\r\n";
                    for (const auto& [name, value] : r.headers)
                        out += name + ": " + value + "\r\n";
                    out += "Content-Type: application/octet-stream\r\n";
                    out += "Content-Length: " + QByteArray::number(r.body.size()) + "\r\n";
                    out += "Connection: close\r\n\r\n";
                    out += r.body;
                    s->write(out);
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }

    /// Fixed answer when no handler is set.
    Response response;
    Handler handler;

    [[nodiscard]] bool isListening() const { return m_ok; }
    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }
    [[nodiscard]] QUrl url(const QString& path = QStringLiteral("/list.dat"),
                           const QString& host = QStringLiteral("127.0.0.1")) const
    {
        return QUrl(QStringLiteral("http://%1:%2%3").arg(host).arg(port()).arg(path));
    }

    [[nodiscard]] int requestCount() const { return static_cast<int>(m_requests.size()); }
    [[nodiscard]] QByteArray request(int i = -1) const
    {
        if (m_requests.isEmpty())
            return {};
        return i < 0 ? m_requests.last() : m_requests.value(i);
    }
    /// Value of header @p name in request @p i (-1 = last); empty when absent.
    [[nodiscard]] QByteArray header(const QByteArray& name, int i = -1) const
    {
        for (const QByteArray& line : request(i).split('\n'))
            if (line.toLower().startsWith(QByteArray(name.toLower() + ':')))
                return line.mid(name.size() + 1).trimmed();
        return {};
    }

private:
    static QByteArray reason(int status)
    {
        switch (status) {
        case 200: return "OK";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 404: return "Not Found";
        default:  return "Status";
        }
    }

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffer;
    QByteArrayList m_requests;
    bool m_ok = false;
};
