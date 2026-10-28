/// @file tst_IpcTls.cpp
/// @brief GUI <-> core link: TLS for remote cores, pinned certificate, token only inside TLS.

#include "TestHelpers.h"

#include "IpcConnection.h"
#include "IpcTlsIdentity.h"
#include "IpcTls.h"
#include "app/IpcClient.h"
#include "app/KnownCores.h"
#include "crypto/SelfSignedCert.h"

#include <QFile>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace eMule;
using namespace eMule::Ipc;

namespace {

/// IpcTlsServer + a core that answers the handshake and records what it got.
class TlsCore : public QObject {
public:
    explicit TlsCore(bool loopbackIsRemote)
    {
        const auto pem = generateSelfSignedPem("emulecored", 30);
        if (pem) {
            server.setIdentity(QSslCertificate(pem->certPem, QSsl::Pem),
                               QSslKey(pem->keyPem, QSsl::Ec, QSsl::Pem));
        }
        server.setTreatLoopbackAsRemote(loopbackIsRemote);
        connect(&server, &IpcTlsServer::ready, this, [this](QSslSocket* s, bool local) {
            ++readyCount;
            lastLocal = local;
            m_conn = std::make_unique<IpcConnection>(s);
            lastEncrypted = m_conn->isEncrypted();
            connect(m_conn.get(), &IpcConnection::messageReceived, this,
                    [this](const IpcMessage& msg) {
                        received.append(msg);
                        if (msg.type() != IpcMsgType::Handshake)
                            return;
                        token = msg.fieldString(1);
                        IpcMessage ok(IpcMsgType::HandshakeOk, msg.seqId());
                        ok.append(QString::fromLatin1(ProtocolVersion));
                        ok.append(QStringLiteral("tls core"));
                        ok.append(QStringLiteral("session-1"));
                        ok.append(false);
                        ok.append(QString());
                        m_conn->sendMessage(ok);
                    });
        });
        connect(&server, &IpcTlsServer::rejected, this,
                [this](const QString&, const QString&) { ++rejectedCount; });
    }

    [[nodiscard]] bool listen() { return server.listen(QHostAddress::LocalHost, 0); }
    [[nodiscard]] quint16 port() const { return server.serverPort(); }
    [[nodiscard]] QString fingerprint() const { return certFingerprint(server.certificate()); }

    IpcTlsServer server;
    int readyCount = 0;
    int rejectedCount = 0;
    bool lastLocal = false;
    bool lastEncrypted = false;
    QString token;
    QList<IpcMessage> received;

private:
    std::unique_ptr<IpcConnection> m_conn;
};

} // namespace

class tst_IpcTls : public QObject {
    Q_OBJECT

private slots:
    void remoteCore_tlsAndTokenInside();
    void remotePlain_isRejected();
    void loopbackPlain_stillWorks();
    void loopbackTls_withToken();
    void pinMismatch_sendsNothingUntilTrusted();
    void knownCores_roundTrip();
    void identity_createdOnceKeptAndPrivate();
};

void tst_IpcTls::remoteCore_tlsAndTokenInside()
{
    TlsCore core(/*loopbackIsRemote=*/true);
    QVERIFY(core.server.hasIdentity());
    QVERIFY(core.listen());

    KnownCores pins;
    IpcClient client;
    client.setKnownCores(&pins);
    client.setAuthToken(QStringLiteral("secret-token"));
    QSignalSpy connected(&client, &IpcClient::connected);
    client.connectToDaemon(QHostAddress::LocalHost, core.port());
    QVERIFY(connected.wait(10000));

    QCOMPARE(core.readyCount, 1);
    QVERIFY(!core.lastLocal);
    QVERIFY(core.lastEncrypted);
    QCOMPARE(core.token, QStringLiteral("secret-token"));
    // Pinned on first use
    QCOMPARE(pins.pinned(KnownCores::key(QStringLiteral("127.0.0.1"), core.port())),
             core.fingerprint());
    client.disconnectFromDaemon();
}

void tst_IpcTls::remotePlain_isRejected()
{
    TlsCore core(/*loopbackIsRemote=*/true);
    QVERIFY(core.listen());

    QTcpSocket raw;
    raw.connectToHost(QHostAddress::LocalHost, core.port());
    QVERIFY(raw.waitForConnected(5000));
    IpcMessage hs(IpcMsgType::Handshake, 1);
    hs.append(QString::fromLatin1(ProtocolVersion));
    hs.append(QStringLiteral("secret-token"));
    raw.write(encodeFrame(hs.toArray().toCborValue().toCbor()));
    raw.flush();

    QTRY_COMPARE_WITH_TIMEOUT(core.rejectedCount, 1, 10000);
    QCOMPARE(core.readyCount, 0);
    QVERIFY(core.received.isEmpty());
}

