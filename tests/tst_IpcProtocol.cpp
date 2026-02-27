/// @file tst_IpcProtocol.cpp
/// @brief Unit tests for IPC wire protocol framing.

#include "IpcProtocol.h"

#include <QCborValue>
#include <QTest>

using namespace eMule::Ipc;

class tst_IpcProtocol : public QObject {
    Q_OBJECT

private slots:
    void encodeFrame_producesCorrectHeader();
    void encodeFrame_cborArrayOverload();
    void tryDecodeFrame_roundTrip();
    void tryDecodeFrame_insufficientData();
    void tryDecodeFrame_incompletePayload();
    void tryDecodeFrame_oversizedPayload();
    void tryDecodeFrame_nonArrayPayload();
    void tryDecodeFrame_multipleFrames();
    void msgTypeEnum_values();
};

void tst_IpcProtocol::encodeFrame_producesCorrectHeader()
{
    const QByteArray payload("hello");
    const QByteArray frame = encodeFrame(payload);

    // Frame = 4-byte header + payload
    QCOMPARE(frame.size(), FrameHeaderSize + payload.size());

    // Header is big-endian uint32 of payload length
    const auto len = qFromBigEndian<uint32_t>(
        reinterpret_cast<const uint8_t*>(frame.constData()));
    QCOMPARE(len, static_cast<uint32_t>(payload.size()));

    // Payload follows header
    QCOMPARE(frame.mid(FrameHeaderSize), payload);
}

void tst_IpcProtocol::encodeFrame_cborArrayOverload()
{
    QCborArray arr;
    arr.append(100);
    arr.append(1);
    arr.append(QStringLiteral("test"));

    const QByteArray frame = encodeFrame(arr);
    QVERIFY(frame.size() > FrameHeaderSize);

    // Should be decodable
    auto result = tryDecodeFrame(frame);
    QVERIFY(result.has_value());
    QCOMPARE(result->message.size(), 3);
    QCOMPARE(result->message.at(0).toInteger(), 100);
}

void tst_IpcProtocol::tryDecodeFrame_roundTrip()
{
    QCborArray original;
    original.append(110);
    original.append(42);

    const QByteArray frame = encodeFrame(original);
    auto result = tryDecodeFrame(frame);

    QVERIFY(result.has_value());
    QCOMPARE(result->bytesConsumed, frame.size());
    QCOMPARE(result->message.size(), original.size());
    QCOMPARE(result->message.at(0).toInteger(), 110);
    QCOMPARE(result->message.at(1).toInteger(), 42);
}

void tst_IpcProtocol::tryDecodeFrame_insufficientData()
{
    // Less than 4 bytes
    QByteArray buf(3, '\0');
    QVERIFY(!tryDecodeFrame(buf).has_value());

    // Empty buffer
    QVERIFY(!tryDecodeFrame(QByteArray()).has_value());
}

void tst_IpcProtocol::tryDecodeFrame_incompletePayload()
{
    QCborArray arr;
    arr.append(100);
    arr.append(0);

    QByteArray frame = encodeFrame(arr);
    // Truncate the payload
    frame.chop(2);

    QVERIFY(!tryDecodeFrame(frame).has_value());
}

void tst_IpcProtocol::tryDecodeFrame_oversizedPayload()
{
    // Craft a header claiming > MaxPayloadSize
    uint8_t header[4];
    qToBigEndian<uint32_t>(MaxPayloadSize + 1, header);
    QByteArray buf(reinterpret_cast<const char*>(header), 4);

    QVERIFY(!tryDecodeFrame(buf).has_value());
}

void tst_IpcProtocol::tryDecodeFrame_nonArrayPayload()
{
    // Encode a CBOR map instead of array
    QCborMap map;
    map.insert(QStringLiteral("key"), QStringLiteral("value"));
    QByteArray cborData = QCborValue(map).toCbor();
    QByteArray frame = encodeFrame(cborData);

    QVERIFY(!tryDecodeFrame(frame).has_value());
}

void tst_IpcProtocol::tryDecodeFrame_multipleFrames()
{
    QCborArray msg1;
    msg1.append(100);
    msg1.append(1);

    QCborArray msg2;
    msg2.append(110);
    msg2.append(2);

    QByteArray buffer = encodeFrame(msg1) + encodeFrame(msg2);

    // Decode first
    auto r1 = tryDecodeFrame(buffer);
    QVERIFY(r1.has_value());
    QCOMPARE(r1->message.at(0).toInteger(), 100);

    buffer.remove(0, r1->bytesConsumed);

    // Decode second
    auto r2 = tryDecodeFrame(buffer);
    QVERIFY(r2.has_value());
    QCOMPARE(r2->message.at(0).toInteger(), 110);
}

void tst_IpcProtocol::msgTypeEnum_values()
{
    // Verify key enum values match the protocol spec
    QCOMPARE(static_cast<int>(IpcMsgType::Handshake), 100);
    QCOMPARE(static_cast<int>(IpcMsgType::GetDownloads), 110);
    QCOMPARE(static_cast<int>(IpcMsgType::HandshakeOk), 300);
    QCOMPARE(static_cast<int>(IpcMsgType::Result), 301);
    QCOMPARE(static_cast<int>(IpcMsgType::Error), 302);
    QCOMPARE(static_cast<int>(IpcMsgType::PushStatsUpdate), 400);
    QCOMPARE(static_cast<int>(IpcMsgType::PushLogMessage), 450);
}

QTEST_GUILESS_MAIN(tst_IpcProtocol)
#include "tst_IpcProtocol.moc"
