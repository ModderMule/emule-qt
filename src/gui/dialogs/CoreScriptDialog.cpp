#include "pch.h"
/// @file CoreScriptDialog.cpp
/// @brief Save a connect-to-core script.

#include "dialogs/CoreScriptDialog.h"
#include "dialogs/PhonePairDialog.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "prefs/Preferences.h"
#include "utils/CoreConnectScript.h"
#include "utils/DialogSizing.h"
#include "utils/Log.h"
#include "utils/ArgStr.h"

#include <QCborArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace eMule {

CoreScriptDialog::CoreScriptDialog(bool localCore, const QString& host, uint16_t port,
                                   const QString& token, const QString& listenAddress,
                                   IpcClient* ipc, QWidget* parent)
    : RememberedSizeDialog(parent)
    , m_localCore(localCore)
    , m_token(token.trimmed())
    , m_ipc(ipc)
{
    setWindowTitle(tr("Create Core Connect Script"));

    auto* mainLayout = new QVBoxLayout(this);

    auto* header = new QLabel(
        tr("Saves a script that starts eMule Qt on another computer and connects it to "
           "this core. Choose the system of that computer: Windows runs it with right-click › "
           "Run with PowerShell, macOS with a double-click, Linux from a terminal."),
        this);
    header->setWordWrap(true);
    DialogSizing::enableHeightForWidth(header);
    mainLayout->addWidget(header);

    auto* group = new QGroupBox(tr("Core"), this);
    auto* form = new QFormLayout(group);

    m_hostEdit = new QLineEdit(group);
    if (localCore) {
        // A remote GUI cannot dial what this machine calls itself
        m_hostEdit->setPlaceholderText(tr("public IP address or domain (required)"));
    } else {
        m_hostEdit->setText(host);
    }
    form->addRow(tr("Address:"), m_hostEdit);

    m_portSpin = new QSpinBox(group);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(port != 0 ? port : 4712);
    form->addRow(tr("Port:"), m_portSpin);

    // Where the script runs, not where the core does: this OS first guess
    m_targetCombo = new QComboBox(group);
    for (const auto t : {CoreConnectScript::Target::Windows, CoreConnectScript::Target::MacOS,
                         CoreConnectScript::Target::Linux})
        m_targetCombo->addItem(CoreConnectScript::targetName(t), static_cast<int>(t));
    m_targetCombo->setCurrentIndex(
        m_targetCombo->findData(static_cast<int>(CoreConnectScript::nativeTarget())));
    form->addRow(tr("Script for:"), m_targetCombo);

    m_embedTokenCheck = new QCheckBox(tr("Save core token inside the script"), group);
    m_embedTokenCheck->setToolTip(
        tr("Anyone with the script then has full control of the core. "
           "When off, the script asks for the token each time."));
    m_embedTokenCheck->setChecked(!m_token.isEmpty());
    m_embedTokenCheck->setEnabled(!m_token.isEmpty());
    if (m_token.isEmpty())
        m_embedTokenCheck->setToolTip(tr("No token known here: the script asks for it."));
    form->addRow(QString{}, m_embedTokenCheck);

    m_hostHint = new QLabel(group);
    m_hostHint->setWordWrap(true);
    m_hostHint->setStyleSheet(QStringLiteral("color: gray; font-style: italic;"));
    DialogSizing::enableHeightForWidth(m_hostHint);
    form->addRow(m_hostHint);
    mainLayout->addWidget(group);

    // Local core bound to loopback: nobody else reaches it, whatever the script says
    m_listenRow = new QWidget(this);
    auto* listenLayout = new QHBoxLayout(m_listenRow);
    listenLayout->setContentsMargins(0, 0, 0, 0);
    m_listenLabel = new QLabel(m_listenRow);
    m_listenLabel->setWordWrap(true);
    DialogSizing::enableHeightForWidth(m_listenLabel);
    m_listenBtn = new QPushButton(tr("Listen on This Address"), m_listenRow);
    m_listenBtn->setToolTip(tr("The core listens on the address above now and keeps it; "
                               "no restart"));
    listenLayout->addWidget(m_listenLabel, 1);
    listenLayout->addWidget(m_listenBtn, 0, Qt::AlignTop);
    mainLayout->addWidget(m_listenRow);
    m_listenRow->setVisible(false);
    connect(m_listenBtn, &QPushButton::clicked, this, [this] { listenOn(this->host()); });
    if (localCore) {
        QStringList addrs;
        for (const QHostAddress& a : Preferences::ipcBindAddressesOf(listenAddress)
                                         .value_or(QList{Preferences::ipcBindAddressOf(listenAddress)}))
            addrs.append(a.toString());
        setListenState(listenAddress, addrs);
        queryListen();
    }
    mainLayout->addStretch();

    auto* buttons = new QDialogButtonBox(this);
    m_saveBtn = buttons->addButton(QDialogButtonBox::Save);
    // the qDonkey phone app: a QR code instead of a script
    m_phoneBtn = buttons->addButton(tr("Pair Phone..."), QDialogButtonBox::ActionRole);
    m_phoneBtn->setToolTip(tr("Show a QR code the qDonkey phone app scans to reach this core"));
    buttons->addButton(QDialogButtonBox::Cancel);
    m_saveBtn->setDefault(true);
    mainLayout->addWidget(buttons);

    connect(m_hostEdit, &QLineEdit::textChanged, this, &CoreScriptDialog::updateSaveButton);
    connect(m_hostEdit, &QLineEdit::textChanged, this, &CoreScriptDialog::updateListenRow);
    connect(m_saveBtn, &QPushButton::clicked, this, &CoreScriptDialog::save);
    connect(m_phoneBtn, &QPushButton::clicked, this, &CoreScriptDialog::pairPhone);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateSaveButton();
    updateListenRow();

    m_hostEdit->setFocus();
    DialogSizing::applySize(this, QSize(420, 260), QSize(460, 300));
}

