/// @file tst_HttpFileDownload.cpp
/// @brief Tests for net/HttpFileDownload — the shared config-file downloader.
///
/// Covers the asynchronous get(), which is the path the three GUI update buttons take
/// (IP filter, nodes.dat, server.met). Served from an in-process QTcpServer so there is
/// no external dependency and nothing to be flaky about.

#include "TestHelpers.h"
#include "net/HttpFileDownload.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <zlib.h>

using namespace eMule;

namespace {

/// gzip-compress @p data (windowBits 16+MAX_WBITS selects the gzip wrapper).
QByteArray gzipCompress(const QByteArray& data)
{
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }
    QByteArray out(static_cast<qsizetype>(deflateBound(&zs, static_cast<uLong>(data.size()))) + 32,
                   Qt::Uninitialized);
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    zs.avail_in = static_cast<uInt>(data.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());

    const int rc = deflate(&zs, Z_FINISH);
    const auto written = static_cast<qsizetype>(zs.total_out);
    deflateEnd(&zs);
    if (rc != Z_STREAM_END)
        return {};
    out.truncate(written);
    return out;
}

/// One-shot HTTP/1.1 server that answers every request with the same body.
/// Deliberately not gzip *transfer* encoding — the point is a compressed payload, which
/// is what Qt does not unwrap for us.
class OneShotServer : public QObject {
public:
    explicit OneShotServer(QByteArray body, int status = 200)
        : m_body(std::move(body)), m_status(status)
    {
        m_ok = m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            auto* sock = m_server.nextPendingConnection();
            QObject::connect(sock, &QTcpSocket::readyRead, sock, [this, sock]() {
                m_request += sock->readAll();
                if (!m_request.contains("\r\n\r\n"))
                    return;   // headers still arriving

                QByteArray resp = "HTTP/1.1 " + QByteArray::number(m_status)
                                + (m_status == 200 ? " OK" : " Not Found") + "\r\n";
                resp += "Content-Type: application/octet-stream\r\n";
                resp += "Content-Length: " + QByteArray::number(m_body.size()) + "\r\n";
                resp += "Connection: close\r\n\r\n";
                resp += m_body;
                sock->write(resp);
                sock->disconnectFromHost();
            });
            QObject::connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        });
    }

    [[nodiscard]] bool isListening() const { return m_ok; }
    [[nodiscard]] QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/list.dat").arg(m_server.serverPort()));
    }
    [[nodiscard]] QByteArray request() const { return m_request; }

private:
    QTcpServer m_server;
    QByteArray m_body;
    QByteArray m_request;
    int m_status;
    bool m_ok = false;
};

/// Drive the event loop until the async callback fires (or we give up).
struct Capture {
    bool called = false;
    bool ok = false;
    QByteArray data;
    QString entryName;
    QString error;
};

} // namespace

class tst_HttpFileDownload : public QObject {
    Q_OBJECT

private slots:
    void get_unwrapsGzippedPayload();
    void get_passesPlainPayloadThrough();
    void get_reportsHttpError();
    void get_sendsUserAgent();
};

// ---------------------------------------------------------------------------

void tst_HttpFileDownload::get_unwrapsGzippedPayload()
{
    // The GUI path: a list mirror serving ipfilter.dat.gz must reach the caller as text.
    const QByteArray plain = "# Level 1\n1.2.3.0 - 1.2.3.255 , 0 , blocked\n";
    OneShotServer server(gzipCompress(plain));
    QVERIFY(server.isListening());

    QObject ctx;
    Capture cap;
    HttpFileDownload::Options opts;
    opts.preferredNames = {QStringLiteral("ipfilter.dat")};

    HttpFileDownload::get(&ctx, server.url(), opts,
        [&cap](bool ok, const QByteArray& d, const QString& name, const QString& err) {
            cap = {true, ok, d, name, err};
        });

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY2(cap.ok, qPrintable(cap.error));
    QCOMPARE(cap.data, plain);
}

void tst_HttpFileDownload::get_passesPlainPayloadThrough()
{
    const QByteArray plain = "# Level 1\n1.2.3.0 - 1.2.3.255 , 0 , blocked\n";
    OneShotServer server(plain);
    QVERIFY(server.isListening());

    QObject ctx;
    Capture cap;
    HttpFileDownload::get(&ctx, server.url(), {},
        [&cap](bool ok, const QByteArray& d, const QString& name, const QString& err) {
            cap = {true, ok, d, name, err};
        });

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY2(cap.ok, qPrintable(cap.error));
    QCOMPARE(cap.data, plain);
    QVERIFY(cap.entryName.isEmpty());   // nothing was unpacked
}

void tst_HttpFileDownload::get_reportsHttpError()
{
    // A 404 must surface as a failure, not as an empty-but-successful download that
    // then overwrites a good config file with nothing.
    OneShotServer server("not found", 404);
    QVERIFY(server.isListening());

    QObject ctx;
    Capture cap;
    HttpFileDownload::get(&ctx, server.url(), {},
        [&cap](bool ok, const QByteArray& d, const QString& name, const QString& err) {
            cap = {true, ok, d, name, err};
        });

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY(!cap.ok);
    QVERIFY(!cap.error.isEmpty());
}

void tst_HttpFileDownload::get_sendsUserAgent()
{
    // Only one of the four original call sites identified itself; the shared helper must
    // do it for all of them, since some mirrors reject an empty User-Agent.
    OneShotServer server("data");
    QVERIFY(server.isListening());

    QObject ctx;
    Capture cap;
    HttpFileDownload::get(&ctx, server.url(), {},
        [&cap](bool ok, const QByteArray& d, const QString& name, const QString& err) {
            cap = {true, ok, d, name, err};
        });

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY(server.request().contains("User-Agent: eMuleQt/"));
}

QTEST_MAIN(tst_HttpFileDownload)
#include "tst_HttpFileDownload.moc"
