/// @file IrcPanel.cpp
/// @brief IRC tab panel implementation — matches MFC eMule IRC window.

#include "panels/IrcPanel.h"

#include "app/UiState.h"
#include "chat/IrcClient.h"
#include "prefs/Preferences.h"

#include <QAction>
#include <QDateTime>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QStringListModel>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace eMule {

// ---------------------------------------------------------------------------
// mIRC color table (16 standard colors)
// ---------------------------------------------------------------------------

static constexpr const char* kMircColors[] = {
    "#FFFFFF", "#000000", "#00007F", "#009300",
    "#FF0000", "#7F0000", "#9C009C", "#FC7F00",
    "#FFFF00", "#00FF00", "#007F7F", "#00FFFF",
    "#0000FF", "#FF00FF", "#7F7F7F", "#D2D2D2",
};
static constexpr int kMircColorCount = std::size(kMircColors);

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

// ---------------------------------------------------------------------------
// IrcPanel
// ---------------------------------------------------------------------------

IrcPanel::IrcPanel(QWidget* parent)
    : QWidget(parent)
    , m_statusKey(QStringLiteral("__status__"))
{
    m_irc = new IrcClient(this);
    setupUi();
    connectIrcSignals();
}

IrcPanel::~IrcPanel() = default;

void IrcPanel::setCustomFont(const QFont& font)
{
    m_customFont = font;
    if (m_statusBrowser)
        m_statusBrowser->setFont(font);
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        if (auto* browser = qobject_cast<QTextBrowser*>(it->widget))
            browser->setFont(font);
    }
}

// ---------------------------------------------------------------------------
// Event filter — arrow key history on input field
// ---------------------------------------------------------------------------

bool IrcPanel::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down) {
            auto* ch = activeChannel();
            if (!ch || ch->inputHistory.isEmpty())
                return true;

            if (ke->key() == Qt::Key_Up) {
                if (ch->historyPos < 0)
                    ch->historyPos = static_cast<int>(ch->inputHistory.size()) - 1;
                else if (ch->historyPos > 0)
                    --ch->historyPos;
            } else {
                if (ch->historyPos >= 0 && ch->historyPos < ch->inputHistory.size() - 1)
                    ++ch->historyPos;
                else {
                    ch->historyPos = -1;
                    m_input->clear();
                    return true;
                }
            }

            if (ch->historyPos >= 0 && ch->historyPos < ch->inputHistory.size())
                m_input->setText(ch->inputHistory[ch->historyPos]);
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ---------------------------------------------------------------------------
// Connection slots
// ---------------------------------------------------------------------------

void IrcPanel::onConnectClicked()
{
    if (m_irc->isConnected()) {
        m_irc->disconnect();
        return;
    }

    // Validate nick
    QString nick = thePrefs.ircNick();
    if (nick.isEmpty() || nick.length() > 25) {
        bool ok = false;
        nick = QInputDialog::getText(
            this, tr("Select an IRC nick."),
            tr("Should be no longer than 25 characters: letters, digits or "
               "symbols [_-{}]\\.\nNick can be changed again in Options->IRC."),
            QLineEdit::Normal, nick, &ok);
        if (!ok || nick.isEmpty())
            return;
        // Strip bad chars
        nick.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_\\-{}\\[\\]\\\\]")));
        if (nick.isEmpty())
            return;
        nick.truncate(25);
        thePrefs.setIrcNick(nick);
    }

    QString server = thePrefs.ircServer();
    if (server.isEmpty())
        server = QStringLiteral("irc.mindforge.org:6667");

    appendToStatus(formatTimestamp() +
        QStringLiteral(" Connecting to <b>%1</b> as <b>%2</b>...")
            .arg(server.toHtmlEscaped(), nick.toHtmlEscaped()));

    m_irc->connectToServer(server, nick);
}

void IrcPanel::onCloseClicked()
{
    const QString name = activeChannelName();
    if (name.isEmpty() || name == m_statusKey)
        return;

    // If it's a real channel, part it first
    auto it = m_channels.find(name);
    if (it != m_channels.end() && it->type == IrcChannel::Normal && m_irc->isConnected())
        m_irc->partChannel(it->name);

    removeChannelTab(name);
}

void IrcPanel::onSendClicked()
{
    const QString text = m_input->text();
    if (text.isEmpty())
        return;

    addToHistory(text);
    processInput(text);
    m_input->clear();
}

void IrcPanel::onTabChanged(int /*index*/)
{
    updateNickList();

    // Reset activity color for the newly active tab
    const int idx = m_tabWidget->currentIndex();
    if (idx >= 0)
        m_tabWidget->tabBar()->setTabTextColor(idx, QColor());
}

void IrcPanel::onTabCloseRequested(int index)
{
    const QString tabText = m_tabWidget->tabText(index);
    const QString key = tabText.toLower();

    auto it = m_channels.find(key);
    if (it != m_channels.end()) {
        if (it->type == IrcChannel::Normal && m_irc->isConnected())
            m_irc->partChannel(it->name);
        removeChannelTab(key);
    }
}

// ---------------------------------------------------------------------------
// IRC signal handlers
// ---------------------------------------------------------------------------

void IrcPanel::onIrcConnected()
{
    appendToStatus(formatTimestamp() +
        QStringLiteral(" <font color='#009300'>Connected to server.</font>"));
}

