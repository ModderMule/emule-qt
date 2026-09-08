/// @file tst_IndexerPrefs.cpp
/// @brief Persistence of the `indexers:` block, and the API-key rules around it.
///
/// The same three silent failure modes tst_UsenetPrefs documents for provider
/// passwords apply here, because both ride on the one file-wide AES key that
/// `notifications:` carries: the ordering of the block, the mint condition, and
/// a decrypt that fails by returning an empty string.
///
/// One rule is specific to indexers and gets its own case: the URL is stored
/// **verbatim**. There is no single correct normalisation — a bare host needs
/// "/api" appended and a Jackett endpoint must not have it — so the completion
/// happens at request time, and a save that rewrote the field would take away
/// the user's ability to correct it.
///
/// As with the news-server passwords, this is obfuscation and not secrecy: the
/// key sits in the same file as the ciphertext.

#include "prefs/Preferences.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;

namespace {

IndexerConfig makeIndexer(const QString& name, const QString& url)
{
    IndexerConfig ix;
    ix.name = name;
    ix.url = url;
    ix.apiKey = QStringLiteral("key-") + name;
    return ix;
}

QString readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // namespace

class tst_IndexerPrefs : public QObject {
    Q_OBJECT

private slots:
    void init();

    void indexersRoundTrip();
    void urlIsStoredExactlyAsTyped();
    void apiKeyIsNotStoredInClear();
    void indexerOnlyConfiguration_mintsAnEncryptionKey();
    void plaintextApiKeyIsAcceptedOnceThenRewritten();
    void indexersBlockIsWrittenAfterNotifications();
    void wrongKeyLeavesTheApiKeyEmpty();
    void invalidEntriesAreDropped();
    void duplicateNamesAreCollapsed();
    void listIsCapped();
    void searchSettingsAreClamped();
    void defaultsAreSaneWithNoIndexersBlock();

