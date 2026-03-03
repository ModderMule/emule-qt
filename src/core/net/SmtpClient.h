#pragma once

/// @file SmtpClient.h
/// @brief Simple async SMTP email client for notification emails.
///
/// Uses QTcpSocket + QSslSocket for TLS STARTTLS support.
/// Fire-and-forget: logs success/failure via Log system.

#include <QObject>
#include <QString>

class QSslSocket;

namespace eMule {

class SmtpClient : public QObject {
    Q_OBJECT

public:
    explicit SmtpClient(QObject* parent = nullptr);
    ~SmtpClient() override;

    /// Send an email asynchronously. Fire-and-forget.
    /// @param useTls  true = use TLS.  Port 465 → implicit SSL; otherwise → STARTTLS.
    /// @param allowSelfSigned  true = skip certificate verification (for self-signed certs).
    void sendMail(const QString& server, int port, bool useTls,
                  int authType, const QString& user, const QString& password,
                  const QString& from, const QString& to,
                  const QString& subject, const QString& body,
                  bool allowSelfSigned = false);

signals:
    /// Emitted when the SMTP transaction completes (success or failure).
    void finished(bool success, const QString& message);

private slots:
    void onReadyRead();
    void onError();

private:
    enum class State {
        Disconnected,
        Greeting,
        EhloSent,
        StartTlsSent,
        EhloAfterTls,
        AuthSent,
        MailFromSent,
        RcptToSent,
        DataSent,
        BodySent,
        QuitSent
    };

    void sendLine(const QString& line);
    void processResponse(const QString& response);
    void finish(bool success, const QString& message);

    QSslSocket* m_socket = nullptr;
    State m_state = State::Disconnected;

    // Current email parameters
    QString m_from;
    QString m_to;
    QString m_subject;
    QString m_body;
    int m_authType = 0;
    QString m_user;
    QString m_password;
    bool m_useTls = false;
    bool m_implicitSsl = false;
};

} // namespace eMule
