#pragma once

/// @file IrcPanel.h
/// @brief IRC tab panel replicating the MFC eMule IRC window.
///
/// Layout (matching IRC screenshots):
///   - Left:  Nick list (QListView) with "Nick (N)" header
///   - Right: Tabbed area with Status, Channels, and dynamic channel tabs
///   - Bottom: Connect/Close buttons, format toolbar, input field, Send button

#include "utils/InputHistory.h"

#include <QFont>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;
class QStringListModel;
class QTabWidget;
class QTextBrowser;
class QToolButton;
class QTreeWidget;

namespace eMule {

class IpcClient;

class IrcClient;

/// mIRC formatting state, carried across the pieces of one message.
///
/// A message is split at its links so they can be linkified from the raw text, and
/// bold/italic/colour has to survive those cuts — hence the state lives here rather
/// than inside renderMircCodes().
struct MircFormat {
    bool bold      = false;
    bool italic    = false;
    bool underline = false;
    int  fg        = -1;
    int  bg        = -1;
    bool inSpan    = false;

    /// Close the open <span>, if any. Returns the markup to append.
    [[nodiscard]] QString close();
};

/// Per-channel state for the IRC panel.
struct IrcChannel {
    enum Type { Status, ChannelList, Normal, Private };

    QString name;
    QStringList nicks;
    QString topic;
    QWidget* widget = nullptr;   ///< QTextBrowser or QTreeWidget in the tab
    InputHistory history;        ///< Up/Down recall (IrcWnd.cpp:458)
    QString typed;               ///< Tab completion: what the user typed (MFC m_sTyped)
    QString tabbed;              ///< Tab completion: last completed input (MFC m_sTabd)
    Type type = Normal;
};

class IrcPanel : public QWidget {
    Q_OBJECT

public:
    explicit IrcPanel(QWidget* parent = nullptr);
    ~IrcPanel() override;

    /// Set a custom font on all IRC text browsers.
    void setCustomFont(const QFont& font);

    /// The daemon link, for eMule's add-friend / send-link exchange only: our own
    /// identity for the reply, and the friend list.
    void setIpcClient(IpcClient* ipc) { m_ipc = ipc; }

    /// The eD2K link "Send this to friend" offers, set from Shared Files' "Add To
    /// IRC Clipboard" (MFC CIrcWnd::SetSendFileString).
    void setSendLink(const QString& link) { m_sendLink = link; }

    [[nodiscard]] bool isIrcConnected() const;

signals:
    /// A link in a channel, private or status tab was clicked. Carries the link as
    /// PLAIN TEXT, not a QUrl: an eD2K link is not a representable QUrl, and one
    /// built from it stringifies back to an empty string (see TextLinks.h).
    /// main.cpp routes it, which is why this panel needs no IpcClient of its own.
    void linkActivated(const QString& link);
    /// A peer sent an eD2K link and the options let it in: start it.
    void linkReceived(const QString& link);
    /// Logged in or gone; Shared Files greys "Add To IRC Clipboard" with it.
    void ircConnectionChanged(bool connected);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    // Connection flow
    void onConnectClicked();
    void onCloseClicked();
    void onSendClicked();
    void onTabChanged(int index);
    void onTabCloseRequested(int index);

