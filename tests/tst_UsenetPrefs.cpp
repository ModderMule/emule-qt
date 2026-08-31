/// @file tst_UsenetPrefs.cpp
/// @brief Persistence of the `usenet:` block, and the credential rules around it.
///
/// Provider passwords ride on the file-wide AES key that `notifications:`
/// carries. That arrangement has three failure modes which are all silent, and
/// each has a test here:
///
///   - **Ordering.** The block must be written and read *after* `notifications`,
///     because that is where the key is minted and read. Get it wrong and every
///     password loads back empty with nothing in the log.
///   - **The mint condition.** The key is only created when some secret needs
///     it. A user who configures Usenet and nothing else has no SMTP password
///     and no cache key, so unless Usenet counts, no key exists and every
///     password is dropped on the first save.
///   - **Decrypt failure.** aesDecryptFromBase64() returns an empty string both
///     for "was empty" and for "wrong key". A password that was *present* and
///     came back empty has to be reported, or the user only ever sees their
///     provider say "authentication failed".
///
/// What this deliberately does not claim: the encryption is obfuscation, not
/// secrecy. The key sits in the same file as the ciphertext. It defeats a
/// shoulder-surfer, a config pasted into a bug report and a grep across a
/// backup; it does not defeat anyone holding preferences.yml.

#include "prefs/Preferences.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;

namespace {

NewsServer makeServer(const QString& name, const QString& host, quint16 port)
{
    NewsServer s;
    s.name = name;
    s.host = host;
    s.port = port;
    s.user = QStringLiteral("user-") + name;
    s.pass = QStringLiteral("secret-") + name;
    return s;
}

QString readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // namespace

class tst_UsenetPrefs : public QObject {
    Q_OBJECT

private slots:
    void init();

