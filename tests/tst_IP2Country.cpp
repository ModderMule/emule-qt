/// @file tst_IP2Country.cpp
/// @brief GeoLite2 country lookup, the MaxMind downloader and the hostname resolver.
///
/// Uses MaxMind's public GeoIP2-Country-Test.mmdb (MaxMind-DB repo, test-data/);
/// expected codes come from its source JSON. Never touches the network.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "geo/GeoIpUpdater.h"
#include "geo/HostCountryResolver.h"
#include "geo/IP2Country.h"
#include "prefs/Preferences.h"

#include <archive.h>
#include <archive_entry.h>

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>

using namespace eMule;
using namespace eMule::testing;

namespace {

QString testDb()
{
    return testDataDir() + QStringLiteral("/GeoIP2-Country-Test.mmdb");
}

/// A GeoLite2-style tarball: <dir>/GeoLite2-Country.mmdb plus a licence file.
QByteArray makeTarGz(const QByteArray& mmdb)
{
    QByteArray out(static_cast<qsizetype>(mmdb.size() + 64 * 1024), '\0');
    size_t used = 0;
    archive* a = archive_write_new();
    archive_write_add_filter_gzip(a);
    archive_write_set_format_pax_restricted(a);
    archive_write_open_memory(a, out.data(), static_cast<size_t>(out.size()), &used);

    auto add = [a](const char* name, const QByteArray& data) {
        archive_entry* e = archive_entry_new();
        archive_entry_set_pathname(e, name);
        archive_entry_set_size(e, data.size());
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_write_header(a, e);
        archive_write_data(a, data.constData(), static_cast<size_t>(data.size()));
        archive_entry_free(e);
    };
    add("GeoLite2-Country_20260922/LICENSE.txt", QByteArray("licence text"));
    add("GeoLite2-Country_20260922/GeoLite2-Country.mmdb", mmdb);
    archive_write_close(a);
    archive_write_free(a);
    out.truncate(static_cast<qsizetype>(used));
    return out;
}

/// The test database with its metadata build_epoch moved by @p delta seconds.
/// Its encoding is uint64 of 4 bytes: control 0x04, extended type 0x02, then big-endian.
QByteArray shiftedBuildEpoch(qint64 delta)
{
    QFile f(testDb());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QByteArray d = f.readAll();
    const qsizetype at = d.lastIndexOf("build_epoch") + 11;
    if (at < 11 || d.at(at) != 0x04 || d.at(at + 1) != 0x02)
        return {};
    quint32 epoch = 0;
    for (int i = 0; i < 4; ++i)
        epoch = (epoch << 8) | static_cast<quint8>(d.at(at + 2 + i));
    epoch = static_cast<quint32>(epoch + delta);
    for (int i = 3; i >= 0; --i, epoch >>= 8)
        d[at + 2 + i] = static_cast<char>(epoch & 0xFF);
    return d;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/// One-shot HTTP responder that records the request headers.
class FakeMaxMind : public QObject {
public:
    int status = 200;
    QByteArray body;
    QByteArray location;      ///< sent with a 3xx status
    QByteArray lastRequest;
    int requests = 0;

    explicit FakeMaxMind(const QHostAddress& bind = QHostAddress::LocalHost)
    {
        m_server.listen(bind);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = m_server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    m_buffer[s] += s->readAll();
                    if (!m_buffer[s].contains("\r\n\r\n"))
                        return;
                    lastRequest = m_buffer.take(s);
                    ++requests;
                    const QByteArray reason = status == 200 ? "OK" : status == 304 ? "Not Modified"
                                            : status == 302 ? "Found" : "Unauthorized";
                    const QByteArray payload = status == 200 ? body : QByteArray();
                    const QByteArray extra = location.isEmpty() ? QByteArray()
                                                                : "Location: " + location + "\r\n";
                    s->write("HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n"
                             + extra +
                             "Content-Type: application/gzip\r\n"
                             "Content-Length: " + QByteArray::number(payload.size()) + "\r\n"
                             "Connection: close\r\n\r\n" + payload);
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }

    [[nodiscard]] QUrl url(const QString& host = QStringLiteral("127.0.0.1")) const
    {
        return QUrl(QStringLiteral("http://%1:%2/geoip/download").arg(host).arg(m_server.serverPort()));
    }

    [[nodiscard]] QByteArray header(const QByteArray& name) const
    {
        for (const QByteArray& line : lastRequest.split('\n'))
            if (line.toLower().startsWith(QByteArray(name.toLower() + ":")))
                return line.mid(name.size() + 1).trimmed();
        return {};
    }

private:
    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffer;
};

QStringList g_log;

void captureLog(QtMsgType, const QMessageLogContext&, const QString& msg)
{
    g_log.append(msg);
}

bool runUpdate(GeoIpUpdater& updater, bool& ok, QString& message)
{
    bool done = false;
    updater.updateNow([&](bool success, const QString& msg) {
        done = true;
        ok = success;
        message = msg;
    });
    return QTest::qWaitFor([&] { return done; }, 10000);
}

} // namespace

class tst_IP2Country : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void lookup_knownRanges();
    void lookup_entryWithoutCountryIsEmpty();
    void lookup_lanAndNullAreEmpty();
    void lookup_noDatabase();
    void open_invalidFileKeepsOldDatabase();
    void isValidDatabase_rejectsGarbage();
    void reload_duringLookupsIsSafe();

