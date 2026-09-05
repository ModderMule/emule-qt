#pragma once

/// @file FakeNntpServer.h
/// @brief A scriptable in-process NNTP server for tests.
///
/// Modelled on NZBGet's `daemon/nserv/NntpServer.cpp`, which exists for the same
/// reason: the interesting parts of an NNTP client are the refusals — 400 at the
/// greeting, 481 on authentication, 430 for a missing article, and a server that
/// simply stops answering. None of those can be provoked against a real provider
/// on demand, and all of them route differently in the client.
///
/// Cleartext only. TLS adds a certificate fixture and tests the Qt handshake
/// rather than our state machine; the implicit/STARTTLS branch is covered by the
/// live test instead.
///
/// No Q_OBJECT: it declares no signals or slots of its own, so it stays
/// header-only. Same choice, same reason, as tests/FakeCacheServer.h.

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

namespace eMule::testing {

class FakeNntpServer : public QTcpServer {
public:
    explicit FakeNntpServer(QObject* parent = nullptr)
        : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] { onNewConnection(); });
    }

    /// Listen on loopback. Returns the port, or 0 on failure.
    quint16 start()
    {
        if (!listen(QHostAddress::LocalHost, 0))
            return 0;
        return serverPort();
    }

    // -- Scripting ----------------------------------------------------------

    /// The greeting. Default is "posting allowed".
    void setGreeting(QByteArray line) { m_greeting = std::move(line); }

    /// Accept the connection and then say nothing at all — the case that wedges
    /// a client with no response watchdog.
    void setMute(bool mute) { m_mute = mute; }

    /// Credentials the server will accept. Empty user disables authentication,
    /// so the client goes straight from MODE READER to ready.
    void setCredentials(QString user, QString pass)
    {
        m_user = std::move(user);
        m_pass = std::move(pass);
    }

    /// Answer AUTHINFO PASS with 481 regardless of what was sent.
    void setRejectAuth(bool reject) { m_rejectAuth = reject; }

    void setCapabilities(QStringList caps) { m_capabilities = std::move(caps); }

    /// Make CAPABILITIES answer "500 command not recognized", as pre-RFC-3977
    /// servers do.
    void setSupportsCapabilities(bool supported) { m_supportsCaps = supported; }

    /// Register a newsgroup: count / low / high.
    void addGroup(const QString& name, qint64 count, qint64 low, qint64 high)
    {
        m_groups.insert(name, {count, low, high});
    }

    /// Register an article body by message-id (with or without angle brackets).
    void addArticle(const QString& messageId, QByteArray body)
    {
        m_articles.insert(normalizeId(messageId), std::move(body));
    }

    /// Close the connection abruptly the next time a command arrives.
    void setDropOnNextCommand(bool drop) { m_dropOnNextCommand = drop; }

    /// Drop the connection the next @p times a BODY for @p messageId arrives,
    /// then serve it normally.
    ///
    /// A *transport* fault aimed at one article, which is the one thing
    /// setDropOnNextCommand cannot express: it fires on whatever command comes
    /// next, so which article it hits depends on scheduling. The queue treats
    /// this as "retry the same level", the branch that is not the missing-article
    /// ladder.
    void setDropOnArticle(const QString& messageId, int times = 1)
    {
        m_dropArticles.insert(normalizeId(messageId), times);
    }

    // -- Observation --------------------------------------------------------

    /// Every command line received, in order, across all connections.
    [[nodiscard]] const QStringList& receivedCommands() const { return m_received; }
    [[nodiscard]] int connectionCount() const { return m_connections; }

    /// Whether a password ever reached the server in the clear. Guards against a
    /// regression where AUTHINFO is sent before a requested TLS upgrade.
    [[nodiscard]] bool sawPlaintextPassword() const { return m_sawPlaintextPassword; }