    void serversRoundTrip();
    void passwordIsNotStoredInClear();
    void usenetOnlyConfiguration_mintsAnEncryptionKey();
    void plaintextPassIsAcceptedOnceThenRewritten();
    void usenetBlockIsWrittenAfterNotifications();
    void wrongKeyLeavesThePasswordEmpty();
    void invalidServersAreDropped();
    void listIsCapped();
    void defaultsAreSaneWithNoUsenetBlock();

private:
    QTemporaryDir m_dir;
    QString m_file;
};

void tst_UsenetPrefs::init()
{
    QVERIFY(m_dir.isValid());
    m_file = m_dir.filePath(QStringLiteral("preferences-%1.yml")
                                .arg(QTest::currentTestFunction()));
}

void tst_UsenetPrefs::serversRoundTrip()
{
    {
        Preferences p;
        p.setUsenetEnabled(true);
        p.setUsenetRetryIntervalSeconds(120);

        NewsServer main = makeServer(QStringLiteral("main"),
                                     QStringLiteral("news.example.com"), 563);
        main.tlsMode = NntpTlsMode::Implicit;
        main.level = 0;
        main.maxConnections = 20;
        main.retention = 3000;
        main.certVerification = NntpCertVerification::Strict;

        NewsServer block = makeServer(QStringLiteral("block"),
                                      QStringLiteral("block.example.net"), 119);
        block.tlsMode = NntpTlsMode::StartTls;
        block.level = 5;                  // sparse on purpose; the pool normalizes
        block.optional = true;
        block.joinGroup = true;
        block.group = 2;
        block.maxConnections = 4;
        block.certVerification = NntpCertVerification::Minimal;
        block.enabled = false;

        p.setUsenetServers({main, block});
        QVERIFY(p.saveTo(m_file));
    }

    Preferences p;
    QVERIFY(p.load(m_file));
    QCOMPARE(p.usenetEnabled(), true);
    QCOMPARE(p.usenetRetryIntervalSeconds(), 120);

    const auto servers = p.usenetServers();
    QCOMPARE(servers.size(), 2);

    const NewsServer& main = servers.at(0);
    QCOMPARE(main.name, QStringLiteral("main"));
    QCOMPARE(main.host, QStringLiteral("news.example.com"));
    QCOMPARE(main.port, quint16(563));
    QCOMPARE(main.tlsMode, NntpTlsMode::Implicit);
    QCOMPARE(main.user, QStringLiteral("user-main"));
    QCOMPARE(main.pass, QStringLiteral("secret-main"));
    QCOMPARE(main.level, 0);
    QCOMPARE(main.maxConnections, 20);
    QCOMPARE(main.retention, 3000);
    QCOMPARE(main.certVerification, NntpCertVerification::Strict);
    QVERIFY(main.enabled);

    // Every tier field has to survive: they are what make a block account work,
    // and a silently-defaulted `optional` turns an optional server into one that
    // can fail a download.
    const NewsServer& block = servers.at(1);
    QCOMPARE(block.tlsMode, NntpTlsMode::StartTls);
    QCOMPARE(block.pass, QStringLiteral("secret-block"));
    QCOMPARE(block.level, 5);
    QCOMPARE(block.group, 2);
    QCOMPARE(block.optional, true);
    QCOMPARE(block.joinGroup, true);
    QCOMPARE(block.maxConnections, 4);
    QCOMPARE(block.certVerification, NntpCertVerification::Minimal);
    QCOMPARE(block.enabled, false);
}

void tst_UsenetPrefs::passwordIsNotStoredInClear()
{
    Preferences p;
    p.setUsenetServers({makeServer(QStringLiteral("main"),
                                   QStringLiteral("news.example.com"), 563)});
    QVERIFY(p.saveTo(m_file));

    const QString yaml = readAll(m_file);
    QVERIFY(!yaml.isEmpty());
    QVERIFY(yaml.contains(QStringLiteral("passEnc")));
    QVERIFY(!yaml.contains(QStringLiteral("secret-main")));

    // A fresh IV per encryption, so the same password twice is two blobs.
    Preferences p2;
    NewsServer a = makeServer(QStringLiteral("a"), QStringLiteral("h1"), 563);
    NewsServer b = makeServer(QStringLiteral("b"), QStringLiteral("h2"), 563);
    b.pass = a.pass;
    p2.setUsenetServers({a, b});
    const QString file2 = m_dir.filePath(QStringLiteral("two.yml"));
    QVERIFY(p2.saveTo(file2));

    const QString yaml2 = readAll(file2);
    const auto blobs = yaml2.split(QStringLiteral("passEnc:"), Qt::SkipEmptyParts);
    QCOMPARE(blobs.size(), 3);   // text before, then one tail per blob
    QVERIFY(blobs.at(1).section(u'\n', 0, 0) != blobs.at(2).section(u'\n', 0, 0));
}

void tst_UsenetPrefs::usenetOnlyConfiguration_mintsAnEncryptionKey()
{
    // No SMTP password, no HTTP Cache key — the two secrets that used to be the
    // only reasons a key got minted.
    Preferences p;
    p.setUsenetServers({makeServer(QStringLiteral("main"),
                                   QStringLiteral("news.example.com"), 563)});
    QVERIFY(p.saveTo(m_file));

    const QString yaml = readAll(m_file);
    QVERIFY(yaml.contains(QStringLiteral("emailEncryptionKey")));

    Preferences p2;
    QVERIFY(p2.load(m_file));
    QCOMPARE(p2.usenetServers().size(), 1);
    QCOMPARE(p2.usenetServers().at(0).pass, QStringLiteral("secret-main"));
}

void tst_UsenetPrefs::plaintextPassIsAcceptedOnceThenRewritten()
{
    // preferences.yml is a file users edit by hand, so a plain `pass:` has to
    // work for first setup. It must never be written back, though.
    const QString yaml = QStringLiteral(
        "usenet:\n"
        "  enabled: true\n"
        "  servers:\n"
        "    - name: hand-edited\n"
        "      host: news.example.com\n"
        "      port: 563\n"
        "      user: someone\n"
        "      pass: typed-by-hand\n");
    {
        QFile f(m_file);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(yaml.toUtf8());
    }

    Preferences p;
    QVERIFY(p.load(m_file));
    QCOMPARE(p.usenetServers().size(), 1);
    QCOMPARE(p.usenetServers().at(0).pass, QStringLiteral("typed-by-hand"));

    QVERIFY(p.saveTo(m_file));
    const QString written = readAll(m_file);
    QVERIFY(written.contains(QStringLiteral("passEnc")));
    QVERIFY(!written.contains(QStringLiteral("typed-by-hand")));

    Preferences p2;
    QVERIFY(p2.load(m_file));
    QCOMPARE(p2.usenetServers().at(0).pass, QStringLiteral("typed-by-hand"));
}

void tst_UsenetPrefs::usenetBlockIsWrittenAfterNotifications()
{
    Preferences p;
    p.setUsenetServers({makeServer(QStringLiteral("main"),
                                   QStringLiteral("news.example.com"), 563)});
    QVERIFY(p.saveTo(m_file));

    const QString yaml = readAll(m_file);
    const int notifications = yaml.indexOf(QStringLiteral("\nnotifications:"));
    const int usenet = yaml.indexOf(QStringLiteral("\nusenet:"));
    QVERIFY(notifications >= 0);
    QVERIFY(usenet >= 0);

    // The one ordering the whole scheme rests on: the key is minted while
    // `notifications` is being written, so anything encrypted under it has to
    // come later in the document *and* later in the loader.
    QVERIFY2(usenet > notifications,
             "usenet: must be written after notifications:, which mints the key");
}

void tst_UsenetPrefs::wrongKeyLeavesThePasswordEmpty()
{
    Preferences p;
    p.setUsenetServers({makeServer(QStringLiteral("main"),
                                   QStringLiteral("news.example.com"), 563)});
    QVERIFY(p.saveTo(m_file));

    // Corrupt the key, as a half-merged config or a copied file would.
    QString yaml = readAll(m_file);
    const int keyStart = yaml.indexOf(QStringLiteral("emailEncryptionKey: "));
    QVERIFY(keyStart >= 0);
    const int valueStart = keyStart + int(qstrlen("emailEncryptionKey: "));
    const int valueEnd = yaml.indexOf(u'\n', valueStart);
    yaml.replace(valueStart, valueEnd - valueStart, QString(64, u'0'));
    {
        QFile f(m_file);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(yaml.toUtf8());
    }

    // The entry survives with an empty password rather than vanishing: the user
    // needs to see the account and be able to retype the password. The loader
    // logs the decrypt failure, which is the only signal distinguishing this
    // from "no password was ever set".
    Preferences p2;
    QVERIFY(p2.load(m_file));
    QCOMPARE(p2.usenetServers().size(), 1);
    QVERIFY(p2.usenetServers().at(0).pass.isEmpty());
    QCOMPARE(p2.usenetServers().at(0).host, QStringLiteral("news.example.com"));
}

void tst_UsenetPrefs::invalidServersAreDropped()
{
    Preferences p;
    NewsServer hostless;
    hostless.name = QStringLiteral("nope");
    hostless.port = 563;

    NewsServer portless = makeServer(QStringLiteral("noport"),
                                     QStringLiteral("news.example.com"), 0);

    p.setUsenetServers({hostless, portless,
                        makeServer(QStringLiteral("ok"),
                                   QStringLiteral("news.example.com"), 563)});

    // Sanitised on the way in, so the pool, the Options page and the IPC
    // handler all see the same list and none of them has to re-check.
    QCOMPARE(p.usenetServers().size(), 1);
    QCOMPARE(p.usenetServers().at(0).name, QStringLiteral("ok"));
}

void tst_UsenetPrefs::listIsCapped()
{
    QList<NewsServer> many;
    for (int i = 0; i < Preferences::kMaxUsenetServers + 5; ++i) {
        many.append(makeServer(QStringLiteral("s%1").arg(i),
                               QStringLiteral("news%1.example.com").arg(i), 563));
    }

    Preferences p;
    p.setUsenetServers(many);
    QCOMPARE(p.usenetServers().size(), Preferences::kMaxUsenetServers);

    // And the cap survives a round trip — a hand-edited file must not be able
    // to grow the list past it either.
    QVERIFY(p.saveTo(m_file));
    Preferences p2;
    QVERIFY(p2.load(m_file));
    QCOMPARE(p2.usenetServers().size(), Preferences::kMaxUsenetServers);
}

void tst_UsenetPrefs::defaultsAreSaneWithNoUsenetBlock()
{
    // An older preferences.yml has no usenet: block at all. Loading it must
    // leave the defaults alone rather than producing an enabled engine with
    // zero servers.
    Preferences p;
    QCOMPARE(p.usenetEnabled(), false);
    QVERIFY(p.usenetServers().isEmpty());
    QCOMPARE(p.usenetRetryIntervalSeconds(), 60);

    QVERIFY(p.saveTo(m_file));
    Preferences p2;
    QVERIFY(p2.load(m_file));
    QCOMPARE(p2.usenetEnabled(), false);
    QVERIFY(p2.usenetServers().isEmpty());
    QCOMPARE(p2.usenetRetryIntervalSeconds(), 60);
}

QTEST_MAIN(tst_UsenetPrefs)
#include "tst_UsenetPrefs.moc"
