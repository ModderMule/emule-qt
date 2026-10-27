#include "pch.h"
#include "controls/LogWidget.h"
#include "utils/Appearance.h"

#include "controls/LogTextView.h"

#include "app/AppConfig.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/TextLinks.h"

#include <QColor>
#include <QDateTime>
#include <QFontDatabase>
#include <QIcon>
#include <QStackedWidget>
#include <QTabBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

#include <algorithm>


namespace eMule {

LogWidget* LogWidget::s_instance = nullptr;
QtMessageHandler LogWidget::s_previousHandler = nullptr;

namespace {

/// Internal URL for the banner's version-check link — not a real scheme, just a
/// sentinel the panel recognises and turns into an in-app check.
constexpr QLatin1StringView kVersionCheckUrl{"emuleqt:versioncheck"};

/// Server Info font. eMule shows server messages in the dialog font, Tahoma, so
/// servers lay out login-message ASCII art for it: e.g. eNode-go's banner relies
/// on '$' and '`' sharing one advance width. Any other face breaks the rows apart.
/// Where Tahoma is missing (usually Linux) the bundled Wine clone stands in; its
/// design widths match Tahoma's.
QFont serverInfoFont()
{
    static const bool registered = [] {
        if (QFontDatabase::hasFamily(QStringLiteral("Tahoma")))
            return true;
        return QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/WineTahoma.ttf")) >= 0;
    }();
    Q_UNUSED(registered);
    return QFont(QStringLiteral("Tahoma"), scaledPointSize(10));
}

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
    m_serverInfoBrowser = new LogTextView;
    m_serverInfoBrowser->setReadOnly(true);
    // Links are re-emitted rather than opened here: ed2k:// must reach the in-app
    // importer, and the banner's version-check link is an internal action. The
    // reference routes clicks the same way (CServerWnd::OnEnLinkServerBox).
    TextLinks::wireLinkClicks(m_serverInfoBrowser, this,
                              [this](const QString& link) { emit linkActivated(link); });
    m_serverInfoBrowser->setFont(serverInfoFont());
    // Fractional design-unit advances instead of per-glyph pixel rounding: glyphs
    // that share a width in the font then share it on screen at every size.
    m_serverInfoBrowser->document()->setUseDesignMetrics(true);
    if (thePrefs.useOriginalIcons())
        m_tabBar->addTab(QIcon(QStringLiteral(":/icons/ServerInfo.ico")), tr("Server Info"));
    else
        m_tabBar->addTab(tr("Server Info"));
    m_stack->addWidget(m_serverInfoBrowser);

    // Log tab
    m_logBrowser = new LogTextView;
    m_logBrowser->setReadOnly(true);
    m_logBrowser->setFont(QFont(QStringLiteral("Helvetica"), scaledPointSize(9)));
    if (thePrefs.useOriginalIcons())
        m_tabBar->addTab(QIcon(QStringLiteral(":/icons/Log.ico")), tr("Log"));
    else
        m_tabBar->addTab(tr("Log"));
    m_stack->addWidget(m_logBrowser);

    // Verbose tab
    m_verboseBrowser = new LogTextView;
    m_verboseBrowser->setReadOnly(true);
    m_verboseBrowser->setFont(QFont(QStringLiteral("Helvetica"), scaledPointSize(9)));
    if (thePrefs.useOriginalIcons())
        m_tabBar->addTab(QIcon(QStringLiteral(":/icons/Log.ico")), tr("Verbose"));
    else
        m_tabBar->addTab(tr("Verbose"));
    m_stack->addWidget(m_verboseBrowser);

    // Leecher tab — Xtreme's Anti-Leecher-Log pane, right after Verbose
    m_leecherTabIndex = addLogTab(m_leecherBrowser, QStringLiteral(":/icons/Log.ico"), tr("Leecher"));
    setLeecherTabVisible(thePrefs.antiLeecher());

    // Usenet tab (shown only when showUsenetLog is true)
    m_usenetBrowser = new LogTextView;
    m_usenetBrowser->setReadOnly(true);
    m_usenetBrowser->setFont(QFont(QStringLiteral("Helvetica"), scaledPointSize(9)));
    m_usenetTabIndex = m_tabBar->count();
    if (thePrefs.useOriginalIcons())
        m_tabBar->addTab(QIcon(QStringLiteral(":/icons/Usenet.ico")), tr("Usenet"));
    else
        m_tabBar->addTab(tr("Usenet"));
    m_stack->addWidget(m_usenetBrowser);
    setUsenetTabVisible(thePrefs.showUsenetLog());

