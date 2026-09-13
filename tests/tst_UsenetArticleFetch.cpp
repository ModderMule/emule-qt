/// @file tst_UsenetArticleFetch.cpp
/// @brief BODY -> yEnc decode -> sparse write, assembled into a whole file.
///
/// This is phase 2's exit criterion in miniature: a multi-part file fetched
/// article by article from a real NNTP conversation and reassembled
/// byte-identically. Everything below the fetcher has its own unit test; what
/// this covers is the joins between them, where the interesting bugs live:
///
///   - Parts are fetched **out of order** on purpose. Segments genuinely arrive
///     that way once a queue runs several connections, and a writer that only
///     works front-to-back passes an in-order test and corrupts every real
///     download.
///   - The `=ypart begin` 1-based correction has to survive the whole chain.
///     An off-by-one here still passes every CRC — pcrc32 verifies the payload,
///     not where it lands — so only a byte-for-byte comparison catches it.
///   - A missing article must come back as ArticleNotFound and leave the
///     connection usable, because that is the signal the queue escalates on.

#include "FakeNntpServer.h"

#include "decode/YencDecoder.h"
#include "nntp/ArticleFetcher.h"
#include "nntp/NntpSocket.h"
#include "queue/ArticleWriter.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule::usenet;
using eMule::testing::FakeNntpServer;

namespace {

constexpr int kPartSize = 3000;
constexpr int kParts = 5;

QByteArray payload(int size)
{
    QByteArray data;
    data.resize(size);
    for (int i = 0; i < size; ++i)
        data[i] = static_cast<char>((i * 31 + (i >> 5) * 7) & 0xFF);
    return data;
}

quint32 crc32Of(QByteArrayView data)
{
    return yencCrc32(0, data);
}

/// One yEnc article body, exactly as a provider would serve it — including the
/// 1-based `begin`.
QByteArray makeArticle(const QByteArray& whole, int partNumber, int partCount,
                       const QString& name)
{
    const int offset = (partNumber - 1) * kPartSize;
    const QByteArray part = whole.mid(offset, kPartSize);

    QByteArray out;
    out += QStringLiteral("=ybegin part=%1 total=%2 line=128 size=%3 name=%4")
               .arg(partNumber).arg(partCount).arg(whole.size()).arg(name).toLatin1();
    out += '\n';
    // begin is 1-based; end is inclusive.
    out += QStringLiteral("=ypart begin=%1 end=%2")
               .arg(offset + 1).arg(offset + part.size()).toLatin1();
    out += '\n';

    QByteArray line;
    for (const char raw : part) {
        const auto enc = static_cast<quint8>(static_cast<quint8>(raw) + 42);
        if (enc == 0x00 || enc == 0x0A || enc == 0x0D || enc == '=') {
            line.append('=');
            line.append(static_cast<char>(static_cast<quint8>(enc + 64)));
        } else {
            line.append(static_cast<char>(enc));
        }
        if (line.size() >= 128) {
            out += line;
            out += '\n';
            line.clear();
        }
    }
    if (!line.isEmpty()) {
        out += line;
        out += '\n';
    }

    out += QStringLiteral("=yend size=%1 part=%2 pcrc32=%3")
               .arg(part.size()).arg(partNumber)
               .arg(crc32Of(part), 8, 16, QLatin1Char('0')).toLatin1();
    return out;
}

QString messageIdFor(int part)
{
    return QStringLiteral("part%1@example.com").arg(part);
}

} // namespace

class tst_UsenetArticleFetch : public QObject {
    Q_OBJECT

private slots:
    void assemblesAMultiPartFileOutOfOrder();
    void missingArticleEscalatesAndKeepsTheConnection();
    void obfuscatedNameComesFromTheArticle();
    void aCrcMismatchIsReportedAsCorrupt();
};

