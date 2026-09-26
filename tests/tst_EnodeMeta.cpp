/// @file tst_EnodeMeta.cpp
/// @brief eNode meta hash, metafile identity, gRPC-Web framing, account store.
///
/// Hash and NZB vectors are the cross-repository ones from
/// external/enodemeta/testdata — every implementation must reproduce them.

#include "TestHelpers.h"
#include "enodemeta/GrpcWeb.h"
#include "enodemeta/MetaAccountStore.h"
#include "enodemeta/MetaApiClient.h"
#include "enodemeta/MetaHash.h"
#include "enodemeta/MetaIdentity.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QtProtobuf/QProtobufSerializer>

using namespace eMule;
using namespace eMule::enodemeta;

namespace {

QJsonObject loadVectors(const char* name)
{
    QFile f(QStringLiteral(ENODEMETA_TESTDATA "/%1").arg(QString::fromLatin1(name)));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QByteArray nzbOf(const QJsonObject& v)
{
    if (v.contains(u"nzbHex"))
        return QByteArray::fromHex(v.value(u"nzbHex").toString().toLatin1());
    return v.value(u"nzb").toString().toUtf8();
}

QByteArray hex(const QByteArray& b)
{
    return b.toHex().toUpper();
}

QByteArray digestHex(const Digest& d)
{
    return hex(QByteArray(reinterpret_cast<const char*>(d.data()), kDigestSize));
}

// minimal protobuf helpers to hand-build a google.rpc.Status
QByteArray varint(quint64 v)
{
    QByteArray out;
    do {
        uint8_t b = v & 0x7F;
        v >>= 7;
        if (v)
            b |= 0x80;
        out.append(static_cast<char>(b));
    } while (v);
    return out;
}

QByteArray lenField(int field, const QByteArray& payload)
{
    return varint(static_cast<quint64>(field) << 3 | 2) + varint(static_cast<quint64>(payload.size())) + payload;
}

QByteArray trailerFrame(const QByteArray& text)
{
    QByteArray f = grpcweb::frame(text);
    f[0] = static_cast<char>(0x80);
    return f;
}

} // namespace

class tst_EnodeMeta : public QObject {
    Q_OBJECT

private slots:
    void fold_vectors();
    void build_vectors();
    void reject_vectors();
    void crossCheck_tags();
    void sameRelease_ignoresIndex();
    void nzb_identityVectors();
    void nzb_sameIdentity();
    void nzb_reject();
    void torrent_v1Identity();
    void torrent_bareInfoDict();
    void torrent_v2AndHybrid();
    void verifyMetaFile_matchAndMismatch();
    void grpcweb_frameRoundTrip();
    void grpcweb_trailersOnly();
    void grpcweb_truncated();
    void grpcweb_errorInfoDetail();
    void accountStore_roundTrip();
    void credentials_onlyOverTls();
};

void tst_EnodeMeta::fold_vectors()
{
    const auto v = loadVectors("meta-hash-vectors.json");
    const auto folds = v.value(u"fold").toArray();
    QVERIFY(!folds.isEmpty());
    for (const auto& e : folds) {
        const auto o = e.toObject();
        if (o.value(u"width").toInt() != kDigestSize)
            continue;
        const QByteArray in = QByteArray::fromHex(o.value(u"inputHex").toString().toLatin1());
        QCOMPARE(digestHex(fold10(in)), o.value(u"outputHex").toString().toLatin1());
    }
}

void tst_EnodeMeta::build_vectors()
{
    const auto builds = loadVectors("meta-hash-vectors.json").value(u"build").toArray();
    QVERIFY(builds.size() > 5);
    int checked = 0;
    for (const auto& e : builds) {
        const auto o = e.toObject();
        const QString label = o.value(u"label").toString();
        const QByteArray identity = QByteArray::fromHex(o.value(u"identityHex").toString().toLatin1());
        const auto kind = static_cast<Kind>(o.value(u"kind").toInt());
        const auto flags = static_cast<uint8>(o.value(u"flags").toInt());
        const auto index = static_cast<uint32>(o.value(u"fileIndex").toDouble());
        const auto h = build(kind, flags, index, identity);

        if (!o.contains(u"hashHex")) {
            QVERIFY2(!h, qPrintable(label));
            continue;
        }
        QVERIFY2(h, qPrintable(label));
        const QByteArray got(reinterpret_cast<const char*>(h->data()), kMetaHashSize);
        QCOMPARE(hex(got), o.value(u"hashHex").toString().toLatin1());

        // and back
        const auto p = parse(h->data());
        QVERIFY(p);
        QCOMPARE(p->kind, kind);
        QCOMPARE(p->flags, flags);
        QVERIFY(verifyIdentity(h->data(), identity));
        ++checked;
    }
    QVERIFY(checked > 5);
}

void tst_EnodeMeta::reject_vectors()
{
    const auto rejects = loadVectors("meta-hash-vectors.json").value(u"reject").toArray();
    QVERIFY(!rejects.isEmpty());
    for (const auto& e : rejects) {
        const auto o = e.toObject();
        const QByteArray h = QByteArray::fromHex(o.value(u"hashHex").toString().toLatin1());
        const auto p = parse(std::span(reinterpret_cast<const uint8*>(h.constData()),
                                       static_cast<size_t>(h.size())));
        QVERIFY2(!p, qPrintable(o.value(u"label").toString()));
    }
}

void tst_EnodeMeta::crossCheck_tags()
{
    const QByteArray id = QByteArray::fromHex("0CEC613B424DC858488612F7571BEFAF67C52616");
    const auto h = build(Kind::BtV1, FlagMultiFile, 5, id);
    QVERIFY(h);
    const auto p = parse(h->data());
    QVERIFY(p);
    QVERIFY(crossCheck(*p, 1, 1, 5));
    QVERIFY(!crossCheck(*p, 3, 1, 5));          // kind disagrees
    QVERIFY(!crossCheck(*p, 1, 2, 5));          // version disagrees
    QVERIFY(!crossCheck(*p, 1, 1, 6));          // index disagrees
    QVERIFY(crossCheck(*p, 1, 1, 0x10005));     // only the low 16 bits are in the hash
}

void tst_EnodeMeta::sameRelease_ignoresIndex()
{
    const QByteArray id = QByteArray::fromHex("0CEC613B424DC858488612F7571BEFAF67C52616");
    const auto a = build(Kind::BtV1, FlagMultiFile, 1, id);
    const auto b = build(Kind::BtV1, FlagMultiFile, 2, id);
    QVERIFY(a && b);
    QVERIFY(sameRelease(a->data(), b->data()));
}

void tst_EnodeMeta::nzb_identityVectors()
{
    const auto vectors = loadVectors("nzb-identity-vectors.json").value(u"identity").toArray();
    QVERIFY(vectors.size() >= 10);
    for (const auto& e : vectors) {
        const auto o = e.toObject();
        const QString label = o.value(u"label").toString();
        const auto pre = nzbIdentityPreimage(nzbOf(o));
        QVERIFY2(pre, qPrintable(label + u": " + (pre ? QString() : pre.error())));
        QCOMPARE(hex(*pre), o.value(u"preimageHex").toString().toLatin1());
        const auto id = nzbIdentity(nzbOf(o));
        QVERIFY(id);
        QCOMPARE(hex(*id), o.value(u"digestHex").toString().toLatin1());

        // the whole-set row minted from it verifies against the document
        const QByteArray whole = QByteArray::fromHex(o.value(u"wholeSetMetaHashHex").toString().toLatin1());
        if (whole.size() == kMetaHashSize)
            QVERIFY2(verifyMetaFile(reinterpret_cast<const uint8*>(whole.constData()), nzbOf(o)),
                     qPrintable(label));
    }
}

void tst_EnodeMeta::nzb_sameIdentity()
{
    const auto pairs = loadVectors("nzb-identity-vectors.json").value(u"sameIdentity").toArray();
    QVERIFY(!pairs.isEmpty());
    for (const auto& e : pairs) {
        const auto o = e.toObject();
        const auto a = nzbIdentity(o.value(u"a").toString().toUtf8());
        const auto b = nzbIdentity(o.value(u"b").toString().toUtf8());
        QVERIFY2(a && b, qPrintable(o.value(u"label").toString()));
        QCOMPARE(*a, *b);
    }
}

void tst_EnodeMeta::nzb_reject()
{
    const auto rejects = loadVectors("nzb-identity-vectors.json").value(u"reject").toArray();
    QVERIFY(!rejects.isEmpty());
    for (const auto& e : rejects) {
        const auto o = e.toObject();
        QVERIFY2(!nzbIdentity(nzbOf(o)), qPrintable(o.value(u"label").toString()));
    }
}

void tst_EnodeMeta::torrent_v1Identity()
{
    const QByteArray info = "d6:lengthi5e4:name1:a12:piece lengthi16384e6:pieces20:AAAAAAAAAAAAAAAAAAAAe";
    const QByteArray t = "d8:announce9:udp://x:14:info" + info + "e";

    const auto id = torrentIdentity(t);
    QVERIFY2(id, qPrintable(id ? QString() : id.error()));
    QCOMPARE(id->kind, Kind::BtV1);
    QCOMPARE(id->identity, QCryptographicHash::hash(info, QCryptographicHash::Sha1));
    QVERIFY(id->infoHashV2.isEmpty());
}

void tst_EnodeMeta::torrent_bareInfoDict()
{
    const QByteArray info = "d6:lengthi5e4:name1:a12:piece lengthi16384e6:pieces20:AAAAAAAAAAAAAAAAAAAAe";
    const auto id = torrentIdentity(info);
    QVERIFY(id);
    QCOMPARE(id->identity, QCryptographicHash::hash(info, QCryptographicHash::Sha1));
    QVERIFY(!torrentIdentity("d4:info"));   // truncated
    QVERIFY(!torrentIdentity("not bencode"));
}

void tst_EnodeMeta::torrent_v2AndHybrid()
{
    const QByteArray v2 = "d9:file treed1:ad0:d6:lengthi5eeee12:meta versioni2e4:name1:a12:piece lengthi16384ee";
    auto id = torrentIdentity(v2);
    QVERIFY2(id, qPrintable(id ? QString() : id.error()));
    QCOMPARE(id->kind, Kind::BtV2);
    QCOMPARE(id->identity, QCryptographicHash::hash(v2, QCryptographicHash::Sha256));

    const QByteArray hybrid = "d9:file treed1:ad0:d6:lengthi5eeee6:lengthi5e12:meta versioni2e4:name1:a"
                              "12:piece lengthi16384e6:pieces20:AAAAAAAAAAAAAAAAAAAAe";
    id = torrentIdentity(hybrid);
    QVERIFY(id);
    QCOMPARE(id->kind, Kind::BtV1);   // hybrid is identified by its v1 hash
    QCOMPARE(id->infoHashV2.size(), 32);
}

void tst_EnodeMeta::verifyMetaFile_matchAndMismatch()
{
    const QByteArray info = "d6:lengthi5e4:name1:a12:piece lengthi16384e6:pieces20:AAAAAAAAAAAAAAAAAAAAe";
    const auto h = build(Kind::BtV1, 0, 0, QCryptographicHash::hash(info, QCryptographicHash::Sha1));
    QVERIFY(h);
    QVERIFY(verifyMetaFile(h->data(), info));

    QByteArray other = info;
    other.replace("1:a", "1:b");
    QVERIFY(!verifyMetaFile(h->data(), other));

    // a torrent served for an nzb row is refused
    auto nzbHash = *h;
    nzbHash[3] = static_cast<uint8>(0x30 | FlagPathAuthoritative);
    QVERIFY(!verifyMetaFile(nzbHash.data(), info));
}

void tst_EnodeMeta::grpcweb_frameRoundTrip()
{
    pb::MetaFile file;
    file.setContent("d4:infod4:name1:aee");
    file.setContentType(QStringLiteral("application/x-bittorrent"));
    QProtobufSerializer ser;
    const QByteArray msg = file.serialize(&ser);

    const QByteArray body = grpcweb::frame(msg) + trailerFrame("grpc-status: 0\r\ngrpc-message: \r\n");
    const auto r = grpcweb::parseResponse(body, {});
    QVERIFY2(r, qPrintable(r ? QString() : r.error()));
    QCOMPARE(r->status, 0);
    QVERIFY(r->data);
    pb::MetaFile back;
    QVERIFY(back.deserialize(&ser, *r->data));
    QCOMPARE(back.content(), file.content());
    QCOMPARE(back.contentType(), file.contentType());

    // request frame header: flag 0, big-endian length
    const QByteArray f = grpcweb::frame("abc");
    QCOMPARE(f, QByteArray("\x00\x00\x00\x00\x03" "abc", 8));
}

void tst_EnodeMeta::grpcweb_trailersOnly()
{
    const grpcweb::HeaderList headers{{"Grpc-Status", "5"}, {"Grpc-Message", "metafile%20not%20found"}};
    const auto r = grpcweb::parseResponse({}, headers);
    QVERIFY(r);
    QCOMPARE(r->status, int(grpcweb::NotFound));
    QCOMPARE(r->message, QStringLiteral("metafile not found"));
    QVERIFY(!r->data);

    QVERIFY(!grpcweb::parseResponse({}, {}));   // no status anywhere
}

void tst_EnodeMeta::grpcweb_truncated()
{
    QByteArray body = grpcweb::frame("abcdef");
    body.chop(2);
    QVERIFY(!grpcweb::parseResponse(body, {}));
    QVERIFY(!grpcweb::parseResponse(QByteArray("\x00\x00", 2), {}));
    // two data frames in a unary reply
    QVERIFY(!grpcweb::parseResponse(grpcweb::frame("a") + grpcweb::frame("b")
                                        + trailerFrame("grpc-status: 0\r\n"), {}));
}

void tst_EnodeMeta::grpcweb_errorInfoDetail()
{
    pb::PendingStep step;
    step.setId_proto(QStringLiteral("payment"));
    step.setTitle(QStringLiteral("Membership"));
    step.setUrl(QStringLiteral("https://srv.example/account/step/payment"));
    pb::ErrorInfo info;
    info.setMsgCode(QStringLiteral("account.pending"));
    info.setAccountUrl(QStringLiteral("https://srv.example/account"));
    info.setPendingSteps({step});
    QProtobufSerializer ser;

    const QByteArray any = lenField(1, "type.googleapis.com/enode.meta.v1.ErrorInfo")
                         + lenField(2, info.serialize(&ser));
    const QByteArray status = varint(1 << 3 | 0) + varint(7)
                            + lenField(2, "account pending") + lenField(3, any);
    QByteArray b64 = status.toBase64();
    while (b64.endsWith('='))
        b64.chop(1);   // gRPC sends it unpadded

    const auto r = grpcweb::parseResponse(
        trailerFrame("grpc-status: 7\r\ngrpc-message: account%20pending\r\ngrpc-status-details-bin: "
                     + b64 + "\r\n"), {});
    QVERIFY(r);
    QCOMPARE(r->status, int(grpcweb::PermissionDenied));
    const auto raw = grpcweb::statusDetail(r->statusDetails, "enode.meta.v1.ErrorInfo");
    QVERIFY(raw);
    pb::ErrorInfo back;
    QVERIFY(back.deserialize(&ser, *raw));
    QCOMPARE(back.msgCode(), QStringLiteral("account.pending"));
    QCOMPARE(back.pendingSteps().size(), 1);
    QCOMPARE(back.pendingSteps().first().url(), step.url());
    QVERIFY(!grpcweb::statusDetail(r->statusDetails, "other.Type"));
}

void tst_EnodeMeta::accountStore_roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("metaaccounts.yml"));
    const QString origin = QStringLiteral("https://srv.example:4671");
    {
        MetaAccountStore store(path);
        QVERIFY(!store.account(origin).hasToken());
        store.setAccount(origin, {QStringLiteral("alice"), QStringLiteral("tok123"), 0});
    }
    MetaAccountStore store(path);
    QCOMPARE(store.account(origin).username, QStringLiteral("alice"));
    QCOMPARE(store.account(origin).token, QStringLiteral("tok123"));
    QVERIFY(QFile::permissions(path) & QFileDevice::ReadOwner);
    QVERIFY(!(QFile::permissions(path) & QFileDevice::ReadOther));

    store.clearToken(origin);
    QVERIFY(!store.account(origin).hasToken());
    QCOMPARE(store.account(origin).username, QStringLiteral("alice"));   // kept for the form

    store.setAccount(origin, {QStringLiteral("alice"), QStringLiteral("old"), 1000});   // long expired
    QVERIFY(!store.account(origin).hasToken());
}

void tst_EnodeMeta::credentials_onlyOverTls()
{
    QVERIFY(MetaApiClient::credentialsAllowed(QUrl(QStringLiteral("https://srv.example:4671"))));
    QVERIFY(MetaApiClient::credentialsAllowed(QUrl(QStringLiteral("http://127.0.0.1:4671"))));
    QVERIFY(MetaApiClient::credentialsAllowed(QUrl(QStringLiteral("http://localhost:4671"))));
    QVERIFY(!MetaApiClient::credentialsAllowed(QUrl(QStringLiteral("http://203.0.113.5:4671"))));

    MetaEndpoint ep{QStringLiteral("HTTPS://Srv.Example/api"), {}, {}};
    QCOMPARE(ep.origin(), QStringLiteral("https://srv.example:443"));
}

QTEST_GUILESS_MAIN(tst_EnodeMeta)
#include "tst_EnodeMeta.moc"