    // -- feeds ---------------------------------------------------------------
    void feedsRoundTrip();
    void aUrlFeedsAddressIsNotStoredInClear();
    void anIntervalBelowTheFloorIsClamped();
    void invalidFeedsAreDropped();

private:
    QTemporaryDir m_dir;
    QString m_file;
};

void tst_IndexerPrefs::init()
{
    QVERIFY(m_dir.isValid());
    m_file = m_dir.filePath(QStringLiteral("preferences-%1.yml")
                                .arg(QTest::currentTestFunction()));
}

void tst_IndexerPrefs::indexersRoundTrip()
{
    {
        Preferences p;
        IndexerConfig primary = makeIndexer(QStringLiteral("Primary"),
                                         QStringLiteral("https://api.example.org/"));
        primary.kind = IndexerKind::Newznab;
        primary.timeoutMs = 45000;

        IndexerConfig hydra = makeIndexer(QStringLiteral("Hydra"),
                                          QStringLiteral("http://box:5076/api"));
        hydra.kind = IndexerKind::Both;
        hydra.enabled = false;

        p.setIndexers({primary, hydra});
        p.setIndexerResultLimit(200);
        p.setIndexerMaxPages(5);
        QVERIFY(p.saveTo(m_file));
    }

    Preferences p;
    QVERIFY(p.load(m_file));

    const auto list = p.indexers();
    QCOMPARE(list.size(), 2);

    QCOMPARE(list.at(0).name, QStringLiteral("Primary"));
    QCOMPARE(list.at(0).apiKey, QStringLiteral("key-Primary"));
    QCOMPARE(list.at(0).kind, IndexerKind::Newznab);
    QCOMPARE(list.at(0).timeoutMs, 45000);
    QVERIFY(list.at(0).enabled);

    QCOMPARE(list.at(1).kind, IndexerKind::Both);
    QCOMPARE(list.at(1).apiKey, QStringLiteral("key-Hydra"));
    QVERIFY(!list.at(1).enabled);

    QCOMPARE(p.indexerResultLimit(), 200);
    QCOMPARE(p.indexerMaxPages(), 5);
}

void tst_IndexerPrefs::urlIsStoredExactlyAsTyped()
{
    // Both forms have to survive a round trip unchanged. Normalising on save
    // would break one of them and there is no third option that works for both.
    const QString bare = QStringLiteral("https://api.example.org/");
    const QString jackett =
        QStringLiteral("http://localhost:9117/api/v2.0/indexers/x/results/torznab/api");

    {
        Preferences p;
        p.setIndexers({makeIndexer(QStringLiteral("a"), bare),
                       makeIndexer(QStringLiteral("b"), jackett)});
        QVERIFY(p.saveTo(m_file));
    }

    Preferences p;
    QVERIFY(p.load(m_file));
    QCOMPARE(p.indexers().at(0).url, bare);
    QCOMPARE(p.indexers().at(1).url, jackett);

    // …while the request-time completion still differs between them.
    QCOMPARE(p.indexers().at(0).apiUrl().path(), QStringLiteral("/api"));
    QCOMPARE(p.indexers().at(1).apiUrl().path(),
             QStringLiteral("/api/v2.0/indexers/x/results/torznab/api"));
}

void tst_IndexerPrefs::apiKeyIsNotStoredInClear()
{
    {
        Preferences p;
        p.setIndexers({makeIndexer(QStringLiteral("Primary"),
                                   QStringLiteral("https://api.example.com/"))});
        QVERIFY(p.saveTo(m_file));
    }

    const QString yaml = readAll(m_file);
    QVERIFY(!yaml.isEmpty());
    QVERIFY2(!yaml.contains(QStringLiteral("key-Primary")), qPrintable(yaml));
    QVERIFY(yaml.contains(QStringLiteral("apiKeyEnc:")));
    QVERIFY(!yaml.contains(QStringLiteral("\n      apiKey:")));
}

void tst_IndexerPrefs::indexerOnlyConfiguration_mintsAnEncryptionKey()
{
    // A user who configures only a search indexer has no SMTP password, no HTTP
    // Cache key and no news-server password. Without an indexer term in the mint
    // condition, no key is ever created and the API key is dropped on the first
    // save — with nothing in the log to say so.
    {
        Preferences p;
        p.setIndexers({makeIndexer(QStringLiteral("Solo"),
                                   QStringLiteral("https://api.example.com/"))});
        QVERIFY(p.saveTo(m_file));
    }

    const QString yaml = readAll(m_file);
    QVERIFY2(yaml.contains(QStringLiteral("emailEncryptionKey:")), qPrintable(yaml));

    Preferences p;
    QVERIFY(p.load(m_file));
    QCOMPARE(p.indexers().size(), 1);
    QCOMPARE(p.indexers().at(0).apiKey, QStringLiteral("key-Solo"));
}

void tst_IndexerPrefs::plaintextApiKeyIsAcceptedOnceThenRewritten()
{
    // The escape hatch for first setup and hand-editing. Accepted on load,
    // never written back.
    const QString yaml = QStringLiteral(
        "notifications:\n"
        "  emailEnabled: false\n"
        "indexers:\n"
        "  accounts:\n"
        "    - name: Hand\n"
        "      url: https://api.example.com/\n"
        "      apiKey: typed-by-hand\n"
        "      enabled: true\n");

    QFile f(m_file);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(yaml.toUtf8());
    f.close();

    Preferences p;
    QVERIFY(p.load(m_file));
    QCOMPARE(p.indexers().size(), 1);
    QCOMPARE(p.indexers().at(0).apiKey, QStringLiteral("typed-by-hand"));

    QVERIFY(p.saveTo(m_file));
    const QString rewritten = readAll(m_file);
    QVERIFY2(!rewritten.contains(QStringLiteral("typed-by-hand")), qPrintable(rewritten));
    QVERIFY(rewritten.contains(QStringLiteral("apiKeyEnc:")));
}

void tst_IndexerPrefs::indexersBlockIsWrittenAfterNotifications()
{
    // Load or write this block before `notifications` and every API key comes
    // back empty, because that is where the file-wide key is read and minted.
    {
        Preferences p;
        p.setIndexers({makeIndexer(QStringLiteral("Ordered"),
                                   QStringLiteral("https://api.example.com/"))});
        QVERIFY(p.saveTo(m_file));
    }

    const QString yaml = readAll(m_file);
    const int notifications = yaml.indexOf(QStringLiteral("\nnotifications:"));
    const int indexers = yaml.indexOf(QStringLiteral("\nindexers:"));

    QVERIFY(notifications >= 0);
    QVERIFY(indexers >= 0);
    QVERIFY2(notifications < indexers, "the indexers block must be written after notifications");
}

void tst_IndexerPrefs::wrongKeyLeavesTheApiKeyEmpty()
{
    {
        Preferences p;
        p.setIndexers({makeIndexer(QStringLiteral("Broken"),
                                   QStringLiteral("https://api.example.com/"))});
        QVERIFY(p.saveTo(m_file));
    }

    // Corrupt the key the blob was encrypted under, as a hand-edit or a merge
    // would. The entry survives; only the secret is lost.
    QString yaml = readAll(m_file);
    const int keyPos = yaml.indexOf(QStringLiteral("emailEncryptionKey: "));
    QVERIFY(keyPos >= 0);
    const int valueStart = keyPos + int(strlen("emailEncryptionKey: "));
    yaml.replace(valueStart, 8, QStringLiteral("00000000"));

    QFile f(m_file);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write(yaml.toUtf8());
    f.close();

    Preferences p;
    QVERIFY(p.load(m_file));
    QCOMPARE(p.indexers().size(), 1);
    QVERIFY(p.indexers().at(0).apiKey.isEmpty());
}

void tst_IndexerPrefs::invalidEntriesAreDropped()
{
    Preferences p;
    IndexerConfig noName;
    noName.url = QStringLiteral("https://api.example.com/");
    IndexerConfig noUrl;
    noUrl.name = QStringLiteral("Nameless");
    IndexerConfig badUrl = makeIndexer(QStringLiteral("Bad"), QStringLiteral("not a url"));

    p.setIndexers({noName, noUrl, badUrl,
                   makeIndexer(QStringLiteral("Good"),
                               QStringLiteral("https://api.example.com/"))});

    QCOMPARE(p.indexers().size(), 1);
    QCOMPARE(p.indexers().at(0).name, QStringLiteral("Good"));
}

void tst_IndexerPrefs::duplicateNamesAreCollapsed()
{
    // The name keys the caps sidecar on disk, so two accounts sharing one would
    // silently share a capabilities file.
    Preferences p;
    p.setIndexers({makeIndexer(QStringLiteral("Same"), QStringLiteral("https://a.example/")),
                   makeIndexer(QStringLiteral("same"), QStringLiteral("https://b.example/"))});

    QCOMPARE(p.indexers().size(), 1);
    QCOMPARE(p.indexers().at(0).url, QStringLiteral("https://a.example/"));
}

void tst_IndexerPrefs::listIsCapped()
{
    Preferences p;
    QList<IndexerConfig> many;
    for (int i = 0; i < Preferences::kMaxIndexers + 5; ++i) {
        many.append(makeIndexer(QStringLiteral("ix%1").arg(i),
                                QStringLiteral("https://a%1.example/").arg(i)));
    }
    p.setIndexers(many);
    QCOMPARE(p.indexers().size(), Preferences::kMaxIndexers);
}

void tst_IndexerPrefs::searchSettingsAreClamped()
{
    Preferences p;
    p.setIndexerResultLimit(999999);
    p.setIndexerMaxPages(0);
    p.setIndexerTimeoutSeconds(1);
    p.setIndexerCapsRefreshDays(0);

    QCOMPARE(p.indexerResultLimit(), 1000);
    QCOMPARE(p.indexerMaxPages(), 1);
    QCOMPARE(p.indexerTimeoutSeconds(), 5);
    QCOMPARE(p.indexerCapsRefreshDays(), 1);
}

void tst_IndexerPrefs::defaultsAreSaneWithNoIndexersBlock()
{
    QFile f(m_file);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("nick: someone\n");
    f.close();

    Preferences p;
    QVERIFY(p.load(m_file));
    QVERIFY(p.indexers().isEmpty());
    QCOMPARE(p.indexerResultLimit(), 100);
    QCOMPARE(p.indexerMaxPages(), 3);
    QCOMPARE(p.indexerTimeoutSeconds(), 30);
    QCOMPARE(p.indexerCapsRefreshDays(), 7);
}

void tst_IndexerPrefs::feedsRoundTrip()
{
    {
        Preferences p;
        IndexerFeed search;
        search.name = QStringLiteral("Ubuntu");
        search.query = QStringLiteral("ubuntu");
        search.categories = {6000, 7000};
        search.indexers = {QStringLiteral("Primary")};
        search.accept = QStringLiteral("desktop");
        search.reject = QStringLiteral("arm64");
        search.minSize = 1024;
        search.maxSize = 4096;
        search.maxAgeDays = 14;
        search.intervalMinutes = 45;
        search.grabExisting = true;

        IndexerFeed pasted;
        pasted.name = QStringLiteral("Pasted");
        pasted.kind = IndexerFeedKind::Url;
        pasted.url = QStringLiteral("https://ix.example/rss?r=abc");
        pasted.enabled = false;

        p.setIndexerFeeds({search, pasted});
        QVERIFY(p.saveTo(m_file));
    }

    Preferences p;
    QVERIFY(p.load(m_file));
    const auto feeds = p.indexerFeeds();
    QCOMPARE(feeds.size(), 2);

    QCOMPARE(feeds[0].name, QStringLiteral("Ubuntu"));
    QCOMPARE(feeds[0].kind, IndexerFeedKind::SavedSearch);
    QCOMPARE(feeds[0].query, QStringLiteral("ubuntu"));
    QCOMPARE(feeds[0].categories, QList<int>({6000, 7000}));
    QCOMPARE(feeds[0].indexers, QStringList{QStringLiteral("Primary")});
    QCOMPARE(feeds[0].accept, QStringLiteral("desktop"));
    QCOMPARE(feeds[0].reject, QStringLiteral("arm64"));
    QCOMPARE(feeds[0].minSize, 1024);
    QCOMPARE(feeds[0].maxSize, 4096);
    QCOMPARE(feeds[0].maxAgeDays, 14);
    QCOMPARE(feeds[0].intervalMinutes, 45);
    QVERIFY(feeds[0].grabExisting);

    QCOMPARE(feeds[1].name, QStringLiteral("Pasted"));
    QCOMPARE(feeds[1].kind, IndexerFeedKind::Url);
    QCOMPARE(feeds[1].url, QStringLiteral("https://ix.example/rss?r=abc"));
    QVERIFY(!feeds[1].enabled);
}

void tst_IndexerPrefs::aUrlFeedsAddressIsNotStoredInClear()
{
    Preferences p;
    IndexerFeed pasted;
    pasted.name = QStringLiteral("Pasted");
    pasted.kind = IndexerFeedKind::Url;
    // A feed URL is a credential wearing a URL's clothes: newznab's RSS endpoint
    // takes the key as a query parameter, so the whole string has to be stored
    // the way an apiKey is.
    pasted.url = QStringLiteral("https://ix.example/rss?t=search&r=SUPERSECRET");
    p.setIndexerFeeds({pasted});
    QVERIFY(p.saveTo(m_file));

    const QString text = readAll(m_file);
    QVERIFY2(!text.contains(QStringLiteral("SUPERSECRET")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("urlEnc")));
}

void tst_IndexerPrefs::anIntervalBelowTheFloorIsClamped()
{
    // Clamped in the setter and not only in the dialog: a hand-edited file
    // naming one minute would poll a public indexer ninety-six times an hour,
    // and the usual answer to that is a banned account.
    Preferences p;
    IndexerFeed feed;
    feed.name = QStringLiteral("Impatient");
    feed.intervalMinutes = 1;
    p.setIndexerFeeds({feed});

    QCOMPARE(p.indexerFeeds().size(), 1);
    QCOMPARE(p.indexerFeeds().first().intervalMinutes, IndexerFeed::kMinIntervalMinutes);

    // And on the way back in, for a file that was written by hand.
    QVERIFY(p.saveTo(m_file));
    QFile f(m_file);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QString text = QString::fromUtf8(f.readAll());
    f.close();
    text.replace(QStringLiteral("intervalMinutes: 15"), QStringLiteral("intervalMinutes: 1"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
    f.write(text.toUtf8());
    f.close();

    Preferences reloaded;
    QVERIFY(reloaded.load(m_file));
    QCOMPARE(reloaded.indexerFeeds().first().intervalMinutes, IndexerFeed::kMinIntervalMinutes);
}

void tst_IndexerPrefs::invalidFeedsAreDropped()
{
    Preferences p;

    IndexerFeed unnamed;
    unnamed.query = QStringLiteral("x");

    IndexerFeed urlless;
    urlless.name = QStringLiteral("Broken");
    urlless.kind = IndexerFeedKind::Url;

    IndexerFeed fileScheme;
    fileScheme.name = QStringLiteral("Local");
    fileScheme.kind = IndexerFeedKind::Url;
    // http and https only: a feed naming file: would have the daemon poll its
    // own disk on a timer.
    fileScheme.url = QStringLiteral("file:///etc/passwd");

    IndexerFeed good;
    good.name = QStringLiteral("Good");

    IndexerFeed duplicate;
    duplicate.name = QStringLiteral("GOOD");

    p.setIndexerFeeds({unnamed, urlless, fileScheme, good, duplicate});
    QCOMPARE(p.indexerFeeds().size(), 1);
    QCOMPARE(p.indexerFeeds().first().name, QStringLiteral("Good"));
}

QTEST_MAIN(tst_IndexerPrefs)
#include "tst_IndexerPrefs.moc"
