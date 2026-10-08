#include "pch.h"
/// @file AddFriendDialog.cpp
/// @brief "Add..." dialog implementation — matches MFC eMule layout.

#include "dialogs/AddFriendDialog.h"
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
    reqLayout->addRow(tr("IP Address:"), m_ipEdit);

    m_portEdit = new QLineEdit(this);
    m_portEdit->setFixedWidth(60);
    m_portEdit->setPlaceholderText(QStringLiteral("4662"));
    reqLayout->addRow(tr("Port:"), m_portEdit);

    mainLayout->addWidget(requiredGroup);

    // Additional Information group
    auto* additionalGroup = new QGroupBox(tr("Additional Information"), this);
    auto* addLayout = new QFormLayout(additionalGroup);

    m_nameEdit = new QLineEdit(this);
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

QString AddFriendDialog::ipAddress() const
{
    return m_ipEdit->text().trimmed();
}

int AddFriendDialog::port() const
{
    return m_portEdit->text().toInt();
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
    setWindowTitle(tr("Friend Details"));
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
    if (ipAddress().isEmpty()) {
        QMessageBox::warning(this, tr("Add Friend"),
                             tr("Please enter an IP address."));
        m_ipEdit->setFocus();
        return;
    }
    if (port() <= 0 || port() > 65535) {
        QMessageBox::warning(this, tr("Add Friend"),
                             tr("Please enter a valid port (1-65535)."));
        m_portEdit->setFocus();
        return;
    }
    accept();
}

} // namespace eMule
