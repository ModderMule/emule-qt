/// @file tst_ArrCompat.cpp
/// @brief Sonarr/Radarr bridge helpers: eD2K as a padded info hash, magnet round
///        trip, qBittorrent states, Torznab XML, Wanted lists, Gluetun answers.

#include "TestHelpers.h"
#include "webserver/ArrCompat.h"

#include <QJsonDocument>
#include <QTest>
#include <QXmlStreamReader>

using namespace eMule;
using namespace eMule::arr;

namespace {
const QString kMd4 = QStringLiteral("0123456789abcdef0123456789abcdef");
}

class tst_ArrCompat : public QObject {
    Q_OBJECT

private slots:
    void btih_padsAndUnpads()
    {
        QCOMPARE(btihForMd4(kMd4.toUpper()), kMd4 + QStringLiteral("00000000"));
        QCOMPARE(md4FromBtih(kMd4 + QStringLiteral("00000000")), kMd4);
        // a real torrent hash is no eD2K file
        QVERIFY(md4FromBtih(kMd4 + QStringLiteral("00000001")).isEmpty());
        QVERIFY(md4FromBtih(kMd4).isEmpty());
    }

    void magnet_roundTrip()
    {
        const QString name = QStringLiteral("Show Name S01E02 [x264] & more.mkv");
        const QString magnet = magnetFor(kMd4, name, 734003200);
        const auto ref = parseArrLink(magnet);
        QVERIFY(ref);
        QCOMPARE(ref->hashHex, kMd4);
        QCOMPARE(ref->name, name);
        QCOMPARE(ref->size, quint64(734003200));
        QCOMPARE(ed2kLinkFor(*ref),
                 QStringLiteral("ed2k://|file|Show Name S01E02 [x264] & more.mkv|734003200|%1|/").arg(kMd4.toUpper()));
    }

    void parseArrLink_acceptsEd2kAndUrnEd2k()
    {
        const auto ed2k = parseArrLink(QStringLiteral("ed2k://|file|a.avi|1000|%1|/").arg(kMd4.toUpper()));
        QVERIFY(ed2k);
        QCOMPARE(ed2k->hashHex, kMd4);
        QCOMPARE(ed2k->size, quint64(1000));
        const auto urn = parseArrLink(QStringLiteral("magnet:?xt=urn:ed2k:%1&xl=5&dn=b.mkv").arg(kMd4));
        QVERIFY(urn);
        QCOMPARE(urn->name, QStringLiteral("b.mkv"));
    }

    void parseArrLink_refusesTorrentsAndSizeless()
    {
        QVERIFY(!parseArrLink(QStringLiteral("magnet:?xt=urn:btih:%1ffffffff&xl=5").arg(kMd4)));
        QVERIFY(!parseArrLink(QStringLiteral("magnet:?xt=urn:btih:%100000000").arg(kMd4)));   // no size
        QVERIFY(!parseArrLink(QStringLiteral("http://example.invalid/a.torrent")));
    }

    void qbitState_mapsTheDownload()
    {
        QCOMPARE(qbitState({.complete = true}), QStringLiteral("pausedUP"));
        QCOMPARE(qbitState({.completing = true}), QStringLiteral("moving"));
        QCOMPARE(qbitState({.error = true, .paused = true}), QStringLiteral("error"));
        QCOMPARE(qbitState({.paused = true, .datarate = 5}), QStringLiteral("pausedDL"));
        QCOMPARE(qbitState({.datarate = 5}), QStringLiteral("downloading"));
        QCOMPARE(qbitState({.sources = 3}), QStringLiteral("queuedDL"));
        QCOMPARE(qbitState({}), QStringLiteral("stalledDL"));
    }

    void categories_mapToEd2kTypes()
    {
        QCOMPARE(ed2kTypeForCategories({5000, 5030, 5040}), QStringLiteral("Video"));
        QCOMPARE(ed2kTypeForCategories({2000, 5000}), QStringLiteral("Video"));
        QCOMPARE(ed2kTypeForCategories({3000}), QStringLiteral("Audio"));
        QCOMPARE(ed2kTypeForCategories({7020}), QStringLiteral("Doc"));
        QCOMPARE(ed2kTypeForCategories({3000, 5000}), QString());
        QCOMPARE(ed2kTypeForCategories({}), QString());
        QCOMPARE(parseCategories(QStringLiteral("5030, 5040,x,5030")), (QList<int>{5030, 5040}));
    }

    void categoryForResult_prefersTheRequestedGroup()
    {
        QCOMPARE(categoryForResult(QStringLiteral("Show.S01E02.mkv"), {2000, 5030}), 5030);
        QCOMPARE(categoryForResult(QStringLiteral("Movie.2019.mkv"), {2040, 5030}), 2040);
        QCOMPARE(categoryForResult(QStringLiteral("Show.S01E02.mkv"), {}), 5000);
        QCOMPARE(categoryForResult(QStringLiteral("Movie.2019.mkv"), {}), 2000);
        QCOMPARE(categoryForResult(QStringLiteral("whatever.mkv"), {5040}), 5040);
    }

    void tvQuery_addsSeasonAndEpisode()
    {
        QCOMPARE(tvQuery(QStringLiteral("Show"), QStringLiteral("1"), QStringLiteral("2")), QStringLiteral("Show S01E02"));
        QCOMPARE(tvQuery(QStringLiteral("Show"), QStringLiteral("3"), QString()), QStringLiteral("Show S03"));
        QCOMPARE(tvQuery(QStringLiteral("Show"), QString(), QString()), QStringLiteral("Show"));
    }

