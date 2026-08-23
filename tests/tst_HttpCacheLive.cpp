/// @file tst_HttpCacheLive.cpp
/// @brief End-to-end test of the HTTP Cache client against real cache servers.
///
/// Proves the half of the feature that no unit test can: that the C++ client and
/// a real HTTP backend agree on the contract. It reads a part off disk, encrypts
/// it, POSTs it, then pulls the blob back down and decrypts it with the key from
/// the offer the publisher produced. If any of the encryption, the framing, the
/// JSON, or the server's Range handling is wrong, this fails.
///
/// Every case runs once per available backend, because there is more than one
/// implementation of the contract and two implementations can drift apart:
///
///   php   an already-running deployment — XAMPP's Apache in practice. Set
///         EMULE_HTTPCACHE_URL and EMULE_HTTPCACHE_KEY (in .env, or in the
///         environment). Skipped when they are unset or nothing answers there.
///
///   go    a binary this test starts itself, over a throwaway config and storage
///         directory on a free loopback port. Set HTTPCACHE_GO_CMD to it, the
///         way SERVER_TEST_CMD points tst_ServerLocalTest at eNode. Skipped when
///         it is unset or the executable is missing — but a binary that is there
///         and will not serve is a failure, not a skip.
///
/// With neither set the whole test skips, so it is inert on a machine with no
/// server. With both, `implementation` from /v1/info pins each row to the server
/// it names: two rows pointing at one backend would otherwise look green while
/// proving half of what the run claims.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "crypto/AesCbc.h"
#include "files/PartFile.h"
#include "httpcache/HttpCacheClient.h"
#include "httpcache/HttpCachePublisher.h"
#include "httpcache/HttpCacheServerProbe.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>
#include <vector>

using namespace eMule;
using namespace eMule::testing;

