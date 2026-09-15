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

#include "net/ProxySettings.h"
#include "prefs/Preferences.h"

#include <QFile>
#include <QNetworkProxy>
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
    void quotaFieldsRoundTrip();
    void aConfigWithoutQuotasLoadsUnmeteredWithAnId();
    void healthCheckSettingsRoundTrip();
    void newsServersFollowTheProxyUnlessSwitchedOff();
    void subjectPatternsRoundTrip();
    void noSubjectPatternsBlockIsWrittenWhenTheDefaultsAreInUse();
    void aSubjectPatternThatWillNotCompileSurvivesASaveAndLoad();
    void aPatternWithNoRoleIsDroppedOnLoad();
    void subjectPatternListIsCapped();
    void aPatternWithTrailingWhitespaceIsNotSilentlyTrimmed();

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

void tst_UsenetPrefs::healthCheckSettingsRoundTrip()
{
    {
        Preferences p;
        // Sampling on, and a pause threshold: the shipped defaults, so a fresh
        // config has to come back saying exactly this.
        QCOMPARE(p.usenetHealthCheck(), 1);
        QCOMPARE(p.usenetHealthMinPercent(), 95);

        p.setUsenetHealthCheck(2);
        p.setUsenetHealthMinPercent(80);
        QVERIFY(p.saveTo(m_file));
    }

    Preferences p2;
    QVERIFY(p2.load(m_file));
    QCOMPARE(p2.usenetHealthCheck(), 2);
    QCOMPARE(p2.usenetHealthMinPercent(), 80);

    // Both are clamped rather than trusted: the mode indexes a three-entry combo
    // and a stray value would silently mean "off", while a percentage outside
    // 0-100 would make the comparison that gates the pause meaningless.
    p2.setUsenetHealthCheck(9);
    QCOMPARE(p2.usenetHealthCheck(), 2);
    p2.setUsenetHealthCheck(-1);
    QCOMPARE(p2.usenetHealthCheck(), 0);
    p2.setUsenetHealthMinPercent(500);
    QCOMPARE(p2.usenetHealthMinPercent(), 100);
}

// ---------------------------------------------------------------------------
// Subject patterns
// ---------------------------------------------------------------------------

namespace {

UsenetSubjectPattern namePattern(const QString& name, const QString& pattern)
{
    UsenetSubjectPattern p;
    p.name = name;
    p.role = UsenetSubjectRole::Name;
    p.pattern = pattern;
    return p;
}

} // namespace

void tst_UsenetPrefs::subjectPatternsRoundTrip()
{
    {
        Preferences p;
        UsenetSubjectPattern counter;
        counter.name = QStringLiteral("my-counter");
        counter.role = UsenetSubjectRole::Part;
        counter.pattern = QStringLiteral("[(](?<index>\\d+)/(?<total>\\d+)[)]");
        counter.pick = UsenetSubjectPick::Last;

        UsenetSubjectPattern hex = namePattern(QStringLiteral("hex"),
                                               QStringLiteral("(?<name>[0-9a-f]+[.]dat)"));
        hex.caseInsensitive = true;
        hex.enabled = false;

        p.setUsenetSubjectPatterns({counter, hex});
        QVERIFY(p.saveTo(m_file));
    }

    Preferences p;
    QVERIFY(p.load(m_file));
    const auto pats = p.usenetSubjectPatterns();
    QCOMPARE(pats.size(), 2);

    QCOMPARE(pats.at(0).name, QStringLiteral("my-counter"));
    QCOMPARE(pats.at(0).role, UsenetSubjectRole::Part);
    QCOMPARE(pats.at(0).pick, UsenetSubjectPick::Last);
    QCOMPARE(pats.at(0).caseInsensitive, false);
    QCOMPARE(pats.at(0).enabled, true);

    QCOMPARE(pats.at(1).name, QStringLiteral("hex"));
    QCOMPARE(pats.at(1).role, UsenetSubjectRole::Name);
    QCOMPARE(pats.at(1).pick, UsenetSubjectPick::First);
    QCOMPARE(pats.at(1).caseInsensitive, true);
    QCOMPARE(pats.at(1).enabled, false);
}

void tst_UsenetPrefs::noSubjectPatternsBlockIsWrittenWhenTheDefaultsAreInUse()
{
    // What keeps every existing preferences.yml byte-identical: the built-ins
    // are not a value, they are the absence of one. Writing them out would also
    // freeze this release's defaults into installations that should keep
    // following ours.
    {
        Preferences p;
        p.setUsenetEnabled(true);
        QVERIFY(p.saveTo(m_file));
    }
    QVERIFY(!readAll(m_file).contains(QStringLiteral("subjectPatterns")));
}

