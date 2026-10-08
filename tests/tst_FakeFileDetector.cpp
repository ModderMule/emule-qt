/// @file tst_FakeFileDetector.cpp
/// @brief Fake-file score: reasons, bands, name grouping, rule file.

#include "search/FakeFileDetector.h"
#include "utils/Opcodes.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTest>

using namespace eMule;

class tst_FakeFileDetector : public QObject {
    Q_OBJECT

private slots:
    void cleanFile_looksGood();
    void genuine_needsTrustOrRating();
    void nameGroups_sameContent_data();
    void nameGroups_sameContent();
    void nameGroups_unrelatedNames();
    void nameGroups_keywordAloneDoesNotJoin();
    void abuseNames_lowerTheScore();
    void namesSpanKinds();
    void badSignalName_tokensAndRegex();
    void badSignalComment();
    void claimedType_archiveAsProIsFine();
    void claimedType_mediaVsProgram();
    void spamAndRatings();
    void multipleAich();
    void media_plausibility();
    void media_tagMismatch();
    void header_mismatchAndMasquerade();
    void score_isCapped();
    void rules_parse();
    void kadTrust_decode();
    void ids_areStable();
    void sample_replay();

private:
    static FakeFileInput video(const QString& name = QStringLiteral("Some.Film.2024.1080p.mkv"))
    {
        FakeFileInput in;
        in.name = name;
        in.size = 2ull << 30;
        in.claimedType = QStringLiteral(ED2KFTSTR_VIDEO);
        return in;
    }
    static FakeFileVerdict assess(const FakeFileInput& in)
    {
        return assessFile(in, FakeFileRules::defaults());
    }
};

void tst_FakeFileDetector::cleanFile_looksGood()
{
    const FakeFileVerdict v = assess(video());
    QCOMPARE(v.score, 0);
    QVERIFY(v.reasons.isEmpty());
    QCOMPARE(v.band, Confidence::LooksGood);
}

void tst_FakeFileDetector::genuine_needsTrustOrRating()
{
    // Many Kad publishers alone is popularity: live, a known fake had them too
    FakeFileInput in = video();
    in.kadTrust = KadTrust::High;
    QCOMPARE(assess(in).band, Confidence::LooksGood);

    // ... it counts when a second, independent name says the same
    in.observedNames = {QStringLiteral("Some Film (2024) [BluRay].mkv")};
    QCOMPARE(assess(in).band, Confidence::Genuine);
    in.kadTrust = KadTrust::Normal;
    QCOMPARE(assess(in).band, Confidence::LooksGood);

    in.observedNames.clear();
    in.userRating = 4;
    QCOMPARE(assess(in).band, Confidence::Genuine);

    // Any finding at all and it is not called genuine
    in.spamRating = 30;
    QCOMPARE(assess(in).score, 15);
    QCOMPARE(assess(in).band, Confidence::LooksGood);
    in.spamRating = 0;
    in.multipleAICH = true;
    QCOMPARE(assess(in).band, Confidence::Caution);
}

void tst_FakeFileDetector::nameGroups_sameContent_data()
{
    QTest::addColumn<QStringList>("names");

    // Names seen for one hash on the live network
    QTest::newRow("short title") << QStringList{
        QStringLiteral("La.trampa.(2024).(Spanish.English.Subs).WEBRip.1080p.x264-EAC3.by.xusman.mkv"),
        QStringLiteral("La trampa [MicroHD].mkv"),
        QStringLiteral("La trampa - 2024 (HD1080p).mkv")};
    QTest::newRow("year only in common") << QStringList{
        QStringLiteral("The Flash 2023 FullHD 1080p.H264 Ita Eng AC3 5.1 Sub Ita Eng realDMDJ.mkv"),
        QStringLiteral("2023 - The Flash (E.Miller,M.Keaton - 144').mkv"),
        QStringLiteral("TheFlash(2023).mkv")};
    QTest::newRow("translated, same episode") << QStringList{
        QStringLiteral("Linternas.1x08.Tierra.y.estrellas.(Spanish.English.Subs).WEBRip.1080p.x265.mkv"),
        QStringLiteral("Lanterns.S01.E08.[tmdbid-95350].Episode.8.mkv")};
    QTest::newRow("accents") << QStringList{
        QStringLiteral("Ancora.Una.Possibilita.One.More.Shot.2024.iTA.AC3.WebRip.1080p.x264-TBR.mkv"),
        QStringLiteral("Ancora una possibilità (One More Shot).mkv")};
    QTest::newRow("single name") << QStringList{QStringLiteral("ubuntu-24.04-desktop-amd64.iso")};
}