void IrcPanel::onIrcLoggedIn()
{
    m_connectBtn->setText(tr("Disconnect"));

    appendToStatus(formatTimestamp() +
        QStringLiteral(" <font color='#009300'>Logged in as <b>%1</b>.</font>")
            .arg(m_irc->currentNick().toHtmlEscaped()));

    // Create Channels tab
    ensureChannelTab(QStringLiteral("__channels__"), IrcChannel::ChannelList);

    // Execute perform string if enabled
    if (thePrefs.ircUsePerform()) {
        const QString perform = thePrefs.ircPerformString();
        if (!perform.isEmpty())
            m_irc->executePerform(perform);
    }

    // Auto-join help channel if enabled
    if (thePrefs.ircConnectHelpChannel())
        m_irc->joinChannel(QStringLiteral("#emule-english"));

    // Request channel list if enabled
    if (thePrefs.ircLoadChannelList())
        m_irc->requestChannelList();
}

void IrcPanel::onIrcDisconnected()
{
    m_connectBtn->setText(tr("Connect"));

    appendToStatus(formatTimestamp() +
        QStringLiteral(" <font color='#FF0000'>Disconnected from server.</font>"));

    // Append disconnect message to all channel tabs
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        if (it->type == IrcChannel::Normal || it->type == IrcChannel::Private) {
            appendToChannel(it.key(),
                formatTimestamp() +
                QStringLiteral(" <font color='#FF0000'>Disconnected.</font>"));
        }
    }

    // Remove Channels tab, keep channel tabs (they just get disconnect message)
    removeChannelTab(QStringLiteral("__channels__"));

    updateNickList();
}

void IrcPanel::onIrcSocketError(const QString& error)
{
    appendToStatus(formatTimestamp() +
        QStringLiteral(" <font color='#FF0000'><b>Error:</b> %1</font>")
            .arg(error.toHtmlEscaped()));
}

void IrcPanel::onStatusMessage(const QString& message)
{
    if (thePrefs.ircIgnoreMiscInfoMessages())
        return;
    appendToStatus(formatTimestamp() + QStringLiteral(" ") +
        formatMessage(message));
}

void IrcPanel::onChannelMessage(const QString& channel, const QString& nick,
                                 const QString& message)
{
    const QString key = channel.toLower();
    ensureChannelTab(key);

    const QString html = formatTimestamp() +
        QStringLiteral(" &lt;<b>%1</b>&gt; %2")
            .arg(nick.toHtmlEscaped(), formatMessage(message));
    appendToChannel(key, html);

    // Activity indicator
    const int idx = findTab(key);
    if (idx >= 0 && idx != m_tabWidget->currentIndex())
        m_tabWidget->tabBar()->setTabTextColor(idx, Qt::red);
}

void IrcPanel::onPrivateMessage(const QString& nick, const QString& message)
{
    const QString key = nick.toLower();
    ensureChannelTab(key, IrcChannel::Private);

    const QString html = formatTimestamp() +
        QStringLiteral(" &lt;<b>%1</b>&gt; %2")
            .arg(nick.toHtmlEscaped(), formatMessage(message));
    appendToChannel(key, html);

    const int idx = findTab(key);
    if (idx >= 0 && idx != m_tabWidget->currentIndex())
        m_tabWidget->tabBar()->setTabTextColor(idx, Qt::red);
}

void IrcPanel::onActionReceived(const QString& target, const QString& nick,
                                 const QString& message)
{
    const QString key = target.toLower();
    ensureChannelTab(key);

    const QString html = formatTimestamp() +
        QStringLiteral(" <font color='#9C009C'>* %1 %2</font>")
            .arg(nick.toHtmlEscaped(), formatMessage(message));
    appendToChannel(key, html);
}

void IrcPanel::onNoticeReceived(const QString& source, const QString& /*target*/,
                                 const QString& message)
{
    appendToStatus(formatTimestamp() +
        QStringLiteral(" <font color='#9C009C'>-%1- %2</font>")
            .arg(source.toHtmlEscaped(), formatMessage(message)));
}

void IrcPanel::onUserJoined(const QString& channel, const QString& nick)
{
    const QString key = channel.toLower();
    ensureChannelTab(key);

    auto it = m_channels.find(key);
    if (it != m_channels.end()) {
        if (!it->nicks.contains(nick))
            it->nicks.append(nick);
    }

    if (!thePrefs.ircIgnoreJoinMessages()) {
        appendToChannel(key,
            formatTimestamp() +
            QStringLiteral(" <font color='#00007F'>* %1 has joined %2</font>")
                .arg(nick.toHtmlEscaped(), channel.toHtmlEscaped()));
    }

    if (activeChannelName() == key)
        updateNickList();
}

void IrcPanel::onUserParted(const QString& channel, const QString& nick,
                              const QString& reason)
{
    const QString key = channel.toLower();
    auto it = m_channels.find(key);
    if (it != m_channels.end())
        it->nicks.removeAll(nick);

    if (!thePrefs.ircIgnorePartMessages()) {
        QString msg = formatTimestamp() +
            QStringLiteral(" <font color='#00007F'>* %1 has left %2")
                .arg(nick.toHtmlEscaped(), channel.toHtmlEscaped());
        if (!reason.isEmpty())
            msg += QStringLiteral(" (%1)").arg(reason.toHtmlEscaped());
        msg += QStringLiteral("</font>");
        appendToChannel(key, msg);
    }

    if (activeChannelName() == key)
        updateNickList();
}

