/// @file tst_PreferencesIpc.cpp
/// @brief Integration test — SetPreferences / GetPreferences IPC round-trip.
///
/// Runs a minimal in-process IPC server (no emulecored required) that handles
/// Handshake, GetPreferences, and SetPreferences using the real Preferences
/// class backed by a temporary YAML file.

#include "IpcConnection.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "prefs/Preferences.h"

#include <QCborMap>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <yaml-cpp/yaml.h>

using namespace eMule::Ipc;

class tst_PreferencesIpc : public QObject {
    Q_OBJECT

private:
    // Client side
    IpcConnection* m_conn = nullptr;
    int m_nextSeqId = 1;

    // Server side
    QTcpServer* m_server = nullptr;
    IpcConnection* m_serverConn = nullptr;

    // Temp YAML persistence
    QTemporaryDir m_tmpDir;
    QString m_prefsPath;

    /// Send an IPC message and wait for the response (blocking).
    IpcMessage sendAndWait(IpcMessage msg, int timeoutMs = 5000)
    {
        QCborArray arr = msg.toArray();
        if (arr.size() >= 2)
            arr[1] = m_nextSeqId++;
        IpcMessage tagged(std::move(arr));

        const int expectedSeq = tagged.seqId();

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

    /// Handle a message received on the server side.
    void handleServerMessage(const IpcMessage& msg)
    {
        switch (msg.type()) {
        case IpcMsgType::Handshake: {
            IpcMessage reply(IpcMsgType::HandshakeOk, msg.seqId());
            reply.append(QString::fromLatin1(ProtocolVersion));
            reply.append(QStringLiteral("Test IPC Server"));
            m_serverConn->sendMessage(reply);
            break;
        }
        case IpcMsgType::SetPreferences: {
            for (int i = 0; i + 1 < msg.fieldCount(); i += 2) {
                const QString key = msg.fieldString(i);
                const auto val = static_cast<uint32_t>(msg.fieldInt(i + 1));

                if (key == QStringLiteral("maxDownload"))
                    eMule::thePrefs.setMaxDownload(val);
                else if (key == QStringLiteral("maxGraphDownloadRate"))
                    eMule::thePrefs.setMaxGraphDownloadRate(val);
                else if (key == QStringLiteral("maxConnections"))
                    eMule::thePrefs.setMaxConnections(static_cast<uint16_t>(val));
            }
            eMule::thePrefs.save();
            m_serverConn->sendMessage(IpcMessage::makeResult(msg.seqId(), true));
            break;
        }
        case IpcMsgType::GetPreferences: {
            QCborMap prefs;
            prefs.insert(QStringLiteral("maxDownload"),
                         static_cast<qint64>(eMule::thePrefs.maxDownload()));
            prefs.insert(QStringLiteral("maxGraphDownloadRate"),
                         static_cast<qint64>(eMule::thePrefs.maxGraphDownloadRate()));
            prefs.insert(QStringLiteral("maxGraphUploadRate"),
                         static_cast<qint64>(eMule::thePrefs.maxGraphUploadRate()));
            prefs.insert(QStringLiteral("maxConnections"),
                         static_cast<qint64>(eMule::thePrefs.maxConnections()));
            m_serverConn->sendMessage(
                IpcMessage::makeResult(msg.seqId(), true, QCborValue(prefs)));
            break;
        }
        default:
            m_serverConn->sendMessage(
                IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("Not implemented")));
            break;
        }
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmpDir.isValid());
        m_prefsPath = m_tmpDir.filePath(QStringLiteral("preferences.yml"));
        eMule::thePrefs.load(m_prefsPath);

        // Start minimal IPC server on ephemeral port
        m_server = new QTcpServer(this);
        QVERIFY(m_server->listen(QHostAddress::LocalHost, 0));
        const quint16 port = m_server->serverPort();

        // Accept incoming connection and set up server-side handler
        connect(m_server, &QTcpServer::newConnection, this, [this] {
            auto* socket = m_server->nextPendingConnection();
            m_serverConn = new IpcConnection(socket, this);
            connect(m_serverConn, &IpcConnection::messageReceived,
                    this, &tst_PreferencesIpc::handleServerMessage);
        });

        // Connect client to server
        auto* clientSocket = new QTcpSocket;
        clientSocket->connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(clientSocket->waitForConnected(3000));
        QVERIFY(m_server->waitForNewConnection(3000));

        m_conn = new IpcConnection(clientSocket, this);

        // Handshake
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
        delete m_serverConn;
        m_serverConn = nullptr;
        delete m_server;
        m_server = nullptr;
    }

    // ----- Test cases --------------------------------------------------------

    void setAndGet_maxDownload()
    {
        IpcMessage resp = setPreference(QStringLiteral("maxDownload"), 100000);
        QVERIFY2(resp.isValid(), "SetPreferences response not received");

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
        setPreference(QStringLiteral("maxDownload"), 100000);
        setPreference(QStringLiteral("maxGraphDownloadRate"), 125000);

        // Give save a moment to flush to disk
        QTest::qWait(200);

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