void tst_FakeFileDetector::nameGroups_sameContent()
{
    QFETCH(QStringList, names);
    QCOMPARE(countNameGroups(names), 1);

    FakeFileInput in = video(names.first());
    in.claimedType.clear();
    in.observedNames = names;
    QVERIFY(!assess(in).has(FakeReason::MultipleNames));
}

void tst_FakeFileDetector::nameGroups_unrelatedNames()
{
    // A real fake: one 700 MiB hash under all of these
    const QStringList names{
        QStringLiteral("Il Diavolo Veste Prada (2006 - David Frankel).avi"),
        QStringLiteral("Iron Maiden Discography.avi"),
        QStringLiteral("Karaoke.VanBasco.avi"),
        QStringLiteral("Le Cronache di Narnia,Il Principe Caspian [DVX-ITA].avi")};
    QCOMPARE(countNameGroups(names), 4);

    FakeFileInput in = video(names.first());
    in.observedNames = names;
    FakeFileVerdict v = assess(in);
    QVERIFY(v.reasons.contains({FakeReason::MultipleNames, 25}));
    QCOMPARE(v.band, Confidence::Caution);

    // Two groups weigh less: a translated title looks the same
    in.observedNames = names.mid(0, 2);
    v = assess(in);
    QVERIFY(v.reasons.contains({FakeReason::MultipleNames, 10}));
    QCOMPARE(v.band, Confidence::LooksGood);

    // Release words and numbers alone make no group
    QCOMPARE(countNameGroups({QStringLiteral("1080p.x264.mkv"), QStringLiteral("12345.avi")}), 0);
}

void tst_FakeFileDetector::nameGroups_keywordAloneDoesNotJoin()
{
    // Seen in the index: two unrelated long names with one word in common
    const QStringList names{
        QStringLiteral("Casual Summer - Irina and the old lighthouse keeper.avi"),
        QStringLiteral("Russian Summer school documentary part one.avi")};
    QCOMPARE(countNameGroups(names), 2);

    // Two shared words, or one that is most of a short name, still join
    QCOMPARE(countNameGroups({QStringLiteral("Summer Lighthouse Irina Casual.avi"),
                              QStringLiteral("irina - lighthouse (director's cut).avi")}), 1);
    QCOMPARE(countNameGroups({QStringLiteral("Matrix.avi"),
                              QStringLiteral("The Matrix 1999 german dubbed by somebody.avi")}), 1);

    // The search's own words are no link at all
    const QStringList pair{QStringLiteral("Matrix Cooking.avi"), QStringLiteral("Matrix Gardening.avi")};
    QCOMPARE(countNameGroups(pair), 1);
    QCOMPARE(countNameGroups(pair, searchKeywordTokens(QStringLiteral("MATRIX avi"))), 2);
    FakeFileInput in = video(pair.first());
    in.observedNames = pair;
    in.kadTrust = KadTrust::High;
    QCOMPARE(assess(in).band, Confidence::Genuine);
    in.ignoredNameWords = searchKeywordTokens(QStringLiteral("matrix"));
    QVERIFY(assess(in).has(FakeReason::MultipleNames));
    QCOMPARE(assess(in).band, Confidence::LooksGood);

    // A name that is nothing but the keyword says nothing
    QCOMPARE(countNameGroups({QStringLiteral("matrix.avi")}, searchKeywordTokens(QStringLiteral("matrix"))), 0);
}