    void updater_downloadsAndInstalls();
    void updater_notModifiedKeepsDatabase();
    void updater_redirectDropsCredentialsOffHost();
    void updater_rejectedCredentials();
    void updater_noCredentialsNoRequest();

    void bundle_adoptedWhenLiveMissingOrInvalid();
    void bundle_adoptedWhenNewer();
    void bundle_olderOrEqualIgnored();
    void bundle_invalidIgnored();
    void bundle_sameDirIsNoop();

    void resolver_literalAddresses();

private:
    [[nodiscard]] QString livePath() const
    {
        return m_dir.filePath(QString::fromLatin1(kGeoIpDatabaseFilename));
    }
    [[nodiscard]] QString bundlePath() const
    {
        return m_bundle.filePath(QString::fromLatin1(kGeoIpDatabaseFilename));
    }

    QTemporaryDir m_dir;
    QTemporaryDir m_bundle;
    IP2Country m_db;
};

void tst_IP2Country::init()
{
    QVERIFY(m_dir.isValid());
    theApp.ip2Country = &m_db;
    thePrefs.setGeoIpAccountId(QStringLiteral("123456"));
    thePrefs.setGeoIpLicenseKey(QStringLiteral("SeCrEtLiCeNsEkEy"));
    thePrefs.setGeoIpAutoUpdate(false);
    thePrefs.setGeoIpLastCheck(0);
}

void tst_IP2Country::cleanup()
{
    theApp.ip2Country = nullptr;
    m_db.close();
    QFile::remove(livePath());
    QFile::remove(bundlePath());
}

void tst_IP2Country::lookup_knownRanges()
{
    QVERIFY(m_db.open(testDb()));
    QVERIFY(m_db.isLoaded());
    QVERIFY(m_db.buildDate().isValid());

    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("81.2.69.160"))), QStringLiteral("GB"));
    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("89.160.20.130"))), QStringLiteral("SE"));
    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("216.160.83.58"))), QStringLiteral("US"));
    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("2001:218::1"))), QStringLiteral("JP"));
    // A v4-mapped v6 address is the same host
    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("::ffff:89.160.20.130"))),
             QStringLiteral("SE"));
    // Address overload and the theApp helper agree
    QCOMPARE(m_db.countryCode(Address::fromString(QStringLiteral("81.2.69.160"))),
             QStringLiteral("GB"));
    QCOMPARE(countryCodeOf(Address::fromString(QStringLiteral("2001:218::1"))), QStringLiteral("JP"));
    // Cached answer is the same answer
    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("81.2.69.160"))), QStringLiteral("GB"));
}

void tst_IP2Country::lookup_entryWithoutCountryIsEmpty()
{
    QVERIFY(m_db.open(testDb()));
    // Found in the tree but carrying neither country nor registered_country
    // (anycast / continent-only records in the test source data)
    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("214.1.1.7"))), QString());
    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("2a02:d500::7"))), QString());
    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("214.78.120.5"))), QStringLiteral("US"));
}