void IrcPanel::onUserQuit(const QString& nick, const QString& reason)
{
    // Remove nick from all channels they were in
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        if (it->nicks.removeAll(nick) > 0) {
            if (!thePrefs.ircIgnoreQuitMessages()) {
                QString msg = formatTimestamp() +
                    QStringLiteral(" <font color='#00007F'>* %1 has quit")
                        .arg(nick.toHtmlEscaped());
                if (!reason.isEmpty())
                    msg += QStringLiteral(" (%1)").arg(reason.toHtmlEscaped());
                msg += QStringLiteral("</font>");
                appendToChannel(it.key(), msg);
            }
        }
    }
    updateNickList();
}

void IrcPanel::onUserKicked(const QString& channel, const QString& nick,
                              const QString& by, const QString& reason)
{
    const QString key = channel.toLower();
    auto it = m_channels.find(key);
    if (it != m_channels.end())
        it->nicks.removeAll(nick);

    QString msg = formatTimestamp() +
        QStringLiteral(" <font color='#00007F'>* %1 was kicked from %2 by %3")
            .arg(nick.toHtmlEscaped(), channel.toHtmlEscaped(), by.toHtmlEscaped());
    if (!reason.isEmpty())
        msg += QStringLiteral(" (%1)").arg(reason.toHtmlEscaped());
    msg += QStringLiteral("</font>");
    appendToChannel(key, msg);

    // If we were kicked, remove the tab
    if (nick.compare(m_irc->currentNick(), Qt::CaseInsensitive) == 0)
        removeChannelTab(key);
    else if (activeChannelName() == key)
        updateNickList();
}

void IrcPanel::onNickChanged(const QString& oldNick, const QString& newNick)
{
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        const auto idx = it->nicks.indexOf(oldNick);
        if (idx >= 0) {
            it->nicks[idx] = newNick;
            appendToChannel(it.key(),
                formatTimestamp() +
                QStringLiteral(" <font color='#00007F'>* %1 is now known as %2</font>")
                    .arg(oldNick.toHtmlEscaped(), newNick.toHtmlEscaped()));
        }
    }
    updateNickList();
}

void IrcPanel::onTopicChanged(const QString& channel, const QString& nick,
                               const QString& topic)
{
    const QString key = channel.toLower();
    auto it = m_channels.find(key);
    if (it != m_channels.end())
        it->topic = topic;

    QString msg = formatTimestamp();
    if (nick.isEmpty()) {
        msg += QStringLiteral(" <font color='#00007F'>* Channel Topic: %1</font>")
                   .arg(formatMessage(topic));
    } else {
        msg += QStringLiteral(" <font color='#00007F'>* %1 changed the topic to: %2</font>")
                   .arg(nick.toHtmlEscaped(), formatMessage(topic));
    }
    appendToChannel(key, msg);
}

void IrcPanel::onNamesReceived(const QString& channel, const QStringList& nicks)
{
    const QString key = channel.toLower();
    auto it = m_channels.find(key);
    if (it != m_channels.end())
        it->nicks.append(nicks);
}