QString CoreScriptDialog::host() const
{
    return CoreConnectScript::normalizedHost(m_hostEdit->text());
}

uint16_t CoreScriptDialog::port() const
{
    return static_cast<uint16_t>(m_portSpin->value());
}

bool CoreScriptDialog::embedToken() const
{
    return m_embedTokenCheck->isChecked() && !m_token.isEmpty();
}

bool CoreScriptDialog::isLocalOnlyHost(const QString& host)
{
    const QString h = CoreConnectScript::normalizedHost(host);
    if (h.isEmpty() || h == u'*' || h.compare(u"localhost", Qt::CaseInsensitive) == 0)
        return true;
    const QHostAddress addr(h);
    return !addr.isNull()
        && (addr.isLoopback() || addr == QHostAddress::AnyIPv4 || addr == QHostAddress::AnyIPv6);
}

bool CoreScriptDialog::listensFor(const QList<QHostAddress>& listening, const QString& value,
                                  const QString& host)
{
    const QString h = CoreConnectScript::normalizedHost(host);
    const QHostAddress addr(h);
    const bool v4 = addr.isNull() || addr.protocol() == QAbstractSocket::IPv4Protocol;
    const bool v6 = addr.isNull() || addr.protocol() == QAbstractSocket::IPv6Protocol;
    bool beyondLoopback = false;
    for (const QHostAddress& a : listening) {
        if (a == QHostAddress(QHostAddress::Any) || (a == QHostAddress::AnyIPv4 && v4)
            || (a == QHostAddress::AnyIPv6 && v6) || (!addr.isNull() && a.isEqual(addr)))
            return true;
        beyondLoopback |= !a.isLoopback();
    }
    // a name: only the one it listens by
    return addr.isNull() && beyondLoopback
        && CoreConnectScript::normalizedHost(value).compare(h, Qt::CaseInsensitive) == 0;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void CoreScriptDialog::updateSaveButton()
{
    const QString h = host();
    QString hint;
    bool ok = false;
    if (h.isEmpty())
        hint = tr("Enter the address other computers reach this core at.");
    else if (!CoreConnectScript::isValidHost(h))
        hint = tr("Not a valid IP address or domain.");
    else if (isLocalOnlyHost(h))
        hint = tr("Only this computer can use this address.");
    else
        ok = true;
    m_hostHint->setText(hint);
    m_hostHint->setVisible(!hint.isEmpty());
    m_saveBtn->setEnabled(ok);
    m_phoneBtn->setEnabled(ok);
}

void CoreScriptDialog::save()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    const auto target = static_cast<CoreConnectScript::Target>(m_targetCombo->currentData().toInt());
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Core Connect Script"),
        QDir(dir).filePath(CoreConnectScript::suggestedFileName(host(), target)),
        CoreConnectScript::fileFilter(target));
    if (path.isEmpty())
        return;   // dialog stays: Cancel there is not Cancel here

    const CoreConnectScript::Params params{
        .host = host(),
        .port = port(),
        .token = embedToken() ? m_token : QString{},
        // this program's path means nothing on another OS: the script searches
        .appPath = target == CoreConnectScript::nativeTarget() ? CoreConnectScript::currentAppPath()
                                                               : QString{},
    };
    const QByteArray data = CoreConnectScript::generate(CoreConnectScript::kindOf(target), params);

    QSaveFile file(path);
    const bool written = file.open(QIODevice::WriteOnly) && file.write(data) == data.size()
                      && file.commit();
    if (!written) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not save %1:\n%2")
                                 .arg(QDir::toNativeSeparators(path), file.errorString()));
        return;
    }
