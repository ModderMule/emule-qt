/// @file tst_IpcMessage.cpp
/// @brief Unit tests for IpcMessage type-safe wrapper.

#include "IpcMessage.h"

#include <QCborMap>
#include <QTest>

using namespace eMule::Ipc;

class tst_IpcMessage : public QObject {
    Q_OBJECT

private slots:
    void construction_typeAndSeqId();
    void construction_fromCborArray();
    void fieldAccess_stringIntBool();
    void fieldAccess_mapAndArray();
    void fieldAccess_outOfRange();
    void append_chaining();
    void toFrame_roundTrip();
    void isValid_checks();
    void makeResult_success();
    void makeResult_withData();
    void makeError_format();
};

void tst_IpcMessage::construction_typeAndSeqId()
{
    IpcMessage msg(IpcMsgType::Handshake, 7);

    QCOMPARE(msg.type(), IpcMsgType::Handshake);
    QCOMPARE(msg.seqId(), 7);
    QCOMPARE(msg.fieldCount(), 0);
    QVERIFY(msg.isValid());
}

void tst_IpcMessage::construction_fromCborArray()
{
    QCborArray arr;
    arr.append(static_cast<int>(IpcMsgType::GetDownloads));
    arr.append(42);
    arr.append(QStringLiteral("payload"));

    IpcMessage msg(arr);
    QCOMPARE(msg.type(), IpcMsgType::GetDownloads);
    QCOMPARE(msg.seqId(), 42);
    QCOMPARE(msg.fieldCount(), 1);
    QCOMPARE(msg.fieldString(0), QStringLiteral("payload"));
}

void tst_IpcMessage::fieldAccess_stringIntBool()
{
    IpcMessage msg(IpcMsgType::Handshake, 1);
    msg.append(QStringLiteral("1.0"));
    msg.append(int64_t(4712));
    msg.append(true);

    QCOMPARE(msg.fieldString(0), QStringLiteral("1.0"));
    QCOMPARE(msg.fieldInt(1), 4712);
    QCOMPARE(msg.fieldBool(2), true);
}

void tst_IpcMessage::fieldAccess_mapAndArray()
{
    IpcMessage msg(IpcMsgType::Result, 1);

    QCborMap map;
    map.insert(QStringLiteral("key"), QStringLiteral("value"));
    msg.append(map);

    QCborArray arr;
    arr.append(1);
    arr.append(2);
    msg.append(arr);

    QCOMPARE(msg.fieldMap(0).value(QStringLiteral("key")).toString(), QStringLiteral("value"));
    QCOMPARE(msg.fieldArray(1).size(), 2);
}

void tst_IpcMessage::fieldAccess_outOfRange()
{
    IpcMessage msg(IpcMsgType::Handshake, 1);

    // No fields appended — accessing field 0 should return empty/default
    QCOMPARE(msg.fieldString(0), QString());
    QCOMPARE(msg.fieldInt(0), int64_t(0));
    QCOMPARE(msg.fieldBool(0), false);
    QCOMPARE(msg.fieldString(99), QString());
}

void tst_IpcMessage::append_chaining()
{
    IpcMessage msg(IpcMsgType::Subscribe, 5);
    msg.append(int64_t(0xFF)).append(QStringLiteral("test")).append(true);

    QCOMPARE(msg.fieldCount(), 3);
    QCOMPARE(msg.fieldInt(0), 0xFF);
    QCOMPARE(msg.fieldString(1), QStringLiteral("test"));
    QCOMPARE(msg.fieldBool(2), true);
}

void tst_IpcMessage::toFrame_roundTrip()
{
    IpcMessage original(IpcMsgType::GetDownload, 10);
    original.append(QStringLiteral("ABC123"));

    const QByteArray frame = original.toFrame();
    auto decoded = tryDecodeFrame(frame);
    QVERIFY(decoded.has_value());

    IpcMessage restored(decoded->message);
    QCOMPARE(restored.type(), IpcMsgType::GetDownload);
    QCOMPARE(restored.seqId(), 10);
    QCOMPARE(restored.fieldString(0), QStringLiteral("ABC123"));
}

void tst_IpcMessage::isValid_checks()
{
    // Default-constructed message is invalid
    IpcMessage empty;
    QVERIFY(!empty.isValid());

    // Valid message
    IpcMessage valid(IpcMsgType::Handshake, 0);
    QVERIFY(valid.isValid());

    // Array with invalid type (<100)
    QCborArray badArr;
    badArr.append(50);
    badArr.append(0);
    IpcMessage badMsg(badArr);
    QVERIFY(!badMsg.isValid());
}

void tst_IpcMessage::makeResult_success()
{
    auto msg = IpcMessage::makeResult(7, true);

    QCOMPARE(msg.type(), IpcMsgType::Result);
    QCOMPARE(msg.seqId(), 7);
    QCOMPARE(msg.fieldBool(0), true);
    QCOMPARE(msg.fieldCount(), 1);
}

void tst_IpcMessage::makeResult_withData()
{
    QCborArray data;
    data.append(QStringLiteral("item1"));
    data.append(QStringLiteral("item2"));

    auto msg = IpcMessage::makeResult(3, true, QCborValue(data));

    QCOMPARE(msg.type(), IpcMsgType::Result);
    QCOMPARE(msg.fieldBool(0), true);
    QCOMPARE(msg.fieldArray(1).size(), 2);
}

void tst_IpcMessage::makeError_format()
{
    auto msg = IpcMessage::makeError(5, 404, QStringLiteral("Not found"));

    QCOMPARE(msg.type(), IpcMsgType::Error);
    QCOMPARE(msg.seqId(), 5);
    QCOMPARE(msg.fieldInt(0), 404);
    QCOMPARE(msg.fieldString(1), QStringLiteral("Not found"));
}

QTEST_GUILESS_MAIN(tst_IpcMessage)
#include "tst_IpcMessage.moc"