void IrcPanel::onNamesFinished(const QString& channel)
{
    const QString key = channel.toLower();
    auto it = m_channels.find(key);
    if (it == m_channels.end())
        return;

    // Sort: ops (@) first, voiced (+) second, rest alphabetical
    std::sort(it->nicks.begin(), it->nicks.end(), [](const QString& a, const QString& b) {
        const bool aOp = a.startsWith(QLatin1Char('@'));
        const bool bOp = b.startsWith(QLatin1Char('@'));
        if (aOp != bOp)
            return aOp;
        const bool aVoice = a.startsWith(QLatin1Char('+'));
        const bool bVoice = b.startsWith(QLatin1Char('+'));
        if (aVoice != bVoice)
            return aVoice;
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    if (activeChannelName() == key)
        updateNickList();
}

void IrcPanel::onChannelListed(const QString& channel, int userCount,
                                const QString& topic)
{
    if (!m_channelListWidget)
        return;

    // Apply channel list filter if enabled
    if (thePrefs.ircUseChannelFilter()) {
        const QString filter = thePrefs.ircChannelFilter();
        const auto parts = filter.split(QLatin1Char('|'));
        const QString nameFilter = parts.value(0);
        const int minUsers = parts.value(1).toInt();
        if (!nameFilter.isEmpty() && !channel.contains(nameFilter, Qt::CaseInsensitive))
            return;
        if (userCount < minUsers)
            return;
    }

    auto* item = new QTreeWidgetItem(m_channelListWidget);
    item->setText(0, channel);
    item->setText(1, QString::number(userCount));
    item->setText(2, topic);
    item->setIcon(0, QIcon(QStringLiteral(":/icons/IRC.ico")));
    item->setData(0, Qt::UserRole, channel);
}

void IrcPanel::onChannelListStarted()
{
    m_channelListPending = true;
    if (m_channelListWidget)
        m_channelListWidget->clear();
}

void IrcPanel::onChannelListFinished()
{
    m_channelListPending = false;
    if (m_channelListWidget) {
        m_channelListWidget->sortByColumn(1, Qt::DescendingOrder);
        m_channelListWidget->resizeColumnToContents(0);
        m_channelListWidget->resizeColumnToContents(1);
    }
}

void IrcPanel::onNickInUse(const QString& nick)
{
    appendToStatus(formatTimestamp() +
        QStringLiteral(" <font color='#FF0000'>Nick <b>%1</b> is already in use. "
                        "Try a different nick.</font>")
            .arg(nick.toHtmlEscaped()));

    bool ok = false;
    QString newNick = QInputDialog::getText(
        this, tr("Nick in use"),
        tr("The nick \"%1\" is already in use.\nPlease choose another:").arg(nick),
        QLineEdit::Normal, nick + QStringLiteral("_"), &ok);
    if (ok && !newNick.isEmpty()) {
        newNick.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_\\-{}\\[\\]\\\\]")));
        newNick.truncate(25);
        if (!newNick.isEmpty()) {
            thePrefs.setIrcNick(newNick);
            m_irc->changeNick(newNick);
        }
    }
}

// ---------------------------------------------------------------------------
// setupUi
// ---------------------------------------------------------------------------

void IrcPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    mainLayout->addWidget(m_splitter, 1);

    // --- Left panel: Nick list ---
    auto* leftWidget = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(4, 2, 0, 2);
    leftLayout->setSpacing(2);

    // Nick header with count
    auto* nickHeader = new QHBoxLayout;
    m_nickLabel = new QLabel(tr("Nick"), leftWidget);
    auto headerFont = m_nickLabel->font();
    headerFont.setBold(true);
    m_nickLabel->setFont(headerFont);
    nickHeader->addWidget(m_nickLabel);
    nickHeader->addStretch();

    auto* nickArrow = new QLabel(QStringLiteral("\u25B8"), leftWidget);
    nickHeader->addWidget(nickArrow);
    leftLayout->addLayout(nickHeader);

    // Nick list view
    m_nickModel = new QStringListModel(this);
    m_nickListView = new QListView(leftWidget);
    m_nickListView->setModel(m_nickModel);
    m_nickListView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_nickListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    leftLayout->addWidget(m_nickListView, 1);

    m_splitter->addWidget(leftWidget);

    // --- Right panel: Tab area ---
    auto* rightWidget = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 2, 4, 2);
    rightLayout->setSpacing(2);

    m_tabWidget = new QTabWidget(rightWidget);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(false);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->tabBar()->setExpanding(false);
    rightLayout->addWidget(m_tabWidget, 1);

    connect(m_tabWidget, &QTabWidget::currentChanged, this, &IrcPanel::onTabChanged);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &IrcPanel::onTabCloseRequested);

    m_splitter->addWidget(rightWidget);

    // Default split: ~12% nick list, ~88% chat (matching MFC)
    m_splitter->setSizes({120, 880});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    theUiState.bindIrcSplitter(m_splitter);

    // --- Status tab (always present) ---
    m_statusBrowser = new QTextBrowser(this);
    m_statusBrowser->setOpenExternalLinks(true);
    m_statusBrowser->setReadOnly(true);

    IrcChannel statusCh;
    statusCh.name = QStringLiteral("Status");
    statusCh.type = IrcChannel::Status;
    statusCh.widget = m_statusBrowser;
    m_channels[m_statusKey] = statusCh;

    m_tabWidget->addTab(m_statusBrowser, QIcon(QStringLiteral(":/icons/IRC.ico")),
                        tr("Status"));
    // Remove close button from Status tab
    m_tabWidget->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    m_tabWidget->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);

    // --- Bottom bar ---
    auto* bottomLayout = new QHBoxLayout;
    bottomLayout->setContentsMargins(4, 2, 4, 4);
    bottomLayout->setSpacing(4);

    m_connectBtn = new QPushButton(tr("Connect"), this);
    m_closeBtn = new QPushButton(tr("Close"), this);
    bottomLayout->addWidget(m_connectBtn);
    bottomLayout->addWidget(m_closeBtn);

    connect(m_connectBtn, &QPushButton::clicked, this, &IrcPanel::onConnectClicked);
    connect(m_closeBtn, &QPushButton::clicked, this, &IrcPanel::onCloseClicked);

    // Separator
    auto* sep = new QLabel(QStringLiteral("|"), this);
    sep->setStyleSheet(QStringLiteral("color: gray;"));
    bottomLayout->addWidget(sep);

    // Format toolbar
    m_smileyBtn = new QToolButton(this);
    m_smileyBtn->setIcon(QIcon(QStringLiteral(":/icons/Smiley_Smile.ico")));
    m_smileyBtn->setIconSize(QSize(20, 20));
    m_smileyBtn->setAutoRaise(true);
    m_smileyBtn->setToolTip(tr("Smileys"));
    connect(m_smileyBtn, &QToolButton::clicked, this, &IrcPanel::showSmileySelector);
    bottomLayout->addWidget(m_smileyBtn);

    m_boldBtn = new QToolButton(this);
    m_boldBtn->setText(QStringLiteral("B"));
    m_boldBtn->setFont([]{
        QFont f;
        f.setBold(true);
        return f;
    }());
    m_boldBtn->setAutoRaise(true);
    m_boldBtn->setFixedSize(24, 24);
    m_boldBtn->setToolTip(tr("Bold"));
    connect(m_boldBtn, &QToolButton::clicked, this, [this]{ insertFormatCode('\x02'); });
    bottomLayout->addWidget(m_boldBtn);

    m_italicBtn = new QToolButton(this);
    m_italicBtn->setText(QStringLiteral("I"));
    m_italicBtn->setFont([]{
        QFont f;
        f.setItalic(true);
        return f;
    }());
    m_italicBtn->setAutoRaise(true);
    m_italicBtn->setFixedSize(24, 24);
    m_italicBtn->setToolTip(tr("Italic"));
    connect(m_italicBtn, &QToolButton::clicked, this, [this]{ insertFormatCode('\x1D'); });
    bottomLayout->addWidget(m_italicBtn);

    m_underlineBtn = new QToolButton(this);
    m_underlineBtn->setText(QStringLiteral("U"));
    m_underlineBtn->setFont([]{
        QFont f;
        f.setUnderline(true);
        return f;
    }());
    m_underlineBtn->setAutoRaise(true);
    m_underlineBtn->setFixedSize(24, 24);
    m_underlineBtn->setToolTip(tr("Underline"));
    connect(m_underlineBtn, &QToolButton::clicked, this, [this]{ insertFormatCode('\x1F'); });
    bottomLayout->addWidget(m_underlineBtn);

    m_colorBtn = new QToolButton(this);
    m_colorBtn->setText(QStringLiteral("\u25A0"));
    m_colorBtn->setAutoRaise(true);
    m_colorBtn->setFixedSize(24, 24);
    m_colorBtn->setToolTip(tr("Color"));
    connect(m_colorBtn, &QToolButton::clicked, this, &IrcPanel::showColorPopup);
    bottomLayout->addWidget(m_colorBtn);

    m_resetBtn = new QToolButton(this);
    m_resetBtn->setText(QStringLiteral("F"));
    m_resetBtn->setAutoRaise(true);
    m_resetBtn->setFixedSize(24, 24);
    m_resetBtn->setToolTip(tr("Reset Formatting"));
    connect(m_resetBtn, &QToolButton::clicked, this, [this]{ insertFormatCode('\x0F'); });
    bottomLayout->addWidget(m_resetBtn);

    // Separator
    auto* sep2 = new QLabel(QStringLiteral("|"), this);
    sep2->setStyleSheet(QStringLiteral("color: gray;"));
    bottomLayout->addWidget(sep2);

    // Input + Send
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Type a message..."));
    m_input->installEventFilter(this);
    bottomLayout->addWidget(m_input, 1);

    connect(m_input, &QLineEdit::returnPressed, this, &IrcPanel::onSendClicked);

    m_sendBtn = new QPushButton(tr("Send"), this);
    bottomLayout->addWidget(m_sendBtn);
    connect(m_sendBtn, &QPushButton::clicked, this, &IrcPanel::onSendClicked);

    mainLayout->addLayout(bottomLayout);
}

