#include "pch.h"
/// @file AddFriendDialog.cpp
/// @brief "Add..." dialog implementation — matches MFC eMule layout.

#include "dialogs/AddFriendDialog.h"
#include "net/Address.h"

#include <QPointer>
#include "utils/DialogSizing.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace eMule {

AddFriendDialog::AddFriendDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add..."));

    auto* mainLayout = new QVBoxLayout(this);

    // Required Information group
    auto* requiredGroup = new QGroupBox(tr("Required Information"), this);
    auto* reqLayout = new QFormLayout(requiredGroup);

    m_ipEdit = new QLineEdit(this);
    m_ipEdit->setPlaceholderText(QStringLiteral("0.0.0.0"));
    m_ipLabel = new QLabel(tr("IP Address:"), this);
    reqLayout->addRow(m_ipLabel, m_ipEdit);

    m_portEdit = new QLineEdit(this);
    m_portEdit->setFixedWidth(60);
    m_portEdit->setPlaceholderText(QStringLiteral("4662"));
    reqLayout->addRow(tr("Port:"), m_portEdit);

    mainLayout->addWidget(requiredGroup);

    // Additional Information group
    auto* additionalGroup = new QGroupBox(tr("Additional Information"), this);
    auto* addLayout = new QFormLayout(additionalGroup);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setMaxLength(50);   // MFC GetMaxUserNickLength (AddFriend.cpp:87)
    addLayout->addRow(tr("Name:"), m_nameEdit);

    m_hashEdit = new QLineEdit(this);
    addLayout->addRow(tr("Hash:"), m_hashEdit);

    m_kadIdLabel = new QLabel(tr("Unknown"), this);
    addLayout->addRow(tr("KadID:"), m_kadIdLabel);

    mainLayout->addWidget(additionalGroup);

    // Last Seen label
    m_lastSeenLabel = new QLabel(this);
    auto* lastSeenLayout = new QHBoxLayout;
    lastSeenLayout->addWidget(new QLabel(tr("Last Seen:"), this));
    lastSeenLayout->addWidget(m_lastSeenLabel);
    lastSeenLayout->addStretch();
    mainLayout->addLayout(lastSeenLayout);

    mainLayout->addStretch();

    // Buttons
    auto* buttonBox = new QDialogButtonBox(this);
    auto* addBtn = buttonBox->addButton(tr("Add"), QDialogButtonBox::AcceptRole);
    m_addButton = addBtn;
    m_cancelButton = buttonBox->addButton(QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttonBox);

    connect(addBtn, &QPushButton::clicked, this, &AddFriendDialog::onAddClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_ipEdit->setFocus();

    // Not resizable, like the MFC original — but sized from the content, so a longer
    // translation or a larger system font grows the dialog instead of being cut off.
    DialogSizing::applyFixedSize(this, QSize(340, 260));
}

bool AddFriendDialog::parseEndpoint(const QString& ipText, const QString& portText,
                                    QString& address, int& port)
{
    QString host = ipText.trimmed();
    QString portPart;

    if (host.startsWith(QLatin1Char('['))) {
        // "[v6]" or "[v6]:port"
        const qsizetype close = host.indexOf(QLatin1Char(']'));
        if (close < 0)
            return false;
        if (close + 1 < host.size()) {
            if (host.at(close + 1) != QLatin1Char(':'))
                return false;
            portPart = host.mid(close + 2);
        }
        host = host.mid(1, close - 1);
    } else if (host.count(QLatin1Char(':')) == 1) {
        // "v4:port" — a bare IPv6 literal has more than one colon
        portPart = host.section(QLatin1Char(':'), 1);
        host = host.section(QLatin1Char(':'), 0, 0);
    }

    // Four dotted numbers for IPv4, as MFC asks: the system parser would also take
    // "203.0.113" and read it as 203.0.0.113.
    if (!host.contains(QLatin1Char(':')) && host.split(QLatin1Char('.')).size() != 4)
        return false;
    const Address parsed = Address::fromString(host);
    if (parsed.isNull())
        return false;

    bool ok = false;
    const int value = (portPart.isEmpty() ? portText.trimmed() : portPart).toInt(&ok);
    if (!ok || value <= 0 || value > 65535)
        return false;
    address = parsed.toString();
    port = value;
    return true;
}

QString AddFriendDialog::ipAddress() const
{
    QString address;
    int port = 0;
    return parseEndpoint(m_ipEdit->text(), m_portEdit->text(), address, port) ? address
                                                                              : m_ipEdit->text().trimmed();
}

int AddFriendDialog::port() const
{
    QString address;
    int port = 0;
    return parseEndpoint(m_ipEdit->text(), m_portEdit->text(), address, port) ? port
                                                                              : m_portEdit->text().toInt();
}

QString AddFriendDialog::friendName() const
{
    return m_nameEdit->text().trimmed();
}

QString AddFriendDialog::friendHash() const
{
    return m_hashEdit->text().trimmed();
}

void AddFriendDialog::showFriend(const QString& name, const QString& hash, const QString& address,
                                 int port, const QString& kadId, qint64 lastSeen)
{
    // MFC IDS_DETAILS and IDS_USERID (AddFriend.cpp:96, 105). The address itself, the
    // hash, "Never" and the Kad id stay readable here — deliberate, 2026-10.
    setWindowTitle(tr("Details"));
    m_ipLabel->setText(tr("User ID:"));
    m_ipEdit->setText(address);
    m_portEdit->setText(port > 0 ? QString::number(port) : QString());
    m_nameEdit->setText(name);
    m_hashEdit->setText(hash);
    for (QLineEdit* edit : {m_ipEdit, m_portEdit, m_nameEdit, m_hashEdit}) {
        edit->setReadOnly(true);
        edit->setPlaceholderText(QString());
    }
    if (!kadId.isEmpty())
        m_kadIdLabel->setText(kadId);
    m_lastSeenLabel->setText(lastSeen > 0
        ? QLocale().toString(QDateTime::fromSecsSinceEpoch(lastSeen), QLocale::ShortFormat)
        : tr("Never"));
    m_addButton->hide();
    m_cancelButton->setText(tr("Close"));
    m_cancelButton->setFocus();
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void AddFriendDialog::onAddClicked()
{
    // MFC CAddFriend::OnAddBtn (AddFriend.cpp:112-155)
    QString address;
    int port = 0;
    if (!parseEndpoint(m_ipEdit->text(), m_portEdit->text(), address, port)) {
        QMessageBox::warning(this, tr("Add Friend"), tr("You have to enter a valid IP and port!"));
        // the port field when the address alone is fine
        QString ignored;
        int dummy = 0;
        const bool addressOk = parseEndpoint(m_ipEdit->text(), QStringLiteral("1"), ignored, dummy);
        (addressOk ? m_portEdit : m_ipEdit)->setFocus();
        return;
    }
    if (!m_submitter) {
        accept();
        return;
    }

    // Stays open until the friend list has taken it: a duplicate is said here,
    // where it can be corrected, instead of the dialog closing on nothing.
    m_addButton->setEnabled(false);
    m_submitter(*this, [self = QPointer<AddFriendDialog>(this)](bool added) {
        if (!self)
            return;
        self->m_addButton->setEnabled(true);
        if (added) {
            self->accept();
            return;
        }
        QMessageBox::warning(self, tr("Add Friend"),
            tr("Friend not added.\n\nThere is already a friend with same IP address and port available."));
        self->m_ipEdit->setFocus();
    });
}

} // namespace eMule
