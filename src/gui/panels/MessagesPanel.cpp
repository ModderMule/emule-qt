/// @file MessagesPanel.cpp
/// @brief Messages tab panel implementation — matches MFC eMule Messages window.

#include "panels/MessagesPanel.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/FriendListModel.h"
#include "dialogs/AddFriendDialog.h"

#include "IpcProtocol.h"

#include <QAction>
#include <QCborArray>
#include <QCborValue>
#include <QCborMap>
#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QTabBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace eMule {

using namespace Ipc;

MessagesPanel::MessagesPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

MessagesPanel::~MessagesPanel() = default;

void MessagesPanel::setIpcClient(IpcClient* client)
{
    m_ipc = client;

    connect(m_ipc, &IpcClient::connected, this, [this]() {
        requestFriendList();
    });

    connect(m_ipc, &IpcClient::chatMessageReceived,
            this, &MessagesPanel::onChatMessagePush);
    connect(m_ipc, &IpcClient::friendListChanged,
            this, &MessagesPanel::onFriendListPush);

    // Start refresh timer
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(5000);
    connect(m_refreshTimer, &QTimer::timeout, this, &MessagesPanel::onRefreshTimer);
    m_refreshTimer->start();

    if (m_ipc->isConnected())
        requestFriendList();
}

void MessagesPanel::setCustomFont(const QFont& font)
{
    if (m_chatBrowser)
        m_chatBrowser->setFont(font);
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void MessagesPanel::onFriendClicked(const QModelIndex& index)
{
    if (!index.isValid())
        return;

    const auto* row = m_friendModel->rowAt(index.row());
    if (!row)
        return;

    updateInfoSection(index.row());
    openChatTab(row->hash, row->name.isEmpty() ? row->hash : row->name);
    // openChatTab sets the current tab, which fires onChatTabChanged,
    // which sets m_activeFriendHash and calls updateChatDisplay()
    m_messageInput->setFocus();
}

void MessagesPanel::onSendClicked()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    if (m_activeFriendHash.isEmpty())
        return;

    const QString text = m_messageInput->text().trimmed();
    if (text.isEmpty())
        return;

    IpcMessage msg(IpcMsgType::SendChatMessage);
    msg.append(m_activeFriendHash);
    msg.append(text);

    m_ipc->sendRequest(std::move(msg), [this, text](const IpcMessage& resp) {
        if (resp.fieldBool(0)) {
            // Success — append outgoing message to local history
            appendChatMessage(m_activeFriendHash, tr("Me"), text, true);
            updateChatDisplay();
        }
    });

    m_messageInput->clear();
}

void MessagesPanel::onCloseClicked()
{
    const int current = m_chatTabBar->currentIndex();
    if (current < 0)
        return;
    closeChatTab(current);
}

void MessagesPanel::onRefreshTimer()
{
    if (m_ipc && m_ipc->isConnected())
        requestFriendList();
}

void MessagesPanel::onFriendContextMenu(const QPoint& pos)
{
    m_contextMenu->exec(m_friendListView->viewport()->mapToGlobal(pos));
}

void MessagesPanel::onChatMessagePush(const IpcMessage& msg)
{
    const QString senderHash = msg.fieldString(0);
    const QString senderName = msg.fieldString(1);
    const QString message    = msg.fieldString(2);

    appendChatMessage(senderHash, senderName, message, false);

    // Auto-open a tab for the sender if one doesn't exist
    if (findTabByHash(senderHash) < 0)
        openChatTab(senderHash, senderName);

    // If this is the active chat, refresh the display
    if (senderHash == m_activeFriendHash)
        updateChatDisplay();
}

