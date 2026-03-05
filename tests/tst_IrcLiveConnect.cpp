/// @file tst_IrcLiveConnect.cpp
/// @brief Live IRC connection test — connect to real IRC server.
///
/// Verifies that IrcClient can connect to irc.mindforge.org:6667,
/// complete IRC login (receive RPL_WELCOME 001), and cleanly disconnect.
/// Requires internet connectivity.
///
/// Only built when EMULE_LIVE_TESTS=ON (off by default).

#include "TestHelpers.h"

#include "chat/IrcClient.h"

#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTest>

using eMule::IrcClient;

class tst_IrcLiveConnect : public QObject
{
    Q_OBJECT

private slots:
    void connectLoginDisconnect();
};

void tst_IrcLiveConnect::connectLoginDisconnect()
{
    IrcClient irc;

    QSignalSpy connectedSpy(&irc, &IrcClient::connected);
    QSignalSpy loggedInSpy(&irc, &IrcClient::loggedIn);
    QSignalSpy disconnectedSpy(&irc, &IrcClient::disconnected);
    QSignalSpy errorSpy(&irc, &IrcClient::socketError);

    // Generate a random nick to avoid collisions
    const auto suffix = QRandomGenerator::global()->bounded(10000u, 99999u);
    const auto nick = QStringLiteral("eMuleQtTest_%1").arg(suffix);

    irc.connectToServer(QStringLiteral("irc.mindforge.org:6667"), nick);

    // Wait for TCP connection (up to 10s)
    if (!connectedSpy.wait(10000)) {
        if (!errorSpy.isEmpty())
            QSKIP(qPrintable(QStringLiteral("Network error: %1").arg(
                errorSpy.first().first().toString())));
        QSKIP("Could not connect to irc.mindforge.org:6667 — network unavailable");
    }

    QVERIFY(irc.isConnected());

    // Wait for IRC login (RPL_WELCOME 001) — up to 10s
    if (!loggedInSpy.wait(10000)) {
        if (!errorSpy.isEmpty())
            QSKIP(qPrintable(QStringLiteral("Login error: %1").arg(
                errorSpy.first().first().toString())));
        QSKIP("IRC login timed out — server may be unavailable");
    }

    QVERIFY(irc.isLoggedIn());
    QVERIFY(!irc.currentNick().isEmpty());

    // Clean disconnect
    irc.disconnect();

    // Wait for disconnect confirmation
    if (disconnectedSpy.isEmpty())
        QVERIFY(disconnectedSpy.wait(5000));

    QVERIFY(!irc.isConnected());
    QVERIFY(!irc.isLoggedIn());
}

QTEST_MAIN(tst_IrcLiveConnect)
#include "tst_IrcLiveConnect.moc"
