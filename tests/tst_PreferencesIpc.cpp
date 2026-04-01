/// @file tst_PreferencesIpc.cpp
/// @brief Integration test — SetPreferences / GetPreferences IPC round-trip.
///
/// Connects to a running emulecored on localhost:4712 and verifies that
/// bandwidth settings survive a Set → Get → YAML cycle with large values.
///
/// Requires: emulecored running locally (start it before running this test).

#include "IpcConnection.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCborMap>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>

#include <yaml-cpp/yaml.h>

using namespace eMule::Ipc;

class tst_PreferencesIpc : public QObject {
    Q_OBJECT

private:
    IpcConnection* m_conn = nullptr;    // owns the socket
    int m_nextSeqId = 1;
    QString m_prefsPath;

    /// Send an IPC message and wait for the response (blocking).
    IpcMessage sendAndWait(IpcMessage msg, int timeoutMs = 5000)
    {
        // Assign sequence ID
        QCborArray arr = msg.toArray();
        if (arr.size() >= 2)
            arr[1] = m_nextSeqId++;
        IpcMessage tagged(std::move(arr));

        const int expectedSeq = tagged.seqId();

        // Collect responses via signal
        IpcMessage result;
        bool found = false;
        auto conn = connect(m_conn, &IpcConnection::messageReceived,
                            this, [&](const IpcMessage& resp) {
            if (resp.seqId() == expectedSeq) {
                result = resp;
                found = true;
            }
        });

        m_conn->sendMessage(tagged);

        // Spin event loop until response arrives or timeout
        QElapsedTimer timer;
        timer.start();
        while (!found && timer.elapsed() < timeoutMs)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

        disconnect(conn);
        return result;
    }

    /// Send SetPreferences with a single key-value pair.
    IpcMessage setPreference(const QString& key, qint64 value)
    {
        IpcMessage req(IpcMsgType::SetPreferences);
        req.append(key);
        req.append(value);
        return sendAndWait(std::move(req));
    }

    /// Send GetPreferences and return the prefs map.
    QCborMap getPreferences()
    {
        IpcMessage req(IpcMsgType::GetPreferences);
        IpcMessage resp = sendAndWait(std::move(req));
        if (!resp.isValid() || !resp.fieldBool(0))
            return {};
        return resp.fieldMap(1);
    }

    /// Read a uint32 value from the YAML preferences file on disk.
    uint32_t readYamlValue(const QString& section, const QString& key)
    {
        YAML::Node root = YAML::LoadFile(m_prefsPath.toStdString());
        if (auto s = root[section.toStdString()])
            return s[key.toStdString()].as<uint32_t>(0);
        return 0;
    }

private slots:
    void initTestCase()
    {
        // Resolve preferences path
        m_prefsPath = QDir::homePath() + QStringLiteral("/eMuleQt/Config/preferences.yml");
        QVERIFY2(QFile::exists(m_prefsPath),
                 qPrintable(QStringLiteral("preferences.yml not found at ") + m_prefsPath));

        // Connect to daemon — IpcConnection takes ownership of the socket
        auto* socket = new QTcpSocket;
        socket->connectToHost(QStringLiteral("127.0.0.1"), 4712);
        QVERIFY2(socket->waitForConnected(3000), "Cannot connect to daemon on localhost:4712");

        m_conn = new IpcConnection(socket, this);

        // Handshake (localhost — no auth required by daemon for local connections)
        IpcMessage handshake(IpcMsgType::Handshake);
        handshake.append(QString::fromLatin1(ProtocolVersion));

        IpcMessage hsResp = sendAndWait(std::move(handshake));
        QVERIFY2(hsResp.isValid(), "Handshake response not received");
        QCOMPARE(hsResp.type(), IpcMsgType::HandshakeOk);
    }

    void cleanupTestCase()
    {
        delete m_conn;
        m_conn = nullptr;
    }

    // ----- Test cases --------------------------------------------------------

    void setAndGet_maxDownload()
    {
        // Set maxDownload to a large value
        IpcMessage resp = setPreference(QStringLiteral("maxDownload"), 100000);
        QVERIFY2(resp.isValid(), "SetPreferences response not received");

        // Read back via GetPreferences
        QCborMap prefs = getPreferences();
        QVERIFY(!prefs.isEmpty());

        auto actual = static_cast<uint32_t>(prefs.value(QStringLiteral("maxDownload")).toInteger());
        QCOMPARE(actual, uint32_t(100000));
    }

    void setAndGet_multipleValues()
    {
        const uint32_t values[] = {100000, 50000, 1, 0, 999999};

        for (uint32_t expected : values) {
            setPreference(QStringLiteral("maxDownload"), static_cast<qint64>(expected));

            QCborMap prefs = getPreferences();
            QVERIFY(!prefs.isEmpty());
            auto actual = static_cast<uint32_t>(prefs.value(QStringLiteral("maxDownload")).toInteger());
            QCOMPARE(actual, expected);
        }
    }

    void setAndGet_yamlPersistence()
    {
        // Set value via IPC
        setPreference(QStringLiteral("maxDownload"), 100000);

        // Also set maxGraphDownloadRate (capacity must be >= limit)
        setPreference(QStringLiteral("maxGraphDownloadRate"), 125000);

        // Give daemon a moment to flush YAML (save is synchronous in handler,
        // but we need the write to hit disk)
        QTest::qWait(200);

        // Verify on disk
        uint32_t diskMaxDown = readYamlValue(QStringLiteral("bandwidth"), QStringLiteral("maxDownload"));
        QCOMPARE(diskMaxDown, uint32_t(100000));

        uint32_t diskCapacity = readYamlValue(QStringLiteral("bandwidth"), QStringLiteral("maxGraphDownloadRate"));
        QCOMPARE(diskCapacity, uint32_t(125000));
    }

    void setAndGet_maxGraphDownloadRate()
    {
        setPreference(QStringLiteral("maxGraphDownloadRate"), 200000);

        QCborMap prefs = getPreferences();
        QVERIFY(!prefs.isEmpty());
        auto actual = static_cast<uint32_t>(prefs.value(QStringLiteral("maxGraphDownloadRate")).toInteger());
        QCOMPARE(actual, uint32_t(200000));
    }

    void setAndGet_maxConnections()
    {
        setPreference(QStringLiteral("maxConnections"), 1000);

        QCborMap prefs = getPreferences();
        QVERIFY(!prefs.isEmpty());
        auto actual = static_cast<uint16_t>(prefs.value(QStringLiteral("maxConnections")).toInteger());
        QCOMPARE(actual, uint16_t(1000));
    }
};

QTEST_MAIN(tst_PreferencesIpc)
#include "tst_PreferencesIpc.moc"
