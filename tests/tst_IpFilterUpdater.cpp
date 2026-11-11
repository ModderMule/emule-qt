/// @file tst_IpFilterUpdater.cpp
/// @brief Tests for ipfilter/IpFilterUpdater: scheduled runs are conditional (#112),
/// manual runs always download in full.

#include "FakeHttpServer.h"
#include "TestHelpers.h"
#include "ipfilter/IPFilter.h"
#include "ipfilter/IpFilterUpdater.h"
#include "utils/BackgroundFileWriter.h"
#include "prefs/Preferences.h"

#include <QDateTime>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace eMule;
using eMule::testing::TempDir;

namespace {

const QByteArray kList = "1.2.3.0 - 1.2.3.255 , 0 , blocked\n";
const QByteArray kETag = "\"6ae623cb-2d1503\"";
const QByteArray kLastModified = "Sat, 31 Oct 2026 17:22:19 GMT";

QString validatorKey()
{
    return QString::fromLatin1(IpFilterUpdater::kValidatorKey);
}

} // namespace

class tst_IpFilterUpdater : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void scheduled_downloadsAndStoresValidators();
    void scheduled_notModifiedKeepsFile();
    void scheduled_otherUrlSendsNoValidators();
    void scheduled_missingFileSendsNoValidators();
    void scheduled_notDueSendsNothing();
    void updateNow_alwaysDownloadsInFull();

private:
    /// Answers 304 to the current ETag, else the list.
    void serveConditional();
    /// Run one scheduled check; true when it finished.
    bool runScheduled(bool& ok, QString& message);
    [[nodiscard]] QString listPath() const { return m_dir->filePath(QStringLiteral("ipfilter.dat")); }

    std::unique_ptr<TempDir> m_dir;
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<IPFilter> m_filter;
    std::unique_ptr<IpFilterUpdater> m_updater;
    QString m_url;
};

void tst_IpFilterUpdater::init()
{
    m_dir = std::make_unique<TempDir>();
    m_server = std::make_unique<FakeHttpServer>();
    QVERIFY(m_server->isListening());
    m_filter = std::make_unique<IPFilter>();
    m_updater = std::make_unique<IpFilterUpdater>(m_filter.get(), m_dir->path());

    m_url = m_server->url(QStringLiteral("/ipfilter.zip")).toString();
    thePrefs.setIpFilterUpdateUrl(m_url);
    thePrefs.setIpFilterAutoUpdate(true);
    thePrefs.setIpFilterUpdateDays(1);
    thePrefs.setIpFilterLastUpdate(0);
    thePrefs.setDownloadValidators(validatorKey(), m_url, {});
    serveConditional();
}

void tst_IpFilterUpdater::cleanup()
{
    m_updater.reset();
    m_filter.reset();
    m_server.reset();
    m_dir.reset();
    thePrefs.setIpFilterAutoUpdate(false);
}

void tst_IpFilterUpdater::scheduled_downloadsAndStoresValidators()
{
    bool ok = false;
    QString message;
    QVERIFY(runScheduled(ok, message));
    QVERIFY2(ok, qPrintable(message));

    QCOMPARE(m_server->requestCount(), 1);
    QVERIFY(m_server->header("If-None-Match").isEmpty());   // nothing on disk yet
    BackgroundFileWriter::instance().flush();
    QVERIFY(QFile::exists(listPath()));
    QCOMPARE(m_filter->entryCount(), 1);
    QVERIFY(thePrefs.ipFilterLastUpdate() > 0);

    const HttpValidators v = thePrefs.downloadValidators(validatorKey(), m_url);
    QCOMPARE(v.etag, kETag);
    QCOMPARE(v.lastModified, kLastModified);
}

