#pragma once

/// @file SmtpClient.h
/// @brief Simple async SMTP email client for notification emails.
///
/// Plain, implicit SSL/TLS or STARTTLS; AUTH PLAIN or LOGIN (MFC IDD_SMTPSERVER).
/// Fire-and-forget: logs success/failure via Log system.

#include <QObject>
#include <QString>

class QSslSocket;

namespace eMule {

/// MFC IDC_SMTPSEC, in its order.
enum class SmtpSecurity { None = 0, SslTls = 1, StartTls = 2 };
/// MFC IDC_SMTPAUTH, in its order.
enum class SmtpAuth { None = 0, Plain = 1, Login = 2 };

class SmtpClient : public QObject {
    Q_OBJECT

public:
    /// The port each security mode defaults to (MFC SMTPdialog.cpp:98-116).
    [[nodiscard]] static constexpr int defaultPort(SmtpSecurity security)
    {
        return security == SmtpSecurity::SslTls ? 465 : security == SmtpSecurity::StartTls ? 587 : 25;
    }
    /// What the old "use TLS" switch meant: implicit SSL on 465, STARTTLS elsewhere.
    [[nodiscard]] static constexpr SmtpSecurity securityFromLegacyTls(bool useTls, int port)
    {
        return !useTls ? SmtpSecurity::None : port == 465 ? SmtpSecurity::SslTls : SmtpSecurity::StartTls;
    }

    explicit SmtpClient(QObject* parent = nullptr);
    ~SmtpClient() override;

    /// Send an email asynchronously. Fire-and-forget.
    /// @param allowSelfSigned  true = skip certificate verification (for self-signed certs).
    void sendMail(const QString& server, int port, SmtpSecurity security,
                  SmtpAuth authType, const QString& user, const QString& password,
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
        AuthLoginUser,   ///< "AUTH LOGIN" sent, user name asked next
        AuthLoginPass,
        MailFromSent,
        RcptToSent,
        DataSent,
        BodySent,
        QuitSent
    };

    void sendLine(const QString& line);
    void processResponse(const QString& response);
    void finish(bool success, const QString& message);
    /// Start the configured authentication, or go straight to MAIL FROM.
    void authenticateOrSend();

    QSslSocket* m_socket = nullptr;
    State m_state = State::Disconnected;

    // Current email parameters
    QString m_from;
    QString m_to;
    QString m_subject;
    QString m_body;
    SmtpAuth m_authType = SmtpAuth::None;
    QString m_user;
    QString m_password;
    SmtpSecurity m_security = SmtpSecurity::None;
};

} // namespace eMule