void tst_IP2Country::lookup_lanAndNullAreEmpty()
{
    QVERIFY(m_db.open(testDb()));
    QVERIFY(m_db.countryCode(QHostAddress(QStringLiteral("192.168.17.4"))).isEmpty());
    QVERIFY(m_db.countryCode(QHostAddress(QStringLiteral("10.3.2.1"))).isEmpty());
    QVERIFY(m_db.countryCode(QHostAddress(QStringLiteral("127.0.0.1"))).isEmpty());
    QVERIFY(m_db.countryCode(QHostAddress(QStringLiteral("0.0.0.0"))).isEmpty());
    QVERIFY(m_db.countryCode(QHostAddress()).isEmpty());
    QVERIFY(m_db.countryCode(Address()).isEmpty());
    // Public but absent from the test database
    QVERIFY(m_db.countryCode(QHostAddress(QStringLiteral("93.184.216.34"))).isEmpty());
}

void tst_IP2Country::lookup_noDatabase()
{
    QVERIFY(!m_db.isLoaded());
    QVERIFY(m_db.countryCode(QHostAddress(QStringLiteral("81.2.69.160"))).isEmpty());
    QVERIFY(!m_db.buildDate().isValid());

    QString error;
    QVERIFY(!m_db.open(m_dir.filePath(QStringLiteral("missing.mmdb")), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!m_db.isLoaded());

    theApp.ip2Country = nullptr;
    QVERIFY(countryCodeOf(QHostAddress(QStringLiteral("81.2.69.160"))).isEmpty());
}

void tst_IP2Country::open_invalidFileKeepsOldDatabase()
{
    QVERIFY(m_db.open(testDb()));
    const QString junk = m_dir.filePath(QStringLiteral("junk.mmdb"));
    QFile f(junk);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(4096, 'x'));
    f.close();

    QVERIFY(!m_db.open(junk));
    QCOMPARE(m_db.path(), testDb());
    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("81.2.69.160"))), QStringLiteral("GB"));
}

void tst_IP2Country::isValidDatabase_rejectsGarbage()
{
    QVERIFY(IP2Country::isValidDatabase(testDb()));
    QString error;
    QVERIFY(!IP2Country::isValidDatabase(m_dir.filePath(QStringLiteral("nope.mmdb")), &error));
    QVERIFY(!error.isEmpty());
}

void tst_IP2Country::reload_duringLookupsIsSafe()
{
    QVERIFY(m_db.open(testDb()));
    std::atomic<bool> stop{false};
    std::atomic<int> wrong{0};
    QThread* reader = QThread::create([&] {
        while (!stop) {
            const QString cc = m_db.countryCode(QHostAddress(QStringLiteral("89.160.20.130")));
            // Empty only in the instant between close() and open()
            if (!cc.isEmpty() && cc != u"SE")
                ++wrong;
        }
    });
    reader->start();
    for (int i = 0; i < 200; ++i) {
        if (i % 7 == 0)
            m_db.close();
        QVERIFY(m_db.open(testDb()));
    }
    stop = true;
    reader->wait();
    delete reader;
    QCOMPARE(wrong.load(), 0);
}

void tst_IP2Country::updater_downloadsAndInstalls()
{
    QFile src(testDb());
    QVERIFY(src.open(QIODevice::ReadOnly));
    FakeMaxMind server;
    server.body = makeTarGz(src.readAll());

    GeoIpUpdater updater(&m_db, m_dir.path());
    updater.setDownloadUrl(server.url());
    QSignalSpy changed(&updater, &GeoIpUpdater::databaseChanged);

    g_log.clear();
    const auto previous = qInstallMessageHandler(captureLog);
    bool ok = false;
    QString message;
    const bool finished = runUpdate(updater, ok, message);
    qInstallMessageHandler(previous);

    QVERIFY(finished);
    QVERIFY2(ok, qPrintable(message));
    QCOMPARE(changed.count(), 1);
    QVERIFY(QFileInfo::exists(updater.databasePath()));
    QVERIFY(!QFileInfo::exists(updater.databasePath() + QStringLiteral(".new")));
    QCOMPARE(m_db.path(), updater.databasePath());
    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("81.2.69.160"))), QStringLiteral("GB"));
    QVERIFY(thePrefs.geoIpLastCheck() > 0);

    // Basic auth, first download unconditional
    QCOMPARE(server.header("Authorization"),
             QByteArray("Basic " + QByteArray("123456:SeCrEtLiCeNsEkEy").toBase64()));
    QVERIFY(server.header("If-Modified-Since").isEmpty());

    // The key never reaches a log line
    for (const QString& line : std::as_const(g_log))
        QVERIFY2(!line.contains(QStringLiteral("SeCrEtLiCeNsEkEy")), qPrintable(line));
}

