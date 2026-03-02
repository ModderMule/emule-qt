#pragma once

/// @file IrcPanel.h
/// @brief IRC tab panel replicating the MFC eMule IRC window.
///
/// Layout (matching IRC screenshots):
///   - Left:  Nick list (QListView) with "Nick (N)" header
///   - Right: Tabbed area with Status, Channels, and dynamic channel tabs
///   - Bottom: Connect/Close buttons, format toolbar, input field, Send button

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

class IrcClient;

/// Per-channel state for the IRC panel.
struct IrcChannel {
    enum Type { Status, ChannelList, Normal, Private };

    QString name;
    QStringList nicks;
    QString topic;
    QWidget* widget = nullptr;   ///< QTextBrowser or QTreeWidget in the tab
    QVector<QString> inputHistory;
    int historyPos = -1;
    Type type = Normal;
};

class IrcPanel : public QWidget {
    Q_OBJECT

public:
    explicit IrcPanel(QWidget* parent = nullptr);
    ~IrcPanel() override;

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
    [[nodiscard]] QString renderMircCodes(const QString& text) const;
    [[nodiscard]] QString renderSmileys(const QString& text) const;
    [[nodiscard]] QString detectUrls(const QString& text) const;
    [[nodiscard]] QString formatMessage(const QString& text) const;

    // Nick list
    void updateNickList();

    // Input processing
    void processInput(const QString& text);
    void handleSlashCommand(const QString& cmd, const QString& args);
    void addToHistory(const QString& text);

    // Format buttons
    void insertFormatCode(char code);
    void showColorPopup();
    void showSmileySelector();

    // Members
    IrcClient* m_irc = nullptr;

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

    // Channel list accumulator
    QTreeWidget* m_channelListWidget = nullptr;
    bool m_channelListPending = false;
};

} // namespace eMule
