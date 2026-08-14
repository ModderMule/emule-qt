#include "pch.h"
#include "controls/LogWidget.h"

#include "app/AppConfig.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/TextLinks.h"

#include <QColor>
#include <QDateTime>
#include <QIcon>
#include <QStackedWidget>
#include <QTabBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QVBoxLayout>

#include <algorithm>


namespace eMule {

LogWidget* LogWidget::s_instance = nullptr;
QtMessageHandler LogWidget::s_previousHandler = nullptr;

namespace {

/// Server Info is the first tab, as in the reference (CServerWnd::PaneServerInfo).
constexpr int kServerInfoTabIndex = 0;

/// Internal URL for the banner's version-check link — not a real scheme, just a
/// sentinel the panel recognises and turns into an in-app check.
constexpr QLatin1StringView kVersionCheckUrl{"emuleqt:versioncheck"};

} // namespace

LogWidget::LogWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Tab bar — left-aligned at top of log area (matching SearchPanel pattern)
    m_tabBar = new QTabBar(this);
    m_tabBar->setExpanding(false);
    m_tabBar->setStyleSheet(QStringLiteral("QTabBar::tab { min-width: 80px; }"));
    layout->addWidget(m_tabBar, 0, Qt::AlignLeft);

    m_stack = new QStackedWidget;
    layout->addWidget(m_stack);

    connect(m_tabBar, &QTabBar::currentChanged, m_stack, &QStackedWidget::setCurrentIndex);

    // Server Info tab
    m_serverInfoBrowser = new QTextBrowser;
    m_serverInfoBrowser->setReadOnly(true);
    // Links are re-emitted rather than opened here: ed2k:// must reach the in-app
    // importer, and the banner's version-check link is an internal action. The
    // reference routes clicks the same way (CServerWnd::OnEnLinkServerBox).
    TextLinks::wireLinkClicks(m_serverInfoBrowser, this,
                              [this](const QString& link) { emit linkActivated(link); });
    m_serverInfoBrowser->setFont(QFont(QStringLiteral("Helvetica"), 9));
    if (thePrefs.useOriginalIcons())
        m_tabBar->addTab(QIcon(QStringLiteral(":/icons/ServerInfo.ico")), tr("Server Info"));
    else
        m_tabBar->addTab(tr("Server Info"));
    m_stack->addWidget(m_serverInfoBrowser);

    // Log tab
    m_logBrowser = new QTextBrowser;
    m_logBrowser->setReadOnly(true);
    m_logBrowser->setFont(QFont(QStringLiteral("Helvetica"), 9));
    if (thePrefs.useOriginalIcons())
        m_tabBar->addTab(QIcon(QStringLiteral(":/icons/Log.ico")), tr("Log"));
    else
        m_tabBar->addTab(tr("Log"));
    m_stack->addWidget(m_logBrowser);

    // Verbose tab
    m_verboseBrowser = new QTextBrowser;
    m_verboseBrowser->setReadOnly(true);
    m_verboseBrowser->setFont(QFont(QStringLiteral("Helvetica"), 9));
    if (thePrefs.useOriginalIcons())
        m_tabBar->addTab(QIcon(QStringLiteral(":/icons/Log.ico")), tr("Verbose"));
    else
        m_tabBar->addTab(tr("Verbose"));
    m_stack->addWidget(m_verboseBrowser);

    // Kad tab
    m_kadBrowser = new QTextBrowser;
    m_kadBrowser->setReadOnly(true);
    m_kadBrowser->setFont(QFont(QStringLiteral("Helvetica"), 9));
    if (thePrefs.useOriginalIcons())
        m_tabBar->addTab(QIcon(QStringLiteral(":/icons/Kad.ico")), tr("Kad"));
    else
        m_tabBar->addTab(tr("Kad"));
    m_stack->addWidget(m_kadBrowser);

    // IPC tab (shown only when enableIpcLog is true)
    m_ipcLogBrowser = new QTextBrowser;
    m_ipcLogBrowser->setReadOnly(true);
    m_ipcLogBrowser->setFont(QFont(QStringLiteral("Helvetica"), 9));
    m_ipcTabIndex = m_tabBar->count();
    if (thePrefs.useOriginalIcons())
        m_tabBar->addTab(QIcon(QStringLiteral(":/icons/Convert.ico")), tr("IPC"));
    else
        m_tabBar->addTab(tr("IPC"));
    m_stack->addWidget(m_ipcLogBrowser);
    setIpcTabVisible(thePrefs.enableIpcLog());