void tst_IP2Country::updater_notModifiedKeepsDatabase()
{
    QVERIFY(QFile::copy(testDb(), m_dir.filePath(QString::fromLatin1(kGeoIpDatabaseFilename))));
    FakeMaxMind server;
    server.status = 304;

    GeoIpUpdater updater(&m_db, m_dir.path());
    updater.setDownloadUrl(server.url());
    updater.setBundleDir(QString());   // a fetched data/config copy must not interfere
    updater.start();   // opens the existing file; auto-update is off
    QVERIFY(m_db.isLoaded());
    QSignalSpy changed(&updater, &GeoIpUpdater::databaseChanged);

    bool ok = false;
    QString message;
    QVERIFY(runUpdate(updater, ok, message));
    QVERIFY2(ok, qPrintable(message));
    QCOMPARE(changed.count(), 0);
    QVERIFY(!server.header("If-Modified-Since").isEmpty());
    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("2001:218::1"))), QStringLiteral("JP"));
    QCOMPARE(server.requests, 1);   // start() alone didn't download: auto-update off
}

void tst_IP2Country::updater_redirectDropsCredentialsOffHost()
{
    // MaxMind answers with a 302 to a presigned storage URL that rejects any
    // Authorization header — so the second hop must go without one.
    QFile src(testDb());
    QVERIFY(src.open(QIODevice::ReadOnly));
    FakeMaxMind storage(QHostAddress::Any);
    storage.body = makeTarGz(src.readAll());
    FakeMaxMind maxmind;
    maxmind.status = 302;
    maxmind.location = storage.url(QStringLiteral("localhost")).toEncoded() + "?X-Amz-Signature=abc";

    GeoIpUpdater updater(&m_db, m_dir.path());
    updater.setDownloadUrl(maxmind.url());
    bool ok = false;
    QString message;
    QVERIFY(runUpdate(updater, ok, message));
    QVERIFY2(ok, qPrintable(message));
    QCOMPARE(maxmind.requests, 1);
    QCOMPARE(storage.requests, 1);
    QVERIFY(!maxmind.header("Authorization").isEmpty());
    QVERIFY(storage.header("Authorization").isEmpty());
    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("81.2.69.160"))), QStringLiteral("GB"));
}

void tst_IP2Country::updater_rejectedCredentials()
{
    FakeMaxMind server;
    server.status = 401;

    GeoIpUpdater updater(&m_db, m_dir.path());
    updater.setDownloadUrl(server.url());
    bool ok = true;
    QString message;
    QVERIFY(runUpdate(updater, ok, message));
    QVERIFY(!ok);
    QVERIFY2(message.contains(QStringLiteral("401")), qPrintable(message));
    QVERIFY(!m_db.isLoaded());
    QVERIFY(!QFileInfo::exists(updater.databasePath()));
}

void tst_IP2Country::updater_noCredentialsNoRequest()
{
    thePrefs.setGeoIpLicenseKey(QString());
    FakeMaxMind server;
    GeoIpUpdater updater(&m_db, m_dir.path());
    updater.setDownloadUrl(server.url());

    bool ok = true;
    QString message;
    QVERIFY(runUpdate(updater, ok, message));
    QVERIFY(!ok);
    QCOMPARE(server.requests, 0);

    // The schedule stays quiet too, even with auto-update on and no database
    thePrefs.setGeoIpAutoUpdate(true);
    updater.checkSchedule();
    QTest::qWait(50);
    QVERIFY(!updater.isRunning());
    QCOMPARE(server.requests, 0);
}