    // Kad tab
    m_kadBrowser = new LogTextView;
    m_kadBrowser->setReadOnly(true);
    m_kadBrowser->setFont(QFont(QStringLiteral("Helvetica"), scaledPointSize(9)));
    if (thePrefs.useOriginalIcons())
        m_tabBar->addTab(QIcon(QStringLiteral(":/icons/Kad.ico")), tr("Kad"));
    else
        m_tabBar->addTab(tr("Kad"));
    m_stack->addWidget(m_kadBrowser);

    // ed3k tabs (shown while ed3kEnabled): the node's log, its debug lines, its Kad
    m_ed3kTabIndex = addLogTab(m_ed3kBrowser, QStringLiteral(":/icons/Log.ico"), tr("ed3k"));
    addLogTab(m_ed3kVerboseBrowser, QStringLiteral(":/icons/Log.ico"), tr("ed3k Verbose"));
    addLogTab(m_ed3kKadBrowser, QStringLiteral(":/icons/Kad.ico"), tr("ed3k Kad"));
    setEd3kTabsVisible(thePrefs.ed3kEnabled());

    // IPC tab (shown only when enableIpcLog is true)
    m_ipcLogBrowser = new LogTextView;
    m_ipcLogBrowser->setReadOnly(true);
    m_ipcLogBrowser->setFont(QFont(QStringLiteral("Helvetica"), scaledPointSize(9)));
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
        // MFC: a pane that got text while another was selected opens at its last
        // line, unless its Autoscroll is off (CServerWnd::UpdateLogTabSelection —
        // srchybrid/ServerWnd.cpp:550-582).
        auto* view = qobject_cast<LogTextView*>(m_stack->widget(index));
        if (m_unseenTabs.remove(index) && view && view->autoScroll())
            view->scrollToBottom();
    });

    // Initial info message
    appendLog(QStringLiteral("<font color='%1'>eMule Qt v%2 ready</font>")
                  .arg(roleColorName(TextRole::Info), QString(kAppVersion)));
    writeServerInfoBanner();
    // The start-up lines are not "new activity" — don't highlight tabs for them.
    clearHighlights();

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
    case ServerMsgType::Success: open = QStringLiteral("<span style='color:%1'>").arg(roleColorName(TextRole::Success)); break;
    case ServerMsgType::Error:   open = QStringLiteral("<span style='color:%1;font-weight:bold'>").arg(roleColorName(TextRole::Error)); break;
    case ServerMsgType::Warning: open = QStringLiteral("<span style='color:%1'>").arg(roleColorName(TextRole::Warning)); break;
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
    noteAppended(m_serverInfoBrowser);
}

void LogWidget::beginBatch()
{
    if (m_batchDepth++ == 0)
        setUpdatesEnabled(false);
}

void LogWidget::endBatch()
{
    if (m_batchDepth > 0 && --m_batchDepth == 0)
        setUpdatesEnabled(true);
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

void LogWidget::appendLeecher(const QString& msg, const QString& ts, qint64 seqId)
{
    appendTo(m_leecherBrowser, m_leecherSeqIds, msg, ts, seqId);
}

void LogWidget::routeLeecher(const QString& colored, const QString& ts, qint64 seqId)
{
    if (m_leecherTabVisible)
        appendLeecher(colored, ts, seqId);
    else
        appendVerbose(colored, ts, seqId);
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

void LogWidget::appendUsenet(const QString& msg, const QString& ts, qint64 seqId)
{
    const QString timestamp = ts.isEmpty()
        ? QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))
        : ts;
    if (seqId == 0)
        seqId = QDateTime::currentDateTime().toSecsSinceEpoch();
    const QString html = QStringLiteral("<font color='gray'>%1</font> %2").arg(timestamp, msg);
    insertSorted(m_usenetBrowser, m_usenetSeqIds, seqId, html);
    trimToLimit(m_usenetBrowser, m_usenetSeqIds);
}

void LogWidget::appendEd3k(const QString& msg, const QString& ts, qint64 seqId)
{
    appendTo(m_ed3kBrowser, m_ed3kSeqIds, msg, ts, seqId);
}

void LogWidget::appendEd3kVerbose(const QString& msg, const QString& ts, qint64 seqId)
{
    appendTo(m_ed3kVerboseBrowser, m_ed3kVerboseSeqIds, msg, ts, seqId);
}

void LogWidget::appendEd3kKad(const QString& msg, const QString& ts, qint64 seqId)
{
    appendTo(m_ed3kKadBrowser, m_ed3kKadSeqIds, msg, ts, seqId);
}