// ---------------------------------------------------------------------------
// connectIrcSignals
// ---------------------------------------------------------------------------

void IrcPanel::connectIrcSignals()
{
    connect(m_irc, &IrcClient::connected,    this, &IrcPanel::onIrcConnected);
    connect(m_irc, &IrcClient::loggedIn,     this, &IrcPanel::onIrcLoggedIn);
    connect(m_irc, &IrcClient::disconnected, this, &IrcPanel::onIrcDisconnected);
    connect(m_irc, &IrcClient::socketError,  this, &IrcPanel::onIrcSocketError);

    connect(m_irc, &IrcClient::statusMessage, this, &IrcPanel::onStatusMessage);
    connect(m_irc, &IrcClient::channelMessageReceived, this, &IrcPanel::onChannelMessage);
    connect(m_irc, &IrcClient::privateMessageReceived, this, &IrcPanel::onPrivateMessage);
    connect(m_irc, &IrcClient::actionReceived,  this, &IrcPanel::onActionReceived);
    connect(m_irc, &IrcClient::noticeReceived,  this, &IrcPanel::onNoticeReceived);

    connect(m_irc, &IrcClient::userJoined,  this, &IrcPanel::onUserJoined);
    connect(m_irc, &IrcClient::userParted,  this, &IrcPanel::onUserParted);
    connect(m_irc, &IrcClient::userQuit,    this, &IrcPanel::onUserQuit);
    connect(m_irc, &IrcClient::userKicked,  this, &IrcPanel::onUserKicked);
    connect(m_irc, &IrcClient::nickChanged, this, &IrcPanel::onNickChanged);
    connect(m_irc, &IrcClient::topicChanged, this, &IrcPanel::onTopicChanged);

    connect(m_irc, &IrcClient::namesReceived,  this, &IrcPanel::onNamesReceived);
    connect(m_irc, &IrcClient::namesFinished,  this, &IrcPanel::onNamesFinished);

    connect(m_irc, &IrcClient::channelListed,   this, &IrcPanel::onChannelListed);
    connect(m_irc, &IrcClient::channelListStarted,  this, &IrcPanel::onChannelListStarted);
    connect(m_irc, &IrcClient::channelListFinished, this, &IrcPanel::onChannelListFinished);

    connect(m_irc, &IrcClient::nickInUse, this, &IrcPanel::onNickInUse);
}

// ---------------------------------------------------------------------------
// Tab/channel management
// ---------------------------------------------------------------------------

int IrcPanel::findTab(const QString& name) const
{
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        const QString tabKey = m_tabWidget->tabText(i).toLower();
        if (tabKey == name)
            return i;
    }

    // Also check by widget pointer
    auto it = m_channels.constFind(name);
    if (it != m_channels.constEnd() && it->widget) {
        return m_tabWidget->indexOf(it->widget);
    }

    return -1;
}

