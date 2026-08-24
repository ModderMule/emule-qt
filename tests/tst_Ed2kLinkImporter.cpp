/// @file tst_Ed2kLinkImporter.cpp
/// @brief Skip policy for eD2K link imports.
///
/// The clipboard watcher must never offer to download a file we already have — that is what
/// Source::Automatic covers. A file the user pasted or clicked is different: a completed or
/// cancelled download is a deliberate re-download request and must survive the filter.

#include "utils/Ed2kLinkImporter.h"

#include <QFileOpenEvent>
#include <QUrl>
#include <QtTest>

using eMule::Ed2kLinkImporter;

using eMule::Ed2kLinkImporter;
using KnownType = eMule::SearchFile::KnownType;

class TestEd2kLinkImporter : public QObject {
    Q_OBJECT

private slots:
    void shouldSkip_data();
    void shouldSkip();

    void skipReason_data();
    void skipReason();

    void skipMessage_data();
    void skipMessage();
    void skipMessageDistinguishesDownloading();

    void splitLinks_data();
    void splitLinks();

    void configLinkIsRoutedNotInvalid();
    void malformedConfigLinkIsRedacted();

    void linkKindsIn_data();
    void linkKindsIn();

    void linkFromFileOpenEvent_data();
    void linkFromFileOpenEvent();
};

void TestEd2kLinkImporter::shouldSkip_data()
{
    QTest::addColumn<int>("type");
    QTest::addColumn<int>("source");
    QTest::addColumn<bool>("expected");

    const int automatic = static_cast<int>(Ed2kLinkImporter::Source::Automatic);
    const int manual    = static_cast<int>(Ed2kLinkImporter::Source::Manual);

    // Having the file already makes re-adding pointless no matter who asked.
    QTest::newRow("shared/auto")        << int(KnownType::Shared)      << automatic << true;
    QTest::newRow("shared/manual")      << int(KnownType::Shared)      << manual    << true;
    QTest::newRow("downloading/auto")   << int(KnownType::Downloading) << automatic << true;
    QTest::newRow("downloading/manual") << int(KnownType::Downloading) << manual    << true;

    // Gone from the transfer list: silent for the clipboard, allowed by hand.
    QTest::newRow("downloaded/auto")    << int(KnownType::Downloaded)  << automatic << true;
    QTest::newRow("downloaded/manual")  << int(KnownType::Downloaded)  << manual    << false;
    QTest::newRow("cancelled/auto")     << int(KnownType::Cancelled)   << automatic << true;
    QTest::newRow("cancelled/manual")   << int(KnownType::Cancelled)   << manual    << false;

    // Never seen before — always downloadable.
    QTest::newRow("unknown/auto")       << int(KnownType::Unknown)     << automatic << false;
    QTest::newRow("unknown/manual")     << int(KnownType::Unknown)     << manual    << false;
    QTest::newRow("undetermined/auto")  << int(KnownType::NotDetermined) << automatic << false;
    QTest::newRow("undetermined/manual")<< int(KnownType::NotDetermined) << manual    << false;
}

void TestEd2kLinkImporter::shouldSkip()
{
    QFETCH(int, type);
    QFETCH(int, source);
    QFETCH(bool, expected);

    QCOMPARE(Ed2kLinkImporter::shouldSkip(static_cast<KnownType>(type),
                                          static_cast<Ed2kLinkImporter::Source>(source)),
             expected);
}

void TestEd2kLinkImporter::skipReason_data()
{
    QTest::addColumn<int>("type");
    QTest::addColumn<bool>("hasReason");

    QTest::newRow("shared")       << int(KnownType::Shared)        << true;
    QTest::newRow("downloading")  << int(KnownType::Downloading)   << true;
    QTest::newRow("downloaded")   << int(KnownType::Downloaded)    << true;
    QTest::newRow("cancelled")    << int(KnownType::Cancelled)     << true;
    QTest::newRow("unknown")      << int(KnownType::Unknown)       << false;
    QTest::newRow("undetermined") << int(KnownType::NotDetermined) << false;
}