void MessagesPanel::onFriendListPush(const IpcMessage& /*msg*/)
{
    requestFriendList();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void MessagesPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    mainLayout->addWidget(m_splitter);

    // --- Left panel: Friends list + Info ---
    auto* leftWidget = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(4, 2, 0, 2);

    // Friends header with icon
    auto* friendsHeader = new QHBoxLayout;
    auto* friendsIcon = new QLabel(leftWidget);
    friendsIcon->setPixmap(QIcon(QStringLiteral(":/icons/User.ico")).pixmap(16, 16));
    friendsHeader->addWidget(friendsIcon);
    m_friendsLabel = new QLabel(tr("Friends (0)"), leftWidget);
    auto headerFont = m_friendsLabel->font();
    headerFont.setBold(true);
    m_friendsLabel->setFont(headerFont);
    friendsHeader->addWidget(m_friendsLabel);
    friendsHeader->addStretch();
    leftLayout->addLayout(friendsHeader);

    // Friend list view
    m_friendModel = new FriendListModel(this);
    m_friendListView = new QListView(leftWidget);
    m_friendListView->setModel(m_friendModel);
    m_friendListView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_friendListView->setContextMenuPolicy(Qt::CustomContextMenu);
    leftLayout->addWidget(m_friendListView, 1);

    connect(m_friendListView, &QListView::clicked,
            this, &MessagesPanel::onFriendClicked);
    connect(m_friendListView, &QWidget::customContextMenuRequested,
            this, &MessagesPanel::onFriendContextMenu);

    // Info section
    auto* infoGroup = new QGroupBox(tr("Info"), leftWidget);
    auto* infoLayout = new QVBoxLayout(infoGroup);
    infoLayout->setSpacing(2);

    auto makeInfoRow = [&](const QString& label) -> QLabel* {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel(label, infoGroup);
        lbl->setStyleSheet(QStringLiteral("font-weight: bold;"));
        auto* val = new QLabel(QStringLiteral("-"), infoGroup);
        row->addWidget(lbl);
        row->addWidget(val, 1);
        infoLayout->addLayout(row);
        return val;
    };

    m_infoName       = makeInfoRow(tr("Name:"));
    m_infoHash       = makeInfoRow(tr("Hash:"));
    m_infoSoftware   = makeInfoRow(tr("Software:"));
    m_infoIdent      = makeInfoRow(tr("Identification:"));
    m_infoUploaded   = makeInfoRow(tr("Uploaded:"));
    m_infoDownloaded = makeInfoRow(tr("Downloaded:"));

    leftLayout->addWidget(infoGroup);

    m_splitter->addWidget(leftWidget);

    // --- Right panel: Chat area ---
    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 2, 4, 2);

    // "Messages" static header with icon (matching MFC style)
    auto* messagesHeader = new QHBoxLayout;
    auto* messagesIcon = new QLabel(rightWidget);
    messagesIcon->setPixmap(QIcon(QStringLiteral(":/icons/Messages.ico")).pixmap(16, 16));
    messagesHeader->addWidget(messagesIcon);
    auto* messagesLabel = new QLabel(tr("Messages"), rightWidget);
    auto msgFont = messagesLabel->font();
    msgFont.setBold(true);
    messagesLabel->setFont(msgFont);
    messagesHeader->addWidget(messagesLabel);
    messagesHeader->addStretch();
    rightLayout->addLayout(messagesHeader);

    // Per-friend chat tab bar (hidden when no chats are open)
    m_chatTabBar = new QTabBar(rightWidget);
    m_chatTabBar->setTabsClosable(true);
    m_chatTabBar->setExpanding(false);
    m_chatTabBar->setVisible(false);
    connect(m_chatTabBar, &QTabBar::currentChanged,
            this, &MessagesPanel::onChatTabChanged);
    connect(m_chatTabBar, &QTabBar::tabCloseRequested,
            this, &MessagesPanel::onChatTabCloseRequested);
    rightLayout->addWidget(m_chatTabBar, 0, Qt::AlignLeft);

    // Chat browser
    m_chatBrowser = new QTextBrowser(rightWidget);
    m_chatBrowser->setOpenExternalLinks(false);
    m_chatBrowser->setReadOnly(true);
    rightLayout->addWidget(m_chatBrowser, 1);

    // Message input + buttons
    auto* inputLayout = new QHBoxLayout;

    m_smileyBtn = new QToolButton(rightWidget);
    m_smileyBtn->setIcon(QIcon(QStringLiteral(":/smileys/Smiley_Smile.ico")));
    m_smileyBtn->setIconSize(QSize(20, 20));
    m_smileyBtn->setAutoRaise(true);
    m_smileyBtn->setToolTip(tr("Smileys"));
    connect(m_smileyBtn, &QToolButton::clicked, this, &MessagesPanel::showSmileySelector);
    inputLayout->addWidget(m_smileyBtn);

    m_messageInput = new QLineEdit(rightWidget);
    m_messageInput->setPlaceholderText(tr("Type a message..."));
    inputLayout->addWidget(m_messageInput, 1);

    m_sendBtn = new QPushButton(tr("Send"), rightWidget);
    m_closeBtn = new QPushButton(tr("Close"), rightWidget);
    inputLayout->addWidget(m_sendBtn);
    inputLayout->addWidget(m_closeBtn);
    rightLayout->addLayout(inputLayout);

    connect(m_sendBtn, &QPushButton::clicked, this, &MessagesPanel::onSendClicked);
    connect(m_closeBtn, &QPushButton::clicked, this, &MessagesPanel::onCloseClicked);
    connect(m_messageInput, &QLineEdit::returnPressed, this, &MessagesPanel::onSendClicked);

    m_splitter->addWidget(rightWidget);

    // Default split: ~25% friends, ~75% chat
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 3);

    theUiState.bindMessagesSplitter(m_splitter);

    // --- Context menu ---
    m_contextMenu = new QMenu(this);

    auto* addAction = m_contextMenu->addAction(QIcon(QStringLiteral(":/icons/UserAdd.ico")), tr("Add..."));
    connect(addAction, &QAction::triggered, this, &MessagesPanel::showAddFriendDialog);

    auto* removeAction = m_contextMenu->addAction(QIcon(QStringLiteral(":/icons/UserDelete.ico")), tr("Remove"));
    connect(removeAction, &QAction::triggered, this, [this]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        const auto sel = m_friendListView->currentIndex();
        if (!sel.isValid())
            return;
        const auto* row = m_friendModel->rowAt(sel.row());
        if (!row)
            return;

        IpcMessage msg(IpcMsgType::RemoveFriend);
        msg.append(row->hash);
        m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
            requestFriendList();
        });
    });

    auto* sendMsgAction = m_contextMenu->addAction(QIcon(QStringLiteral(":/icons/UserMessage.ico")), tr("Send Message"));
    connect(sendMsgAction, &QAction::triggered, this, [this]() {
        const auto sel = m_friendListView->currentIndex();
        if (sel.isValid())
            onFriendClicked(sel);
    });

    auto* viewSharedAction = m_contextMenu->addAction(QIcon(QStringLiteral(":/icons/SharedFilesList.ico")), tr("View Shared Files"));
    connect(viewSharedAction, &QAction::triggered, this, [this]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        const auto sel = m_friendListView->currentIndex();
        if (!sel.isValid())
            return;
        const auto* row = m_friendModel->rowAt(sel.row());
        if (!row)
            return;

        IpcMessage msg(IpcMsgType::RequestClientSharedFiles);
        msg.append(row->hash);
        m_ipc->sendRequest(std::move(msg));
    });

    auto* friendSlotAction = m_contextMenu->addAction(QIcon(QStringLiteral(":/icons/FriendSlot.ico")), tr("Establish Friend Slot"));
    friendSlotAction->setCheckable(true);
    connect(friendSlotAction, &QAction::triggered, this, [this, friendSlotAction]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        const auto sel = m_friendListView->currentIndex();
        if (!sel.isValid())
            return;
        const auto* row = m_friendModel->rowAt(sel.row());
        if (!row)
            return;

        IpcMessage msg(IpcMsgType::SetFriendSlot);
        msg.append(row->hash);
        msg.append(friendSlotAction->isChecked());
        m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
            requestFriendList();
        });
    });

    m_contextMenu->addSeparator();

    auto* findAction = m_contextMenu->addAction(QIcon(QStringLiteral(":/icons/Search.ico")), tr("Find..."));
    connect(findAction, &QAction::triggered, this, &MessagesPanel::showFindDialog);

    // Update context menu state before showing
    connect(m_contextMenu, &QMenu::aboutToShow, this,
            [this, removeAction, sendMsgAction, viewSharedAction, friendSlotAction]() {
        const bool hasSel = m_friendListView->currentIndex().isValid();
        removeAction->setEnabled(hasSel);
        sendMsgAction->setEnabled(hasSel);
        viewSharedAction->setEnabled(hasSel);
        friendSlotAction->setEnabled(hasSel);

        if (hasSel) {
            const auto* row = m_friendModel->rowAt(m_friendListView->currentIndex().row());
            friendSlotAction->setChecked(row && row->friendSlot);
        }
    });
}