int IrcPanel::ensureChannelTab(const QString& name, IrcChannel::Type type)
{
    const QString key = name.toLower();

    auto it = m_channels.find(key);
    if (it != m_channels.end() && it->widget) {
        return m_tabWidget->indexOf(it->widget);
    }

    // Create new tab
    IrcChannel ch;
    ch.name = name;
    ch.type = type;

    if (type == IrcChannel::ChannelList) {
        // Channel list tab with QTreeWidget
        auto* tree = new QTreeWidget(this);
        tree->setHeaderLabels({tr("Channel"), tr("Users"), tr("Topic")});
        tree->setRootIsDecorated(false);
        tree->setSelectionMode(QAbstractItemView::SingleSelection);
        tree->setSortingEnabled(true);
        tree->header()->setStretchLastSection(true);

        connect(tree, &QTreeWidget::itemDoubleClicked, this,
                [this](QTreeWidgetItem* item, int /*column*/) {
            const QString channel = item->data(0, Qt::UserRole).toString();
            if (!channel.isEmpty() && m_irc->isConnected())
                m_irc->joinChannel(channel);
        });

        m_channelListWidget = tree;
        ch.widget = tree;

        const int idx = m_tabWidget->addTab(tree,
            QIcon(QStringLiteral(":/icons/IRC.ico")), tr("Channels"));
        // Non-closeable
        m_tabWidget->tabBar()->setTabButton(idx, QTabBar::RightSide, nullptr);
        m_tabWidget->tabBar()->setTabButton(idx, QTabBar::LeftSide, nullptr);

        m_channels[key] = ch;
        return idx;
    }

    // Normal channel or private — QTextBrowser
    auto* browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(true);
    browser->setReadOnly(true);
    if (!m_customFont.family().isEmpty())
        browser->setFont(m_customFont);
    ch.widget = browser;

    QString tabLabel = name;
    if (tabLabel.startsWith(QLatin1Char('#')))
        tabLabel = name; // keep as-is for channels

    const int idx = m_tabWidget->addTab(browser,
        QIcon(QStringLiteral(":/icons/IRC.ico")), tabLabel);

    m_channels[key] = ch;
    return idx;
}

void IrcPanel::removeChannelTab(const QString& name)
{
    const QString key = name.toLower();
    auto it = m_channels.find(key);
    if (it == m_channels.end())
        return;

    if (it->widget) {
        const int idx = m_tabWidget->indexOf(it->widget);
        if (idx >= 0)
            m_tabWidget->removeTab(idx);
        delete it->widget;
    }

    if (key == QStringLiteral("__channels__"))
        m_channelListWidget = nullptr;

    m_channels.erase(it);
}

void IrcPanel::removeAllChannelTabs()
{
    QStringList keys;
    for (auto it = m_channels.cbegin(); it != m_channels.cend(); ++it) {
        if (it.key() != m_statusKey)
            keys.append(it.key());
    }
    for (const QString& k : keys)
        removeChannelTab(k);
}

QString IrcPanel::activeChannelName() const
{
    const int idx = m_tabWidget->currentIndex();
    if (idx < 0)
        return {};

    QWidget* w = m_tabWidget->widget(idx);
    for (auto it = m_channels.cbegin(); it != m_channels.cend(); ++it) {
        if (it->widget == w)
            return it.key();
    }
    return {};
}

IrcChannel* IrcPanel::activeChannel()
{
    const QString name = activeChannelName();
    if (name.isEmpty())
        return nullptr;
    auto it = m_channels.find(name);
    return it != m_channels.end() ? &*it : nullptr;
}

// ---------------------------------------------------------------------------
// Display helpers
// ---------------------------------------------------------------------------

void IrcPanel::appendToChannel(const QString& channel, const QString& html)
{
    const QString key = channel.toLower();
    auto it = m_channels.find(key);
    if (it == m_channels.end())
        return;

    auto* browser = qobject_cast<QTextBrowser*>(it->widget);
    if (browser)
        browser->append(html);
}

void IrcPanel::appendToStatus(const QString& html)
{
    if (m_statusBrowser)
        m_statusBrowser->append(html);
}

QString IrcPanel::formatTimestamp() const
{
    if (!thePrefs.ircAddTimestamp())
        return {};
    return QStringLiteral("<font color='gray'>[%1]</font>")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));
}

QString IrcPanel::renderMircCodes(const QString& text) const
{
    QString result;
    result.reserve(text.size() * 2);

    bool bold = false;
    bool italic = false;
    bool underline = false;
    int fgColor = -1;
    int bgColor = -1;
    bool inSpan = false;

    auto closeSpan = [&]() {
        if (inSpan) {
            result += QStringLiteral("</span>");
            inSpan = false;
        }
    };

    auto openSpan = [&]() {
        closeSpan();
        QString style;
        if (bold)
            style += QStringLiteral("font-weight:bold;");
        if (italic)
            style += QStringLiteral("font-style:italic;");
        if (underline)
            style += QStringLiteral("text-decoration:underline;");
        if (fgColor >= 0 && fgColor < kMircColorCount)
            style += QStringLiteral("color:%1;").arg(QLatin1StringView(kMircColors[fgColor]));
        if (bgColor >= 0 && bgColor < kMircColorCount)
            style += QStringLiteral("background-color:%1;").arg(QLatin1StringView(kMircColors[bgColor]));

        if (!style.isEmpty()) {
            result += QStringLiteral("<span style=\"%1\">").arg(style);
            inSpan = true;
        }
    };

    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        const char16_t code = c.unicode();

        if (code == 0x02) { // Bold toggle
            bold = !bold;
            openSpan();
        } else if (code == 0x1D) { // Italic toggle
            italic = !italic;
            openSpan();
        } else if (code == 0x1F) { // Underline toggle
            underline = !underline;
            openSpan();
        } else if (code == 0x0F) { // Reset
            bold = false;
            italic = false;
            underline = false;
            fgColor = -1;
            bgColor = -1;
            closeSpan();
        } else if (code == 0x03) { // Color
            // Parse color number(s): \x03FG or \x03FG,BG
            int fg = -1;
            int bg = -1;

            if (i + 1 < text.size() && text[i + 1].isDigit()) {
                ++i;
                fg = text[i].digitValue();
                if (i + 1 < text.size() && text[i + 1].isDigit()) {
                    ++i;
                    fg = fg * 10 + text[i].digitValue();
                }
                if (i + 1 < text.size() && text[i + 1] == QLatin1Char(',')) {
                    ++i; // skip comma
                    if (i + 1 < text.size() && text[i + 1].isDigit()) {
                        ++i;
                        bg = text[i].digitValue();
                        if (i + 1 < text.size() && text[i + 1].isDigit()) {
                            ++i;
                            bg = bg * 10 + text[i].digitValue();
                        }
                    }
                }
                fgColor = fg;
                if (bg >= 0)
                    bgColor = bg;
            } else {
                // Bare \x03 = color reset
                fgColor = -1;
                bgColor = -1;
            }
            openSpan();
        } else if (code == 0x16) { // Reverse (swap fg/bg) — simplified: just ignore
            // no-op
        } else {
            if (c == QLatin1Char('<'))
                result += QStringLiteral("&lt;");
            else if (c == QLatin1Char('>'))
                result += QStringLiteral("&gt;");
            else if (c == QLatin1Char('&'))
                result += QStringLiteral("&amp;");
            else
                result += c;
        }
    }

    closeSpan();
    return result;
}