void tst_FakeFileDetector::abuseNames_lowerTheScore()
{
    // A trade word: suspect, never hidden
    FakeFileVerdict v = assess(video(QStringLiteral("(PTHC) some name.avi")));
    QVERIFY(v.reasons.contains({FakeReason::AbuseContentName, 50}));
    QCOMPARE(v.band, Confidence::Suspect);
    QCOMPARE(abuseNameTier(QStringLiteral("x r@ygold y.mpg"), FakeFileRules::defaults()), 2);

    // A word with an innocent use: caution
    v = assess(video(QStringLiteral("Lolita (1962) Stanley Kubrick.mkv")));
    QVERIFY(v.reasons.contains({FakeReason::AbuseContentName, 25}));
    QCOMPARE(v.band, Confidence::Caution);
    QCOMPARE(abuseNameTier(QStringLiteral("some 12yo thing.avi"), FakeFileRules::defaults()), 1);

    // Whole words; adult names and ages from 16 up are not it
    for (const QString& name : {QStringLiteral("Teen Titans S01E02.mkv"),
                                QStringLiteral("Torpedo boats 1943.avi"),
                                QStringLiteral("Hot 18yo teens xxx.avi"),
                                QStringLiteral("Tu y yo 2.avi"),
                                QStringLiteral("English for kids 5 years old.pdf")})
        QCOMPARE(abuseNameTier(name, FakeFileRules::defaults()), 0);

    // Another name of the hash counts, and High trust cannot make it genuine
    FakeFileInput in = video();
    in.observedNames = {QStringLiteral("Some Film 2024 hussyfan.mkv")};
    in.kadTrust = KadTrust::High;
    QCOMPARE(assess(in).band, Confidence::Suspect);

    // [abuse] adds words; the built-in fake rules stay when it is the only section
    FakeFileRules rules = FakeFileRules::parse(QStringLiteral("[abuse]\nSome Bad-Word\n"));
    QCOMPARE(rules.abuseTokens, QStringList{QStringLiteral("some bad word")});
    QVERIFY(rules.tokens.isEmpty());
    QCOMPARE(abuseNameTier(QStringLiteral("x_some.bad.word_y.avi"), rules), 2);
}

void tst_FakeFileDetector::namesSpanKinds()
{
    FakeFileInput in = video(QStringLiteral("Il Diavolo Veste Prada (2006).avi"));
    in.observedNames = {QStringLiteral("Microsoft Office 2010 Pro.zip"),
                        QStringLiteral("Iron Maiden Discography.rar")};
    const FakeFileVerdict v = assess(in);
    QVERIFY(v.has(FakeReason::NamesSpanKinds));
    QVERIFY(v.has(FakeReason::MultipleNames));
    QCOMPARE(v.score, 50);
    QCOMPARE(v.band, Confidence::Suspect);

    // Audio and video are one kind: the soundtrack of a film is not a fake
    in.observedNames = {QStringLiteral("Il Diavolo Veste Prada.ac3")};
    QVERIFY(!assess(in).has(FakeReason::NamesSpanKinds));
}

void tst_FakeFileDetector::badSignalName_tokensAndRegex()
{
    QVERIFY(assess(video(QStringLiteral("Some Film FAKE.mkv"))).has(FakeReason::BadSignalName));
    QVERIFY(assess(video(QStringLiteral("Some.Film.wrong_file.mkv"))).has(FakeReason::BadSignalName));
    QVERIFY(assess(video(QStringLiteral("Some Film.mp4.exe"))).has(FakeReason::BadSignalName));
    // Whole words only
    QVERIFY(!assess(video(QStringLiteral("Fakers United.mkv"))).has(FakeReason::BadSignalName));
    // Seen live: every password recovery tool tripped a bare "password"
    QVERIFY(!assess(video(QStringLiteral("Advanced.Password.Recovery.2010.iso"))).has(FakeReason::BadSignalName));
    QVERIFY(assess(video(QStringLiteral("Some Film (password protected).rar"))).has(FakeReason::BadSignalName));

    // Another name of the hash counts too
    FakeFileInput in = video();
    in.observedNames = {QStringLiteral("Some Film 2024 virus.mkv")};
    QVERIFY(assess(in).has(FakeReason::BadSignalName));
}