void MessagesPanel::requestFriendList()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    const QString sel = saveSelection();

    IpcMessage msg(IpcMsgType::GetFriends);
    m_ipc->sendRequest(std::move(msg), [this, sel](const IpcMessage& resp) {
        const QCborArray friends = resp.fieldArray(1);
        m_friendModel->refreshFromCborArray(friends);
        m_friendsLabel->setText(tr("Friends (%1)").arg(m_friendModel->rowCount()));
        restoreSelection(sel);
    });
}

void MessagesPanel::updateInfoSection(int row)
{
    const auto* r = row >= 0 ? m_friendModel->rowAt(row) : nullptr;
    if (!r) {
        m_infoName->setText(QStringLiteral("-"));
        m_infoHash->setText(QStringLiteral("-"));
        m_infoSoftware->setText(QStringLiteral("-"));
        m_infoIdent->setText(QStringLiteral("-"));
        m_infoUploaded->setText(QStringLiteral("-"));
        m_infoDownloaded->setText(QStringLiteral("-"));
        return;
    }

    m_infoName->setText(r->name.isEmpty() ? QStringLiteral("-") : r->name);
    m_infoHash->setText(r->hash.isEmpty() ? QStringLiteral("-") : r->hash);
    m_infoSoftware->setText(QStringLiteral("-"));
    m_infoIdent->setText(QStringLiteral("-"));
    m_infoUploaded->setText(QStringLiteral("-"));
    m_infoDownloaded->setText(QStringLiteral("-"));
}