/// Every state that can be skipped must be able to explain itself — the reason goes into the
/// log line and the status bar, so a silent skip would leave the user guessing.
void TestEd2kLinkImporter::skipReason()
{
    QFETCH(int, type);
    QFETCH(bool, hasReason);

    QCOMPARE(!Ed2kLinkImporter::skipReason(static_cast<KnownType>(type)).isEmpty(), hasReason);
}

void TestEd2kLinkImporter::skipMessage_data()
{
    skipReason_data();  // same states, same expectation of having something to say
}

/// The message is what the user actually reads — in the log for every skip, and in the popup
/// when a single pasted link was dropped. It has to name the file, or "already have it" gives
/// no clue which one.
void TestEd2kLinkImporter::skipMessage()
{
    QFETCH(int, type);
    QFETCH(bool, hasReason);

    const QString message =
        Ed2kLinkImporter::skipMessage(static_cast<KnownType>(type), QStringLiteral("movie.avi"));

    QCOMPARE(!message.isEmpty(), hasReason);
    if (hasReason)
        QVERIFY(message.contains(QStringLiteral("movie.avi")));
}

/// "You already have it" and "you are already downloading it" are different situations; a
/// shared copy of the wording would tell the user the download is finished when it is not.
void TestEd2kLinkImporter::skipMessageDistinguishesDownloading()
{
    const QString name = QStringLiteral("movie.avi");
    QVERIFY(Ed2kLinkImporter::skipMessage(KnownType::Shared, name)
            != Ed2kLinkImporter::skipMessage(KnownType::Downloading, name));
}

void TestEd2kLinkImporter::splitLinks_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QStringList>("expected");

    const QString a = QStringLiteral("ed2k://|file|a.avi|100|AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA|/");
    const QString b = QStringLiteral("ed2k://|file|b.avi|200|BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB|/");

    QTest::newRow("empty")     << QString()               << QStringList();
    QTest::newRow("blank")     << QStringLiteral("   \n") << QStringList();
    QTest::newRow("one")       << a                       << QStringList{a};
    QTest::newRow("two lines") << (a + QLatin1Char('\n') + b) << QStringList{a, b};

    // Windows clipboards keep the CR; it must not end up inside the link.
    QTest::newRow("crlf") << (a + QStringLiteral("\r\n") + b) << QStringList{a, b};

    // Copied out of a web page or a chat window: no newline between the links.
    QTest::newRow("same line")         << (a + QLatin1Char(' ') + b) << QStringList{a, b};
    QTest::newRow("same line, no gap") << (a + b)                    << QStringList{a, b};

    // File names may contain spaces, so the link must survive as one piece.
    const QString spaced =
        QStringLiteral("ed2k://|file|my movie.avi|100|AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA|/");
    QTest::newRow("spaces in name") << spaced << QStringList{spaced};

    // Junk still comes through so the caller can report it as an invalid link.
    QTest::newRow("not a link") << QStringLiteral("hello") << QStringList{QStringLiteral("hello")};

    // Leading prose is dropped, the link is not.
    QTest::newRow("prose before link")
        << (QStringLiteral("grab this: ") + a) << QStringList{a};
}

void TestEd2kLinkImporter::splitLinks()
{
    QFETCH(QString, text);
    QFETCH(QStringList, expected);

    QCOMPARE(Ed2kLinkImporter::splitLinks(text), expected);
}

// ---------------------------------------------------------------------------
// HTTP Cache configuration links
//
// The importer is the single funnel every paste, click and clipboard hit goes
// through, so this is where an ed2k://|httpcache| link has to stop being treated
// as an unparseable file link. With no IPC client there is nothing to send to,
// which is exactly what makes the routing decision observable on its own.
// ---------------------------------------------------------------------------

void TestEd2kLinkImporter::configLinkIsRoutedNotInvalid()
{
    const QString link = QStringLiteral(
        "ed2k://|httpcache|Cache|https://cache.example.com|1f4b9c02d7e35a68|k=default|/");

    Ed2kLinkImporter::Result result;
    Ed2kLinkImporter::importLinks(link, /*ipc=*/nullptr, /*parent=*/nullptr,
                                  Ed2kLinkImporter::Source::Manual,
                                  Ed2kLinkImporter::Prompt::Silent,
                                  [&result](const Ed2kLinkImporter::Result& r) { result = r; });

    QCOMPARE(result.httpCacheConfigs, 1);
    QCOMPARE(result.added, 0);          // it starts no download, so no tab switch
    QVERIFY(result.invalid.isEmpty());  // and it is not "a link that could not be parsed"
}