void LogWidget::routeEd3k(bool kad, QtMsgType type, const QString& colored, const QString& ts, qint64 seqId)
{
    if (m_ed3kTabsVisible) {
        if (kad)
            appendEd3kKad(colored, ts, seqId);
        else if (type == QtDebugMsg)
            appendEd3kVerbose(colored, ts, seqId);
        else
            appendEd3k(colored, ts, seqId);
    } else if (type == QtWarningMsg) {
        appendVerbose(colored, ts, seqId);
    } else if (type != QtDebugMsg) {
        appendLog(colored, ts, seqId);
    }
}

void LogWidget::routeUsenet(QtMsgType type, const QString& colored,
                            const QString& ts, qint64 seqId)
{
    if (m_usenetTabVisible)
        appendUsenet(colored, ts, seqId);
    else if (type == QtWarningMsg)
        appendVerbose(colored, ts, seqId);
    else if (type != QtDebugMsg)
        appendLog(colored, ts, seqId);
}

void LogWidget::insertSorted(LogTextView* browser, QList<qint64>& seqIds,
                              qint64 seqId, const QString& html)
{
    noteAppended(browser);

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

void LogWidget::trimToLimit(LogTextView* browser, QList<qint64>& seqIds)
{
    const int removed = browser->trimTo(static_cast<int>(thePrefs.maxLogLines()));
    // Keep seqIds in sync
    seqIds.remove(0, std::min(removed, static_cast<int>(seqIds.size())));
}

void LogWidget::trimToLimit(LogTextView* browser)
{
    browser->trimTo(static_cast<int>(thePrefs.maxLogLines()));
}

void LogWidget::appendIpcMessage(const QString& msg, bool outgoing)
{
    if (!m_ipcLogBrowser) return;
    const QString color = outgoing ? themed(QColor(0x22, 0x8B, 0x22), QColor(0x5B, 0xC8, 0x5B)).name()
                                   : themed(QColor(0x99, 0x33, 0xCC), QColor(0xD0, 0x8C, 0xFF)).name();
    const QString arrow = outgoing ? QStringLiteral("→") : QStringLiteral("←");
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_ipcLogBrowser->append(
        QStringLiteral("<font color='gray'>%1</font> <font color='%2'>%3 %4</font>")
            .arg(ts, color, arrow, msg.toHtmlEscaped()));
    trimToLimit(m_ipcLogBrowser);
    noteAppended(m_ipcLogBrowser);
}

void LogWidget::setIpcTabVisible(bool visible)
{
    if (m_ipcTabIndex >= 0)
        m_tabBar->setTabVisible(m_ipcTabIndex, visible);
}

void LogWidget::setUsenetTabVisible(bool visible)
{
    m_usenetTabVisible = visible;
    if (m_usenetTabIndex >= 0)
        m_tabBar->setTabVisible(m_usenetTabIndex, visible);
}

void LogWidget::setLeecherTabVisible(bool visible)
{
    m_leecherTabVisible = visible;
    if (m_leecherTabIndex >= 0)
        m_tabBar->setTabVisible(m_leecherTabIndex, visible);
}

void LogWidget::setEd3kTabsVisible(bool visible)
{
    m_ed3kTabsVisible = visible;
    for (int i = 0; m_ed3kTabIndex >= 0 && i < 3; ++i)
        m_tabBar->setTabVisible(m_ed3kTabIndex + i, visible);
}

void LogWidget::clearAll()
{
    m_serverInfoBrowser->clear();
    m_logBrowser->clear();
    m_verboseBrowser->clear();
    m_leecherBrowser->clear();
    m_kadBrowser->clear();
    m_usenetBrowser->clear();
    m_ed3kBrowser->clear();
    m_ed3kVerboseBrowser->clear();
    m_ed3kKadBrowser->clear();
    if (m_ipcLogBrowser) m_ipcLogBrowser->clear();
    m_logSeqIds.clear();
    m_verboseSeqIds.clear();
    m_leecherSeqIds.clear();
    m_kadSeqIds.clear();
    m_usenetSeqIds.clear();
    m_ed3kSeqIds.clear();
    m_ed3kVerboseSeqIds.clear();
    m_ed3kKadSeqIds.clear();
    appendLog(QStringLiteral("<font color='%1'>eMule Qt v%2 ready</font>")
                  .arg(roleColorName(TextRole::Info), QString(kAppVersion)));
    clearHighlights();
    // The reference's Reset leaves the Server Info pane empty — the startup banner
    // is not re-written (srchybrid/ServerWnd.cpp:539).
}

QString LogWidget::logText() const { return m_logBrowser->toPlainText(); }
QString LogWidget::verboseText() const { return m_verboseBrowser->toPlainText(); }
QString LogWidget::leecherText() const { return m_leecherBrowser->toPlainText(); }
QString LogWidget::kadText() const { return m_kadBrowser->toPlainText(); }
QString LogWidget::usenetText() const { return m_usenetBrowser->toPlainText(); }
QString LogWidget::ed3kText() const { return m_ed3kBrowser->toPlainText(); }
QString LogWidget::ed3kVerboseText() const { return m_ed3kVerboseBrowser->toPlainText(); }
QString LogWidget::ed3kKadText() const { return m_ed3kKadBrowser->toPlainText(); }

void LogWidget::setCustomFont(const QFont& font)
{
    for (auto* browser : {m_serverInfoBrowser, m_logBrowser, m_verboseBrowser, m_leecherBrowser,
                          m_usenetBrowser, m_kadBrowser, m_ed3kBrowser, m_ed3kVerboseBrowser,
                          m_ed3kKadBrowser, m_ipcLogBrowser})
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
        colored = QStringLiteral("<font color='%1'>%2</font>")
                      .arg(roleColorName(TextRole::Caution), msg.toHtmlEscaped());
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        colored = QStringLiteral("<font color='%1'><b>%2</b></font>")
                      .arg(roleColorName(TextRole::Error), msg.toHtmlEscaped());
        break;
    case QtInfoMsg:
    default:
        colored = QStringLiteral("<font color='%1'>%2</font>")
                      .arg(roleColorName(TextRole::Info), msg.toHtmlEscaped());
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

    if (std::strcmp(cat, "emule.leecher") == 0) {
        QMetaObject::invokeMethod(s_instance, [colored]() {
            if (s_instance) s_instance->routeLeecher(colored);
        }, Qt::QueuedConnection);
        return;
    }

    if (std::strcmp(cat, "emule.usenet") == 0) {
        QMetaObject::invokeMethod(s_instance, [type, colored]() {
            if (s_instance) s_instance->routeUsenet(type, colored);
        }, Qt::QueuedConnection);
        return;
    }

    if (std::strncmp(cat, "emule.ed3k", 10) == 0 && (cat[10] == '\0' || cat[10] == '.')) {
        const bool kad = std::strcmp(cat, "emule.ed3k.kad") == 0;
        QMetaObject::invokeMethod(s_instance, [kad, type, colored]() {
            if (s_instance) s_instance->routeEd3k(kad, type, colored);
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
}

void LogWidget::noteAppended(QTextBrowser* browser)
{
    // MFC highlights the tab when the pane it feeds is not the visible one
    // (StatusSelector.HighlightItem — srchybrid/EmuleDlg.cpp:916-918, 926-928,
    // 969-971). The Usenet, Kad and IPC tabs follow the same rule.
    const int index = m_stack->indexOf(browser);
    if (index < 0 || index == m_tabBar->currentIndex())
        return;
    m_unseenTabs.insert(index);
    m_tabBar->setTabTextColor(index, QColor(Qt::red));
}

void LogWidget::clearHighlights()
{
    m_unseenTabs.clear();
    for (int i = 0; i < m_tabBar->count(); ++i)
        m_tabBar->setTabTextColor(i, QColor{});
}

void LogWidget::appendTo(LogTextView* browser, QList<qint64>& seqIds, const QString& msg,
                         const QString& ts, qint64 seqId)
{
    const QString timestamp = ts.isEmpty()
        ? QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))
        : ts;
    if (seqId == 0)
        seqId = QDateTime::currentDateTime().toSecsSinceEpoch();
    const QString html = QStringLiteral("<font color='gray'>%1</font> %2").arg(timestamp, msg);
    insertSorted(browser, seqIds, seqId, html);
    trimToLimit(browser, seqIds);
}

int LogWidget::addLogTab(LogTextView*& browser, const QString& icon, const QString& title)
{
    browser = new LogTextView;
    browser->setReadOnly(true);
    browser->setFont(QFont(QStringLiteral("Helvetica"), scaledPointSize(9)));
    const int index = m_tabBar->count();
    if (thePrefs.useOriginalIcons())
        m_tabBar->addTab(QIcon(icon), title);
    else
        m_tabBar->addTab(title);
    m_stack->addWidget(browser);
    return index;
}

} // namespace eMule