    void nameMatchesQuery_needsEveryWord()
    {
        QVERIFY(nameMatchesQuery(QStringLiteral("The.Show.S01E02.720p.mkv"), QStringLiteral("the show S01E02")));
        QVERIFY(!nameMatchesQuery(QStringLiteral("The.Show.S01E03.mkv"), QStringLiteral("the show S01E02")));
        QCOMPARE(releaseTitle(QStringLiteral("The.Show.S01E02.720p.mkv")), QStringLiteral("The.Show.S01E02.720p"));
    }

    void torznabFeed_carriesMagnetAndAttrs()
    {
        TorznabItem item;
        item.title = QStringLiteral("The.Show.S01E02");
        item.fileName = QStringLiteral("The.Show.S01E02.mkv");
        item.hashHex = kMd4;
        item.size = 1234;
        item.category = 5030;
        item.seeders = 2;
        item.peers = 7;
        item.published = QDateTime(QDate(2026, 11, 28), QTime(10, 0), QTimeZone::UTC);
        const QByteArray xml = torznabFeed({item}, QStringLiteral("http://h/torznab/api"));

        QXmlStreamReader r(xml);
        QMap<QString, QStringList> attrs;
        QString enclosure, pubDate;
        while (!r.atEnd()) {
            if (r.readNext() != QXmlStreamReader::StartElement)
                continue;
            if (r.name() == QLatin1StringView("attr"))
                attrs[r.attributes().value(QStringLiteral("name")).toString()]
                    << r.attributes().value(QStringLiteral("value")).toString();
            else if (r.name() == QLatin1StringView("enclosure"))
                enclosure = r.attributes().value(QStringLiteral("url")).toString();
            else if (r.name() == QLatin1StringView("pubDate"))
                pubDate = r.readElementText();
        }
        QVERIFY2(!r.hasError(), qPrintable(r.errorString()));
        QCOMPARE(attrs.value(QStringLiteral("infohash")), QStringList{kMd4 + QStringLiteral("00000000")});
        QCOMPARE(attrs.value(QStringLiteral("seeders")), QStringList{QStringLiteral("2")});
        QCOMPARE(attrs.value(QStringLiteral("category")), (QStringList{QStringLiteral("5030"), QStringLiteral("5000")}));
        QCOMPARE(pubDate, QStringLiteral("Sat, 28 Nov 2026 10:00:00 +0000"));
        const auto ref = parseArrLink(enclosure);
        QVERIFY(ref);
        QCOMPARE(ref->size, quint64(1234));
    }

    void torznabCapsAndError_areXml()
    {
        QXmlStreamReader caps(torznabCaps());
        while (!caps.atEnd())
            caps.readNext();
        QVERIFY(!caps.hasError());
        QVERIFY(torznabError(100, QStringLiteral("bad")).contains("code=\"100\""));
    }

    void sonarrMissing_buildsEpisodeQueries()
    {
        const QByteArray json = R"({"page":1,"records":[
            {"seasonNumber":1,"episodeNumber":2,"monitored":true,"series":{"title":"The Show"}},
            {"seasonNumber":2,"episodeNumber":10,"monitored":false,"series":{"title":"Other"}},
            {"seasonNumber":3,"episodeNumber":4,"series":{}}]})";
        const auto terms = parseSonarrMissing(QJsonDocument::fromJson(json), QStringLiteral("Sonarr"));
        QCOMPARE(terms.size(), 1);
        QCOMPARE(terms.first().query, QStringLiteral("The Show S01E02"));
        QCOMPARE(terms.first().category, 5000);
    }

    void radarrMovies_onlyMissingAndAvailable()
    {
        const QByteArray json = R"([
            {"title":"Wanted","year":2019,"monitored":true,"hasFile":false,"isAvailable":true},
            {"title":"Have","year":2018,"monitored":true,"hasFile":true,"isAvailable":true},
            {"title":"Later","year":2030,"monitored":true,"hasFile":false,"isAvailable":false},
            {"title":"Ignored","year":2017,"monitored":false,"hasFile":false}])";
        const auto terms = parseRadarrMovies(QJsonDocument::fromJson(json), QStringLiteral("Radarr"), 10);
        QCOMPARE(terms.size(), 1);
        QCOMPARE(terms.first().query, QStringLiteral("Wanted 2019"));
        QCOMPARE(terms.first().category, 2000);
    }

    void gluetun_parsesBothPortShapes()
    {
        QCOMPARE(parseGluetunPort(R"({"port":51413})"), quint16(51413));
        QCOMPARE(parseGluetunPort(R"({"ports":[40000,40001]})"), quint16(40000));
        QCOMPARE(parseGluetunPort(R"({"port":0})"), quint16(0));
        QCOMPARE(parseGluetunPort("not json"), quint16(0));
        QCOMPARE(parseGluetunStatus(R"({"status":"running"})"), QStringLiteral("running"));
        QCOMPARE(parseGluetunPublicIp(R"({"public_ip":"198.51.100.7"})"), QStringLiteral("198.51.100.7"));
    }
};

QTEST_GUILESS_MAIN(tst_ArrCompat)
#include "tst_ArrCompat.moc"
