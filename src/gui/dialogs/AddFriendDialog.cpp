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

    // ed3k (no MFC original): an ed3k:friend link instead of address and port
    auto* ed3kGroup = new QGroupBox(tr("ed3k"), this);
    m_ed3kGroup = ed3kGroup;
    auto* ed3kLayout = new QFormLayout(ed3kGroup);
    m_ed3kLinkEdit = new QLineEdit(this);
    m_ed3kLinkEdit->setPlaceholderText(QStringLiteral("ed3k:friend?key=..."));
    ed3kLayout->addRow(tr("ed3k link:"), m_ed3kLinkEdit);
    m_ed3kTextEdit = new QLineEdit(this);
    m_ed3kTextEdit->setMaxLength(96);   // friend.md §3: at most 96 B, checked in UTF-8 by the node
    m_ed3kTextEdit->setPlaceholderText(tr("Request text (optional)"));
    ed3kLayout->addRow(tr("Message:"), m_ed3kTextEdit);
    mainLayout->addWidget(ed3kGroup);
    ed3kGroup->setVisible(false);   // setEd3kSubmitter / setEd3kAvailable show it

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

void AddFriendDialog::setEd3kAvailable(bool on)
{
    m_ed3kGroup->setVisible(on && m_ed3kSubmitter);
    DialogSizing::applyFixedSize(this, QSize(340, on ? 330 : 260));
}

QString AddFriendDialog::ed3kLink() const
{
    return m_ed3kLinkEdit->text().trimmed();
}

QString AddFriendDialog::ed3kText() const
{
    return m_ed3kTextEdit->text().trimmed();
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
    m_ed3kGroup->hide();
    m_addButton->hide();
    m_cancelButton->setText(tr("Close"));
    m_cancelButton->setFocus();
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void AddFriendDialog::onAddClicked()
{
    // an ed3k link wins: the address fields are eD2K's
    if (m_ed3kGroup->isVisible() && !ed3kLink().isEmpty()) {
        submitEd3k();
        return;
    }
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

// ---------------------------------------------------------------------------
// Private: ed3k
// ---------------------------------------------------------------------------

void AddFriendDialog::submitEd3k()
{
    if (!ed3kLink().startsWith(QLatin1String("ed3k:friend?"), Qt::CaseInsensitive)) {
        QMessageBox::warning(this, tr("Add Friend"), tr("This is not an ed3k friend link."));
        m_ed3kLinkEdit->setFocus();
        return;
    }
    m_addButton->setEnabled(false);
    m_ed3kSubmitter(ed3kLink(), ed3kText(), [self = QPointer<AddFriendDialog>(this)](int result) {
        if (!self)
            return;
        self->m_addButton->setEnabled(true);
        if (result == kEd3kAdded) {
            self->accept();
            return;
        }
        QMessageBox::warning(self, tr("Add Friend"), tr("Friend not added.\n\n%1").arg(self->ed3kError(result)));
        self->m_ed3kLinkEdit->setFocus();
    });
}

// ED3K_ERR_* of ed3k.h (the GUI does not link packmule)
QString AddFriendDialog::ed3kError(int code) const
{
    switch (code) {
    case kEd3kNoNode: return tr("The ed3k node is not running.");
    case -10:         return tr("The ed3k link is invalid.");
    case -11:         return tr("The message is too long (at most 96 bytes).");
    case -15:         return tr("This is your own ed3k link.");
    case -16:         return tr("Too many ed3k friends (at most 64).");
    case -13:         return tr("ed3k friends are switched off.");
    default:          return tr("ed3k error %1.").arg(code);
    }
}

} // namespace eMule