QString IrcPanel::renderSmileys(const QString& text) const
{
    QString result = text;

    // Sort by code length descending to avoid partial matches
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
        const QString img = QStringLiteral("<img src=\"qrc:/icons/%1.ico\" width=\"16\" height=\"16\">")
                                .arg(QLatin1StringView(kSmileys[entry.index].icon));
        result.replace(code, img);
    }
    return result;
}

QString IrcPanel::detectUrls(const QString& text) const
{
    static const QRegularExpression urlRe(
        QStringLiteral("((?:https?|ftp|ed2k)://[^\\s<>\"]+)"),
        QRegularExpression::CaseInsensitiveOption);

    QString result = text;
    // Process URLs in reverse order to preserve indices
    QVector<QRegularExpressionMatch> matches;
    auto it = urlRe.globalMatch(result);
    while (it.hasNext())
        matches.append(it.next());

    for (qsizetype i = matches.size() - 1; i >= 0; --i) {
        const auto& match = matches[i];
        const QString url = match.captured(1);
        const QString link = QStringLiteral("<a href=\"%1\">%1</a>").arg(url);
        result.replace(match.capturedStart(1), match.capturedLength(1), link);
    }
    return result;
}

QString IrcPanel::formatMessage(const QString& text) const
{
    // Order: mIRC codes → HTML-escape happens inside renderMircCodes,
    //        then smileys, then URLs
    QString html = renderMircCodes(text);
    html = renderSmileys(html);
    html = detectUrls(html);
    return html;
}

// ---------------------------------------------------------------------------
// Nick list
// ---------------------------------------------------------------------------

void IrcPanel::updateNickList()
{
    const QString key = activeChannelName();
    auto it = m_channels.constFind(key);

    if (it == m_channels.constEnd() || it->type != IrcChannel::Normal) {
        m_nickLabel->setText(tr("Nick"));
        m_nickModel->setStringList({});
        return;
    }

    m_nickLabel->setText(tr("Nick (%1)").arg(it->nicks.size()));
    m_nickModel->setStringList(it->nicks);
}

// ---------------------------------------------------------------------------
// Input processing
// ---------------------------------------------------------------------------

void IrcPanel::processInput(const QString& text)
{
    if (text.startsWith(QLatin1Char('/'))) {
        const auto spaceIdx = text.indexOf(QLatin1Char(' '));
        const QString cmd = (spaceIdx >= 0) ? text.mid(1, spaceIdx - 1) : text.mid(1);
        const QString args = (spaceIdx >= 0) ? text.mid(spaceIdx + 1) : QString();
        handleSlashCommand(cmd.toLower(), args);
        return;
    }

    // Plain text — send to active channel
    const QString key = activeChannelName();
    if (key.isEmpty() || key == m_statusKey || !m_irc->isConnected())
        return;

    auto it = m_channels.find(key);
    if (it == m_channels.end())
        return;

    m_irc->sendMessage(it->name, text);

    // Echo locally
    const QString html = formatTimestamp() +
        QStringLiteral(" &lt;<b>%1</b>&gt; %2")
            .arg(m_irc->currentNick().toHtmlEscaped(), formatMessage(text));
    appendToChannel(key, html);
}