void tst_IpFilterUpdater::scheduled_notModifiedKeepsFile()
{
    bool ok = false;
    QString message;
    QVERIFY(runScheduled(ok, message));
    QVERIFY2(ok, qPrintable(message));
    const QDateTime written = QFileInfo(listPath()).lastModified();

    thePrefs.setIpFilterLastUpdate(0);   // due again
    QVERIFY(runScheduled(ok, message));
    QVERIFY2(ok, qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("up to date")), qPrintable(message));

    QCOMPARE(m_server->requestCount(), 2);
    QCOMPARE(m_server->header("If-None-Match"), kETag);
    QCOMPARE(m_server->header("If-Modified-Since"), kLastModified);
    QCOMPARE(QFileInfo(listPath()).lastModified(), written);   // not rewritten
    QCOMPARE(m_filter->entryCount(), 1);
    QVERIFY(thePrefs.ipFilterLastUpdate() > 0);
}

void tst_IpFilterUpdater::scheduled_otherUrlSendsNoValidators()
{
    bool ok = false;
    QString message;
    QVERIFY(runScheduled(ok, message));
    QVERIFY2(ok, qPrintable(message));

    // Validators of one list say nothing about another
    thePrefs.setIpFilterUpdateUrl(m_server->url(QStringLiteral("/other.zip")).toString());
    thePrefs.setIpFilterLastUpdate(0);
    QVERIFY(runScheduled(ok, message));
    QVERIFY2(ok, qPrintable(message));
    QCOMPARE(m_server->requestCount(), 2);
    QVERIFY(m_server->header("If-None-Match").isEmpty());
    QVERIFY(m_server->header("If-Modified-Since").isEmpty());
}

void tst_IpFilterUpdater::scheduled_missingFileSendsNoValidators()
{
    bool ok = false;
    QString message;
    QVERIFY(runScheduled(ok, message));
    QVERIFY2(ok, qPrintable(message));

    // A 304 would leave the user with no list at all
    QVERIFY(QFile::remove(listPath()));
    thePrefs.setIpFilterLastUpdate(0);
    QVERIFY(runScheduled(ok, message));
    QVERIFY2(ok, qPrintable(message));
    QVERIFY(m_server->header("If-None-Match").isEmpty());
    BackgroundFileWriter::instance().flush();
    QVERIFY(QFile::exists(listPath()));
}

void tst_IpFilterUpdater::scheduled_notDueSendsNothing()
{
    thePrefs.setIpFilterLastUpdate(QDateTime::currentSecsSinceEpoch());
    m_updater->checkSchedule();
    QVERIFY(!m_updater->isRunning());
    QTest::qWait(50);
    QCOMPARE(m_server->requestCount(), 0);
}

void tst_IpFilterUpdater::updateNow_alwaysDownloadsInFull()
{
    bool ok = false;
    QString message;
    QVERIFY(runScheduled(ok, message));
    QVERIFY2(ok, qPrintable(message));

    bool done = false;
    m_updater->updateNow({}, [&](bool success, int entries, const QString& msg) {
        done = true;
        ok = success;
        message = msg;
        QCOMPARE(entries, 1);
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QVERIFY2(ok, qPrintable(message));
    QCOMPARE(m_server->requestCount(), 2);
    QVERIFY(m_server->header("If-None-Match").isEmpty());
    QVERIFY(m_server->header("If-Modified-Since").isEmpty());
}

// ---------------------------------------------------------------------------

void tst_IpFilterUpdater::serveConditional()
{
    m_server->handler = [](const QByteArray&, const QByteArray& request) -> FakeHttpServer::Response {
        const QList<std::pair<QByteArray, QByteArray>> headers = {
            {"ETag", kETag}, {"Last-Modified", kLastModified}};
        if (request.contains(QByteArray("If-None-Match: " + kETag)))
            return {304, {}, headers};
        return {200, kList, headers};
    };
}

bool tst_IpFilterUpdater::runScheduled(bool& ok, QString& message)
{
    QSignalSpy spy(m_updater.get(), &IpFilterUpdater::updateFinished);
    m_updater->checkSchedule();
    if (!spy.wait(10000) && spy.isEmpty())
        return false;
    ok = spy.first().at(0).toBool();
    message = spy.first().at(2).toString();
    return true;
}

QTEST_MAIN(tst_IpFilterUpdater)
#include "tst_IpFilterUpdater.moc"