void tst_IpcTls::loopbackPlain_stillWorks()
{
    TlsCore core(/*loopbackIsRemote=*/false);
    QVERIFY(core.listen());

    KnownCores pins;
    IpcClient client;
    client.setKnownCores(&pins);
    QSignalSpy connected(&client, &IpcClient::connected);
    client.connectToDaemon(QHostAddress::LocalHost, core.port());
    QVERIFY(connected.wait(10000));

    QVERIFY(core.lastLocal);
    QVERIFY(!core.lastEncrypted);
    QVERIFY(pins.entries().empty());   // no TLS, nothing to pin
    client.disconnectFromDaemon();
}

void tst_IpcTls::loopbackTls_withToken()
{
    // A token always goes over TLS: a Docker port forward looks like loopback here
    TlsCore core(/*loopbackIsRemote=*/false);
    QVERIFY(core.listen());

    KnownCores pins;
    IpcClient client;
    client.setKnownCores(&pins);
    client.setAuthToken(QStringLiteral("secret-token"));
    QSignalSpy connected(&client, &IpcClient::connected);
    client.connectToDaemon(QHostAddress::LocalHost, core.port());
    QVERIFY(connected.wait(10000));

    QVERIFY(core.lastLocal);
    QVERIFY(core.lastEncrypted);
    QCOMPARE(core.token, QStringLiteral("secret-token"));
    client.disconnectFromDaemon();
}

void tst_IpcTls::pinMismatch_sendsNothingUntilTrusted()
{
    TlsCore core(/*loopbackIsRemote=*/true);
    QVERIFY(core.listen());

    const QString key = KnownCores::key(QStringLiteral("127.0.0.1"), core.port());
    KnownCores pins;
    pins.pin(key, QStringLiteral("00:11:22"));

    IpcClient client;
    client.setKnownCores(&pins);
    client.setAuthToken(QStringLiteral("secret-token"));
    QSignalSpy changed(&client, &IpcClient::certificateChanged);
    QSignalSpy connected(&client, &IpcClient::connected);
    client.connectToDaemon(QHostAddress::LocalHost, core.port());
    QVERIFY(changed.wait(10000));

    QCOMPARE(changed.first().at(0).toString(), key);
    QCOMPARE(changed.first().at(1).toString(), QStringLiteral("00:11:22"));
    QCOMPARE(changed.first().at(2).toString(), core.fingerprint());
    QCOMPARE(client.coreLink(), IpcClient::CoreLink::Refused);
    QTest::qWait(300);
    QVERIFY(core.received.isEmpty());   // the token never left
    QCOMPARE(connected.count(), 0);

    client.trustNewCertificate();
    QVERIFY(connected.wait(10000));
    QCOMPARE(pins.pinned(key), core.fingerprint());
    QCOMPARE(core.token, QStringLiteral("secret-token"));
    client.disconnectFromDaemon();
}

void tst_IpcTls::knownCores_roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ipc-known-cores.json"));
    {
        KnownCores pins(path);
        pins.pin(QStringLiteral("nas:4712"), QStringLiteral("AA:BB"));
        pins.pin(KnownCores::key(QStringLiteral("::1"), 4712), QStringLiteral("CC:DD"));
    }
    KnownCores pins(path);
    QCOMPARE(pins.entries().size(), size_t{2});
    QCOMPARE(pins.check(QStringLiteral("nas:4712"), QStringLiteral("aa:bb")), KnownCores::Check::Match);
    QCOMPARE(pins.check(QStringLiteral("nas:4712"), QStringLiteral("EE:FF")), KnownCores::Check::Mismatch);
    QCOMPARE(pins.check(QStringLiteral("other:4712"), QStringLiteral("AA:BB")), KnownCores::Check::New);
    QCOMPARE(pins.pinned(QStringLiteral("[::1]:4712")), QStringLiteral("CC:DD"));
    pins.remove(QStringLiteral("nas:4712"));
    QCOMPARE(KnownCores(path).entries().size(), size_t{1});
}

void tst_IpcTls::identity_createdOnceKeptAndPrivate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const IpcTlsIdentity first = loadOrCreateIpcTlsIdentity(dir.path(), &error);
    QVERIFY2(!first.cert.isNull() && !first.key.isNull(), qPrintable(error));
    QVERIFY(first.cert.expiryDate() > QDateTime::currentDateTimeUtc().addYears(19));

    const QString keyPath = dir.filePath(QStringLiteral("ipc-key.pem"));
#ifndef Q_OS_WIN
    QCOMPARE(QFile::permissions(keyPath) & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
             QFileDevice::Permissions{});
#endif
    // Same certificate on the next start: the GUI's pin stays valid
    const IpcTlsIdentity again = loadOrCreateIpcTlsIdentity(dir.path());
    QCOMPARE(certFingerprint(again.cert), certFingerprint(first.cert));

    // A broken key is reported, not silently replaced
    QFile f(keyPath);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("garbage");
    f.close();
    error.clear();
    QVERIFY(loadOrCreateIpcTlsIdentity(dir.path(), &error).cert.isNull());
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(tst_IpcTls)
#include "tst_IpcTls.moc"