void tst_UsenetPrefs::aSubjectPatternThatWillNotCompileSurvivesASaveAndLoad()
{
    // Deliberately not validated here. Deleting a user's typo on the next save
    // is worse than keeping it -- the typo is the only record of what they were
    // trying to do, and a broken rule costs nothing at run time because its role
    // falls back to the built-ins.
    {
        Preferences p;
        p.setUsenetSubjectPatterns({namePattern(QStringLiteral("broken"),
                                                QStringLiteral("(?<name>["))});
        QVERIFY(p.saveTo(m_file));
    }

    Preferences p;
    QVERIFY(p.load(m_file));
    QCOMPARE(p.usenetSubjectPatterns().size(), 1);
    QCOMPARE(p.usenetSubjectPatterns().first().pattern, QStringLiteral("(?<name>["));
}

void tst_UsenetPrefs::aPatternWithNoRoleIsDroppedOnLoad()
{
    // The asymmetry with the case above: a bad regex still says what the user
    // meant and has somewhere to sit. A rule with no role is not a rule, and
    // guessing one would quietly fill the wrong field.
    {
        Preferences p;
        p.setUsenetEnabled(true);
        QVERIFY(p.saveTo(m_file));
    }

    QString yaml = readAll(m_file);
    yaml.replace(QStringLiteral("usenet:"),
                 QStringLiteral("usenet:\n  subjectPatterns:\n"
                                "    - {name: no-role, pattern: \"(?<name>x)\"}\n"
                                "    - {name: fine, role: name, pattern: \"(?<name>y)\"}"));
    QFile f(m_file);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write(yaml.toUtf8()) > 0);
    f.close();

    Preferences p;
    QVERIFY(p.load(m_file));
    const auto pats = p.usenetSubjectPatterns();
    QCOMPARE(pats.size(), 1);
    QCOMPARE(pats.first().name, QStringLiteral("fine"));
}

void tst_UsenetPrefs::subjectPatternListIsCapped()
{
    QList<UsenetSubjectPattern> many;
    for (int i = 0; i < Preferences::kMaxSubjectPatterns + 12; ++i) {
        many.append(namePattern(QStringLiteral("rule-%1").arg(i),
                                QStringLiteral("(?<name>x%1)").arg(i)));
    }

    Preferences p;
    p.setUsenetSubjectPatterns(many);
    QCOMPARE(p.usenetSubjectPatterns().size(), Preferences::kMaxSubjectPatterns);

    // And duplicates collapse on the name, the way servers and feeds do.
    p.setUsenetSubjectPatterns({namePattern(QStringLiteral("dup"), QStringLiteral("(?<name>a)")),
                                namePattern(QStringLiteral("DUP"), QStringLiteral("(?<name>b)"))});
    QCOMPARE(p.usenetSubjectPatterns().size(), 1);
    QCOMPARE(p.usenetSubjectPatterns().first().pattern, QStringLiteral("(?<name>a)"));
}

void tst_UsenetPrefs::aPatternWithTrailingWhitespaceIsNotSilentlyTrimmed()
{
    // A regex can legitimately end in a literal space. The habit of trimming
    // every string that comes out of a YAML loader would edit the user's rule
    // and change what it matches, which is the exact kind of helpfulness this
    // preference exists to avoid. The *name* is trimmed; the pattern is not.
    const QString pattern = QStringLiteral("(?<name>[a-z]+[.]mkv) ");
    {
        Preferences p;
        p.setUsenetSubjectPatterns({namePattern(QStringLiteral("  spaced  "), pattern)});
        QVERIFY(p.saveTo(m_file));
    }

    Preferences p;
    QVERIFY(p.load(m_file));
    QCOMPARE(p.usenetSubjectPatterns().size(), 1);
    QCOMPARE(p.usenetSubjectPatterns().first().name, QStringLiteral("spaced"));
    QCOMPARE(p.usenetSubjectPatterns().first().pattern, pattern);
}

QTEST_MAIN(tst_UsenetPrefs)

