#include "pch.h"
/// @file MetaAccountDialog.cpp
/// @brief Log in to an eNode server's Meta API account, or see its state.

#include "dialogs/MetaAccountDialog.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "app/IpcClient.h"

#include <QCborArray>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPointer>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace eMule {

using namespace Ipc;

namespace {

// enode.meta.v1 enums, as the daemon forwards them
constexpr int kAuthModeAccountRequired = 2;
constexpr int kStateActive = 2;
constexpr int kStateExpired = 3;
constexpr int kStateDisabled = 4;

MetaStatus statusOf(const QCborMap& m)
{
    return static_cast<MetaStatus>(m.value(QStringLiteral("status")).toInteger());
}

} // namespace

MetaAccountDialog::MetaAccountDialog(IpcClient* ipc, const QCborMap& meta, Purpose purpose, QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
    , m_purpose(purpose)
{
    setWindowIcon(QIcon(QStringLiteral(":/icons/Server.ico")));
    setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(this);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    layout->addWidget(m_info);

    m_form = new QWidget(this);
    auto* form = new QFormLayout(m_form);
    form->setContentsMargins(0, 0, 0, 0);
    m_user = new QLineEdit(m_form);
    m_pass = new QLineEdit(m_form);
    m_pass->setEchoMode(QLineEdit::Password);
    form->addRow(tr("User name:"), m_user);
    form->addRow(tr("Password:"), m_pass);
    layout->addWidget(m_form);

    m_steps = new QLabel(this);
    m_steps->setWordWrap(true);
    m_steps->setTextFormat(Qt::RichText);
    m_steps->setOpenExternalLinks(true);
    layout->addWidget(m_steps);

    m_links = new QLabel(this);
    m_links->setTextFormat(Qt::RichText);
    m_links->setOpenExternalLinks(true);   // default browser
    layout->addWidget(m_links);

    m_error = new QLabel(this);
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color: #c00000;"));
    layout->addWidget(m_error);

    auto* buttons = new QDialogButtonBox(this);
    m_loginButton = buttons->addButton(tr("Log In"), QDialogButtonBox::AcceptRole);
    m_refreshButton = buttons->addButton(tr("Check Again"), QDialogButtonBox::ActionRole);
    m_logoutButton = buttons->addButton(tr("Log Out"), QDialogButtonBox::ActionRole);
    m_closeButton = buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    // Log In handles AcceptRole itself; the dialog only accepts once the server agrees
    disconnect(buttons, &QDialogButtonBox::accepted, this, nullptr);
    connect(m_loginButton, &QPushButton::clicked, this, &MetaAccountDialog::onLogin);
    connect(m_refreshButton, &QPushButton::clicked, this, &MetaAccountDialog::refresh);
    connect(m_logoutButton, &QPushButton::clicked, this, &MetaAccountDialog::onLogout);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_pass, &QLineEdit::returnPressed, this, &MetaAccountDialog::onLogin);

    setMinimumWidth(400);
    applyMeta(meta);
    if (m_purpose == Purpose::Manage)
        refresh();
}