    // IRC signals
    void onIrcConnected();
    void onIrcLoggedIn();
    void onIrcDisconnected();
    void onIrcSocketError(const QString& error);
    void onStatusMessage(const QString& message);
    void onChannelMessage(const QString& channel, const QString& nick,
                          const QString& message);
    void onPrivateMessage(const QString& nick, const QString& message);
    void onActionReceived(const QString& target, const QString& nick,
                          const QString& message);
    void onNoticeReceived(const QString& source, const QString& target,
                          const QString& message);
    void onUserJoined(const QString& channel, const QString& nick);
    void onUserParted(const QString& channel, const QString& nick,
                      const QString& reason);
    void onUserQuit(const QString& nick, const QString& reason);
    void onUserKicked(const QString& channel, const QString& nick,
                      const QString& by, const QString& reason);
    void onNickChanged(const QString& oldNick, const QString& newNick);
    void onTopicChanged(const QString& channel, const QString& nick,
                        const QString& topic);
    void onNamesReceived(const QString& channel, const QStringList& nicks);
    void onNamesFinished(const QString& channel);
    void onChannelListed(const QString& channel, int userCount,
                         const QString& topic);
    void onChannelListStarted();
    void onChannelListFinished();
    void onNickInUse(const QString& nick);
    void onEmuleProto(const QString& nick, const QString& body);
    void onNickContextMenu(const QPoint& pos);

private:
    void setupUi();
    void connectIrcSignals();

    // Tab/channel management
    int findTab(const QString& name) const;
    int ensureChannelTab(const QString& name, IrcChannel::Type type = IrcChannel::Normal);
    void removeChannelTab(const QString& name);
    void removeAllChannelTabs();
    [[nodiscard]] QString activeChannelName() const;
    IrcChannel* activeChannel();

    // Display helpers
    void appendToChannel(const QString& channel, const QString& html);
    void appendToStatus(const QString& html);
    [[nodiscard]] QString formatTimestamp() const;
    [[nodiscard]] QString renderMircCodes(QStringView text, MircFormat& fmt) const;
    [[nodiscard]] QString formatMessage(const QString& text) const;

    // Nick list
    void updateNickList();
    /// The selected nick without its mode prefix; empty when none.
    [[nodiscard]] QString selectedNick() const;
    void openPrivateChannel(const QString& nick);
    void joinSelectedChannels(QTreeWidget* tree);

    // eMule's CTCP extensions (IrcEmuleProto.h)
    void requestFriend(const QString& nick);
    void sendLinkTo(const QString& nick);
    void answerFriendRequest(const QString& nick, const QString& verify);
    void protoNotice(const QString& text);

    // Input processing
    void processInput(const QString& text);
    void handleSlashCommand(const QString& cmd, const QString& args);
    void addToHistory(const QString& text);
    /// Tab: complete the last word to the next matching nick (IrcChannelTabCtrl.cpp:84).
    void autoCompleteNick();

    // Format buttons
    void insertFormatCode(char code);
    void showColorPopup();
    void showSmileySelector();

    // Members
    IrcClient* m_irc = nullptr;
    IpcClient* m_ipc = nullptr;
    QString m_sendLink;             ///< link "Send this to friend" offers
    quint32 m_verify = 0;           ///< nonce of our pending friend request, 0 = none
    qint64 m_lastProtoRequestMs = 0;   ///< flood guard, as MFC m_dwLastRequest

    // Layout widgets
    QSplitter* m_splitter = nullptr;
    QLabel* m_nickLabel = nullptr;
    QListView* m_nickListView = nullptr;
    QStringListModel* m_nickModel = nullptr;
    QTabWidget* m_tabWidget = nullptr;
    QTextBrowser* m_statusBrowser = nullptr;

    // Bottom bar
    QPushButton* m_connectBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;
    QToolButton* m_smileyBtn = nullptr;
    QToolButton* m_boldBtn = nullptr;
    QToolButton* m_italicBtn = nullptr;
    QToolButton* m_underlineBtn = nullptr;
    QToolButton* m_colorBtn = nullptr;
    QToolButton* m_resetBtn = nullptr;
    QLineEdit* m_input = nullptr;
    QPushButton* m_sendBtn = nullptr;

    // Channel state
    QMap<QString, IrcChannel> m_channels; // keyed by lowercase name
    QString m_statusKey;                  // key for Status tab in m_channels
    QFont m_customFont;                   // custom font for text browsers

    // Channel list accumulator
    QTreeWidget* m_channelListWidget = nullptr;
    bool m_channelListPending = false;
};

} // namespace eMule