void MessagesPanel::updateChatDisplay()
{
    m_chatBrowser->clear();

    if (m_activeFriendHash.isEmpty())
        return;

    const auto& msgs = m_chatHistory.value(m_activeFriendHash);
    for (const auto& msg : msgs) {
        const QString ts = QDateTime::fromSecsSinceEpoch(msg.timestamp)
                               .toString(QStringLiteral("HH:mm:ss"));
        const QString color = msg.outgoing ? QStringLiteral("#3399FF")
                                            : QStringLiteral("#CC0000");
        const QString escapedText = renderSmileys(msg.text.toHtmlEscaped());
        m_chatBrowser->append(
            QStringLiteral("<font color='gray'>[%1]</font> "
                           "<font color='%2'><b>%3:</b></font> %4")
                .arg(ts, color, msg.sender.toHtmlEscaped(), escapedText));
    }
}

void MessagesPanel::appendChatMessage(const QString& friendHash, const QString& sender,
                                       const QString& text, bool outgoing)
{
    ChatMsg cm;
    cm.sender = sender;
    cm.text = text;
    cm.outgoing = outgoing;
    cm.timestamp = QDateTime::currentSecsSinceEpoch();
    m_chatHistory[friendHash].append(std::move(cm));
}

QString MessagesPanel::saveSelection() const
{
    const auto idx = m_friendListView->currentIndex();
    if (!idx.isValid())
        return {};
    const auto* row = m_friendModel->rowAt(idx.row());
    return row ? row->hash : QString();
}

void MessagesPanel::restoreSelection(const QString& key)
{
    if (key.isEmpty())
        return;
    const int row = m_friendModel->findByHash(key);
    if (row >= 0) {
        m_friendListView->setCurrentIndex(m_friendModel->index(row));
    }
}