void IrcPanel::handleSlashCommand(const QString& cmd, const QString& args)
{
    if (cmd == QLatin1StringView("join")) {
        if (!args.isEmpty() && m_irc->isConnected()) {
            const auto spaceIdx = args.indexOf(QLatin1Char(' '));
            const QString channel = (spaceIdx >= 0) ? args.left(spaceIdx) : args;
            const QString key = (spaceIdx >= 0) ? args.mid(spaceIdx + 1) : QString();
            m_irc->joinChannel(channel, key);
        }
    } else if (cmd == QLatin1StringView("part") || cmd == QLatin1StringView("leave")) {
        if (m_irc->isConnected()) {
            QString channel = args.trimmed();
            if (channel.isEmpty()) {
                auto* ch = activeChannel();
                if (ch && ch->type == IrcChannel::Normal)
                    channel = ch->name;
            }
            if (!channel.isEmpty())
                m_irc->partChannel(channel);
        }
    } else if (cmd == QLatin1StringView("msg") || cmd == QLatin1StringView("privmsg")) {
        const auto spaceIdx = args.indexOf(QLatin1Char(' '));
        if (spaceIdx > 0 && m_irc->isConnected()) {
            const QString target = args.left(spaceIdx);
            const QString msg = args.mid(spaceIdx + 1);
            m_irc->sendMessage(target, msg);
            // Echo to status
            appendToStatus(formatTimestamp() +
                QStringLiteral(" -> <b>%1</b>: %2")
                    .arg(target.toHtmlEscaped(), formatMessage(msg)));
        }
    } else if (cmd == QLatin1StringView("me")) {
        auto* ch = activeChannel();
        if (ch && m_irc->isConnected()) {
            m_irc->sendCtcp(ch->name, QStringLiteral("ACTION"), args);
            appendToChannel(ch->name.toLower(),
                formatTimestamp() +
                QStringLiteral(" <font color='#9C009C'>* %1 %2</font>")
                    .arg(m_irc->currentNick().toHtmlEscaped(), formatMessage(args)));
        }
    } else if (cmd == QLatin1StringView("nick")) {
        if (!args.isEmpty() && m_irc->isConnected()) {
            thePrefs.setIrcNick(args.trimmed());
            m_irc->changeNick(args.trimmed());
        }
    } else if (cmd == QLatin1StringView("topic")) {
        auto* ch = activeChannel();
        if (ch && m_irc->isConnected())
            m_irc->sendRaw(QStringLiteral("TOPIC %1 :%2").arg(ch->name, args));
    } else if (cmd == QLatin1StringView("list")) {
        if (m_irc->isConnected())
            m_irc->requestChannelList(args.trimmed());
    } else {
        // Unknown command — send as raw
        if (m_irc->isConnected())
            m_irc->sendRaw(cmd.toUpper() + (args.isEmpty() ? QString() :
                QStringLiteral(" %1").arg(args)));
    }
}

void IrcPanel::addToHistory(const QString& text)
{
    auto* ch = activeChannel();
    if (!ch)
        return;

    ch->inputHistory.append(text);
    if (ch->inputHistory.size() > 100)
        ch->inputHistory.removeFirst();
    ch->historyPos = -1;
}

// ---------------------------------------------------------------------------
// Format buttons
// ---------------------------------------------------------------------------

void IrcPanel::insertFormatCode(char code)
{
    const int pos = m_input->cursorPosition();
    QString text = m_input->text();
    text.insert(pos, QChar(static_cast<char16_t>(code)));
    m_input->setText(text);
    m_input->setCursorPosition(pos + 1);
    m_input->setFocus();
}

void IrcPanel::showColorPopup()
{
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    auto* gridWidget = new QWidget(menu);
    auto* grid = new QGridLayout(gridWidget);
    grid->setSpacing(2);
    grid->setContentsMargins(4, 4, 4, 4);

    for (int i = 0; i < kMircColorCount; ++i) {
        auto* btn = new QToolButton(gridWidget);
        btn->setFixedSize(24, 24);
        btn->setAutoRaise(true);
        btn->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #888;")
                               .arg(QLatin1StringView(kMircColors[i])));
        btn->setToolTip(QStringLiteral("Color %1").arg(i));

        connect(btn, &QToolButton::clicked, this, [this, i, menu]() {
            const int pos = m_input->cursorPosition();
            QString text = m_input->text();
            // Insert \x03 followed by color number
            QString colorCode = QStringLiteral("%1%2")
                                    .arg(QChar(char16_t(0x03)))
                                    .arg(i, 2, 10, QLatin1Char('0'));
            text.insert(pos, colorCode);
            m_input->setText(text);
            m_input->setCursorPosition(pos + static_cast<int>(colorCode.length()));
            m_input->setFocus();
            menu->close();
        });

        grid->addWidget(btn, i / 8, i % 8);
    }

    auto* widgetAction = new QWidgetAction(menu);
    widgetAction->setDefaultWidget(gridWidget);
    menu->addAction(widgetAction);

    menu->popup(m_colorBtn->mapToGlobal(QPoint(0, -menu->sizeHint().height())));
}

void IrcPanel::showSmileySelector()
{
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    auto* gridWidget = new QWidget(menu);
    auto* grid = new QGridLayout(gridWidget);
    grid->setSpacing(2);
    grid->setContentsMargins(4, 4, 4, 4);

    for (int i = 0; i < kSmileyCount; ++i) {
        auto* btn = new QToolButton(gridWidget);
        btn->setIcon(QIcon(QStringLiteral(":/icons/%1.ico").arg(QLatin1StringView(kSmileys[i].icon))));
        btn->setIconSize(QSize(24, 24));
        btn->setFixedSize(28, 28);
        btn->setAutoRaise(true);
        btn->setToolTip(QString::fromLatin1(kSmileys[i].code));

        connect(btn, &QToolButton::clicked, this, [this, i, menu]() {
            const QString code = QString::fromLatin1(kSmileys[i].code);
            const int pos = m_input->cursorPosition();
            QString text = m_input->text();

            QString insert;
            if (pos > 0 && !text[pos - 1].isSpace())
                insert += QLatin1Char(' ');
            insert += code;
            if (pos < text.length() && !text[pos].isSpace())
                insert += QLatin1Char(' ');

            text.insert(pos, insert);
            m_input->setText(text);
            m_input->setCursorPosition(pos + static_cast<int>(insert.length()));
            m_input->setFocus();
            menu->close();
        });

        grid->addWidget(btn, i / kSmileyCols, i % kSmileyCols);
    }

    auto* widgetAction = new QWidgetAction(menu);
    widgetAction->setDefaultWidget(gridWidget);
    menu->addAction(widgetAction);

    menu->popup(m_smileyBtn->mapToGlobal(QPoint(0, -menu->sizeHint().height())));
}

} // namespace eMule