    // Clear a tab's highlight once the user actually looks at it.
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        m_tabBar->setTabTextColor(index, QColor{});
    });

    // Initial info message
    appendLog(QStringLiteral("<font color='#3399FF'>eMule Qt v%1 ready</font>")
                  .arg(QString(kAppVersion)));
    writeServerInfoBanner();

    // Install handler to capture core log output
    installMessageHandler();
}

LogWidget::~LogWidget()
{
    removeMessageHandler();
}

void LogWidget::appendServerInfo(const QString& text, ServerMsgType type)
{
    // Colours mirror the reference's log-line palette defaults
    // (srchybrid/Preferences.cpp:368-370). Info deliberately carries no colour so
    // it follows the widget's own text colour, as CLR_DEFAULT does there — but it
    // still gets a <span>, see below.
    QString open;
    switch (type) {
    case ServerMsgType::Success: open = QStringLiteral("<span style='color:#0000FF'>"); break;
    case ServerMsgType::Error:   open = QStringLiteral("<span style='color:#FF0000;font-weight:bold'>"); break;
    case ServerMsgType::Warning: open = QStringLiteral("<span style='color:#800080'>"); break;
    case ServerMsgType::Info:    open = QStringLiteral("<span>"); break;
    }

    // The wrapping <span> is load-bearing: QTextBrowser::append() guesses between
    // HTML and plain text via Qt::mightBeRichText(), and a line with no tags is
    // taken as plain text — which would print our escaped "&amp;" verbatim. Opening
    // with a tag makes the rich-text branch unconditional.
    //
    // One append() per line, too: in HTML a raw newline is whitespace, not a break,
    // so a multi-line payload passed as one string renders as a single run-on line.
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString body = line.trimmed().isEmpty()
            ? QStringLiteral("&nbsp;")            // keep blank separators visible
            : TextLinks::escapeAndLinkify(line);
        m_serverInfoBrowser->append(open + body + QStringLiteral("</span>"));
    }

    trimToLimit(m_serverInfoBrowser);
    highlightTab(kServerInfoTabIndex);
}

void LogWidget::appendLog(const QString& msg, const QString& ts, qint64 seqId)
{
    const QString timestamp = ts.isEmpty()
        ? QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))
        : ts;
    if (seqId == 0)
        seqId = QDateTime::currentDateTime().toSecsSinceEpoch();
    const QString html = QStringLiteral("<font color='gray'>%1</font> %2").arg(timestamp, msg);
    insertSorted(m_logBrowser, m_logSeqIds, seqId, html);
    trimToLimit(m_logBrowser, m_logSeqIds);
}

void LogWidget::appendVerbose(const QString& msg, const QString& ts, qint64 seqId)
{
    const QString timestamp = ts.isEmpty()
        ? QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))
        : ts;
    if (seqId == 0)
        seqId = QDateTime::currentDateTime().toSecsSinceEpoch();
    const QString html = QStringLiteral("<font color='gray'>%1</font> %2").arg(timestamp, msg);
    insertSorted(m_verboseBrowser, m_verboseSeqIds, seqId, html);
    trimToLimit(m_verboseBrowser, m_verboseSeqIds);
}

void LogWidget::appendKad(const QString& msg, const QString& ts, qint64 seqId)
{
    const QString timestamp = ts.isEmpty()
        ? QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))
        : ts;
    if (seqId == 0)
        seqId = QDateTime::currentDateTime().toSecsSinceEpoch();
    const QString html = QStringLiteral("<font color='gray'>%1</font> %2").arg(timestamp, msg);
    insertSorted(m_kadBrowser, m_kadSeqIds, seqId, html);
    trimToLimit(m_kadBrowser, m_kadSeqIds);
}

