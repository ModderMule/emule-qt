#pragma once

/// @file LogWidget.h
/// @brief Tabbed log display matching the MFC "Server Info | Log | Verbose" tabs.
///
/// Installs a Qt message handler to capture qCInfo/qCWarning/qCDebug output
/// from the core logging categories and routes them to the appropriate tabs.

#include <QWidget>

#include <cstdint>

class QTabWidget;
class QTextBrowser;

namespace eMule {

/// Tabbed log widget with Server Info, Log, Verbose, and Kad tabs.
/// Captures Qt logging category output from the core layer.
class LogWidget : public QWidget {
    Q_OBJECT

public:
    explicit LogWidget(QWidget* parent = nullptr);
    ~LogWidget() override;

    /// Append a message to the Server Info tab.
    void appendServerInfo(const QString& msg);

    /// Append a message to the Log tab.
    /// If @p ts is non-empty it is used as the timestamp; otherwise current time.
    void appendLog(const QString& msg, const QString& ts = {});

    /// Append a message to the Verbose tab.
    void appendVerbose(const QString& msg, const QString& ts = {});

    /// Append a message to the Kad tab.
    void appendKad(const QString& msg, const QString& ts = {});

    /// Clear all tabs.
    void clearAll();

    /// Set a custom font on all log browser tabs.
    void setCustomFont(const QFont& font);

    /// Install the global message handler to capture core log output.
    void installMessageHandler();

    /// Remove the global message handler.
    void removeMessageHandler();

private:
    /// Remove oldest lines from @p browser if it exceeds the configured limit.
    static void trimToLimit(QTextBrowser* browser);
    QTabWidget* m_tabs = nullptr;
    QTextBrowser* m_serverInfoBrowser = nullptr;
    QTextBrowser* m_logBrowser = nullptr;
    QTextBrowser* m_verboseBrowser = nullptr;
    QTextBrowser* m_kadBrowser = nullptr;

    /// Static instance pointer for the message handler callback.
    static LogWidget* s_instance;

    /// The Qt message handler installed before ours (to chain).
    static QtMessageHandler s_previousHandler;

    /// Our custom message handler.
    static void messageHandler(QtMsgType type, const QMessageLogContext& context,
                               const QString& msg);
};

} // namespace eMule