void MetaAccountDialog::updateMeta(const QCborMap& meta)
{
    applyMeta(meta);
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void MetaAccountDialog::applyMeta(const QCborMap& meta)
{
    const QString addr = meta.value(QStringLiteral("serverAddr")).toString();
    if (!addr.isEmpty())
        m_serverAddr = addr;
    const QString name = meta.value(QStringLiteral("serverName")).toString();
    if (!name.isEmpty())
        m_serverName = name;
    const QString server = m_serverName.isEmpty() ? m_serverAddr : m_serverName;
    setWindowTitle(tr("eNode Account — %1").arg(server));

    const MetaStatus status = statusOf(meta);
    const bool loggedIn = meta.value(QStringLiteral("loggedIn")).toBool();
    const int state = static_cast<int>(meta.value(QStringLiteral("state")).toInteger());
    const int authMode = static_cast<int>(meta.value(QStringLiteral("authMode")).toInteger());
    const QString username = meta.value(QStringLiteral("username")).toString();
    const QString regUrl = meta.value(QStringLiteral("registrationUrl")).toString();
    const QString accUrl = meta.value(QStringLiteral("accountUrl")).toString();

    if (!username.isEmpty() && m_user->text().isEmpty())
        m_user->setText(username);

    const bool inactive = status == MetaStatus::AccountInactive
                       || (loggedIn && state != 0 && state != kStateActive);
    const bool needLogin = !inactive && (status == MetaStatus::AuthRequired || (!loggedIn && m_purpose == Purpose::Manage
                                                                                && authMode == kAuthModeAccountRequired));

    // Download purpose: the moment the account works, hand back to the caller
    if (m_purpose == Purpose::Download && status == MetaStatus::Ok && (!loggedIn || state == kStateActive)
        && meta.contains(QStringLiteral("loggedIn"))) {
        accept();
        return;
    }

    QString info;
    if (needLogin) {
        info = m_purpose == Purpose::Download
            ? tr("<b>%1</b> needs an account to download torrent and Usenet search results. "
                 "Log in, or register an account on the server's website.").arg(server.toHtmlEscaped())
            : tr("Log in to your account on <b>%1</b>.").arg(server.toHtmlEscaped());
    } else if (inactive) {
        QString why = tr("is not active yet");
        if (state == kStateExpired)
            why = tr("has expired");
        else if (state == kStateDisabled)
            why = tr("has been disabled by the operator");
        info = tr("Your account <b>%1</b> on %2 %3. Complete the steps below on the server's website, "
                  "then choose Check Again.")
                   .arg((username.isEmpty() ? m_user->text() : username).toHtmlEscaped(),
                        server.toHtmlEscaped(), why);
    } else if (loggedIn) {
        const qint64 expires = meta.value(QStringLiteral("expiresAt")).toInteger();
        const QString until = expires > 0
            ? QLocale().toString(QDateTime::fromSecsSinceEpoch(expires), QLocale::ShortFormat)
            : tr("no expiry");
        info = tr("Logged in to <b>%1</b> as <b>%2</b>.<br>Access until: %3")
                   .arg(server.toHtmlEscaped(), username.toHtmlEscaped(), until);
    } else if (authMode != 0 && authMode != kAuthModeAccountRequired) {
        info = tr("<b>%1</b> needs no account — its torrent and Usenet results are free to download.")
                   .arg(server.toHtmlEscaped());
    } else {
        info = tr("Checking the account on <b>%1</b>...").arg(server.toHtmlEscaped());
    }
    m_info->setText(info);

    // open registration steps
    QStringList steps;
    for (const auto& v : meta.value(QStringLiteral("pendingSteps")).toArray()) {
        const QCborMap step = v.toMap();
        const QString title = step.value(QStringLiteral("title")).toString();
        const QString url = step.value(QStringLiteral("url")).toString();
        steps << (url.isEmpty() ? title.toHtmlEscaped() : link(url, title.isEmpty() ? url : title));
    }
    m_steps->setText(steps.isEmpty() ? QString()
                                     : tr("Open steps:") + QStringLiteral("<ul><li>")
                                           + steps.join(QStringLiteral("</li><li>")) + QStringLiteral("</li></ul>"));
    m_steps->setVisible(inactive && !steps.isEmpty());

    QStringList links;
    if (needLogin && !regUrl.isEmpty())
        links << link(regUrl, tr("Register an account"));
    if ((inactive || loggedIn) && !accUrl.isEmpty())
        links << link(accUrl, tr("Open account page"));
    m_links->setText(links.join(QStringLiteral(" &nbsp;·&nbsp; ")));
    m_links->setVisible(!links.isEmpty());

    m_form->setVisible(needLogin);
    m_loginButton->setVisible(needLogin);
    m_loginButton->setDefault(needLogin);
    m_refreshButton->setVisible(inactive);
    m_logoutButton->setVisible(loggedIn || inactive);
    m_closeButton->setText(m_purpose == Purpose::Download && needLogin ? tr("Cancel") : tr("Close"));
    if (needLogin)
        (m_user->text().isEmpty() ? m_user : m_pass)->setFocus();
    adjustSize();
}

void MetaAccountDialog::onLogin()
{
    if (!m_ipc || !m_ipc->isConnected()) {
        m_error->setText(tr("Not connected to the eMule core."));
        return;
    }
    const QString user = m_user->text().trimmed();
    const QString pass = m_pass->text();
    if (user.isEmpty() || pass.isEmpty()) {
        m_error->setText(tr("Enter a user name and a password."));
        return;
    }

    IpcMessage msg(IpcMsgType::MetaLogin);
    msg.append(m_serverAddr);
    msg.append(user);
    msg.append(pass);
    m_pass->clear();
    setBusy(true, tr("Logging in..."));

    QPointer<MetaAccountDialog> self(this);
    m_ipc->sendRequest(std::move(msg), [self](const IpcMessage& resp) {
        if (!self)
            return;
        self->setBusy(false);
        if (!resp.isValid()) {
            self->m_error->setText(tr("The connection to the eMule core was lost."));
            return;
        }
        const QCborMap meta = resp.fieldMap(2);
        if (!resp.fieldBool(0)) {
            self->m_error->setText(resp.fieldString(1));
            if (!meta.isEmpty())
                self->applyMeta(meta);
            return;
        }
        self->m_error->clear();
        self->applyMeta(meta);
    });
}

void MetaAccountDialog::onLogout()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    IpcMessage msg(IpcMsgType::MetaLogout);
    msg.append(m_serverAddr);
    setBusy(true);
    QPointer<MetaAccountDialog> self(this);
    m_ipc->sendRequest(std::move(msg), [self](const IpcMessage&) {
        if (!self)
            return;
        self->setBusy(false);
        self->refresh();
    });
}

void MetaAccountDialog::refresh()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    IpcMessage msg(IpcMsgType::GetMetaAuthStatus);
    msg.append(m_serverAddr);
    setBusy(true, tr("Checking..."));
    QPointer<MetaAccountDialog> self(this);
    m_ipc->sendRequest(std::move(msg), [self](const IpcMessage& resp) {
        if (!self)
            return;
        self->setBusy(false);
        if (!resp.isValid())
            return;
        if (resp.fieldBool(0)) {
            self->m_error->clear();
            self->applyMeta(resp.fieldMap(1));
        } else {
            self->m_error->setText(resp.fieldString(1));
            const QCborMap meta = resp.fieldMap(2);
            if (!meta.isEmpty())
                self->applyMeta(meta);
        }
    });
}

void MetaAccountDialog::setBusy(bool busy, const QString& note)
{
    m_loginButton->setEnabled(!busy);
    m_logoutButton->setEnabled(!busy);
    m_refreshButton->setEnabled(!busy);
    m_form->setEnabled(!busy);
    if (busy)
        m_error->setText(note);
}

QString MetaAccountDialog::link(const QString& url, const QString& text)
{
    // server-supplied: only web links may reach the browser
    const QUrl u(url);
    if (!u.isValid() || (u.scheme() != u"https" && u.scheme() != u"http"))
        return text.toHtmlEscaped();
    return QStringLiteral("<a href=\"%1\">%2</a>").arg(url.toHtmlEscaped(), text.toHtmlEscaped());
}

} // namespace eMule