void LogWidget::insertSorted(QTextBrowser* browser, QList<qint64>& seqIds,
                              qint64 seqId, const QString& html)
{
    // Fast path: new entry is in order (most common case)
    if (seqIds.isEmpty() || seqId >= seqIds.last()) {
        browser->append(html);
        seqIds.append(seqId);
        return;
    }

    // Binary search for the insertion point
    auto it = std::lower_bound(seqIds.begin(), seqIds.end(), seqId);
    const int pos = static_cast<int>(it - seqIds.begin());
    seqIds.insert(pos, seqId);

    // Insert into the QTextBrowser at the corresponding block position.
    // Each append() creates one block, so block index == seqIds index.
    QTextDocument* doc = browser->document();
    QTextCursor cursor(doc);

    if (pos == 0) {
        cursor.movePosition(QTextCursor::Start);
    } else {
        // Move to the end of the block before insertion point
        QTextBlock block = doc->findBlockByNumber(pos - 1);
        cursor.setPosition(block.position() + block.length() - 1);
    }

    cursor.insertBlock();
    cursor.insertHtml(html);
}

void LogWidget::trimToLimit(QTextBrowser* browser, QList<qint64>& seqIds)
{
    const int limit = static_cast<int>(thePrefs.maxLogLines());
    QTextDocument* doc = browser->document();
    const int excess = doc->blockCount() - limit;
    if (excess <= 0)
        return;
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, excess);
    cursor.removeSelectedText();

    // Keep seqIds in sync
    if (excess <= seqIds.size())
        seqIds.remove(0, excess);
}

void LogWidget::trimToLimit(QTextBrowser* browser)
{
    const int limit = static_cast<int>(thePrefs.maxLogLines());
    QTextDocument* doc = browser->document();
    const int excess = doc->blockCount() - limit;
    if (excess <= 0)
        return;
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, excess);
    cursor.removeSelectedText();
}

void LogWidget::appendIpcMessage(const QString& msg, bool outgoing)
{
    if (!m_ipcLogBrowser) return;
    const QString color = outgoing ? QStringLiteral("#228B22") : QStringLiteral("#9933CC");
    const QString arrow = outgoing ? QStringLiteral("→") : QStringLiteral("←");
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_ipcLogBrowser->append(
        QStringLiteral("<font color='gray'>%1</font> <font color='%2'>%3 %4</font>")
            .arg(ts, color, arrow, msg.toHtmlEscaped()));
    trimToLimit(m_ipcLogBrowser);
}

void LogWidget::setIpcTabVisible(bool visible)
{
    if (m_ipcTabIndex >= 0)
        m_tabBar->setTabVisible(m_ipcTabIndex, visible);
}

void LogWidget::clearAll()
{
    m_serverInfoBrowser->clear();
    m_logBrowser->clear();
    m_verboseBrowser->clear();
    m_kadBrowser->clear();
    if (m_ipcLogBrowser) m_ipcLogBrowser->clear();
    m_logSeqIds.clear();
    m_verboseSeqIds.clear();
    m_kadSeqIds.clear();
    appendLog(QStringLiteral("<font color='#3399FF'>eMule Qt v%1 ready</font>")
                  .arg(QString(kAppVersion)));
    // The reference's Reset leaves the Server Info pane empty — the startup banner
    // is not re-written (srchybrid/ServerWnd.cpp:539).
}

QString LogWidget::logText() const { return m_logBrowser->toPlainText(); }
QString LogWidget::verboseText() const { return m_verboseBrowser->toPlainText(); }
QString LogWidget::kadText() const { return m_kadBrowser->toPlainText(); }

void LogWidget::setCustomFont(const QFont& font)
{
    for (auto* browser : {m_serverInfoBrowser, m_logBrowser, m_verboseBrowser,
                          m_kadBrowser, m_ipcLogBrowser})
        if (browser) browser->setFont(font);
}

void LogWidget::installMessageHandler()
{
    s_instance = this;
    s_previousHandler = qInstallMessageHandler(messageHandler);
}

void LogWidget::removeMessageHandler()
{
    if (s_instance == this) {
        qInstallMessageHandler(s_previousHandler);
        s_previousHandler = nullptr;
        s_instance = nullptr;
        closeLogFileSink();
    }
}

