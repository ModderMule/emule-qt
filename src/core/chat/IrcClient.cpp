#include "pch.h"
/// @file IrcClient.cpp
/// @brief IRC protocol client implementation — replaces MFC CIrcMain + CIrcSocket.

#include "chat/IrcClient.h"
#include "utils/Log.h"

#include <QTcpSocket>

namespace eMule {

static constexpr auto kDefaultIrcPort = 6667;
static constexpr auto kIrcVersion = "(SMIRCv00.69)";

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

IrcClient::IrcClient(QObject* parent)
    : QObject(parent)
{
}

IrcClient::~IrcClient()
{
    if (m_socket) {
        m_socket->abort();
        delete m_socket;
    }
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void IrcClient::connectToServer(const QString& serverAddress, const QString& nick,
                                const QString& user)
{
    if (m_socket)
        disconnect();

    m_serverAddress = serverAddress;
    m_nick = nick.left(25);
    m_user = user;
    m_version = QStringLiteral("eMule Qt %1").arg(QLatin1StringView(kIrcVersion));
    m_loggedIn = false;

    // Parse host:port
    QString host = serverAddress;
    int port = kDefaultIrcPort;
    const auto colonIdx = serverAddress.lastIndexOf(u':');
    if (colonIdx > 0) {
        bool ok = false;
        const int p = serverAddress.mid(colonIdx + 1).toInt(&ok);
        if (ok && p > 0 && p <= 65535) {
            port = p;
            host = serverAddress.left(colonIdx);
        }
    }

    m_socket = new QTcpSocket(this);
    QObject::connect(m_socket, &QTcpSocket::connected,
                     this, &IrcClient::onSocketConnected);
    QObject::connect(m_socket, &QTcpSocket::disconnected,
                     this, &IrcClient::onSocketDisconnected);
    QObject::connect(m_socket, &QTcpSocket::readyRead,
                     this, &IrcClient::onSocketReadyRead);
    QObject::connect(m_socket, &QTcpSocket::errorOccurred,
                     this, &IrcClient::onSocketError);

    m_socket->connectToHost(host, static_cast<quint16>(port));
}

void IrcClient::disconnect()
{
    m_connected = false;
    m_loggedIn = false;
    m_readBuffer.clear();

    if (m_socket) {
        // Disconnect signals first to prevent onSocketDisconnected from
        // running during disconnectFromHost() and nullifying m_socket.
        QObject::disconnect(m_socket, nullptr, this, nullptr);
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    emit disconnected();
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

void IrcClient::sendRaw(const QString& line)
{
    if (!m_socket || m_socket->state() != QTcpSocket::ConnectedState)
        return;

    QByteArray data;
    if (m_enableUTF8)
        data = line.toUtf8();
    else
        data = line.toLatin1();
    data.append("\r\n");

    m_socket->write(data);
    emit rawLineSent(line);
}

void IrcClient::sendMessage(const QString& target, const QString& message)
{
    sendRaw(QStringLiteral("PRIVMSG %1 :%2").arg(target, message));
}

void IrcClient::joinChannel(const QString& channel, const QString& key)
{
    if (key.isEmpty())
        sendRaw(QStringLiteral("JOIN %1").arg(channel));
    else
        sendRaw(QStringLiteral("JOIN %1 %2").arg(channel, key));
}

void IrcClient::partChannel(const QString& channel, const QString& reason)
{
    if (reason.isEmpty())
        sendRaw(QStringLiteral("PART %1").arg(channel));
    else
        sendRaw(QStringLiteral("PART %1 :%2").arg(channel, reason));
}

void IrcClient::changeNick(const QString& newNick)
{
    sendRaw(QStringLiteral("NICK %1").arg(newNick));
}

void IrcClient::sendCtcp(const QString& target, const QString& command,
                         const QString& params)
{
    if (params.isEmpty())
        sendRaw(QStringLiteral("PRIVMSG %1 :\001%2\001").arg(target, command));
    else
        sendRaw(QStringLiteral("PRIVMSG %1 :\001%2 %3\001").arg(target, command, params));
}

void IrcClient::requestChannelList(const QString& filter)
{
    if (filter.isEmpty())
        sendRaw(QStringLiteral("LIST"));
    else
        sendRaw(QStringLiteral("LIST %1").arg(filter));
}

void IrcClient::requestMode(const QString& channel)
{
    sendRaw(QStringLiteral("MODE %1").arg(channel));
}

void IrcClient::executePerform(const QString& performString)
{
    const QStringList commands = performString.split(u'|', Qt::SkipEmptyParts);
    for (QString cmd : commands) {
        cmd = cmd.trimmed();
        if (cmd.isEmpty())
            continue;
        if (cmd.startsWith(u'/'))
            cmd.remove(0, 1);

        // Convert shorthand "msg" → "privmsg"
        if (cmd.startsWith(u"msg ", Qt::CaseInsensitive))
            cmd.prepend(QStringLiteral("priv"));

        // Convert "privmsg nickserv" → "ns"
        if (cmd.startsWith(u"privmsg nickserv", Qt::CaseInsensitive)) {
            cmd.remove(0, 16);
            cmd.prepend(QStringLiteral("ns"));
        } else if (cmd.startsWith(u"privmsg chanserv", Qt::CaseInsensitive)) {
            cmd.remove(0, 16);
            cmd.prepend(QStringLiteral("cs"));
        }

        sendRaw(cmd);
    }
}

// ---------------------------------------------------------------------------
// Socket event handlers
// ---------------------------------------------------------------------------

void IrcClient::onSocketConnected()
{
    m_connected = true;
    emit connected();

    // Send login sequence: USER + NICK (matching MFC CIrcMain::SendLogin)
    if (m_user.isEmpty())
        m_user = m_nick;
    sendRaw(QStringLiteral("USER %1 8 * :%2").arg(m_user, m_version));
    sendRaw(QStringLiteral("NICK %1").arg(m_nick));
}

void IrcClient::onSocketDisconnected()
{
    m_connected = false;
    m_loggedIn = false;
    m_readBuffer.clear();

    m_socket->deleteLater();
    m_socket = nullptr;

    emit disconnected();
}

void IrcClient::onSocketReadyRead()
{
    m_readBuffer.append(m_socket->readAll());

    // Process complete lines (terminated by \n)
    qsizetype idx;
    while ((idx = m_readBuffer.indexOf('\n')) >= 0) {
        QByteArray rawLine = m_readBuffer.left(idx);
        m_readBuffer.remove(0, idx + 1);

        // Strip trailing \r
        if (!rawLine.isEmpty() && rawLine.back() == '\r')
            rawLine.chop(1);

        if (rawLine.isEmpty())
            continue;

        QString line;
        if (m_enableUTF8)
            line = QString::fromUtf8(rawLine);
        else
            line = QString::fromLatin1(rawLine);

        processLine(line);
    }
}

void IrcClient::onSocketError()
{
    if (m_socket)
        emit socketError(m_socket->errorString());
}

// ---------------------------------------------------------------------------
// Message processing
// ---------------------------------------------------------------------------

void IrcClient::processLine(const QString& line)
{
    emit rawLineReceived(line);

    // Handle PING directly (no prefix, must respond quickly)
    if (line.startsWith(u"PING ")) {
        QString reply = line;
        reply.replace(0, 4, QStringLiteral("PONG"));
        sendRaw(reply);
        return;
    }

    // Handle ERROR
    if (line.startsWith(u"ERROR")) {
        emit statusMessage(line);
        return;
    }

    const IrcMessage msg = IrcMessage::parse(line);
    if (!msg.isValid())
        return;

    dispatchMessage(msg);
}

void IrcClient::dispatchMessage(const IrcMessage& msg)
{
    const QString& cmd = msg.command;

    if (cmd == u"PRIVMSG") {
        handlePrivMsg(msg);
        return;
    }

    if (cmd == u"JOIN") {
        const QString channel = msg.params.isEmpty() ? QString()
            : (msg.params[0].startsWith(u':') ? msg.params[0].mid(1) : msg.params[0]);
        emit userJoined(channel, msg.nickname);
        return;
    }

    if (cmd == u"PART") {
        const QString channel = msg.params.value(0);
        const QString reason = msg.params.value(1);
        emit userParted(channel, msg.nickname, reason);
        return;
    }

    if (cmd == u"QUIT") {
        const QString reason = msg.params.value(0);
        emit userQuit(msg.nickname, reason);
        return;
    }

    if (cmd == u"NICK") {
        QString newNick = msg.params.value(0);
        if (newNick.startsWith(u':'))
            newNick.remove(0, 1);
        const QString oldNick = msg.nickname;
        if (oldNick == m_nick)
            m_nick = newNick;
        emit nickChanged(oldNick, newNick);
        return;
    }

    if (cmd == u"KICK") {
        const QString channel = msg.params.value(0);
        const QString kicked = msg.params.value(1);
        const QString reason = msg.params.value(2);
        emit userKicked(channel, kicked, msg.nickname, reason);
        return;
    }

    if (cmd == u"TOPIC") {
        const QString channel = msg.params.value(0);
        const QString topic = msg.params.value(1);
        emit topicChanged(channel, msg.nickname, topic);
        return;
    }

    if (cmd == u"MODE") {
        const QString target = msg.params.value(0);
        // Collect remaining params as modes + params string
        QString modes;
        QString modeParams;
        if (msg.params.size() > 1)
            modes = msg.params[1];
        if (msg.params.size() > 2) {
            QStringList rest;
            for (int i = 2; i < msg.params.size(); ++i)
                rest.append(msg.params[i]);
            modeParams = rest.join(u' ');
        }
        emit modeChanged(target, msg.nickname, modes, modeParams);
        return;
    }

    if (cmd == u"NOTICE") {
        const QString target = msg.params.value(0);
        const QString message = msg.params.value(1);
        const QString source = msg.nickname.isEmpty() ? msg.prefix : msg.nickname;
        emit noticeReceived(source, target, message);
        return;
    }

    // Numeric commands
    if (msg.isNumeric()) {
        handleNumeric(msg);
        return;
    }

    // Unhandled — emit as status
    emit statusMessage(msg.command + u' ' + msg.params.join(u' '));
}

void IrcClient::handlePrivMsg(const IrcMessage& msg)
{
    if (msg.params.size() < 2)
        return;

    QString target = msg.params[0];
    QString message = msg.params[1];

    // Determine if private message (target is our nick) or channel
    if (!target.startsWith(u'#'))
        target = msg.nickname;

    // Check for CTCP (starts with \001)
    if (message.startsWith(u'\001') && message.endsWith(u'\001')) {
        message = message.mid(1, message.size() - 2);

        if (message.startsWith(u"ACTION ", Qt::CaseInsensitive)) {
            emit actionReceived(target, msg.nickname, message.mid(7));
            return;
        }

        // Parse CTCP command and params
        const auto spaceIdx = message.indexOf(u' ');
        QString ctcpCmd;
        QString ctcpParams;
        if (spaceIdx >= 0) {
            ctcpCmd = message.left(spaceIdx).toUpper();
            ctcpParams = message.mid(spaceIdx + 1);
        } else {
            ctcpCmd = message.toUpper();
        }

        // Auto-respond to VERSION
        if (ctcpCmd == u"VERSION") {
            sendRaw(QStringLiteral("NOTICE %1 :\001VERSION %2\001")
                        .arg(msg.nickname, m_version));
        }

        // Auto-respond to PING (CTCP ping)
        if (ctcpCmd == u"PING") {
            sendRaw(QStringLiteral("NOTICE %1 :\001PING %2\001")
                        .arg(msg.nickname, ctcpParams));
        }

        emit ctcpRequestReceived(msg.nickname, ctcpCmd, ctcpParams);
        return;
    }

    // Normal message
    if (target.startsWith(u'#'))
        emit channelMessageReceived(target, msg.nickname, message);
    else
        emit privateMessageReceived(msg.nickname, message);
}

void IrcClient::handleNumeric(const IrcMessage& msg)
{
    const int code = msg.numericCode();

    // Skip the first param (usually our nick) to get the payload
    QString payload;
    if (msg.params.size() > 1) {
        QStringList rest = msg.params.mid(1);
        payload = rest.join(u' ');
    } else if (!msg.params.isEmpty()) {
        payload = msg.params[0];
    }

    switch (code) {
    // RPL_WELCOME (001) — successfully registered
    case 1:
        m_loggedIn = true;
        emit loggedIn();
        emit statusMessage(payload);
        return;

    // RPL_YOURHOST (002), RPL_CREATED (003), RPL_MYINFO (004)
    case 2:
    case 3:
    case 4:
        emit statusMessage(payload);
        return;

    // RPL_ISUPPORT (005) — server capabilities
    case 5:
        emit serverNumeric(code, payload);
        emit statusMessage(payload);
        return;

    // RPL_LISTSTART (321)
    case 321:
        emit channelListStarted();
        emit statusMessage(QStringLiteral("Start of /LIST"));
        return;

    // RPL_LIST (322) — "<channel> <# visible> :<topic>"
    case 322:
        if (msg.params.size() >= 3) {
            const QString& channel = msg.params[1];
            const int userCount = msg.params[2].toInt();
            const QString topic = msg.params.size() > 3 ? msg.params[3] : QString();
            if (channel.startsWith(u'#'))
                emit channelListed(channel, userCount, topic);
        }
        return;

    // RPL_LISTEND (323)
    case 323:
        emit channelListFinished();
        return;

    // RPL_TOPIC (332) — "<channel> :<topic>"
    case 332:
        if (msg.params.size() >= 3) {
            const QString& channel = msg.params[1];
            const QString& topic = msg.params[2];
            emit topicChanged(channel, {}, topic);
        }
        return;

    // RPL_NAMREPLY (353) — "( "=" / "*" / "@" ) <channel> :nicks"
    case 353:
        if (msg.params.size() >= 3) {
            // params: [our nick,] <type>, <channel>, <nicks>
            // After skipping our nick (first param is handled by payload skip)
            // Find the channel (starts with #)
            QString channel;
            QString nickStr;
            for (int i = 1; i < msg.params.size(); ++i) {
                if (msg.params[i].startsWith(u'#'))
                    channel = msg.params[i];
                else if (i == msg.params.size() - 1)
                    nickStr = msg.params[i]; // trailing param = nick list
            }
            if (!channel.isEmpty() && !nickStr.isEmpty()) {
                const QStringList nicks = nickStr.split(u' ', Qt::SkipEmptyParts);
                emit namesReceived(channel, nicks);
            }
        }
        return;

    // RPL_ENDOFNAMES (366) — "<channel> :End of NAMES list"
    case 366:
        if (msg.params.size() >= 2) {
            const QString& channel = msg.params[1];
            emit namesFinished(channel);
            // Request channel mode (matching MFC behavior)
            requestMode(channel);
        }
        return;

    // ERR_NICKNAMEINUSE (433)
    case 433:
        emit nickInUse(msg.params.value(1));
        emit statusMessage(payload);
        return;

    // RPL_CHANNELMODEIS (324) — "<channel> <mode> <mode params>"
    case 324:
        if (msg.params.size() >= 3) {
            const QString& channel = msg.params[1];
            const QString& modes = msg.params[2];
            QString modeParams;
            if (msg.params.size() > 3) {
                QStringList rest = msg.params.mid(3);
                modeParams = rest.join(u' ');
            }
            emit modeChanged(channel, {}, modes, modeParams);
        }
        return;

    default:
        break;
    }

    // Generic: emit as server numeric + status
    emit serverNumeric(code, payload);
    emit statusMessage(payload);
}

} // namespace eMule
