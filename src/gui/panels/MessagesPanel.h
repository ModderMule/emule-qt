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

struct ChatMsg {
    QString sender;
    QString text;
    bool    outgoing = false;
    qint64  timestamp = 0;
};

class MessagesPanel : public QWidget {
    Q_OBJECT

public:
    explicit MessagesPanel(QWidget* parent = nullptr);
    ~MessagesPanel() override;

    void setIpcClient(IpcClient* client);

private slots:
    void onFriendClicked(const QModelIndex& index);
    void onSendClicked();
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
    void appendChatMessage(const QString& friendHash, const QString& sender,
                           const QString& text, bool outgoing);
    [[nodiscard]] QString saveSelection() const;
    void restoreSelection(const QString& key);

    void showAddFriendDialog();
    void showFindDialog();
    [[nodiscard]] QString renderSmileys(const QString& text) const;

    [[nodiscard]] int findTabByHash(const QString& friendHash) const;
    void openChatTab(const QString& friendHash, const QString& friendName);
    void closeChatTab(int tabIndex);
    void updateTabBarVisibility();

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
    QTimer* m_refreshTimer = nullptr;

    // IPC client
    IpcClient* m_ipc = nullptr;

    // Active chat friend hash
    QString m_activeFriendHash;

    // Chat history (session-only, keyed by friend hash)
    QMap<QString, QVector<ChatMsg>> m_chatHistory;
};

} // namespace eMule
