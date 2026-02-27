#include "controls/LogWidget.h"

#include "prefs/Preferences.h"

#include <QHBoxLayout>
#include <QDateTime>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QVBoxLayout>

#include <cstring>

namespace eMule {

LogWidget* LogWidget::s_instance = nullptr;
QtMessageHandler LogWidget::s_previousHandler = nullptr;

LogWidget::LogWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Top row: tabs + reset button
    auto* topRow = new QHBoxLayout;
    topRow->setContentsMargins(0, 0, 0, 0);

    m_tabs = new QTabWidget;
    m_tabs->setTabPosition(QTabWidget::South);

    // Server Info tab
    m_serverInfoBrowser = new QTextBrowser;
    m_serverInfoBrowser->setReadOnly(true);
    m_serverInfoBrowser->setOpenExternalLinks(true);
    m_serverInfoBrowser->setFont(QFont(QStringLiteral("Helvetica"), 9));
    m_tabs->addTab(m_serverInfoBrowser, QStringLiteral("Server Info"));

    // Log tab
    m_logBrowser = new QTextBrowser;
    m_logBrowser->setReadOnly(true);
    m_logBrowser->setFont(QFont(QStringLiteral("Helvetica"), 9));
    m_tabs->addTab(m_logBrowser, QStringLiteral("Log"));

    // Verbose tab
    m_verboseBrowser = new QTextBrowser;
    m_verboseBrowser->setReadOnly(true);
    m_verboseBrowser->setFont(QFont(QStringLiteral("Helvetica"), 9));
    m_tabs->addTab(m_verboseBrowser, QStringLiteral("Verbose"));

    // Kad tab
    m_kadBrowser = new QTextBrowser;
    m_kadBrowser->setReadOnly(true);
    m_kadBrowser->setFont(QFont(QStringLiteral("Helvetica"), 9));
    m_tabs->addTab(m_kadBrowser, QStringLiteral("Kad"));

    topRow->addWidget(m_tabs, 1);

    // Reset button (right side, matching screenshot)
    auto* btnLayout = new QVBoxLayout;
    btnLayout->setContentsMargins(4, 0, 0, 0);
    m_resetBtn = new QPushButton(QStringLiteral("Clear Logs"));
    m_resetBtn->setFixedWidth(50);
    btnLayout->addWidget(m_resetBtn);
    btnLayout->addStretch();
    topRow->addLayout(btnLayout);

    layout->addLayout(topRow);

    connect(m_resetBtn, &QPushButton::clicked, this, &LogWidget::clearAll);

    // Initial info message
    appendLog(QStringLiteral("<font color='#3399FF'>eMule Qt v0.1.0 ready</font>"));

    // Install handler to capture core log output
    installMessageHandler();
}

LogWidget::~LogWidget()
{
    removeMessageHandler();
}

void LogWidget::appendServerInfo(const QString& msg)
{
    m_serverInfoBrowser->append(msg);
}

void LogWidget::appendLog(const QString& msg, const QString& ts)
{
    const QString timestamp = ts.isEmpty()
        ? QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))
        : ts;
    m_logBrowser->append(QStringLiteral("<font color='gray'>%1</font> %2").arg(timestamp, msg));
    trimToLimit(m_logBrowser);
}

void LogWidget::appendVerbose(const QString& msg, const QString& ts)
{
    const QString timestamp = ts.isEmpty()
        ? QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))
        : ts;
    m_verboseBrowser->append(QStringLiteral("<font color='gray'>%1</font> %2").arg(timestamp, msg));
    trimToLimit(m_verboseBrowser);
}

void LogWidget::appendKad(const QString& msg, const QString& ts)
{
    const QString timestamp = ts.isEmpty()
        ? QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))
        : ts;
    m_kadBrowser->append(QStringLiteral("<font color='gray'>%1</font> %2").arg(timestamp, msg));
    trimToLimit(m_kadBrowser);
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

void LogWidget::clearAll()
{
    m_serverInfoBrowser->clear();
    m_logBrowser->clear();
    m_verboseBrowser->clear();
    m_kadBrowser->clear();
    appendLog(QStringLiteral("<font color='#3399FF'>eMule Qt v0.1.0 ready</font>"));
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
    }
}

void LogWidget::messageHandler(QtMsgType type, const QMessageLogContext& context,
                               const QString& msg)
{
    // Always chain to previous handler (console output)
    if (s_previousHandler)
        s_previousHandler(type, context, msg);

    if (!s_instance)
        return;

    // Determine which category this message belongs to
    const char* cat = context.category ? context.category : "";
    const bool isEmuleCategory = (std::strncmp(cat, "emule.", 6) == 0);

    if (!isEmuleCategory)
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
    default:
        colored = QStringLiteral("<font color='#3399FF'>%1</font>").arg(msg.toHtmlEscaped());
        break;
    }

    // Route to the correct tab based on category and severity
    const bool isServer = (std::strcmp(cat, "emule.server") == 0);
    const bool isKad = (std::strcmp(cat, "emule.kad") == 0);
    const bool isVerbose = (type == QtDebugMsg);

    // Server category messages go to Server Info
    if (isServer)
        QMetaObject::invokeMethod(s_instance, [colored]() {
            if (s_instance) s_instance->appendServerInfo(colored);
        }, Qt::QueuedConnection);

    // Kad category messages go to the Kad tab
    if (isKad) {
        QMetaObject::invokeMethod(s_instance, [colored]() {
            if (s_instance) s_instance->appendKad(colored);
        }, Qt::QueuedConnection);
        return;
    }

    // All other emule messages go to the Log tab (except debug which goes to Verbose)
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

} // namespace eMule
