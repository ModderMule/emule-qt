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
/// Cleartext by default. setImplicitTls() serves implicit TLS with a fixed
/// self-signed loopback certificate (client must use CertVerification::None),
/// for what only QSslSocket does — e.g. its two-layer read buffer. STARTTLS is
/// covered by the live test.
///
/// Answers each connection's commands strictly in order, as a real server
/// does with pipelined requests: a held or delayed BODY holds every command
/// behind it on that connection.
///
/// No Q_OBJECT: it declares no signals or slots of its own, so it stays
/// header-only. Same choice, same reason, as tests/FakeCacheServer.h.

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QPointer>
#include <QString>
#include <QSet>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>

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

    /// Serve implicit TLS (like port 563). Set before start().
    void setImplicitTls(bool on) { m_tls = on; }

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

    /// Account connection limit, the Newshosting way: a login past @p limit
    /// concurrently authenticated connections gets "502 Too many connections."
    /// to AUTHINFO PASS. 0 (the default) is unlimited.
    void setConnectionLimit(int limit) { m_connectionLimit = limit; }
    [[nodiscard]] int refusedLogins() const { return m_refusedLogins; }

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

    /// Answer BODY for @p messageId only once releaseHeld() is called.
    ///
    /// An article stuck in flight for as long as the test wants, with the
    /// connection healthy and the socket silent — which is what the queue looks
    /// like mid-download when something else happens to it (a settings save, a
    /// pause). Unlike setMute() it holds one article, so the rest of the
    /// download carries on around it.
    void setHoldArticle(const QString& messageId) { m_holdId = normalizeId(messageId); }

    /// Serve every held BODY and stop holding.
    void releaseHeld()
    {
        m_holdId.clear();
        const auto held = m_held;
        m_held.clear();
        for (const auto& [sock, id] : held) {
            if (!sock)
                continue;
            serveBody(sock, id);
            m_conns[sock.data()].busy = false;
            pump(sock);
        }
    }

    /// Answer each STAT and BODY @p ms late, like a provider's round trip, so
    /// a client that pipelines has commands outstanding.
    void setResponseDelay(int ms) { m_responseDelayMs = ms; }

    /// Most commands ever unanswered on one connection. 2+ = the client pipelined.
    [[nodiscard]] int maxOutstanding() const { return m_maxOutstanding; }

    // -- Observation --------------------------------------------------------

    /// Every command line received, in order, across all connections.
    /// Answer 430 to STAT for @p messageId while still serving it to BODY.
    ///
    /// The asymmetry a health check is *about*. A probe's answer is a guess —
    /// NNTP has one code for expired, taken down, never propagated and "not on
    /// this server" — and this is how a wrong guess gets staged, so a test can
    /// assert that believing it still costs the download nothing.
    void setStatRefusal(const QString& messageId)
    {
        m_statRefusals.insert(normalizeId(messageId));
    }

    [[nodiscard]] const QStringList& receivedCommands() const { return m_received; }
    [[nodiscard]] int connectionCount() const { return m_connections; }

    /// Whether a password ever reached the server in the clear. Guards against a
    /// regression where AUTHINFO is sent before a requested TLS upgrade.
    [[nodiscard]] bool sawPlaintextPassword() const { return m_sawPlaintextPassword; }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        if (!m_tls) {
            QTcpServer::incomingConnection(descriptor);
            return;
        }
        // Self-signed P-256, CN/SAN localhost + 127.0.0.1, valid to 2126. Test only.
        static const QByteArray kCert =
            "-----BEGIN CERTIFICATE-----\n"
            "MIIBmzCCAUGgAwIBAgIUC1WEbmC0BI0mr3SmG+qtxthmhD8wCgYIKoZIzj0EAwIw\n"
            "FDESMBAGA1UEAwwJbG9jYWxob3N0MCAXDTI2MDkyODA4NTYyMVoYDzIxMjYwOTA0\n"
            "MDg1NjIxWjAUMRIwEAYDVQQDDAlsb2NhbGhvc3QwWTATBgcqhkjOPQIBBggqhkjO\n"
            "PQMBBwNCAAS+0ZhPfvISF3TX3kZb50KtMWbMx0RukvMYI4yYnVFLRTe7BZMyEH7L\n"
            "FzfLStKioL1VfnsgdNgLr7xQTyqJ6oRpo28wbTAdBgNVHQ4EFgQUig9YSzddLaJj\n"
            "zdew3pUxWY4wDBcwHwYDVR0jBBgwFoAUig9YSzddLaJjzdew3pUxWY4wDBcwDwYD\n"
            "VR0TAQH/BAUwAwEB/zAaBgNVHREEEzARgglsb2NhbGhvc3SHBH8AAAEwCgYIKoZI\n"
            "zj0EAwIDSAAwRQIge22a6b/s91ZSU0HlXE0NQcrH4b/HYZORBBsvPDFeDiwCIQCB\n"
            "KcSh/M2KsR1IzfGpTO7zO4owqG9roUPpLG4d4DAdag==\n"
            "-----END CERTIFICATE-----\n";
        static const QByteArray kKey =
            "-----BEGIN PRIVATE KEY-----\n"
            "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQg3pgHfe3BTofo+Hi1\n"
            "zc2QutXqGASCltKC+oaLXZWoVXShRANCAAS+0ZhPfvISF3TX3kZb50KtMWbMx0Ru\n"
            "kvMYI4yYnVFLRTe7BZMyEH7LFzfLStKioL1VfnsgdNgLr7xQTyqJ6oRp\n"
            "-----END PRIVATE KEY-----\n";
        auto* sock = new QSslSocket(this);
        if (!sock->setSocketDescriptor(descriptor)) {
            delete sock;
            return;
        }
        sock->setLocalCertificate(QSslCertificate(kCert));
        sock->setPrivateKey(QSslKey(kKey, QSsl::Ec));
        addPendingConnection(sock);   // writes queue until the handshake is done
        sock->startServerEncryption();
    }