namespace {

/// Blocking GET, optionally ranged. Returns an empty array on any failure.
QByteArray httpGet(const QString& url, const QByteArray& range, int* statusOut = nullptr)
{
    QNetworkAccessManager nam;
    QNetworkRequest request{QUrl(url)};
    if (!range.isEmpty())
        request.setRawHeader("Range", range);

    QNetworkReply* reply = nam.get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(60'000, &loop, &QEventLoop::quit);
    loop.exec();

    if (statusOut)
        *statusOut = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    const QByteArray body = reply->readAll();
    reply->deleteLater();

    return body;
}

/// Deterministic filler, so a mismatch is reproducible.
QByteArray partPattern(qsizetype size)
{
    QByteArray out(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
        out[i] = static_cast<char>((i * 131 + (i >> 11) * 17) & 0xFF);
    return out;
}

/// A port nothing is listening on, released again before the server is told to
/// take it. Racy in principle; in practice the window is microseconds, and the
/// alternative is a fixed port that collides with the developer's own server.
quint16 freeLoopbackPort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;

    const quint16 port = probe.serverPort();
    probe.close();

    return port;
}

// ---------------------------------------------------------------------------
// Backends
// ---------------------------------------------------------------------------

/// A cache server this test can talk to.
///
/// The two implementations differ only in how they come to exist — one is
/// already running and one we start — so everything a case needs from either is
/// a base URL and a credential. What they share is `/v1/info`: the PHP backend
/// uses it as its availability check and the Go one as its readiness poll, which
/// is why the request lives here rather than in one of them.
class CacheBackend {
public:
    virtual ~CacheBackend() = default;

    /// Row name, and the `implementation` its `/v1/info` must report.
    virtual QString name() const = 0;

    /// Empty when this backend can be used; otherwise why it cannot, for the
    /// skip message. Called before start(), and allowed to reach the network.
    virtual QString unavailable() = 0;

    /// Bring the backend into service. The default is a backend somebody else
    /// runs, so there is nothing to do. @p why is filled only on false, and
    /// false means a fault rather than a missing prerequisite.
    virtual bool start(QString* why)
    {
        Q_UNUSED(why);
        return true;
    }

    QString baseUrl() const { return m_baseUrl; }
    QString apiKey() const { return m_apiKey; }

protected:
    /// `GET <baseUrl>/v1/info` decoded, or an empty object on any failure.
    static QJsonObject fetchInfo(const QString& baseUrl, int timeoutMs)
    {
        QNetworkAccessManager nam;
        QNetworkRequest request{QUrl(baseUrl + QStringLiteral("/v1/info"))};
        request.setTransferTimeout(timeoutMs);

        QNetworkReply* reply = nam.get(request);

        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(timeoutMs + 1'000, &loop, &QEventLoop::quit);
        loop.exec();

        // The singleShot can win the race against a hung transfer timeout, and a
        // reply deleted while still in flight is a crash rather than a failure.
        if (!reply->isFinished())
            reply->abort();

        const QJsonObject info = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();

        return info;
    }

    /// True when @p info is a cache server's answer rather than a web server's.
    static bool answersAsACache(const QJsonObject& info)
    {
        return info.value(QStringLiteral("service")).toString()
               == QStringLiteral("emule-http-cache");
    }

    QString m_baseUrl;
    QString m_apiKey;
};

/// A deployment somebody else runs. We never start or stop it, so "unavailable"
/// can only mean it did not answer — the local web server being down.
class PhpCacheBackend final : public CacheBackend {
public:
    QString name() const override { return QStringLiteral("php"); }

    QString unavailable() override
    {
        m_baseUrl = qEnvironmentVariable("EMULE_HTTPCACHE_URL").trimmed();
        m_apiKey = qEnvironmentVariable("EMULE_HTTPCACHE_KEY").trimmed();

        // Mirrors Preferences::setHttpCacheBaseUrl(), so a request path never
        // doubles a slash.
        while (m_baseUrl.endsWith(QLatin1Char('/')))
            m_baseUrl.chop(1);

        if (m_baseUrl.isEmpty() || m_apiKey.isEmpty())
            return QStringLiteral("EMULE_HTTPCACHE_URL / EMULE_HTTPCACHE_KEY are not set");

        if (!answersAsACache(fetchInfo(m_baseUrl, 3'000)))
            return QStringLiteral("%1 did not answer /v1/info — is the web server running?")
                .arg(m_baseUrl);

        return {};
    }
};

/// A binary this test starts, over a config and a storage directory that live
/// and die with the run. Nothing has to be installed or configured first: the
/// only prerequisite is the executable, which is the one thing that can skip it.
class GoCacheBackend final : public CacheBackend {
public:
    ~GoCacheBackend() override { stop(); }

    QString name() const override { return QStringLiteral("go"); }

    QString unavailable() override
    {
        const QString command = qEnvironmentVariable("HTTPCACHE_GO_CMD").trimmed();
        if (command.isEmpty())
            return QStringLiteral("HTTPCACHE_GO_CMD is not set");

        // Split like SERVER_TEST_CMD, so "go run ." works as well as a path.
        QStringList parts = QProcess::splitCommand(command);
        if (parts.isEmpty())
            return QStringLiteral("HTTPCACHE_GO_CMD is empty after parsing");

        m_executable = parts.takeFirst();
        m_extraArgs = parts;

        if (!QFileInfo(m_executable).isExecutable())
            return QStringLiteral("%1 is not an executable — build it with ./scripts/build.sh "
                                  "in the cache server checkout")
                .arg(m_executable);

        return {};
    }

    bool start(QString* why) override
    {
        if (!m_dir.isValid()) {
            *why = QStringLiteral("could not create a temporary directory for the cache server");
            return false;
        }

        const quint16 port = freeLoopbackPort();
        if (port == 0) {
            *why = QStringLiteral("could not find a free loopback port");
            return false;
        }

        const QString configPath = m_dir.filePath(QStringLiteral("config.yaml"));
        if (!writeConfig(configPath, port, why))
            return false;

        m_baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);

        m_process = new QProcess();
        m_process->setWorkingDirectory(m_dir.path());
        // Buffered rather than forwarded: a green run stays quiet, and the whole
        // buffer goes into the failure message when the server will not serve.
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        m_process->start(m_executable,
                         m_extraArgs
                             + QStringList{QStringLiteral("serve"), QStringLiteral("--config"),
                                           configPath});

        if (!m_process->waitForStarted(5'000)) {
            *why = QStringLiteral("%1 did not start: %2")
                       .arg(m_executable, m_process->errorString());
            return false;
        }

        // Poll the endpoint rather than parse the log: an answer on /v1/info is
        // the only readiness signal that means what the test needs it to mean.
        for (int attempt = 0; attempt < 40; ++attempt) {
            if (m_process->state() == QProcess::NotRunning)
                break;
            if (answersAsACache(fetchInfo(m_baseUrl, 500)))
                return true;
            QTest::qWait(500);
        }

        *why = QStringLiteral("%1 never served /v1/info on %2. Its output was:\n%3")
                   .arg(m_executable, m_baseUrl,
                        QString::fromLocal8Bit(m_process->readAllStandardOutput()));
        return false;
    }

    void stop()
    {
        if (!m_process)
            return;

        // terminate() rather than kill(): the server drains what is in flight on
        // SIGTERM, and it must be gone before the temporary directory holding its
        // storage is removed out from under it.
        m_process->terminate();
        if (!m_process->waitForFinished(10'000))
            m_process->kill();

        delete m_process;
        m_process = nullptr;
    }

private:
    bool writeConfig(const QString& path, quint16 port, QString* why);

    QString m_executable;
    QStringList m_extraArgs;
    QTemporaryDir m_dir;
    QProcess* m_process = nullptr;
};

bool GoCacheBackend::writeConfig(const QString& path, quint16 port, QString* why)
{
    // Generated per run and written nowhere but this throwaway config, so a
    // leaked test log cannot hand anybody a working upload credential.
    QByteArray secret(24, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(secret.data()), 6);
    m_apiKey = QString::fromLatin1(secret.toHex());

    // Writing a config file is also what marks the server installed — without
    // one every /v1 route answers 503 and serves only the install page.
    const QString config =
        QStringLiteral(
            "server:\n"
            "  addr: \"127.0.0.1:%1\"\n"
            "  mode: release\n"
            "storage:\n"
            "  data_dir: %2\n"
            "  var_dir: %3\n"
            // A cache that lives for one test run does not need the chunk on the
            // platter, and on macOS this is a full drive-cache flush per upload.
            "  fsync: false\n"
            "gc:\n"
            // Nothing here outlives the run, so a sweeper would only compete
            // with the test for the storage directory.
            "  interval: 0\n"
            "upload:\n"
            "  open_upload: false\n"
            "api_keys:\n"
            "  emuleqt-test:\n"
            "    secret: \"%4\"\n"
            "    quota_bytes_per_day: 0\n"
            "    enabled: true\n"
            "log:\n"
            "  level: warn\n"
            "  color: false\n"
            "  access_log: false\n")
            .arg(port)
            .arg(m_dir.filePath(QStringLiteral("storage")),
                 m_dir.filePath(QStringLiteral("var")), m_apiKey);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *why = QStringLiteral("could not write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(config.toUtf8());
    file.close();

    return true;
}

} // namespace

class tst_HttpCacheLive : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void probeIdentifiesTheServer_data();
    void probeIdentifiesTheServer();
    void publishFetchAndDecrypt_data();
    void publishFetchAndDecrypt();
    void serverSupportsRangeResume_data();
    void serverSupportsRangeResume();
    void clientFetchesWholePart_data();
    void clientFetchesWholePart();

private:
    void addBackendRows();
    CacheBackend* backendFor(const QString& name) const;

    QTemporaryDir m_dir;
    QString m_partPath;
    QByteArray m_plain;

    std::vector<std::unique_ptr<CacheBackend>> m_backends;

    // Carried from the publish case into the two that consume it, one entry per
    // backend: the 9.28 MB upload happens once per server, not once per case.
    QHash<QString, HttpCacheOffer> m_offers;
};

void tst_HttpCacheLive::initTestCase()
{
    loadProjectEnv();

    std::vector<std::unique_ptr<CacheBackend>> candidates;
    candidates.push_back(std::make_unique<PhpCacheBackend>());
    candidates.push_back(std::make_unique<GoCacheBackend>());

    for (auto& candidate : candidates) {
        const QString why = candidate->unavailable();
        if (!why.isEmpty()) {
            qInfo("backend \"%s\" not tested: %s", qPrintable(candidate->name()), qPrintable(why));
            continue;
        }

        // Past this line the prerequisite was met, so anything that goes wrong
        // is a fault worth failing on rather than a reason to quietly skip.
        QString failure;
        QVERIFY2(candidate->start(&failure), qPrintable(failure));

        m_backends.push_back(std::move(candidate));
    }

    if (m_backends.empty())
        QSKIP("no HTTP Cache backend available — see this file's header for what to set");

    QVERIFY(m_dir.isValid());

    thePrefs.load(m_dir.filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_dir.path());
    thePrefs.setHttpCacheEnabled(true);
    thePrefs.setHttpCacheAllowDownload(true);

    // One full part, exactly as the manager would hand to the publisher.
    m_plain = partPattern(static_cast<qsizetype>(PARTSIZE));
    m_partPath = m_dir.filePath(QStringLiteral("fake.part"));

    QFile file(m_partPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(m_plain), static_cast<qint64>(m_plain.size()));
    file.close();
}

void tst_HttpCacheLive::cleanupTestCase()
{
    // Explicit rather than left to the fixture's destruction order: a server we
    // started has to be gone before its temporary directory goes with it.
    m_backends.clear();
}

void tst_HttpCacheLive::probeIdentifiesTheServer_data()
{
    addBackendRows();
}

void tst_HttpCacheLive::probeIdentifiesTheServer()
{
    QFETCH(QString, backend);
    CacheBackend* server = backendFor(backend);
    QVERIFY(server);

    // The real handshake, not a hand-rolled GET: this is the code that decides
    // whether an ed2k://|httpcache| link may configure the client at all, and
    // every other test of it runs against a fake.
    HttpCacheServerInfo info;
    bool done = false;

    HttpCacheServerProbe::probe(server->baseUrl(), this,
                                [&info, &done](const HttpCacheServerInfo& result) {
        info = result;
        done = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(done, 30'000);

    QVERIFY2(info.ok, qPrintable(info.error));
    QCOMPARE(info.service, QStringLiteral("emule-http-cache"));
    QCOMPARE(info.version, HttpCacheServerProbe::kSupportedVersion);

    // A server that cannot take a padded part is no use to the publisher.
    QVERIFY2(info.maxChunkSize >= AesCbcEncryptor::cipherLengthFor(PARTSIZE),
             qPrintable(QStringLiteral("maxChunkSize is %1").arg(info.maxChunkSize)));

    // What pins this row to the server it names. Without it, an environment
    // pointing both rows at one backend runs everything twice against the same
    // implementation and reports it as coverage of two.
    QCOMPARE(info.implementation, backend);
}

void tst_HttpCacheLive::publishFetchAndDecrypt_data()
{
    addBackendRows();
}

void tst_HttpCacheLive::publishFetchAndDecrypt()
{
    QFETCH(QString, backend);
    CacheBackend* server = backendFor(backend);
    QVERIFY(server);

    HttpCachePublisher::Request request;
    request.baseUrl = server->baseUrl();
    request.apiKey = server->apiKey();
    request.ttlSeconds = 600;
    request.rateBytesPerSecond = 0;   // unthrottled; the throttle is covered elsewhere
    request.fileHash = {0xAA, 0xBB, 0xCC, 0xDD, 0x01, 0x02, 0x03, 0x04,
                        0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
    request.partIndex = 3;
    request.dataFilePath = m_partPath;
    request.partOffset = 0;
    request.partLength = PARTSIZE;

    auto* publisher = new HttpCachePublisher(this);
    QSignalSpy spy(publisher, &HttpCachePublisher::finished);

    publisher->start(request);
    QVERIFY2(spy.wait(120'000), "publisher never finished");
    QCOMPARE(spy.count(), 1);

    const auto result = spy.at(0).at(0).value<HttpCachePublishResult>();
    const auto offer = spy.at(0).at(1).value<HttpCacheOffer>();

    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!result.url.isEmpty());
    QVERIFY2(offer.isWellFormed(), qPrintable(offer.malformedReason()));

    // The offer must describe the part we actually published.
    QCOMPARE(offer.partIndex, request.partIndex);
    QCOMPARE(offer.plainLength, PARTSIZE);
    QCOMPARE(offer.cipherLength, AesCbcEncryptor::cipherLengthFor(PARTSIZE));
    QCOMPARE(offer.key.size(), qsizetype{kAesKeySize});
    QCOMPARE(offer.iv.size(), qsizetype{kAesIvSize});

    // -- Now be the downloader ------------------------------------------------

    int status = 0;
    const QByteArray cipher = httpGet(offer.url, {}, &status);
    QCOMPARE(status, 200);
    QCOMPARE(static_cast<uint64>(cipher.size()), offer.cipherLength);

    // The digest in the offer is what lets a downloader tell "the blob was
    // mangled" from "the uploader published rubbish".
    QCOMPARE(QCryptographicHash::hash(cipher, QCryptographicHash::Sha256), offer.cipherSha256);

    bool ok = false;
    const QByteArray plain = aesDecrypt(cipher, offer.key, offer.iv, &ok);
    QVERIFY2(ok, "ciphertext did not decrypt with the key from the offer");
    QCOMPARE(plain.size(), m_plain.size());
    QCOMPARE(plain, m_plain);

    // The server must never have been in a position to do that itself.
    QVERIFY(!cipher.contains(m_plain.left(4096)));

    m_offers.insert(backend, offer);
}

void tst_HttpCacheLive::serverSupportsRangeResume_data()
{
    addBackendRows();
}

void tst_HttpCacheLive::serverSupportsRangeResume()
{
    QFETCH(QString, backend);

    const HttpCacheOffer offer = m_offers.value(backend);
    if (offer.url.isEmpty())
        QSKIP("publish step did not run");

    // A downloader that drops mid-chunk resumes from the previous block boundary
    // and seeds the CBC chaining value from it. Without a working 206 that whole
    // path is dead, so assert the server does it and that the maths works.
    const uint64 resumeFrom = 4'096'000;   // block-aligned
    QCOMPARE(resumeFrom % kAesBlockSize, UINT64_C(0));

    int status = 0;
    const QByteArray tail =
        httpGet(offer.url,
                "bytes=" + QByteArray::number(static_cast<qlonglong>(resumeFrom - kAesBlockSize))
                    + "-",
                &status);

    QCOMPARE(status, 206);
    QCOMPARE(static_cast<uint64>(tail.size()), offer.cipherLength - resumeFrom + kAesBlockSize);

    AesCbcDecryptor dec;
    QVERIFY(dec.beginAt(offer.key, tail.left(kAesBlockSize), true));

    QByteArray plainTail = dec.update(tail.mid(kAesBlockSize));
    bool ok = false;
    plainTail.append(dec.finish(&ok));

    QVERIFY2(ok, "resumed decryption failed its padding check");
    QCOMPARE(plainTail, m_plain.mid(static_cast<qsizetype>(resumeFrom)));
}

void tst_HttpCacheLive::clientFetchesWholePart_data()
{
    addBackendRows();
}

void tst_HttpCacheLive::clientFetchesWholePart()
{
    QFETCH(QString, backend);

    HttpCacheOffer offer = m_offers.value(backend);
    if (offer.url.isEmpty())
        QSKIP("publish step did not run");

    // The two cases above act as the downloader by hand. This one runs the real
    // HttpCacheClient — its socket, its staged block intake, its decryptor and
    // its writes into a real PartFile — against a real origin, which is the only
    // way to prove the whole downloader path end to end.
    //
    // One temp directory per backend: every row builds its part file from the
    // same file hash, so a shared one would have the second row resuming into
    // the first row's finished part.
    const QString tempDir = m_dir.filePath(QStringLiteral("temp-") + backend);
    QDir().mkpath(tempDir);

    // Retarget the offer at part 0 so the part file need only be one part long;
    // the blob on the server is the same bytes either way.
    offer.partIndex = 0;
    QVERIFY2(offer.isWellFormed(), qPrintable(offer.malformedReason()));

    // A shade longer than the part, so the gap list never empties and the fetch
    // does not trip PartFile's completion path mid-test.
    PartFile file;
    file.setFileSize(PARTSIZE + 100);
    file.setFileHash(offer.fileHash.data());
    file.setTmpPath(tempDir);
    QVERIFY(file.createPartFile(tempDir));

    auto* client = new HttpCacheClient();
    QSignalSpy spy(client, &HttpCacheClient::fetchFinished);

    std::array<uint8, 16> peerHash{};
    peerHash.fill(0x7E);

    // No live peer behind this fetch; the address only matters if the part later
    // fails MD4, which a real round trip through the real server does not.
    QVERIFY(client->beginFetch(offer, &file, peerHash, Address::fromHostOrder(0x7E7E7E01)));
    QVERIFY2(spy.wait(180'000), "fetch never finished");

    QCOMPARE(spy.first().at(1).value<HttpCacheResult>(), HttpCacheResult::Ok);
    QCOMPARE(client->plainWritten(), PARTSIZE);
    QCOMPARE(client->cipherConsumed(), offer.cipherLength);

    file.flushBuffer();

    QFile part(file.dataFilePath());
    QVERIFY(part.open(QIODevice::ReadOnly));
    QCOMPARE(part.read(static_cast<qint64>(PARTSIZE)), m_plain);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

/// One row per backend that survived initTestCase. Shared by every _data() so
/// the row set cannot drift between cases.
void tst_HttpCacheLive::addBackendRows()
{
    QTest::addColumn<QString>("backend");

    for (const auto& backend : m_backends)
        QTest::newRow(qPrintable(backend->name())) << backend->name();
}

CacheBackend* tst_HttpCacheLive::backendFor(const QString& name) const
{
    for (const auto& backend : m_backends) {
        if (backend->name() == name)
            return backend.get();
    }

    return nullptr;
}

QTEST_MAIN(tst_HttpCacheLive)
#include "tst_HttpCacheLive.moc"