void tst_FakeFileDetector::badSignalComment()
{
    FakeFileInput in = video();
    in.comments = {QStringLiteral("nice"), QStringLiteral("This is a FAKE, do not download")};
    const FakeFileVerdict v = assess(in);
    QVERIFY(v.reasons.contains({FakeReason::BadSignalComment, 15}));
    QVERIFY(!v.has(FakeReason::BadSignalName));
}

void tst_FakeFileDetector::claimedType_archiveAsProIsFine()
{
    FakeFileInput in;
    in.name = QStringLiteral("Some Program 3.1.rar");
    in.size = 80ull << 20;
    in.claimedType = QStringLiteral(ED2KFTSTR_PROGRAM);
    QCOMPARE(assess(in).score, 0);
}

void tst_FakeFileDetector::claimedType_mediaVsProgram()
{
    FakeFileInput in = video(QStringLiteral("Some Film 2024.rar"));
    QVERIFY(assess(in).reasons.contains({FakeReason::ClaimedTypeMismatch, 15}));

    in = video();
    in.claimedType = QStringLiteral(ED2KFTSTR_PROGRAM);
    QVERIFY(assess(in).has(FakeReason::ClaimedTypeMismatch));

    in.claimedType = QStringLiteral(ED2KFTSTR_AUDIO);
    QVERIFY(!assess(in).has(FakeReason::ClaimedTypeMismatch));
}

void tst_FakeFileDetector::spamAndRatings()
{
    FakeFileInput in = video();
    in.spamRating = 30;
    QVERIFY(assess(in).reasons.contains({FakeReason::SpamScore, 15}));
    QCOMPARE(assess(in).band, Confidence::LooksGood);

    in.spamRating = 60;
    in.consideredSpam = true;
    FakeFileVerdict v = assess(in);
    QVERIFY(v.reasons.contains({FakeReason::SpamScore, 25}));
    QVERIFY(v.has(FakeReason::SpamStatus));
    QCOMPARE(v.band, Confidence::Spam);

    in = video();
    in.userRating = 1;
    v = assess(in);
    QVERIFY(v.reasons.contains({FakeReason::FakeRating, 30}));
    QCOMPARE(v.band, Confidence::Caution);

    in.userRating = 0;
    in.kadNoteRatedFake = true;
    QVERIFY(assess(in).reasons.contains({FakeReason::BadRating, 20}));
}

void tst_FakeFileDetector::multipleAich()
{
    FakeFileInput in = video();
    in.multipleAICH = true;
    QVERIFY(assess(in).reasons.contains({FakeReason::MultipleAich, 35}));
}

void tst_FakeFileDetector::media_plausibility()
{
    // 2 GiB, 2 h, 2.3 Mbit/s: consistent
    FakeFileInput in = video();
    in.mediaLengthSec = 7200;
    in.mediaBitrateKbps = 2300;
    QCOMPARE(assess(in).score, 0);

    in.mediaLengthSec = 30;   // 2 GiB in 30 s
    FakeFileVerdict v = assess(in);
    QVERIFY(v.has(FakeReason::ImplausibleMediaLength));
    QVERIFY(v.has(FakeReason::MediaSizeMismatch));

    in.mediaLengthSec = 7200;
    in.mediaBitrateKbps = 20;
    v = assess(in);
    QVERIFY(v.has(FakeReason::ImplausibleMediaBitrate));
    QVERIFY(v.has(FakeReason::MediaSizeMismatch));

    // A 26.7 h audiobook at 64 kbit/s: long, and the size pays for it
    FakeFileInput book;
    book.name = QStringLiteral("Some Novel (Full-Cast Edition).mp3");
    book.size = 768874252;
    book.mediaLengthSec = 96004;
    book.mediaBitrateKbps = 64;
    QCOMPARE(assess(book).score, 0);
    // 30 h of video in 60 MiB is not a film
    in = video();
    in.size = 60ull << 20;
    in.mediaLengthSec = 30 * 3600;
    in.mediaBitrateKbps = 0;
    QVERIFY(assess(in).has(FakeReason::ImplausibleMediaLength));

    // Not media: tags say nothing
    FakeFileInput arc;
    arc.name = QStringLiteral("thing.rar");
    arc.size = 2ull << 30;
    arc.mediaLengthSec = 30;
    arc.mediaBitrateKbps = 20;
    QCOMPARE(assess(arc).score, 0);
}