private:
    void onNewConnection()
    {
        while (QTcpSocket* sock = nextPendingConnection()) {
            ++m_connections;
            connect(sock, &QObject::destroyed, this, [this, sock] { m_conns.remove(sock); });
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

            Conn& conn = m_conns[sock];
            conn.pending.append(line);
            m_maxOutstanding = std::max(m_maxOutstanding,
                                        int(conn.pending.size()) + (conn.busy ? 1 : 0));
            pump(sock);
            if (sock->state() != QAbstractSocket::ConnectedState)
                return;
        }
    }

    /// Answer queued commands in order until one is held or delayed.
    void pump(QTcpSocket* sock)
    {
        while (sock->state() == QAbstractSocket::ConnectedState) {
            Conn& conn = m_conns[sock];
            if (conn.busy || conn.pending.isEmpty())
                return;
            const QString line = conn.pending.takeFirst();
            const QString verb = line.section(u' ', 0, 0).toUpper();
            if (m_responseDelayMs > 0
                && (verb == QLatin1String("BODY") || verb == QLatin1String("STAT"))) {
                conn.busy = true;
                QTimer::singleShot(m_responseDelayMs, sock, [this, sock, line] {
                    m_conns[sock].busy = false;
                    handleCommand(sock, line);
                    pump(sock);
                });
                return;
            }
            handleCommand(sock, line);
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
            const bool valid = !m_rejectAuth && m_offeredUser == m_user && value == m_pass;
            if (valid && m_connectionLimit > 0 && m_authed.size() >= m_connectionLimit) {
                ++m_refusedLogins;
                writeLine(sock, QByteArrayLiteral("502 Too many connections."));
                sock->disconnectFromHost();
            } else if (valid) {
                m_authed.insert(sock);
                connect(sock, &QObject::destroyed, this, [this, sock] { m_authed.remove(sock); });
                connect(sock, &QTcpSocket::disconnected, this, [this, sock] { m_authed.remove(sock); });
                writeLine(sock, QByteArrayLiteral("281 Authentication accepted"));
            } else
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

        // Consulted here as well as in handleBody(), so a probe can be given a
        // transport failure for one named article rather than for whatever
        // command happens to arrive next.
        if (const auto drop = m_dropArticles.find(key);
            drop != m_dropArticles.end() && drop.value() > 0) {
            drop.value() -= 1;
            sock->abort();
            return;
        }

        if (m_statRefusals.contains(key) || !m_articles.contains(key)) {
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

        // Held: nothing goes out until releaseHeld(). The socket stays open and
        // silent, exactly like a provider that is simply slow.
        if (!m_holdId.isEmpty() && key == m_holdId) {
            m_held.append({QPointer<QTcpSocket>(sock), key});
            m_conns[sock].busy = true;
            return;
        }

        serveBody(sock, key);
    }

    void serveBody(QTcpSocket* sock, const QString& key)
    {
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
    bool m_tls = false;
    bool m_rejectAuth = false;
    int m_connectionLimit = 0;
    int m_refusedLogins = 0;
    QSet<QTcpSocket*> m_authed;
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

    /// The article BODY sits on until releaseHeld(), and who is waiting for it.
    QString m_holdId;
    QList<QPair<QPointer<QTcpSocket>, QString>> m_held;

    /// Articles STAT denies but BODY still serves. See setStatRefusal().
    QSet<QString> m_statRefusals;

    /// Per connection: commands not yet answered, and whether the head one is
    /// held or delayed.
    struct Conn {
        QStringList pending;
        bool busy = false;
    };
    QHash<QTcpSocket*, Conn> m_conns;
    int m_responseDelayMs = 0;
    int m_maxOutstanding = 0;

    QStringList m_received;
    int m_connections = 0;
};

} // namespace eMule::testing
