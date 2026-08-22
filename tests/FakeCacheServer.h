#pragma once

/// @file FakeCacheServer.h
/// @brief A minimal, deliberately uncooperative HTTP Cache origin for tests.
///
/// Speaks just enough of docs/protocol/http-cache-spec.md to drive a real
/// HttpCachePublisher: POST /v1/chunks stores a blob and answers with its id and
/// url, GET /v1/chunks/<id> hands it back, GET /v1/info identifies the service.
/// Every answer is scriptable, because the interesting cases are the ones where
/// the server refuses.
///
/// No Q_OBJECT: it declares no signals or slots of its own, and a functor
/// connect() only needs a QObject* for context. That keeps the whole thing
/// header-only.

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include <deque>
#include <memory>
#include <utility>

namespace eMule::testing {

/// One scripted answer to POST /v1/chunks.
struct FakeCacheReply {
    int status = 201;
    QByteArray body;                ///< verbatim; empty means "make one up"
    QByteArray extraHeaders;        ///< e.g. "Retry-After: 120\r\n"
    bool closeWithoutAnswering = false;
};

class FakeCacheServer : public QTcpServer {
public:
    using QTcpServer::QTcpServer;

    /// The standing answer, used once the scripted queue runs dry.
    void setReply(FakeCacheReply reply) { m_reply = std::move(reply); }

    /// A one-shot answer. Queued replies are consumed in order, so a test can say
    /// "fail the first upload, then behave" without touching the server again.
    void queueReply(FakeCacheReply reply) { m_script.push_back(std::move(reply)); }

    /// Connections accepted — uploads, fetches and abandoned attempts alike.
    [[nodiscard]] int requestCount() const { return m_requests; }

    /// POSTs whose body arrived in full and were answered. This is the one to
    /// assert on when the question is "how many times did we upload a part?".
    [[nodiscard]] int uploadCount() const { return m_uploads; }

    /// Body bytes of the most recent upload.
    [[nodiscard]] qint64 bodyBytesReceived() const { return m_bodyBytes; }

    /// The ciphertext a successful POST left behind, by chunk id.
    [[nodiscard]] QByteArray storedChunk(const QString& id) const { return m_chunks.value(id); }

    /// The standing answer to GET /v1/info. An empty body means the canonical one.
    void setInfoReply(FakeCacheReply reply) { m_infoReply = std::move(reply); }

    /// GET /v1/info requests answered.
    [[nodiscard]] int infoCount() const { return m_infoRequests; }

    /// Request headers of the most recent /v1/info, verbatim. The probe must not
    /// put a credential on this request, and that is only checkable from here.
    [[nodiscard]] QByteArray lastInfoHeaders() const { return m_lastInfoHeaders; }

    [[nodiscard]] QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(serverPort());
    }

protected:
    void incomingConnection(qintptr handle) override
    {
        auto* socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);
        ++m_requests;

        // Per connection, not per server: two uploads in one test would otherwise
        // parse the second request against the first one's bytes.
        struct Conn {
            QByteArray buffer;
            bool answered = false;
        };
        auto conn = std::make_shared<Conn>();

        connect(socket, &QTcpSocket::readyRead, this, [this, socket, conn] {
            if (conn->answered)
                return;

            conn->buffer.append(socket->readAll());

            const auto headerEnd = conn->buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;

            const QByteArray headers = conn->buffer.left(headerEnd);

            if (headers.startsWith("GET ")) {
                conn->answered = true;
                const QByteArray path = requestPath(headers);
                if (path.endsWith("/v1/info")) {
                    ++m_infoRequests;
                    m_lastInfoHeaders = headers;
                    serveInfo(socket);
                } else {
                    serveChunk(socket, path);
                }
                return;
            }

            // Wait for the whole declared body before answering, so the test is
            // not racing the upload throttle.
            const qint64 declared = contentLength(headers);
            const qint64 have = conn->buffer.size() - (headerEnd + 4);
            if (have < declared)
                return;

            conn->answered = true;
            m_bodyBytes = have;
            ++m_uploads;
            respond(socket, conn->buffer.mid(headerEnd + 4, static_cast<qsizetype>(declared)));
        });

        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    // Private helpers after the public interface, per the house style.

    [[nodiscard]] static QByteArray requestPath(const QByteArray& headers)
    {
        const QByteArray line = headers.left(headers.indexOf('\r'));
        const auto first = line.indexOf(' ');
        const auto second = line.indexOf(' ', first + 1);
        if (first < 0 || second < 0)
            return {};
        return line.mid(first + 1, second - first - 1);
    }

