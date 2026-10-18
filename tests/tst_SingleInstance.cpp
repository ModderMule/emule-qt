/// @file tst_SingleInstance.cpp
/// @brief One GUI per install dir and profile (issue #29).

#include "app/SingleInstance.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>
#include <memory>

using namespace eMule;

class tst_SingleInstance : public QObject {
    Q_OBJECT

private slots:
    void init();
    void sameDirsHaveOnePrimary();
    void otherConfigDirIsItsOwnInstance();
    void otherInstallDirIsItsOwnInstance();
    void spellingOfTheDirDoesNotMatter();
    void lockIsFreedWhenThePrimaryGoes();
    void forwardReachesThePrimary();
    void forwardWithoutPrimaryFails();
    void relativeFileArgsBecomeAbsolute();

private:
    std::unique_ptr<QTemporaryDir> m_app;
    std::unique_ptr<QTemporaryDir> m_config;
};

namespace {

/// The primary on its own thread: forwardToPrimary() blocks until it answers.
class PrimaryThread : public QThread {
public:
    PrimaryThread(QString appDir, QString configDir)
        : m_appDir(std::move(appDir)), m_configDir(std::move(configDir)) {}

    std::atomic_bool ready{false};
    std::atomic_bool primary{false};
    QStringList received;   // read after wait()

protected:
    void run() override
    {
        SingleInstance instance(m_appDir, m_configDir);
        primary = instance.tryBecomePrimary();
        QObject::connect(&instance, &SingleInstance::activationRequested, &instance,
                         [this](const QStringList& args) {
            received = args;
            quit();
        });
        ready = true;
        if (primary)
            exec();
    }

private:
    QString m_appDir;
    QString m_configDir;
};

} // namespace

void tst_SingleInstance::init()
{
    m_app = std::make_unique<QTemporaryDir>();
    m_config = std::make_unique<QTemporaryDir>();
    QVERIFY(m_app->isValid() && m_config->isValid());
}

void tst_SingleInstance::sameDirsHaveOnePrimary()
{
    SingleInstance first(m_app->path(), m_config->path());
    SingleInstance second(m_app->path(), m_config->path());
    QVERIFY(first.tryBecomePrimary());
    QVERIFY(!second.tryBecomePrimary());
}

void tst_SingleInstance::otherConfigDirIsItsOwnInstance()
{
    QTemporaryDir other;
    SingleInstance first(m_app->path(), m_config->path());
    SingleInstance second(m_app->path(), other.path());
    QVERIFY(first.tryBecomePrimary());
    QVERIFY(second.tryBecomePrimary());
    QVERIFY(first.serverName() != second.serverName());
}

void tst_SingleInstance::otherInstallDirIsItsOwnInstance()
{
    QTemporaryDir other;
    SingleInstance first(m_app->path(), m_config->path());
    SingleInstance second(other.path(), m_config->path());
    QVERIFY(first.tryBecomePrimary());
    QVERIFY(second.tryBecomePrimary());
}

void tst_SingleInstance::spellingOfTheDirDoesNotMatter()
{
    QVERIFY(QDir(m_config->path()).mkdir(QStringLiteral("sub")));
    const QString roundabout = m_config->path() + QStringLiteral("/sub/../");
    SingleInstance first(m_app->path(), m_config->path());
    SingleInstance second(m_app->path() + QLatin1Char('/'), roundabout);
    QCOMPARE(first.serverName(), second.serverName());
}

void tst_SingleInstance::lockIsFreedWhenThePrimaryGoes()
{
    {
        SingleInstance first(m_app->path(), m_config->path());
        QVERIFY(first.tryBecomePrimary());
    }
    SingleInstance second(m_app->path(), m_config->path());
    QVERIFY(second.tryBecomePrimary());
}

void tst_SingleInstance::forwardReachesThePrimary()
{
    PrimaryThread thread(m_app->path(), m_config->path());
    thread.start();
    QTRY_VERIFY(thread.ready);
    QVERIFY(thread.primary);

    SingleInstance second(m_app->path(), m_config->path());
    QVERIFY(!second.tryBecomePrimary());
    const QStringList args{QStringLiteral("ed2k://|file|a.bin|1|00000000000000000000000000000000|/"),
                           QStringLiteral("/tmp/x.nzb")};
    QVERIFY(second.forwardToPrimary(args));
    QVERIFY(thread.wait(5000));
    QCOMPARE(thread.received, args);
}

void tst_SingleInstance::forwardWithoutPrimaryFails()
{
    SingleInstance lonely(m_app->path(), m_config->path());
    QVERIFY(!lonely.forwardToPrimary({}, 300));
}

void tst_SingleInstance::relativeFileArgsBecomeAbsolute()
{
    const QString oldCwd = QDir::currentPath();
    QVERIFY(QDir::setCurrent(m_config->path()));
    QFile file(QStringLiteral("x.nzb"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    const QString link = QStringLiteral("ed2k://|server|1.2.3.4|4661|/");
    const QStringList out = SingleInstance::absoluteArgs({QStringLiteral("x.nzb"), link});
    QDir::setCurrent(oldCwd);

    QCOMPARE(out.size(), 2);
    QCOMPARE(QFileInfo(out[0]).canonicalFilePath(),
             QFileInfo(m_config->path() + QStringLiteral("/x.nzb")).canonicalFilePath());
    QCOMPARE(out[1], link);
}

QTEST_GUILESS_MAIN(tst_SingleInstance)
#include "tst_SingleInstance.moc"