void TestEd2kLinkImporter::malformedConfigLinkIsRedacted()
{
    // %ZZ is not an escape, so this link is refused whole — and the refusal is
    // reported in a dialog and a log line, which is the one place a secret could
    // still leak out of a link that was never applied.
    const QString secret = QStringLiteral("1f4b9c02d7e35a68");
    const QString link =
        QStringLiteral("ed2k://|httpcache|Cache|https://cache.example.com|%1|k=a%ZZ|/").arg(secret);

    Ed2kLinkImporter::Result result;
    Ed2kLinkImporter::importLinks(link, nullptr, nullptr,
                                  Ed2kLinkImporter::Source::Manual,
                                  Ed2kLinkImporter::Prompt::Silent,
                                  [&result](const Ed2kLinkImporter::Result& r) { result = r; });

    QCOMPARE(result.httpCacheConfigs, 0);
    QCOMPARE(result.invalid.size(), 1);
    QVERIFY2(!result.invalid.first().contains(secret), qPrintable(result.invalid.first()));
    QVERIFY(result.invalid.first().contains(QStringLiteral("cache.example.com")));
}

// ---------------------------------------------------------------------------
// linkKindsIn — what a "Paste eD2K Links" action and the clipboard watcher see
//
// Both used to hardcode their own substring test, and both got it wrong: the
// Transfers action looked only for a file link, so an HTTP Cache configuration
// link left it greyed out, and the Servers action used startsWith(), so any text
// in front of a server link greyed that one out too.
// ---------------------------------------------------------------------------

void TestEd2kLinkImporter::linkKindsIn_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("expected");

    const int none  = 0;
    const int file  = static_cast<int>(Ed2kLinkImporter::LinkKind::File);
    const int cache = static_cast<int>(Ed2kLinkImporter::LinkKind::HttpCache);
    const int srv   = static_cast<int>(Ed2kLinkImporter::LinkKind::Server);

    const QString fileLink = QStringLiteral(
        "ed2k://|file|Name.rar|4960062|95818F7E4E4E0C1D1E2A3B4C5D6E7F80|/");
    const QString cacheLink = QStringLiteral(
        "ed2k://|httpcache|HTTP%20Cache%20upload%20config|http://danielmac.local:8080"
        "|9932a62be8ffb62e3d6da8e923e6163dc3a05391a943168c|k=default|/");
    const QString serverLink = QStringLiteral("ed2k://|server|1.2.3.4|4661|/");

    QTest::newRow("empty")       << QString()                       << none;
    QTest::newRow("prose")       << QStringLiteral("no links here") << none;

    QTest::newRow("file")        << fileLink   << file;
    QTest::newRow("httpcache")   << cacheLink  << cache;
    QTest::newRow("server")      << serverLink << srv;

    // Case is not part of the type token.
    QTest::newRow("uppercase")   << cacheLink.toUpper() << cache;

    // The clipboard is rarely just a link.
    QTest::newRow("server after prose")
        << QStringLiteral("Add this: %1").arg(serverLink) << srv;
    QTest::newRow("file on second line")
        << QStringLiteral("here you go\n%1").arg(fileLink) << file;

    QTest::newRow("file and config")
        << QStringLiteral("%1\n%2").arg(fileLink, cacheLink) << (file | cache);

    // serverlist is a different link with a different handler — the trailing '|'
    // in the needle is what keeps it out.
    QTest::newRow("serverlist is not a server")
        << QStringLiteral("ed2k://|serverlist|http://example.com/server.met|/") << none;

    // Nothing in the GUI imports these, so nothing should light up for them.
    QTest::newRow("nodeslist")
        << QStringLiteral("ed2k://|nodeslist|http://example.com/nodes.dat|/") << none;
    QTest::newRow("search")
        << QStringLiteral("ed2k://|search|foo|/") << none;
}

void TestEd2kLinkImporter::linkKindsIn()
{
    QFETCH(QString, text);
    QFETCH(int, expected);

    QCOMPARE(static_cast<int>(Ed2kLinkImporter::linkKindsIn(text).toInt()), expected);
}