void tst_UsenetPrefs::quotaFieldsRoundTrip()
{
    QString mainId;
    {
        Preferences p;
        NewsServer main = makeServer(QStringLiteral("main"),
                                     QStringLiteral("news.example.com"), 563);
        main.quotaKind = NntpQuotaKind::Monthly;
        main.quotaBytes = 500000000000LL;
        main.quotaResetDay = 17;
        main.quotaFallThrough = true;

        NewsServer block = makeServer(QStringLiteral("block"),
                                      QStringLiteral("block.example.net"), 563);
        block.quotaKind = NntpQuotaKind::Block;
        block.quotaBytes = 1000000000000LL;

        p.setUsenetServers({main, block});
        QVERIFY(p.saveTo(m_file));
        mainId = p.usenetServers().at(0).accountId;
    }

    Preferences p;
    QVERIFY(p.load(m_file));
    const auto servers = p.usenetServers();
    QCOMPARE(servers.size(), 2);

    QCOMPARE(servers.at(0).quotaKind, NntpQuotaKind::Monthly);
    QCOMPARE(servers.at(0).quotaBytes, 500000000000LL);
    QCOMPARE(servers.at(0).quotaResetDay, 17);
    QCOMPARE(servers.at(0).quotaFallThrough, true);
    QVERIFY(servers.at(0).isMetered());

    QCOMPARE(servers.at(1).quotaKind, NntpQuotaKind::Block);
    QCOMPARE(servers.at(1).quotaBytes, 1000000000000LL);
    // Not written for a block account, so it comes back as the default rather
    // than as whatever happened to be in memory.
    QCOMPARE(servers.at(1).quotaResetDay, 1);
    QCOMPARE(servers.at(1).quotaFallThrough, false);

    // The id is what the usage meter is keyed by, so it has to survive the file
    // — a minted-every-load id would be a fresh meter on every restart.
    QVERIFY(!mainId.isEmpty());
    QCOMPARE(servers.at(0).accountId, mainId);
    QVERIFY(servers.at(0).accountId != servers.at(1).accountId);
}

void tst_UsenetPrefs::aConfigWithoutQuotasLoadsUnmeteredWithAnId()
{
    // Nothing changes for an existing configuration: no allowance, nothing
    // excluded, nothing parked. The id is minted on load rather than lazily, so
    // the meter has something stable to key on from the first article.
    {
        QFile f(m_file);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("usenet:\n"
                "  servers:\n"
                "    - host: news.example.com\n"
                "      port: 563\n");
        f.close();
    }

    Preferences p;
    QVERIFY(p.load(m_file));
    const auto servers = p.usenetServers();
    QCOMPARE(servers.size(), 1);
    QCOMPARE(servers.at(0).quotaKind, NntpQuotaKind::None);
    QCOMPARE(servers.at(0).quotaBytes, 0);
    QCOMPARE(servers.at(0).quotaResetDay, 1);
    QVERIFY(!servers.at(0).isMetered());
    QVERIFY(!servers.at(0).accountId.isEmpty());
}

void tst_UsenetPrefs::newsServersFollowTheProxyUnlessSwitchedOff()
{
    {
        Preferences p;
        // On by default: a proxy the user set up was meant for their traffic.
        QVERIFY(p.usenetUseProxy());

        p.setProxyType(PROXYTYPE_SOCKS5);
        p.setProxyHost(QStringLiteral("proxy.example"));
        p.setProxyPort(1080);
        const QNetworkProxy route = toNetworkProxy(p.usenetProxySettings());
        QCOMPARE(route.type(), QNetworkProxy::Socks5Proxy);
        QCOMPARE(route.hostName(), QStringLiteral("proxy.example"));
        QCOMPARE(route.port(), quint16(1080));

        p.setUsenetUseProxy(false);
        QCOMPARE(toNetworkProxy(p.usenetProxySettings()).type(), QNetworkProxy::NoProxy);
        // The switch is Usenet's alone; eD2K's route does not move with it.
        QVERIFY(p.proxySettings().useProxy);
        QVERIFY(p.saveTo(m_file));
    }

    Preferences p2;
    QVERIFY(p2.load(m_file));
    QVERIFY(!p2.usenetUseProxy());

    // Qt has no SOCKS4 client; SOCKS5 is the nearest thing it can speak.
    p2.setUsenetUseProxy(true);
    p2.setProxyType(PROXYTYPE_SOCKS4A);
    QCOMPARE(toNetworkProxy(p2.usenetProxySettings()).type(), QNetworkProxy::Socks5Proxy);
    p2.setProxyType(PROXYTYPE_NOPROXY);
    QCOMPARE(toNetworkProxy(p2.usenetProxySettings()).type(), QNetworkProxy::NoProxy);
}


#include "tst_UsenetPrefs.moc"