#ifndef Q_OS_WIN
    // Owner only when it holds the token
    QFileDevice::Permissions perms = QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                   | QFileDevice::ExeOwner;
    if (!embedToken())
        perms |= QFileDevice::ReadGroup | QFileDevice::ExeGroup
               | QFileDevice::ReadOther | QFileDevice::ExeOther;
    QFile::setPermissions(path, perms);
#endif
    logInfo(QStringLiteral("Saved core connect script for %1:%2 to %3")
                .arg(argStr(params.host), argStr(params.port), argStr(path)));
    accept();
}

void CoreScriptDialog::pairPhone()
{
    const auto show = [this](const QString& fingerprint, const PhonePairDialog::Tunnel& tunnel) {
        PhonePairDialog dlg(host(), port(), m_token, fingerprint, tunnel, this);
        dlg.exec();
    };
    if (!m_ipc || !m_ipc->isConnected()) {
        show({}, {});
        return;
    }
    // the certificate fingerprint lets the phone pin it before sending the token; with a pair
    // secret the phone can also reach the core over the ed3k tunnel
    Ipc::IpcMessage req(Ipc::IpcMsgType::GetIpcIdentity);
    req.append(true);
    m_ipc->sendRequest(std::move(req), [self = QPointer(this), show](const Ipc::IpcMessage& resp) {
        if (!self)
            return;
        if (!resp.isValid() || !resp.fieldBool(0)) {
            show({}, {});
            return;
        }
        const QCborMap m = resp.fieldMap(1);
        PhonePairDialog::Tunnel tunnel;
        if (m.value(QStringLiteral("tunnel")).toBool()) {
            tunnel.nodeId = m.value(QStringLiteral("nodeId")).toString();
            tunnel.pairSecret = m.value(QStringLiteral("pairSecret")).toString();
            tunnel.udpPort = static_cast<uint16_t>(m.value(QStringLiteral("udpPort")).toInteger());
            for (const QCborValue& v : m.value(QStringLiteral("endpoints")).toArray())
                tunnel.endpoints.append(v.toString());
            for (const QCborValue& v : m.value(QStringLiteral("relays")).toArray())
                tunnel.relays.append(v.toString());
        }
        show(m.value(QStringLiteral("fingerprint")).toString(), tunnel);
    });
}

