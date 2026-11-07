#pragma once

/// @file ClickNLoadServer.h
/// @brief Click'n'Load 2 on the GUI's machine (hoster.md "Click'n'Load"): link sites and
/// browser add-ons post links to 127.0.0.1:9666 as they would to JDownloader. Each post
/// goes to a handler (the Hoster tab), which hands it to the core over IPC, so it works
/// with a core on another machine too.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <utility>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace eMule {

class ClickNLoadServer : public QObject {
    Q_OBJECT

public:
    static constexpr quint16 kPort = 9666;

    /// One post a page made.
    struct Post {
        enum class Kind {
            Links,   ///< flash/add (urls) or flash/addcrypted2 (crypted + jk or k)
            Dlc,     ///< flash/addcrypted: crypted is a DLC container
        };
        Kind kind = Kind::Links;
        QString urls;
        QString crypted;
        QString jk;
        QString k;
        QString package;
        QString source;          ///< the page: source field, else Referer or Origin
        QStringList passwords;
    };
    /// Answers the page: "success", or "failed <error>".
    using Done = std::function<void(bool ok, const QString& error)>;
    using Handler = std::function<void(const Post& post, const Done& done)>;

    explicit ClickNLoadServer(Handler handler, QObject* parent = nullptr);
    ~ClickNLoadServer() override;

    /// Listens on loopback @p port (IPv4, and IPv6 when there) while @p on. A port in use
    /// (JDownloader runs) is tried again every minute.
    void setEnabled(bool on, quint16 port = kPort);
    [[nodiscard]] bool isEnabled() const { return m_enabled; }
    [[nodiscard]] bool isListening() const;
    [[nodiscard]] quint16 port() const;

    /// Fields of an x-www-form-urlencoded body or query, in order; repeated keys kept.
    [[nodiscard]] static QList<std::pair<QString, QString>> parseForm(const QByteArray& form);
    /// JD's getPackageName(): a trailing " - site.tld" goes.
    [[nodiscard]] static QString cleanPackageName(const QString& name);

private:
    struct Request {
        QByteArray buffer;
        bool answered = false;
    };

    void tryListen();
    void stopListening();
    void onConnection(QTcpServer* server);
    void onReadyRead(QTcpSocket* s);
    void handle(QTcpSocket* s, const QByteArray& method, const QByteArray& target,
                const QHash<QByteArray, QByteArray>& headers, const QByteArray& body);
    void handlePost(QTcpSocket* s, Post post);
    void respond(QTcpSocket* s, int code, const QByteArray& body,
                 const QByteArray& type = QByteArrayLiteral("text/html; charset=utf-8"),
                 const QList<std::pair<QByteArray, QByteArray>>& extra = {});

    Handler m_handler;
    QTcpServer* m_v4 = nullptr;
    QTcpServer* m_v6 = nullptr;
    QTimer* m_retry = nullptr;
    QHash<QTcpSocket*, Request> m_requests;
    bool m_enabled = false;
    bool m_warned = false;   ///< "port in use" logged once per enable
    quint16 m_port = kPort;
};

} // namespace eMule
