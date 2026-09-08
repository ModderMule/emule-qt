#pragma once

/// @file FakeIndexerServer.h
/// @brief A newznab endpoint in-process: no network, no API key, no quota.
///
/// Promoted out of tst_IndexerSearch.cpp when the feed poller needed the same
/// thing. It answers the *whole* URL rather than only the query string, because
/// a feed does two different requests against one host — the search, and then
/// the .nzb the search pointed at — and a fake that cannot tell them apart
/// cannot test the second.

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

namespace eMule::testing {

/// Minimal HTTP/1.0 responder. One request per connection, closed on write —
/// enough for a client that issues plain GETs and reads to the end.
class FakeIndexerServer : public QTcpServer {
public:
    /// Called with the requested URL; returns [status, body].
    using Handler = std::function<QPair<int, QByteArray>(const QUrl&)>;

    explicit FakeIndexerServer(Handler handler, QObject* parent = nullptr)
        : QTcpServer(parent)
        , m_handler(std::move(handler))
    {
        listen(QHostAddress::LocalHost, 0);
    }

    [[nodiscard]] QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1/api").arg(serverPort());
    }

    /// Absolute URL for a path this server will serve, for a fixture that has to
    /// point an <enclosure> back at the fake.
    [[nodiscard]] QString urlFor(const QString& path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path);
    }

    int requestCount = 0;
    QStringList seenQueries;
    QStringList seenPaths;

protected:
    void incomingConnection(qintptr handle) override
    {
        auto* socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);

        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            const QByteArray request = socket->readAll();
            const qsizetype start = request.indexOf(' ');
            const qsizetype end = request.indexOf(' ', start + 1);
            if (start < 0 || end < 0)
                return;

            const QUrl url(QString::fromUtf8(request.mid(start + 1, end - start - 1)));

            ++requestCount;
            seenQueries.append(url.query());
            seenPaths.append(url.path());

            const auto [status, body] = m_handler(url);
            const QByteArray header =
                QByteArrayLiteral("HTTP/1.0 ") + QByteArray::number(status)
                + QByteArrayLiteral(" X\r\nContent-Type: application/xml\r\nContent-Length: ")
                + QByteArray::number(body.size()) + QByteArrayLiteral("\r\n\r\n");
            socket->write(header);
            socket->write(body);
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    Handler m_handler;
};

} // namespace eMule::testing