void CoreScriptDialog::queryListen()
{
    if (!m_ipc)
        return;
    Ipc::IpcMessage msg(Ipc::IpcMsgType::ListenIpcAddress);
    msg.append(QString());
    m_ipc->sendRequest(std::move(msg), [self = QPointer(this)](const Ipc::IpcMessage& resp) {
        if (!self || !resp.isValid() || !resp.fieldBool(0))
            return;
        QStringList addrs;
        for (const QCborValue& v : resp.fieldArray(3))
            addrs.append(v.toString());
        self->setListenState(resp.fieldString(2), addrs);
    });
}

void CoreScriptDialog::listenOn(const QString& value)
{
    if (!m_ipc || m_listenBusy)
        return;
    m_listenBusy = true;
    updateListenRow();
    Ipc::IpcMessage msg(Ipc::IpcMsgType::ListenIpcAddress);
    msg.append(value);
    m_ipc->sendRequest(std::move(msg), [self = QPointer(this), value](const Ipc::IpcMessage& resp) {
        if (!self)
            return;
        self->m_listenBusy = false;
        if (!resp.isValid()) {
            self->updateListenRow();
            QMessageBox::warning(self, self->windowTitle(), tr("The core did not answer."));
            return;
        }
        QStringList addrs;
        for (const QCborValue& v : resp.fieldArray(3))
            addrs.append(v.toString());
        self->setListenState(resp.fieldString(2), addrs);
        if (resp.fieldBool(0)) {
            self->m_changedListen = resp.fieldString(2);
            return;
        }
        const QString error = resp.fieldString(1);
        if (resp.fieldBool(4) && value != u'*') {
            // A router's public address or a name for it: only "all addresses" works
            const auto answer = QMessageBox::question(
                self, self->windowTitle(),
                tr("%1 is no address of this computer, so the core cannot listen on it.\n\n"
                   "If it is your router's public address, the core must listen on all "
                   "addresses and the router forward TCP port %2 to this computer.\n\n"
                   "Listen on all addresses?").arg(value).arg(self->m_portSpin->value()));
            if (answer == QMessageBox::Yes)
                self->listenOn(QStringLiteral("*"));
            return;
        }
        QMessageBox::warning(self, self->windowTitle(),
                             tr("The core cannot listen on %1:\n%2").arg(value, error));
    });
}

void CoreScriptDialog::setListenState(const QString& value, const QStringList& addresses)
{
    m_listenValue = value;
    m_listening.clear();
    bool loopbackOnly = true;
    for (const QString& a : addresses) {
        const QHostAddress addr = a == u'*' ? QHostAddress(QHostAddress::Any) : QHostAddress(a);
        m_listening.append(addr);
        loopbackOnly &= addr.isLoopback();
    }
    // shown once seen loopback-only; stays to show the outcome
    if (m_localCore && loopbackOnly)
        m_listenRow->setVisible(true);
    updateListenRow();
}

void CoreScriptDialog::updateListenRow()
{
    if (!m_listenRow || !m_listenRow->isVisibleTo(this))
        return;
    const QString h = host();
    bool loopbackOnly = true;
    QStringList addrs;
    for (const QHostAddress& a : std::as_const(m_listening)) {
        loopbackOnly &= a.isLoopback();
        addrs.append(a == QHostAddress(QHostAddress::Any) ? tr("all addresses") : a.toString());
    }
    const bool usable = CoreConnectScript::isValidHost(h) && !isLocalOnlyHost(h);
    const bool reachable = usable && listensFor(m_listening, m_listenValue, h);
    if (loopbackOnly)
        m_listenLabel->setText(tr("This core accepts only connections from this computer."));
    else if (usable && !reachable)
        m_listenLabel->setText(tr("The core does not listen on %1.").arg(h));
    else
        m_listenLabel->setText(tr("The core listens on %1. Behind a router, forward TCP port %2 "
                                  "to this computer.")
                                   .arg(argStr(addrs.join(QStringLiteral(", "))),
                                        argStr(m_portSpin->value())));
    m_listenBtn->setEnabled(m_ipc && usable && !reachable && !m_listenBusy);
}

} // namespace eMule