void tst_UsenetArticleFetch::assemblesAMultiPartFileOutOfOrder()
{
    const QByteArray whole = payload(kPartSize * kParts);

    FakeNntpServer server;
    for (int part = 1; part <= kParts; ++part)
        server.addArticle(messageIdFor(part), makeArticle(whole, part, kParts,
                                                          QStringLiteral("big.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NewsServer config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = port;
    config.tlsMode = TlsMode::None;
    config.user = QStringLiteral("testuser");
    config.pass = QStringLiteral("testpass");

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(config);
    QVERIFY(ready.wait(5000));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("big.bin"));

    ArticleWriter writer;
    QString error;
    QVERIFY2(writer.open(path, error), qPrintable(error));
    // Size the file once, up front: extending it article by article is what
    // fragments a multi-GB release.
    QVERIFY2(writer.reserve(whole.size(), error), qPrintable(error));

    // Deliberately scrambled. Real segments arrive this way, and a writer that
    // silently assumed order would pass an in-order test and corrupt every
    // real download.
    const QList<int> order{3, 1, 5, 2, 4};
    for (const int part : order) {
        NzbSegment segment;
        segment.messageId = messageIdFor(part);
        segment.number = part;

        ArticleFetcher fetcher;
        QSignalSpy done(&fetcher, &ArticleFetcher::finished);
        fetcher.fetch(&socket, segment, &writer);
        QVERIFY(done.wait(5000));

        QCOMPARE(done.first().at(0).value<NntpError>(), NntpError::None);
        QCOMPARE(fetcher.articleFileName(), QStringLiteral("big.bin"));
        QCOMPARE(fetcher.declaredFileSize(), qint64(whole.size()));
        QCOMPARE(fetcher.decodedBytes(), qint64(kPartSize));
    }

    QVERIFY2(writer.flush(error), qPrintable(error));
    writer.close();

    QFile out(path);
    QVERIFY(out.open(QIODevice::ReadOnly));
    const QByteArray assembled = out.readAll();

    QCOMPARE(assembled.size(), whole.size());
    // Byte-for-byte, not a hash of the parts: an off-by-one from the 1-based
    // `begin` shifts everything while every pcrc32 still passes.
    QCOMPARE(assembled, whole);
}

void tst_UsenetArticleFetch::missingArticleEscalatesAndKeepsTheConnection()
{
    const QByteArray whole = payload(kPartSize);

    FakeNntpServer server;
    server.addArticle(messageIdFor(1), makeArticle(whole, 1, 1, QStringLiteral("x.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NewsServer config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = port;
    config.tlsMode = TlsMode::None;
    config.user = QStringLiteral("testuser");
    config.pass = QStringLiteral("testpass");

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(config);
    QVERIFY(ready.wait(5000));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ArticleWriter writer;
    QString error;
    QVERIFY(writer.open(dir.filePath(QStringLiteral("x.bin")), error));

    NzbSegment missing;
    missing.messageId = QStringLiteral("nope@example.com");
    missing.number = 1;

    ArticleFetcher fetcher;
    QSignalSpy done(&fetcher, &ArticleFetcher::finished);
    fetcher.fetch(&socket, missing, &writer);
    QVERIFY(done.wait(5000));

    const auto error1 = done.first().at(0).value<NntpError>();
    QCOMPARE(error1, NntpError::ArticleNotFound);
    // The two properties the queue routes on: ask a different *level*, and keep
    // this connection for the next article.
    QVERIFY(escalatesToNextLevel(error1));
    QVERIFY(!isFatalToConnection(error1));
    QVERIFY(socket.isReady());

    // And the same connection still serves a real article afterwards.
    NzbSegment present;
    present.messageId = messageIdFor(1);
    present.number = 1;

    ArticleFetcher second;
    QSignalSpy done2(&second, &ArticleFetcher::finished);
    second.fetch(&socket, present, &writer);
    QVERIFY(done2.wait(5000));
    QCOMPARE(done2.first().at(0).value<NntpError>(), NntpError::None);
}

void tst_UsenetArticleFetch::obfuscatedNameComesFromTheArticle()
{
    // An obfuscated post's subject carries no filename; the real one only
    // appears in the first article's "=ybegin name=". The queue reads it from
    // the fetcher, so it has to survive the whole chain.
    const QByteArray whole = payload(kPartSize);

    FakeNntpServer server;
    server.addArticle(QStringLiteral("xy8@example"),
                      makeArticle(whole, 1, 1, QStringLiteral("The.Real.Name.mkv")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NewsServer config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = port;
    config.tlsMode = TlsMode::None;
    config.user = QStringLiteral("testuser");
    config.pass = QStringLiteral("testpass");

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(config);
    QVERIFY(ready.wait(5000));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ArticleWriter writer;
    QString error;
    QVERIFY(writer.open(dir.filePath(QStringLiteral("scratch.bin")), error));

    NzbSegment segment;
    segment.messageId = QStringLiteral("xy8@example");
    segment.number = 1;

    ArticleFetcher fetcher;
    QSignalSpy done(&fetcher, &ArticleFetcher::finished);
    fetcher.fetch(&socket, segment, &writer);
    QVERIFY(done.wait(5000));

    QCOMPARE(done.first().at(0).value<NntpError>(), NntpError::None);
    QCOMPARE(fetcher.articleFileName(), QStringLiteral("The.Real.Name.mkv"));
}

// A damaged copy is a *content* fault: its own error, and — the part that used
// to be wrong — the connection survives it. The body was read to its terminating
// "." before the CRC was even checked, so the very same socket serves the next
// article. Calling it a protocol error dropped the connection and backed the
// whole account off for a minute over one bad article.
void tst_UsenetArticleFetch::aCrcMismatchIsReportedAsCorrupt()
{
    const QByteArray whole = payload(kPartSize);
    QByteArray damaged = makeArticle(whole, 1, 1, QStringLiteral("x.bin"));
    const qsizetype crcAt = damaged.lastIndexOf("pcrc32=") + 7;
    damaged.replace(crcAt, 8, "deadbeef");

    FakeNntpServer server;
    server.addArticle(QStringLiteral("bad@example.com"), damaged);
    server.addArticle(messageIdFor(1), makeArticle(whole, 1, 1, QStringLiteral("x.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    NewsServer config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = port;
    config.tlsMode = TlsMode::None;
    config.user = QStringLiteral("testuser");
    config.pass = QStringLiteral("testpass");

    NntpSocket socket;
    QSignalSpy ready(&socket, &NntpSocket::ready);
    socket.connectToServer(config);
    QVERIFY(ready.wait(5000));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ArticleWriter writer;
    QString error;
    QVERIFY(writer.open(dir.filePath(QStringLiteral("x.bin")), error));

    NzbSegment bad;
    bad.messageId = QStringLiteral("bad@example.com");
    bad.number = 1;

    ArticleFetcher fetcher;
    QSignalSpy done(&fetcher, &ArticleFetcher::finished);
    fetcher.fetch(&socket, bad, &writer);
    QVERIFY(done.wait(5000));
    QCOMPARE(done.first().at(0).value<NntpError>(), NntpError::ArticleCorrupt);
    QVERIFY(!isFatalToConnection(NntpError::ArticleCorrupt));
    QVERIFY(socket.isReady());

    // The same connection, still in sync: the good copy of another article comes
    // straight down it. This is what the account backoff used to cost.
    NzbSegment good;
    good.messageId = messageIdFor(1);
    good.number = 1;
    QSignalSpy doneGood(&fetcher, &ArticleFetcher::finished);
    fetcher.fetch(&socket, good, &writer);
    QVERIFY(doneGood.wait(5000));
    QCOMPARE(doneGood.first().at(0).value<NntpError>(), NntpError::None);
    QCOMPARE(server.connectionCount(), 1);

    // A 430 is not corruption either.
    NzbSegment missing;
    missing.messageId = QStringLiteral("nope@example.com");
    missing.number = 1;
    QSignalSpy doneMissing(&fetcher, &ArticleFetcher::finished);
    fetcher.fetch(&socket, missing, &writer);
    QVERIFY(doneMissing.wait(5000));
    QCOMPARE(doneMissing.first().at(0).value<NntpError>(), NntpError::ArticleNotFound);
}

QTEST_MAIN(tst_UsenetArticleFetch)
#include "tst_UsenetArticleFetch.moc"