void tst_IP2Country::bundle_adoptedWhenLiveMissingOrInvalid()
{
    QVERIFY(QFile::copy(testDb(), bundlePath()));
    {
        GeoIpUpdater updater(&m_db, m_dir.path());
        updater.setBundleDir(m_bundle.path());
        updater.start();
        QVERIFY(m_db.isLoaded());
        QCOMPARE(readFile(livePath()), readFile(testDb()));
    }
    m_db.close();

    QVERIFY(writeFile(livePath(), QByteArray("truncated download")));
    GeoIpUpdater updater(&m_db, m_dir.path());
    updater.setBundleDir(m_bundle.path());
    updater.start();
    QVERIFY(m_db.isLoaded());
    QCOMPARE(readFile(livePath()), readFile(testDb()));
    QCOMPARE(m_db.countryCode(QHostAddress(QStringLiteral("89.160.20.130"))), QStringLiteral("SE"));
}

void tst_IP2Country::bundle_adoptedWhenNewer()
{
    const QByteArray newer = shiftedBuildEpoch(86400);
    QVERIFY(!newer.isEmpty());
    QVERIFY(QFile::copy(testDb(), livePath()));
    QVERIFY(writeFile(bundlePath(), newer));
    QCOMPARE(IP2Country::buildDateOf(bundlePath()),
             IP2Country::buildDateOf(testDb()).addSecs(86400));

    GeoIpUpdater updater(&m_db, m_dir.path());
    updater.setBundleDir(m_bundle.path());
    QSignalSpy changed(&updater, &GeoIpUpdater::databaseChanged);
    updater.start();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(readFile(livePath()), newer);
    QCOMPARE(m_db.buildDate(), IP2Country::buildDateOf(bundlePath()));
    QVERIFY(!QFileInfo::exists(livePath() + QStringLiteral(".new")));
}

void tst_IP2Country::bundle_olderOrEqualIgnored()
{
    // A download newer than the release's copy must survive the next start
    const QByteArray downloaded = shiftedBuildEpoch(86400);
    QVERIFY(writeFile(livePath(), downloaded));
    for (const QByteArray& bundled : {readFile(testDb()), downloaded}) {
        QVERIFY(writeFile(bundlePath(), bundled));
        GeoIpUpdater updater(&m_db, m_dir.path());
        updater.setBundleDir(m_bundle.path());
        QSignalSpy changed(&updater, &GeoIpUpdater::databaseChanged);
        updater.start();
        QCOMPARE(changed.count(), 0);
        QCOMPARE(readFile(livePath()), downloaded);
        QVERIFY(m_db.isLoaded());
        m_db.close();
    }
}

void tst_IP2Country::bundle_invalidIgnored()
{
    QVERIFY(QFile::copy(testDb(), livePath()));
    QVERIFY(writeFile(bundlePath(), QByteArray("not a database")));
    GeoIpUpdater updater(&m_db, m_dir.path());
    updater.setBundleDir(m_bundle.path());
    updater.start();
    QVERIFY(m_db.isLoaded());
    QCOMPARE(readFile(livePath()), readFile(testDb()));
}

void tst_IP2Country::bundle_sameDirIsNoop()
{
    // Windows portable mode: config/ next to the exe is both bundle and config dir
    QVERIFY(QFile::copy(testDb(), livePath()));
    GeoIpUpdater updater(&m_db, m_dir.path());
    updater.setBundleDir(m_dir.path());
    QSignalSpy changed(&updater, &GeoIpUpdater::databaseChanged);
    updater.start();
    QCOMPARE(changed.count(), 0);
    QVERIFY(m_db.isLoaded());
}

void tst_IP2Country::resolver_literalAddresses()
{
    QVERIFY(m_db.open(testDb()));
    QHash<QString, QString> result;
    bool done = false;
    resolveHostCountries({QStringLiteral("81.2.69.160"), QStringLiteral(" 2001:218::1 "),
                          QStringLiteral("192.168.1.9"), QString()},
                         this, [&](const QHash<QString, QString>& r) {
                             result = r;
                             done = true;
                         });
    QVERIFY(QTest::qWaitFor([&] { return done; }, 2000));
    QCOMPARE(result.value(QStringLiteral("81.2.69.160")), QStringLiteral("GB"));
    QCOMPARE(result.value(QStringLiteral("2001:218::1")), QStringLiteral("JP"));
    QVERIFY(result.contains(QStringLiteral("192.168.1.9")));
    QVERIFY(result.value(QStringLiteral("192.168.1.9")).isEmpty());
}

QTEST_MAIN(tst_IP2Country)
#include "tst_IP2Country.moc"
