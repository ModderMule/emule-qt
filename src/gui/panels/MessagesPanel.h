#pragma once

/// @file MessagesPanel.h
/// @brief Messages tab panel replicating the MFC eMule Messages window.
///
/// Layout (matching Messages screenshots):
///   - Left:  Friend list (QListView) with "Friends (N)" header + Info section
///   - Right: Chat area with "Messages" icon+label header, per-friend chat tabs,
///            message input, Send/Close buttons

#include "IpcMessage.h"

#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QPushButton;
class QSplitter;
class QTabBar;
class QTextBrowser;
class QTimer;
class QToolButton;

namespace eMule {

class FriendListModel;
class IpcClient;
class PanelPoller;

struct ChatMsg {
    QString sender;
    QString text;
    bool    outgoing = false;
    /// A status line from the connection attempt ("*** Connecting"), shown without a
    /// sender and in grey, as MFC's chat window does with STATUS_MSG_COLOR.
    bool    system = false;
    qint64  timestamp = 0;
};

class MessagesPanel : public QWidget {
    Q_OBJECT

public:
    explicit MessagesPanel(QWidget* parent = nullptr);
    ~MessagesPanel() override;

    void setIpcClient(IpcClient* client);

    /// Set a custom font on the chat browser.
    void setCustomFont(const QFont& font);

signals:
    /// A link in the chat was clicked. Carries the link as PLAIN TEXT, not a QUrl:
    /// an eD2K link is not a representable QUrl, and one built from it stringifies
    /// back to an empty string (see TextLinks.h). main.cpp routes it.
    void linkActivated(const QString& link);

    /// Unread chat, for the status bar's message icon (MFC ShowMessageState):
    /// 0 none, 1 and 2 the two blink phases.
    void messageStateChanged(int state);

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void onFriendClicked(const QModelIndex& index);
    void onSendClicked();
    void onChatStatePush(const Ipc::IpcMessage& msg);
    void onCloseClicked();
    void onRefreshTimer();
    void onFriendContextMenu(const QPoint& pos);
    void onChatMessagePush(const Ipc::IpcMessage& msg);
    void onFriendListPush(const Ipc::IpcMessage& msg);
    void showSmileySelector();
    void onChatTabChanged(int index);
    void onChatTabCloseRequested(int index);

private:
    void setupUi();
    void requestFriendList();
    void updateInfoSection(int row);
    void updateChatDisplay();
    void appendChatStatus(const QString& friendHash, const QString& text);
    void appendChatMessage(const QString& friendHash, const QString& sender,
                           const QString& text, bool outgoing);
    [[nodiscard]] QString saveSelection() const;
    void restoreSelection(const QString& key);

    void showAddFriendDialog();
    void showFindDialog();

    [[nodiscard]] int findTabByHash(const QString& friendHash) const;
    /// @p activate false opens it in the background, as an incoming message does.
    void openChatTab(const QString& friendHash, const QString& friendName, bool activate = true);
    void closeChatTab(int tabIndex);
    void updateTabBarVisibility();

    /// Mark or clear a session as unread.
    void setNotify(const QString& friendHash, bool on);
    /// Tab icons, tab text colour and the status-bar state for unread sessions.
    void refreshNotifyCues();

    // Models
    FriendListModel* m_friendModel = nullptr;

    // Views
    QListView*    m_friendListView = nullptr;
    QLabel*       m_friendsLabel = nullptr;
    QTextBrowser* m_chatBrowser = nullptr;
    QLineEdit*    m_messageInput = nullptr;
    QToolButton*  m_smileyBtn = nullptr;
    QPushButton*  m_sendBtn = nullptr;
    QPushButton*  m_closeBtn = nullptr;

    // Chat tab bar (per-friend tabs)
    QTabBar* m_chatTabBar = nullptr;

    // Info section labels
    QLabel* m_infoName = nullptr;
    QLabel* m_infoHash = nullptr;
    QLabel* m_infoSoftware = nullptr;
    QLabel* m_infoIdent = nullptr;
    QLabel* m_infoUploaded = nullptr;
    QLabel* m_infoDownloaded = nullptr;

    // Context menu
    QMenu* m_contextMenu = nullptr;

    // Splitter
    QSplitter* m_splitter = nullptr;

    // Refresh timer
    /// Friend list refresh, suspended while this panel is not the visible tab.
    PanelPoller* m_poller = nullptr;

    // IPC client
    IpcClient* m_ipc = nullptr;

    // Active chat friend hash
    QString m_activeFriendHash;

    // Chat history (session-only, keyed by friend hash)
    QMap<QString, QVector<ChatMsg>> m_chatHistory;

    // Unread sessions (MFC chat item `notify`), blinking while any are left
    QSet<QString> m_notifyHashes;
    QTimer* m_blinkTimer = nullptr;
    bool m_blinkOn = true;
    int m_messageState = 0;
};

} // namespace eMule