void tst_FakeFileDetector::media_tagMismatch()
{
    FakeFileInput in;
    in.name = QStringLiteral("Queen - Bohemian Rhapsody.mp3");
    in.size = 6ull << 20;
    in.artist = QStringLiteral("Queen");
    in.title = QStringLiteral("Track 01");
    QVERIFY(!assess(in).has(FakeReason::NameMediaTagMismatch));

    in.artist = QStringLiteral("Somebody Else");
    in.title = QStringLiteral("Another Song");
    QVERIFY(assess(in).reasons.contains({FakeReason::NameMediaTagMismatch, 10}));

    // Too short to mean anything
    in.artist = QStringLiteral("abc");
    in.title.clear();
    QVERIFY(!assess(in).has(FakeReason::NameMediaTagMismatch));

    // An archive's tags describe a track inside it, not the archive
    in.name = QStringLiteral("The Boston Discography.rar");
    in.artist = QStringLiteral("Somebody Else");
    QVERIFY(!assess(in).has(FakeReason::NameMediaTagMismatch));
}

void tst_FakeFileDetector::header_mismatchAndMasquerade()
{
    FakeFileInput in = video(QStringLiteral("Some Film 2024.avi"));
    in.container = checkHead(QByteArray("MZ\x90\x00\x03\x00\x00\x00\x04\x00\x00\x00", 12), in.name);
    FakeFileVerdict v = assess(in);
    QVERIFY(v.reasons.contains({FakeReason::HeaderExtensionMismatch, 45}));
    QVERIFY(v.reasons.contains({FakeReason::ExecutableMasquerade, 25}));
    QCOMPARE(v.score, 70);
    QCOMPARE(v.band, Confidence::Suspect);

    in.container = checkHead(QByteArray("Rar!\x1A\x07\x00\x00\x00\x00\x00\x00", 12), in.name);
    v = assess(in);
    QVERIFY(v.has(FakeReason::ArchiveMasquerade));
    QVERIFY(!v.has(FakeReason::ExecutableMasquerade));

    // Another media container: wrong, but no masquerade
    in.container = checkHead(QByteArray("\x1A\x45\xDF\xA3\x00\x00\x00\x00\x00\x00\x00\x00", 12), in.name);
    v = assess(in);
    QCOMPARE(v.score, 45);

    in.container = checkHead(QByteArray("RIFF\x00\x00\x00\x00" "AVI ", 12), in.name);
    QCOMPARE(assess(in).score, 0);
}

void tst_FakeFileDetector::score_isCapped()
{
    FakeFileInput in = video(QStringLiteral("Some Film fake.avi"));
    in.container = checkHead(QByteArray("MZ\x90\x00\x03\x00\x00\x00\x04\x00\x00\x00", 12), in.name);
    in.multipleAICH = true;
    in.userRating = 1;
    const FakeFileVerdict v = assess(in);
    QCOMPARE(v.score, 100);
    QCOMPARE(v.band, Confidence::LikelyFake);
}