void MessagesPanel::showAddFriendDialog()
{
    AddFriendDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage msg(IpcMsgType::AddFriend);
    msg.append(dlg.friendHash());
    msg.append(dlg.friendName());
    msg.append(static_cast<int64_t>(0)); // IP will be resolved from string
    msg.append(static_cast<int64_t>(dlg.port()));

    // Convert IP string to uint32 for IPC
    QHostAddress addr(dlg.ipAddress());
    if (!addr.isNull()) {
        // Rebuild with numeric IP
        IpcMessage msg2(IpcMsgType::AddFriend);
        msg2.append(dlg.friendHash());
        msg2.append(dlg.friendName());
        msg2.append(static_cast<int64_t>(addr.toIPv4Address()));
        msg2.append(static_cast<int64_t>(dlg.port()));
        m_ipc->sendRequest(std::move(msg2), [this](const IpcMessage&) {
            requestFriendList();
        });
    } else {
        m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
            requestFriendList();
        });
    }
}

void MessagesPanel::showFindDialog()
{
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Find Friend"),
                                                tr("Name:"), QLineEdit::Normal,
                                                {}, &ok);
    if (!ok || text.isEmpty())
        return;

    const int count = m_friendModel->rowCount();
    for (int i = 0; i < count; ++i) {
        const auto* row = m_friendModel->rowAt(i);
        if (row && row->name.contains(text, Qt::CaseInsensitive)) {
            m_friendListView->setCurrentIndex(m_friendModel->index(i));
            onFriendClicked(m_friendModel->index(i));
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Chat tab management
// ---------------------------------------------------------------------------

int MessagesPanel::findTabByHash(const QString& friendHash) const
{
    for (int i = 0; i < m_chatTabBar->count(); ++i) {
        if (m_chatTabBar->tabData(i).toString() == friendHash)
            return i;
    }
    return -1;
}

void MessagesPanel::openChatTab(const QString& friendHash, const QString& friendName)
{
    const int existing = findTabByHash(friendHash);
    if (existing >= 0) {
        m_chatTabBar->setCurrentIndex(existing);
        return;
    }

    const int idx = m_chatTabBar->addTab(
        QIcon(QStringLiteral(":/icons/Chat.ico")), friendName);
    m_chatTabBar->setTabData(idx, QVariant(friendHash));
    updateTabBarVisibility();
    m_chatTabBar->setCurrentIndex(idx);
}

void MessagesPanel::closeChatTab(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= m_chatTabBar->count())
        return;

    const QString hash = m_chatTabBar->tabData(tabIndex).toString();
    m_chatHistory.remove(hash);
    m_chatTabBar->removeTab(tabIndex);
    updateTabBarVisibility();

    if (m_chatTabBar->count() == 0) {
        m_activeFriendHash.clear();
        updateChatDisplay();
        updateInfoSection(-1);
    }
}

void MessagesPanel::updateTabBarVisibility()
{
    m_chatTabBar->setVisible(m_chatTabBar->count() > 0);
}

void MessagesPanel::onChatTabChanged(int index)
{
    if (index < 0 || index >= m_chatTabBar->count()) {
        m_activeFriendHash.clear();
        updateChatDisplay();
        return;
    }

    const QString hash = m_chatTabBar->tabData(index).toString();
    m_activeFriendHash = hash;
    updateChatDisplay();

    // Select the corresponding friend in the list and update info
    const int friendRow = m_friendModel->findByHash(hash);
    if (friendRow >= 0) {
        m_friendListView->setCurrentIndex(m_friendModel->index(friendRow));
        updateInfoSection(friendRow);
    }
}

void MessagesPanel::onChatTabCloseRequested(int index)
{
    closeChatTab(index);
}

// ---------------------------------------------------------------------------
// Smiley data table (matching MFC SmileySelector.cpp)
// ---------------------------------------------------------------------------

struct SmileyEntry {
    const char* icon;
    const char* code;
};

static constexpr SmileyEntry kSmileys[] = {
    { "Smiley_Smile",    ":-)"     },
    { "Smiley_Happy",    ":-))"    },
    { "Smiley_Laugh",    ":-D"     },
    { "Smiley_Wink",     ";-)"     },
    { "Smiley_Tongue",   ":-P"     },
    { "Smiley_Interest", "=-)"     },
    { "Smiley_Sad",      ":-("     },
    { "Smiley_Cry",      ":'("     },
    { "Smiley_Disgust",  ":-|"     },
    { "Smiley_omg",      ":-O"     },
    { "Smiley_Skeptic",  ":-/"     },
    { "Smiley_Love",     ":-*"     },
    { "Smiley_smileq",   ":-]"     },
    { "Smiley_sadq",     ":-["     },
    { "Smiley_Ph34r",    ":ph34r:" },
    { "Smiley_lookside", ">_>"     },
    { "Smiley_Sealed",   ":-X"     },
};

static constexpr int kSmileyCount = std::size(kSmileys);
static constexpr int kSmileyCols  = 6;

void MessagesPanel::showSmileySelector()
{
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    auto* gridWidget = new QWidget(menu);
    auto* grid = new QGridLayout(gridWidget);
    grid->setSpacing(2);
    grid->setContentsMargins(4, 4, 4, 4);

    for (int i = 0; i < kSmileyCount; ++i) {
        auto* btn = new QToolButton(gridWidget);
        btn->setIcon(QIcon(QStringLiteral(":/smileys/%1.ico").arg(QLatin1StringView(kSmileys[i].icon))));
        btn->setIconSize(QSize(24, 24));
        btn->setFixedSize(28, 28);
        btn->setAutoRaise(true);
        btn->setToolTip(QString::fromLatin1(kSmileys[i].code));

        connect(btn, &QToolButton::clicked, this, [this, i, menu]() {
            const QString code = QString::fromLatin1(kSmileys[i].code);
            const int pos = m_messageInput->cursorPosition();
            QString text = m_messageInput->text();

            // Insert with surrounding spaces (matching MFC SmileySelector behavior)
            QString insert;
            if (pos > 0 && !text[pos - 1].isSpace())
                insert += QLatin1Char(' ');
            insert += code;
            if (pos < text.length() && !text[pos].isSpace())
                insert += QLatin1Char(' ');

            text.insert(pos, insert);
            m_messageInput->setText(text);
            m_messageInput->setCursorPosition(pos + static_cast<int>(insert.length()));
            m_messageInput->setFocus();
            menu->close();
        });

        grid->addWidget(btn, i / kSmileyCols, i % kSmileyCols);
    }

    auto* widgetAction = new QWidgetAction(menu);
    widgetAction->setDefaultWidget(gridWidget);
    menu->addAction(widgetAction);

    menu->popup(m_smileyBtn->mapToGlobal(QPoint(0, -menu->sizeHint().height())));
}

QString MessagesPanel::renderSmileys(const QString& text) const
{
    QString result = text;

    // Replace longer codes first to avoid partial matches (e.g. ":-)" inside ":-))")
    // The table is ordered with ":-)" before ":-))", so process in reverse length order
    // Actually, iterate in order but replace ":-)" last — simplest: sort by code length desc
    struct IndexedEntry {
        int index;
        int codeLen;
    };
    QVector<IndexedEntry> sorted;
    sorted.reserve(kSmileyCount);
    for (int i = 0; i < kSmileyCount; ++i)
        sorted.append({i, static_cast<int>(std::strlen(kSmileys[i].code))});
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.codeLen > b.codeLen;
    });

    for (const auto& entry : sorted) {
        const QString code = QString::fromLatin1(kSmileys[entry.index].code);
        const QString img = QStringLiteral("<img src=\"qrc:/smileys/%1.ico\" width=\"16\" height=\"16\">")
                                .arg(QLatin1StringView(kSmileys[entry.index].icon));
        result.replace(code, img);
    }
    return result;
}

} // namespace eMule