// ---------------------------------------------------------------------------
// linkFromFileOpenEvent - the macOS Apple Event route
//
// A browser or Finder click on an ed2k:// link reaches the application as a
// QEvent::FileOpen. The Cocoa plugin only wraps the string in a QUrl if it parses,
// and an eD2K link never does - '|' is not a legal host character - so Qt takes its
// fallback and the link ends up in file(), not url(). Reading url().toString() was
// what made the whole macOS route silently do nothing for every link type.
//
// Every case here builds the event exactly the way Qt does on that fallback path:
// handleFileOpenEvent(QString) -> FileOpenEvent(QUrl::fromLocalFile(s)) ->
// QFileOpenEvent(url). If that round trip ever stops preserving the link these fail
// here, rather than the feature failing in the field.
// ---------------------------------------------------------------------------

void TestEd2kLinkImporter::linkFromFileOpenEvent_data()
{
    QTest::addColumn<QString>("delivered");   // what macOS handed to Qt
    QTest::addColumn<QString>("expected");

    const QString fileLink = QStringLiteral(
        "ed2k://|file|Name.rar|4960062|95818F7E4E4E0C1D1E2A3B4C5D6E7F80|/");
    QTest::newRow("file") << fileLink << fileLink;

    // A name carrying both a percent escape and an escaped pipe: the link must come
    // back byte for byte, because parseED2KLink() splits on '|' before decoding and
    // a single stray decode would re-split it into the wrong number of fields.
    const QString escapedName = QStringLiteral(
        "ed2k://|file|Some%20Name%7Cwith%20bar.rar|4960062"
        "|95818F7E4E4E0C1D1E2A3B4C5D6E7F80|h=4XEIHTHRC2SIQD5IEFXHRC2SIQD5I|/");
    QTest::newRow("escaped name") << escapedName << escapedName;

    // The credential in a configuration link has to survive intact, or the server
    // rejects the upload key with no clue as to why.
    const QString cacheLink = QStringLiteral(
        "ed2k://|httpcache|HTTP%20Cache%20upload%20config|https://cache.example.com"
        "|9932a62be8ffb62e3d6da8e923e6163dc3a05391a943168c|k=default|/");
    QTest::newRow("httpcache") << cacheLink << cacheLink;

    const QString serverLink = QStringLiteral("ed2k://|server|1.2.3.4|4661|/");
    QTest::newRow("server") << serverLink << serverLink;

    // Case is not part of the scheme test.
    QTest::newRow("uppercase scheme")
        << QStringLiteral("ED2K://|file|A|1|95818F7E4E4E0C1D1E2A3B4C5D6E7F80|/")
        << QStringLiteral("ED2K://|file|A|1|95818F7E4E4E0C1D1E2A3B4C5D6E7F80|/");

    // A dropped file must not be mistaken for a link: the importer would report it as
    // invalid, and the event has to stay available to whatever else may want it.
    QTest::newRow("plain path")  << QStringLiteral("/Users/x/movie.emulecollection") << QString();
    QTest::newRow("http url")    << QStringLiteral("https://example.com/x")          << QString();
    QTest::newRow("empty")       << QString()                                        << QString();
}

void TestEd2kLinkImporter::linkFromFileOpenEvent()
{
    QFETCH(QString, delivered);
    QFETCH(QString, expected);

    // What Qt's cocoa plugin produces for a string that does not parse as a URL.
    if (!delivered.isEmpty()) {
        const QFileOpenEvent fallback(QUrl::fromLocalFile(delivered));
        QCOMPARE(Ed2kLinkImporter::linkFromFileOpenEvent(fallback), expected);
    }

    // And for one that does - magnet: has no authority, so it survives as a real QUrl
    // and has to be read out of url() instead. Same function, other accessor.
    const QUrl magnet(QStringLiteral("magnet:?xt=urn:ed2k:95818F7E4E4E0C1D1E2A3B4C5D6E7F80"));
    QVERIFY(magnet.isValid());
    const QFileOpenEvent parsed(magnet);
    QCOMPARE(Ed2kLinkImporter::linkFromFileOpenEvent(parsed), magnet.toString());
}

QTEST_MAIN(TestEd2kLinkImporter)
#include "tst_Ed2kLinkImporter.moc"
