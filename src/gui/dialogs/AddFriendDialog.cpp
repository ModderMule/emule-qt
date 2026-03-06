#include "pch.h"
/// @file AddFriendDialog.cpp
/// @brief "Add..." dialog implementation — matches MFC eMule layout.

#include "dialogs/AddFriendDialog.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace eMule {

AddFriendDialog::AddFriendDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Add..."));
    setFixedSize(340, 260);

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
    buttonBox->addButton(QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttonBox);

    connect(addBtn, &QPushButton::clicked, this, &AddFriendDialog::onAddClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_ipEdit->setFocus();
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