private:
    void onNewConnection()
    {
        while (QTcpSocket* sock = nextPendingConnection()) {
            ++m_connections;
            connect(sock, &QTcpSocket::readyRead, sock, [this, sock] { onReadyRead(sock); });
            connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);

            if (!m_mute)
                writeLine(sock, m_greeting);
        }
    }

    void onReadyRead(QTcpSocket* sock)
    {
        while (sock->canReadLine()) {
            QByteArray raw = sock->readLine();
            while (raw.endsWith('\n') || raw.endsWith('\r'))
                raw.chop(1);

            const QString line = QString::fromLatin1(raw);
            m_received.append(line);

            if (m_dropOnNextCommand) {
                sock->abort();
                return;
            }
            if (m_mute)
                continue;

            handleCommand(sock, line);
            if (sock->state() != QAbstractSocket::ConnectedState)
                return;
        }
    }

    void handleCommand(QTcpSocket* sock, const QString& line)
    {
        const QString verb = line.section(u' ', 0, 0).toUpper();
        const QString rest = line.section(u' ', 1).trimmed();

        if (verb == QLatin1String("MODE")) {
            writeLine(sock, QByteArrayLiteral("200 Reader mode, posting permitted"));
        } else if (verb == QLatin1String("AUTHINFO")) {
            handleAuthinfo(sock, rest);
        } else if (verb == QLatin1String("CAPABILITIES")) {
            handleCapabilities(sock);
        } else if (verb == QLatin1String("GROUP")) {
            handleGroup(sock, rest);
        } else if (verb == QLatin1String("STAT")) {
            handleStat(sock, rest);
        } else if (verb == QLatin1String("BODY")) {
            handleBody(sock, rest);
        } else if (verb == QLatin1String("QUIT")) {
            writeLine(sock, QByteArrayLiteral("205 Closing connection"));
            sock->disconnectFromHost();
        } else {
            writeLine(sock, QByteArrayLiteral("500 Command not recognized"));
        }
    }

    void handleAuthinfo(QTcpSocket* sock, const QString& rest)
    {
        const QString kind = rest.section(u' ', 0, 0).toUpper();
        const QString value = rest.section(u' ', 1).trimmed();

        if (kind == QLatin1String("USER")) {
            m_offeredUser = value;
            writeLine(sock, QByteArrayLiteral("381 Password required"));
            return;
        }
        if (kind == QLatin1String("PASS")) {
            m_sawPlaintextPassword = true;
            if (!m_rejectAuth && m_offeredUser == m_user && value == m_pass)
                writeLine(sock, QByteArrayLiteral("281 Authentication accepted"));
            else
                writeLine(sock, QByteArrayLiteral("481 Authentication failed"));
            return;
        }
        writeLine(sock, QByteArrayLiteral("501 Unknown AUTHINFO variant"));
    }

    void handleCapabilities(QTcpSocket* sock)
    {
        if (!m_supportsCaps) {
            writeLine(sock, QByteArrayLiteral("500 Command not recognized"));
            return;
        }
        writeLine(sock, QByteArrayLiteral("101 Capability list follows"));
        for (const QString& cap : m_capabilities)
            writeStuffedLine(sock, cap.toLatin1());
        writeLine(sock, QByteArrayLiteral("."));
    }

    void handleGroup(QTcpSocket* sock, const QString& name)
    {
        const auto it = m_groups.constFind(name);
        if (it == m_groups.cend()) {
            writeLine(sock, QByteArrayLiteral("411 No such newsgroup"));
            return;
        }
        writeLine(sock, QStringLiteral("211 %1 %2 %3 %4")
                            .arg(it->count).arg(it->low).arg(it->high).arg(name)
                            .toLatin1());
    }

    void handleStat(QTcpSocket* sock, const QString& id)
    {
        const QString key = normalizeId(id);
        if (!m_articles.contains(key)) {
            writeLine(sock, QByteArrayLiteral("430 No article with that message-id"));
            return;
        }
        writeLine(sock, QStringLiteral("223 0 <%1> Article exists").arg(key).toLatin1());
    }

    void handleBody(QTcpSocket* sock, const QString& id)
    {
        const QString key = normalizeId(id);

        if (const auto drop = m_dropArticles.find(key);
            drop != m_dropArticles.end() && drop.value() > 0) {
            drop.value() -= 1;
            sock->abort();
            return;
        }

        const auto it = m_articles.constFind(key);
        if (it == m_articles.cend()) {
            writeLine(sock, QByteArrayLiteral("430 No article with that message-id"));
            return;
        }
        writeLine(sock, QStringLiteral("222 0 <%1> Body follows").arg(key).toLatin1());
        for (const QByteArray& bodyLine : it->split('\n'))
            writeStuffedLine(sock, bodyLine);
        writeLine(sock, QByteArrayLiteral("."));
    }

    /// One line of a multi-line block. A line starting with '.' gets a second
    /// one prepended, or it would terminate the block (RFC 3977 3.1.1). This
    /// applies to every dot-terminated block, capabilities included -- getting
    /// it wrong here makes the client's correct unstuffing look like a bug.
    static void writeStuffedLine(QTcpSocket* sock, const QByteArray& line)
    {
        if (line.startsWith('.'))
            writeLine(sock, QByteArrayLiteral(".") + line);
        else
            writeLine(sock, line);
    }

    static void writeLine(QTcpSocket* sock, const QByteArray& line)
    {
        sock->write(line);
        sock->write("\r\n", 2);
    }

    static QString normalizeId(const QString& id)
    {
        QString s = id.trimmed();
        if (s.startsWith(u'<'))
            s.remove(0, 1);
        if (s.endsWith(u'>'))
            s.chop(1);
        return s;
    }

    struct GroupInfo {
        qint64 count = 0;
        qint64 low = 0;
        qint64 high = 0;
    };

    QByteArray m_greeting = QByteArrayLiteral("200 eMuleQt fake NNTP service ready");
    bool m_mute = false;
    bool m_rejectAuth = false;
    bool m_supportsCaps = true;
    bool m_dropOnNextCommand = false;

    QString m_user = QStringLiteral("testuser");
    QString m_pass = QStringLiteral("testpass");
    QString m_offeredUser;
    bool m_sawPlaintextPassword = false;

    QStringList m_capabilities{QStringLiteral("VERSION 2"), QStringLiteral("READER"),
                               QStringLiteral("OVER MSGID"), QStringLiteral("POST")};
    QHash<QString, GroupInfo> m_groups;
    QHash<QString, QByteArray> m_articles;

    QHash<QString, int> m_dropArticles;

    QStringList m_received;
    int m_connections = 0;
};

} // namespace eMule::testing
