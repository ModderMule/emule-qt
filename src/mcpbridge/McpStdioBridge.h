#pragma once

/// @file McpStdioBridge.h
/// @brief Forwards MCP JSON-RPC messages from stdin to the daemon's /mcp endpoint.

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

#include <functional>

namespace eMule {

class McpStdioBridge : public QObject {
    Q_OBJECT

public:
    struct Target {
        QUrl url;               ///< http(s)://host:port/mcp
        QByteArray apiKey;
        bool insecureTls = false;   ///< accept a self-signed certificate
    };

    /// Where a local daemon listens, read from @p configDir/preferences.yml.
    /// @p problem says why when the URL comes back empty.
    [[nodiscard]] static Target targetFromConfig(const QString& configDir, QString& problem);

    /// @p writeLine receives each reply, one JSON document without a newline.
    McpStdioBridge(Target target, std::function<void(const QByteArray&)> writeLine,
                   QObject* parent = nullptr);

    /// One line from the client.
    void handleLine(const QByteArray& line);

    /// stdin is closed: quit once the replies in flight are out.
    void inputClosed();

signals:
    void finished();

private:
    void reply(const QJsonValue& id, int code, const QString& message);
    void maybeFinish();

    Target m_target;
    std::function<void(const QByteArray&)> m_writeLine;
    QNetworkAccessManager m_nam;
    int m_pending = 0;
    bool m_inputClosed = false;
};

} // namespace eMule
