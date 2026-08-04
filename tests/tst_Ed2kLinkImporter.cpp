/// @file tst_Ed2kLinkImporter.cpp
/// @brief Skip policy for eD2K link imports.
///
/// The clipboard watcher must never offer to download a file we already have — that is what
/// Source::Automatic covers. A file the user pasted or clicked is different: a completed or
/// cancelled download is a deliberate re-download request and must survive the filter.

#include "utils/Ed2kLinkImporter.h"

#include <QtTest>

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

QTEST_MAIN(TestEd2kLinkImporter)
#include "tst_Ed2kLinkImporter.moc"