    [[nodiscard]] static qint64 contentLength(const QByteArray& headers)
    {
        for (const QByteArray& line : headers.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (!trimmed.toLower().startsWith("content-length:"))
                continue;
            return trimmed.mid(trimmed.indexOf(':') + 1).trimmed().toLongLong();
        }
        return 0;
    }

    [[nodiscard]] FakeCacheReply nextReply()
    {
        if (m_script.empty())
            return m_reply;

        FakeCacheReply reply = m_script.front();
        m_script.pop_front();
        return reply;
    }

    /// Keep the blob and hand back the id it is filed under. Derived from the
    /// upload counter so two parts never collide and the ids stay reproducible.
    QString storeChunk(const QByteArray& payload)
    {
        const QString id = QString::fromLatin1(
            QCryptographicHash::hash(QByteArray::number(m_uploads), QCryptographicHash::Md5)
                .toHex());
        m_chunks.insert(id, payload);
        return id;
    }

    void respond(QTcpSocket* socket, const QByteArray& payload)
    {
        const FakeCacheReply reply = nextReply();

        if (reply.closeWithoutAnswering) {
            socket->abort();
            return;
        }

        QByteArray body = reply.body;
        if (body.isEmpty()) {
            if (reply.status == 201 || reply.status == 200) {
                const QString id = storeChunk(payload);

                // The size is echoed rather than invented: a publisher that sent a
                // truncated body then fails its own size check, without the test
                // having to look for it.
                body = "{\"id\":\"" + id.toLatin1() + "\","
                       "\"url\":\"" + (baseUrl() + QStringLiteral("/v1/chunks/") + id).toLatin1()
                       + "\","
                       "\"size\":" + QByteArray::number(payload.size()) + ","
                       "\"expires\":"
                       + QByteArray::number(QDateTime::currentSecsSinceEpoch() + 21600) + "}";
            } else {
                body = R"({"error":"the fake server said no","status":0})";
            }
        }

        QByteArray head = "HTTP/1.1 " + QByteArray::number(reply.status) + " X\r\n";
        head += "Content-Type: application/json\r\n";
        head += reply.extraHeaders;
        head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        head += "Connection: close\r\n\r\n";

        socket->write(head);
        socket->write(body);
        socket->flush();
        socket->disconnectFromHost();
    }

    /// The handshake a client runs before it trusts a base URL.
    void serveInfo(QTcpSocket* socket)
    {
        if (m_infoReply.closeWithoutAnswering) {
            socket->abort();
            return;
        }

        QByteArray body = m_infoReply.body;
        if (body.isEmpty()) {
            body = R"({"service":"emule-http-cache","version":1,"implementation":"fake",)"
                   R"("maxChunkSize":10485760,"defaultTtl":172800,"maxTtl":604800,)"
                   R"("rangeSupported":true,"uploadRequiresAuth":true})";
        }

        QByteArray head = "HTTP/1.1 " + QByteArray::number(m_infoReply.status) + " X\r\n";
        head += "Content-Type: application/json\r\n";
        head += m_infoReply.extraHeaders;
        head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        head += "Connection: close\r\n\r\n";

        socket->write(head);
        socket->write(body);
        socket->flush();
        socket->disconnectFromHost();
    }

    /// Whole-object GET only. Ranged fetches are HttpCacheClient's business and
    /// tst_HttpCacheResume already drives them against a server of its own.
    void serveChunk(QTcpSocket* socket, const QByteArray& path)
    {
        const QString id = QString::fromLatin1(path.mid(path.lastIndexOf('/') + 1));
        const QByteArray blob = m_chunks.value(id);

        QByteArray head;
        if (blob.isEmpty()) {
            head = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            socket->write(head);
        } else {
            head = "HTTP/1.1 200 OK\r\n";
            head += "Content-Type: application/octet-stream\r\n";
            head += "Content-Length: " + QByteArray::number(blob.size()) + "\r\n";
            head += "Connection: close\r\n\r\n";
            socket->write(head);
            socket->write(blob);
        }

        socket->flush();
        socket->disconnectFromHost();
    }

    FakeCacheReply m_reply;
    FakeCacheReply m_infoReply{200, {}, {}, false};
    std::deque<FakeCacheReply> m_script;
    QHash<QString, QByteArray> m_chunks;
    int m_requests = 0;
    int m_uploads = 0;
    int m_infoRequests = 0;
    QByteArray m_lastInfoHeaders;
    qint64 m_bodyBytes = 0;
};

} // namespace eMule::testing