void LogWidget::applyLogFileSettings()
{
    applyLogFileSink(AppConfig::configDir(), QStringLiteral("emuleqt"),
                     thePrefs.logToDiskGui(), thePrefs.maxLogFileSize());
}

void LogWidget::messageHandler(QtMsgType type, const QMessageLogContext& context,
                               const QString& msg)
{
    // Always chain to previous handler (console output)
    if (s_previousHandler)
        s_previousHandler(type, context, msg);

    // Determine which category this message belongs to
    const char* cat = context.category ? context.category : "";
    const bool isEmuleCategory = (std::strncmp(cat, "emule.", 6) == 0);

    if (!isEmuleCategory)
        return;

    if (!s_instance)
        return;

    // Color the message based on severity
    QString colored;
    switch (type) {
    case QtWarningMsg:
        colored = QStringLiteral("<font color='#CC6600'>%1</font>").arg(msg.toHtmlEscaped());
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        colored = QStringLiteral("<font color='red'><b>%1</b></font>").arg(msg.toHtmlEscaped());
        break;
    case QtInfoMsg:
    default:
        colored = QStringLiteral("<font color='#3399FF'>%1</font>").arg(msg.toHtmlEscaped());
        break;
    }

    // Route to the correct tab based on category and severity
    const bool isServer = (std::strcmp(cat, "emule.server") == 0);
    const bool isKad = (std::strcmp(cat, "emule.kad") == 0);
    const bool isVerbose = (type == QtDebugMsg || type == QtWarningMsg);

    // Server category messages go to Server Info. The pane's real feed is the
    // dedicated PushServerMessage IPC event, not the log — this branch only
    // catches anything logged to emule.server in-process. It passes the RAW text
    // because appendServerInfo escapes internally, and returns so a message
    // cannot also land in the Log/Verbose tab below.
    if (isServer) {
        const ServerMsgType stype = (type == QtCriticalMsg || type == QtFatalMsg)
                                        ? ServerMsgType::Error
                                    : (type == QtWarningMsg) ? ServerMsgType::Warning
                                                             : ServerMsgType::Info;
        QMetaObject::invokeMethod(s_instance, [msg, stype]() {
            if (s_instance) s_instance->appendServerInfo(msg, stype);
        }, Qt::QueuedConnection);
        return;
    }

    // Kad category messages go to the Kad tab
    if (isKad) {
        QMetaObject::invokeMethod(s_instance, [colored]() {
            if (s_instance) s_instance->appendKad(colored);
        }, Qt::QueuedConnection);
        return;
    }

    // All other emule messages go to the Log tab (debug+warning go to Verbose)
    if (isVerbose) {
        QMetaObject::invokeMethod(s_instance, [colored]() {
            if (s_instance) s_instance->appendVerbose(colored);
        }, Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(s_instance, [colored]() {
            if (s_instance) s_instance->appendLog(colored);
        }, Qt::QueuedConnection);
    }
}

void LogWidget::writeServerInfoBanner()
{
    // MFC: CServerWnd::OnInitDialog — srchybrid/ServerWnd.cpp:131-138. The version
    // line, then the update-check hyperlink, then a blank separator. MFC writes
    // "eMule v%s"; kAppVersion is this app's version, so the name has to match it.
    m_serverInfoBrowser->append(
        QStringLiteral("<span>eMule Qt v%1</span>").arg(QString(kAppVersion).toHtmlEscaped()));
    m_serverInfoBrowser->append(
        QStringLiteral("<span><a href='%1'>%2</a></span>")
            .arg(QString(kVersionCheckUrl),
                 tr("Click here to check if a new version is available").toHtmlEscaped()));
    m_serverInfoBrowser->append(QStringLiteral("<span>&nbsp;</span>"));

    // The banner is not "new activity" — don't highlight the tab for it.
    m_tabBar->setTabTextColor(kServerInfoTabIndex, QColor{});
}

void LogWidget::highlightTab(int index)
{
    // MFC highlights the tab when the pane it feeds is not the visible one
    // (StatusSelector.HighlightItem — srchybrid/EmuleDlg.cpp:969-971).
    if (m_tabBar->currentIndex() != index)
        m_tabBar->setTabTextColor(index, QColor(Qt::red));
}

} // namespace eMule
