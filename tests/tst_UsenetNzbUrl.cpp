/// @file tst_UsenetNzbUrl.cpp
/// @brief Fetching an .nzb from a pasted URL: what is refused, and what it is called.
///
/// The daemon fetches, so this is where an operator-supplied URL turns into a
/// request the daemon makes on its own network. Two things therefore get their
/// own cases rather than being left to review:
///
///   - the scheme allow-list, because QNetworkAccessManager speaks file: and
///     qrc: perfectly well and would read the daemon's own disk;
///   - the deliberate *absence* of a private-address block, because the
///     commonest real deployment is a self-hosted indexer on the daemon's LAN
///     and "hardening" that away would break the main use case silently.
///
/// Naming is pure, so it is tested directly. Fetching needs a server, and the
/// one in tst_HttpFileDownload is the right shape — an arbitrary body under an
/// arbitrary path is exactly what an NZB link is.

#include "TestHelpers.h"
#include "nzb/NzbUrlFetch.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <zlib.h>

using namespace eMule::usenet;

namespace {

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

/// One-shot HTTP/1.1 server answering every request with the same body, under a
/// path the caller chooses — the path is what the display name comes from.
class OneShotServer : public QObject {
public:
    explicit OneShotServer(QByteArray body, QString path = QStringLiteral("/release.nzb"),
                           int status = 200)
        : m_body(std::move(body)), m_path(std::move(path)), m_status(status)
    {
        m_ok = m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            auto* sock = m_server.nextPendingConnection();
            QObject::connect(sock, &QTcpSocket::readyRead, sock, [this, sock]() {
                m_request += sock->readAll();
                if (!m_request.contains("\r\n\r\n"))
                    return;

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
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                        .arg(m_server.serverPort()).arg(m_path));
    }

private:
    QTcpServer m_server;
    QByteArray m_body;
    QString m_path;
    QByteArray m_request;
    int m_status;
    bool m_ok = false;
};

QByteArray tinyNzb()
{
    return QByteArrayLiteral(
        "<?xml version=\"1.0\"?>\n"
        "<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n"
        "  <file poster=\"t\" date=\"1700000000\" subject=\"&quot;a.bin&quot; yEnc (1/1)\">\n"
        "    <groups><group>alt.binaries.test</group></groups>\n"
        "    <segments><segment bytes=\"10\" number=\"1\">m1@x</segment></segments>\n"
        "  </file>\n</nzb>\n");
}

} // namespace

class tst_UsenetNzbUrl : public QObject {
    Q_OBJECT

private slots:
    void rejectReason_refusesFileScheme();
    void rejectReason_refusesQrcFtpAndData();
    void rejectReason_refusesHostlessAndRelative();
    void rejectReason_acceptsAPrivateAddress();
    void nameFromUrl_stripsNzbThenGz();
    void nameFromUrl_decodesPercentEscapes();
    void nameFromUrl_isEmptyForApiStyleUrls();
    void fetch_returnsBodyForPlainNzb();
    void fetch_gunzipsCompressedNzb();
    void fetch_reportsHttpErrorAsAFetchFailure();
    void fetch_neverCallsBackAfterContextDies();
};

void tst_UsenetNzbUrl::rejectReason_refusesFileScheme()
{
    // The one that matters. Without the allow-list this asks the daemon to read
    // its own disk and hand the bytes back over IPC.
    QVERIFY(!NzbUrlFetch::rejectReason(QUrl(QStringLiteral("file:///etc/passwd"))).isEmpty());
}

void tst_UsenetNzbUrl::rejectReason_refusesQrcFtpAndData()
{
    for (const char* raw : {"qrc:/x.nzb", "ftp://example.org/x.nzb",
                            "data:text/xml,<nzb/>"}) {
        const QUrl url(QString::fromLatin1(raw), QUrl::StrictMode);
        QVERIFY2(!NzbUrlFetch::rejectReason(url).isEmpty(), raw);
    }
}

void tst_UsenetNzbUrl::rejectReason_refusesHostlessAndRelative()
{
    QVERIFY(!NzbUrlFetch::rejectReason(QUrl(QStringLiteral("not a url"))).isEmpty());
    QVERIFY(!NzbUrlFetch::rejectReason(QUrl(QStringLiteral("http:///x.nzb"))).isEmpty());
    QVERIFY(!NzbUrlFetch::rejectReason(QUrl()).isEmpty());
}

void tst_UsenetNzbUrl::rejectReason_acceptsAPrivateAddress()
{
    // Deliberate, and pinned so nobody "hardens" it away: unlike a peer-supplied
    // HTTP-cache URL, this one is the operator's own, and a self-hosted indexer
    // on the daemon's LAN is the commonest place a .nzb link comes from.
    QVERIFY(NzbUrlFetch::rejectReason(
                QUrl(QStringLiteral("http://192.168.1.10:5076/getnzb/1"))).isEmpty());
    QVERIFY(NzbUrlFetch::rejectReason(
                QUrl(QStringLiteral("https://api.example.org/api?t=get&id=1"))).isEmpty());
}

void tst_UsenetNzbUrl::nameFromUrl_stripsNzbThenGz()
{
    QCOMPARE(NzbUrlFetch::nameFromUrl(QUrl(QStringLiteral("https://x/Some.Release-GRP.nzb"))),
             QStringLiteral("Some.Release-GRP"));
    // Both suffixes: indexers serve .nzb.gz and HttpFileDownload gunzips it.
    QCOMPARE(NzbUrlFetch::nameFromUrl(QUrl(QStringLiteral("https://x/Some.Release.nzb.gz"))),
             QStringLiteral("Some.Release"));
}

void tst_UsenetNzbUrl::nameFromUrl_decodesPercentEscapes()
{
    QCOMPARE(NzbUrlFetch::nameFromUrl(QUrl(QStringLiteral("https://x/Some%20Release.nzb"))),
             QStringLiteral("Some Release"));
}

void tst_UsenetNzbUrl::nameFromUrl_isEmptyForApiStyleUrls()
{
    // Empty is the useful answer, not a failure: it lets UsenetQueue::addNzb fall
    // back to the NZB's own <meta type="name">, which is a better name than
    // anything this URL could have supplied.
    for (const char* raw : {"https://x/api?t=get&id=12345&apikey=k",
                            "https://x/getnzb/98765",
                            "https://x/12345.nzb",
                            "https://x/"}) {
        QVERIFY2(NzbUrlFetch::nameFromUrl(QUrl(QString::fromLatin1(raw))).isEmpty(), raw);
    }
}

void tst_UsenetNzbUrl::fetch_returnsBodyForPlainNzb()
{
    OneShotServer server(tinyNzb(), QStringLiteral("/My.Release.nzb"));
    QVERIFY(server.isListening());

    QObject ctx;
    NzbUrlFetch::Result got;
    bool called = false;
    NzbUrlFetch::fetch(&ctx, server.url(), [&](const NzbUrlFetch::Result& r) {
        got = r;
        called = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(called, 10000);

    QVERIFY2(got.ok(), qPrintable(got.error));
    QCOMPARE(got.data, tinyNzb());
    QCOMPARE(got.name, QStringLiteral("My.Release"));
}

void tst_UsenetNzbUrl::fetch_gunzipsCompressedNzb()
{
    OneShotServer server(gzipCompress(tinyNzb()), QStringLiteral("/My.Release.nzb.gz"));
    QVERIFY(server.isListening());

    QObject ctx;
    NzbUrlFetch::Result got;
    bool called = false;
    NzbUrlFetch::fetch(&ctx, server.url(), [&](const NzbUrlFetch::Result& r) {
        got = r;
        called = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(called, 10000);

    QVERIFY2(got.ok(), qPrintable(got.error));
    QCOMPARE(got.data, tinyNzb());
    // From the URL, never from the archive: libarchive names a raw gzip stream
    // generically, so trusting its entry name would call every .nzb.gz the same.
    QCOMPARE(got.name, QStringLiteral("My.Release"));
}

void tst_UsenetNzbUrl::fetch_reportsHttpErrorAsAFetchFailure()
{
    // A fetch failure and a parse failure are two different problems, and the
    // user fixes them differently — so this must not read as "that was not an
    // NZB", which is what a 404 body would parse as.
    OneShotServer server(QByteArrayLiteral("nope"), QStringLiteral("/gone.nzb"), 404);
    QVERIFY(server.isListening());

    QObject ctx;
    NzbUrlFetch::Result got;
    bool called = false;
    NzbUrlFetch::fetch(&ctx, server.url(), [&](const NzbUrlFetch::Result& r) {
        got = r;
        called = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(called, 10000);

    QVERIFY(!got.ok());
    QVERIFY2(got.error.contains(QStringLiteral("download"), Qt::CaseInsensitive),
             qPrintable(got.error));
}

void tst_UsenetNzbUrl::fetch_neverCallsBackAfterContextDies()
{
    // The daemon handler relies on this: the IPC client can disconnect while the
    // provider is still deciding, and a callback into a dead handler is a crash
    // rather than a wasted message.
    OneShotServer server(tinyNzb(), QStringLiteral("/late.nzb"));
    QVERIFY(server.isListening());

    auto* ctx = new QObject;
    bool called = false;
    NzbUrlFetch::fetch(ctx, server.url(), [&](const NzbUrlFetch::Result&) { called = true; });
    delete ctx;

    QTest::qWait(1500);
    QVERIFY2(!called, "the callback outlived its context");
}

QTEST_MAIN(tst_UsenetNzbUrl)

#include "tst_UsenetNzbUrl.moc"
