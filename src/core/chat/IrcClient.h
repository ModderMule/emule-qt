#pragma once

/// @file IrcClient.h
/// @brief IRC protocol client — replaces MFC CIrcMain + CIrcSocket.
///
/// QObject-based IRC client that uses QTcpSocket internally and emits
/// signals for all protocol events. Decoupled from GUI — the GUI module
/// connects to these signals to display messages, nick lists, etc.

#include "chat/IrcMessage.h"
#include "utils/Types.h"

#include <QObject>
#include <QString>
#include <QStringList>

class QTcpSocket;

namespace eMule {

// ---------------------------------------------------------------------------
// IrcClient — QObject IRC protocol handler
// ---------------------------------------------------------------------------

class IrcClient : public QObject {
    Q_OBJECT

public:
    explicit IrcClient(QObject* parent = nullptr);
    ~IrcClient() override;

    // -- Connection -----------------------------------------------------------

    /// Connect to an IRC server. Address format: "host:port" or just "host".
    void connectToServer(const QString& serverAddress, const QString& nick,
                         const QString& user = {});

    /// Disconnect from the current server.
    void disconnect();

    /// Whether currently connected and logged in.
    [[nodiscard]] bool isConnected() const { return m_connected; }
    [[nodiscard]] bool isLoggedIn() const { return m_loggedIn; }

    // -- Sending commands -----------------------------------------------------

    /// Send a raw IRC line (without trailing \r\n).
    void sendRaw(const QString& line);

    /// Send a PRIVMSG to a channel or user.
    void sendMessage(const QString& target, const QString& message);

    /// Join a channel.
    void joinChannel(const QString& channel, const QString& key = {});

    /// Part (leave) a channel.
    void partChannel(const QString& channel, const QString& reason = {});

    /// Change nick.
    void changeNick(const QString& newNick);

    /// Send a CTCP request.
    void sendCtcp(const QString& target, const QString& command,
                  const QString& params = {});

    /// Request channel list from server.
    void requestChannelList(const QString& filter = {});

    /// Request the mode of a channel.
    void requestMode(const QString& channel);

    // -- Accessors ------------------------------------------------------------

    [[nodiscard]] QString currentNick() const { return m_nick; }
    [[nodiscard]] QString serverAddress() const { return m_serverAddress; }

    // -- Perform string -------------------------------------------------------

    /// Execute a |-delimited perform string (e.g. "/join #chan|/msg nickserv identify pw").
    void executePerform(const QString& performString);

signals:
    // -- Connection state -----------------------------------------------------
    void connected();
    void loggedIn();
    void disconnected();
    void socketError(const QString& errorMessage);

    // -- Messages -------------------------------------------------------------
    void channelMessageReceived(const QString& channel, const QString& nick,
                                const QString& message);
    void privateMessageReceived(const QString& nick, const QString& message);
    void actionReceived(const QString& target, const QString& nick,
                        const QString& message);
    void noticeReceived(const QString& source, const QString& target,
                        const QString& message);

    // -- Channel events -------------------------------------------------------
    void userJoined(const QString& channel, const QString& nick);
    void userParted(const QString& channel, const QString& nick,
                    const QString& reason);
    void userQuit(const QString& nick, const QString& reason);
    void userKicked(const QString& channel, const QString& nick,
                    const QString& by, const QString& reason);
    void nickChanged(const QString& oldNick, const QString& newNick);
    void topicChanged(const QString& channel, const QString& nick,
                      const QString& topic);
    void modeChanged(const QString& target, const QString& nick,
                     const QString& modes, const QString& params);

    // -- Server info ----------------------------------------------------------
    void statusMessage(const QString& message);
    void serverNumeric(int code, const QString& params);
    void nickInUse(const QString& nick);

    // -- Channel list (RPL_LIST) ----------------------------------------------
    void channelListed(const QString& channel, int userCount,
                       const QString& topic);
    void channelListStarted();
    void channelListFinished();

    // -- Nick list (RPL_NAMREPLY) ---------------------------------------------
    void namesReceived(const QString& channel, const QStringList& nicks);
    void namesFinished(const QString& channel);

    // -- CTCP -----------------------------------------------------------------
    void ctcpRequestReceived(const QString& nick, const QString& command,
                             const QString& params);
    void ctcpReplyReceived(const QString& nick, const QString& command,
                           const QString& params);

    // -- Raw (for logging/debugging) ------------------------------------------
    void rawLineReceived(const QString& line);
    void rawLineSent(const QString& line);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError();

private:
    void processLine(const QString& line);
    void dispatchMessage(const IrcMessage& msg);
    void handlePrivMsg(const IrcMessage& msg);
    void handleNumeric(const IrcMessage& msg);

    QTcpSocket* m_socket = nullptr;
    QByteArray m_readBuffer;
    QString m_serverAddress;
    QString m_nick;
    QString m_user;
    QString m_version;
    bool m_connected = false;
    bool m_loggedIn = false;
    bool m_enableUTF8 = true;
};

} // namespace eMule