void tst_FakeFileDetector::rules_parse()
{
    QStringList errors;
    const FakeFileRules rules = FakeFileRules::parse(QStringLiteral(
        "# comment\n"
        "ignored before a section\n"
        "[tokens]\n"
        "  Bad Word \n"
        "nuke\n"
        "[regex]\n"
        "\\.mkv\\.lnk$\n"
        "([unclosed\n"
        "[other]\n"
        "skipped\n"), &errors);

    QCOMPARE(rules.tokens, (QStringList{QStringLiteral("bad word"), QStringLiteral("nuke")}));
    QCOMPARE(rules.regexes.size(), 1);
    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.first().startsWith(QStringLiteral("([unclosed")));

    QVERIFY(rules.matches(QStringLiteral("a_BAD-word_here.avi")));
    QVERIFY(rules.matches(QStringLiteral("Film.MKV.lnk")));
    QVERIFY(!rules.matches(QStringLiteral("Film fake.mkv")));   // not in this file

    // A file that does not exist falls back to the built-in rules
    const FakeFileRules fallback = FakeFileRules::load(QStringLiteral("/nonexistent/FakeFileFilter.dat"));
    QVERIFY(fallback.matches(QStringLiteral("Film fake.mkv")));
}

void tst_FakeFileDetector::kadTrust_decode()
{
    QCOMPARE(kadTrustFromPublishInfo(0), KadTrust::Unknown);
    // names << 24 | publishers << 16 | trust x100
    QCOMPARE(kadTrustFromPublishInfo((1u << 24) | (1u << 16) | 50), KadTrust::Low);
    QCOMPARE(kadTrustFromPublishInfo((2u << 24) | (9u << 16) | 250), KadTrust::Normal);
    QCOMPARE(kadTrustFromPublishInfo((2u << 24) | (40u << 16) | 300), KadTrust::High);
}

void tst_FakeFileDetector::ids_areStable()
{
    QCOMPARE(fakeReasonId(FakeReason::MultipleNames), QStringLiteral("multiple_names"));
    QCOMPARE(fakeReasonId(FakeReason::NameMediaTagMismatch), QStringLiteral("name_media_tag_mismatch"));
    QCOMPARE(fakeReasonId(FakeReason::AbuseContentName), QStringLiteral("abuse_content_name"));
    QCOMPARE(confidenceId(Confidence::LikelyFake), QStringLiteral("likely_fake"));
    QCOMPARE(confidenceId(Confidence::Genuine), QStringLiteral("genuine"));
    QVERIFY(Confidence::Spam < Confidence::Genuine);
}

// Calibration aid, not a check: EMULE_FAKE_SAMPLE=<json> prints the band of every
// file of a sample ([{h, type, names: [[name, count], ...]}, ...]).
void tst_FakeFileDetector::sample_replay()
{
    const QString path = qEnvironmentVariable("EMULE_FAKE_SAMPLE");
    if (path.isEmpty())
        QSKIP("EMULE_FAKE_SAMPLE not set");
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));

    QMap<QString, int> bands;
    const QJsonArray sample = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue& value : sample) {
        const QJsonObject o = value.toObject();
        FakeFileInput in;
        int best = -1;
        for (const QJsonValue& n : o.value(QStringLiteral("names")).toArray()) {
            const QJsonArray pair = n.toArray();
            in.observedNames.push_back(pair.at(0).toString());
            if (pair.at(1).toInt() > best) {
                best = pair.at(1).toInt();
                in.name = pair.at(0).toString();
            }
        }
        in.claimedType = o.value(QStringLiteral("type")).toString();
        const FakeFileVerdict v = assess(in);
        ++bands[confidenceId(v.band)];
        if (v.score > 0) {
            qInfo().noquote() << o.value(QStringLiteral("h")).toString().left(8) << v.score
                              << confidenceId(v.band) << in.observedNames.size() << "names"
                              << v.reasonIds().join(QLatin1Char(','));
        }
    }
    qInfo() << "bands:" << bands;
}

QTEST_GUILESS_MAIN(tst_FakeFileDetector)
#include "tst_FakeFileDetector.moc"
