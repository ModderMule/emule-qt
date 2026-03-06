#include "pch.h"
/// @file SmtpClient.cpp
/// @brief Simple async SMTP client — implementation.

#include "net/SmtpClient.h"
#include "utils/Log.h"

#include <QSslConfiguration>
#include <QSslSocket>
#include <QDateTime>

namespace eMule {

SmtpClient::SmtpClient(QObject* parent)
    : QObject(parent)
{
}

SmtpClient::~SmtpClient() = default;

void SmtpClient::sendMail(const QString& server, int port, bool useTls,
                           int authType, const QString& user, const QString& password,
                           const QString& from, const QString& to,
                           const QString& subject, const QString& body,
                           bool allowSelfSigned)
{
    if (m_state != State::Disconnected) {
        logWarning(QStringLiteral("SMTP: already sending, dropping email to %1").arg(to));
        return;
    }

    m_from = from;
    m_to = to;
    m_subject = subject;
    m_body = body;
    m_authType = authType;
    m_user = user;
    m_password = password;
    m_useTls = useTls;
    m_state = State::Greeting;

    if (!m_socket) {
        m_socket = new QSslSocket(this);
        connect(m_socket, &QSslSocket::readyRead, this, &SmtpClient::onReadyRead);
        connect(m_socket, &QSslSocket::errorOccurred, this, &SmtpClient::onError);
    }

    // Allow self-signed certificates if requested
    if (allowSelfSigned) {
        QSslConfiguration sslConf = m_socket->sslConfiguration();
        sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
        m_socket->setSslConfiguration(sslConf);
    }

    logInfo(QStringLiteral("SMTP: connecting to %1:%2...").arg(server).arg(port));

    // Port 465 = implicit SSL (connect encrypted from the start)
    // Other ports with TLS = STARTTLS (upgrade after plaintext greeting)
    if (useTls && port == 465) {
        m_implicitSsl = true;
        m_socket->connectToHostEncrypted(server, static_cast<quint16>(port));
    } else {
        m_implicitSsl = false;
        m_socket->connectToHost(server, static_cast<quint16>(port));
    }
}

void SmtpClient::onReadyRead()
{
    while (m_socket->canReadLine()) {
        QString line = QString::fromUtf8(m_socket->readLine()).trimmed();
        processResponse(line);
    }
}

void SmtpClient::onError()
{
    finish(false, QStringLiteral("SMTP socket error: %1").arg(m_socket->errorString()));
}

void SmtpClient::sendLine(const QString& line)
{
    m_socket->write((line + QStringLiteral("\r\n")).toUtf8());
}

void SmtpClient::processResponse(const QString& response)
{
    // SMTP responses start with a 3-digit code
    int code = response.left(3).toInt();
    // Multi-line responses have '-' at position 3; wait for final line (space at pos 3)
    if (response.size() > 3 && response[3] == u'-')
        return;

    switch (m_state) {
    case State::Greeting:
        if (code == 220) {
            m_state = State::EhloSent;
            sendLine(QStringLiteral("EHLO emuleqt"));
        } else {
            finish(false, QStringLiteral("SMTP: unexpected greeting: %1").arg(response));
        }
        break;

    case State::EhloSent:
        if (code == 250) {
            if (m_useTls && !m_implicitSsl) {
                // STARTTLS upgrade (port 587 etc.) — implicit SSL skips this
                m_state = State::StartTlsSent;
                sendLine(QStringLiteral("STARTTLS"));
            } else if (m_authType > 0) {
                m_state = State::AuthSent;
                QByteArray credentials;
                credentials.append('\0');
                credentials.append(m_user.toUtf8());
                credentials.append('\0');
                credentials.append(m_password.toUtf8());
                sendLine(QStringLiteral("AUTH PLAIN %1").arg(QString::fromLatin1(credentials.toBase64())));
            } else {
                m_state = State::MailFromSent;
                sendLine(QStringLiteral("MAIL FROM:<%1>").arg(m_from));
            }
        } else {
            finish(false, QStringLiteral("SMTP: EHLO failed: %1").arg(response));
        }
        break;

    case State::StartTlsSent:
        if (code == 220) {
            m_socket->startClientEncryption();
            m_state = State::EhloAfterTls;
            sendLine(QStringLiteral("EHLO emuleqt"));
        } else {
            finish(false, QStringLiteral("SMTP: STARTTLS failed: %1").arg(response));
        }
        break;

    case State::EhloAfterTls:
        if (code == 250) {
            if (m_authType > 0) {
                m_state = State::AuthSent;
                QByteArray credentials;
                credentials.append('\0');
                credentials.append(m_user.toUtf8());
                credentials.append('\0');
                credentials.append(m_password.toUtf8());
                sendLine(QStringLiteral("AUTH PLAIN %1").arg(QString::fromLatin1(credentials.toBase64())));
            } else {
                m_state = State::MailFromSent;
                sendLine(QStringLiteral("MAIL FROM:<%1>").arg(m_from));
            }
        } else {
            finish(false, QStringLiteral("SMTP: EHLO after TLS failed: %1").arg(response));
        }
        break;

    case State::AuthSent:
        if (code == 235) {
            m_state = State::MailFromSent;
            sendLine(QStringLiteral("MAIL FROM:<%1>").arg(m_from));
        } else {
            finish(false, QStringLiteral("SMTP: AUTH failed: %1").arg(response));
        }
        break;

    case State::MailFromSent:
        if (code == 250) {
            m_state = State::RcptToSent;
            sendLine(QStringLiteral("RCPT TO:<%1>").arg(m_to));
        } else {
            finish(false, QStringLiteral("SMTP: MAIL FROM failed: %1").arg(response));
        }
        break;

    case State::RcptToSent:
        if (code == 250) {
            m_state = State::DataSent;
            sendLine(QStringLiteral("DATA"));
        } else {
            finish(false, QStringLiteral("SMTP: RCPT TO failed: %1").arg(response));
        }
        break;

    case State::DataSent:
        if (code == 354) {
            // Send email headers + body
            sendLine(QStringLiteral("From: %1").arg(m_from));
            sendLine(QStringLiteral("To: %1").arg(m_to));
            sendLine(QStringLiteral("Subject: %1").arg(m_subject));
            sendLine(QStringLiteral("Date: %1").arg(
                QDateTime::currentDateTimeUtc().toString(Qt::RFC2822Date)));
            sendLine(QStringLiteral("MIME-Version: 1.0"));
            sendLine(QStringLiteral("Content-Type: text/plain; charset=utf-8"));
            sendLine(QString{}); // blank line separates headers from body
            // Escape lines starting with '.'
            for (const auto& line : m_body.split(u'\n')) {
                if (line.startsWith(u'.'))
                    sendLine(QStringLiteral(".%1").arg(line));
                else
                    sendLine(line);
            }
            m_state = State::BodySent;
            sendLine(QStringLiteral(".")); // end of data
        } else {
            finish(false, QStringLiteral("SMTP: DATA failed: %1").arg(response));
        }
        break;

    case State::BodySent:
        if (code == 250) {
            m_state = State::QuitSent;
            sendLine(QStringLiteral("QUIT"));
        } else {
            finish(false, QStringLiteral("SMTP: message rejected: %1").arg(response));
        }
        break;

    case State::QuitSent:
        finish(true, QStringLiteral("SMTP: email sent successfully to %1").arg(m_to));
        break;

    case State::Disconnected:
        break;
    }
}

void SmtpClient::finish(bool success, const QString& message)
{
    if (success)
        logInfo(message);
    else
        logWarning(message);

    m_state = State::Disconnected;
    if (m_socket) {
        m_socket->disconnectFromHost();
    }
    emit finished(success, message);
}

} // namespace eMule
