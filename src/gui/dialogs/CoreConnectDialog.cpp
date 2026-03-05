/// @file CoreConnectDialog.cpp
/// @brief Last-resort dialog for connecting to a remote eMule daemon.

#include "dialogs/CoreConnectDialog.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace eMule {

CoreConnectDialog::CoreConnectDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Connect to Core"));
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    setMinimumSize(380, 280);
    resize(380, 280);

    auto* mainLayout = new QVBoxLayout(this);

    // Header
    auto* headerLabel = new QLabel(
        tr("Could not find a local eMule core.\n"
           "Enter the address and authentication token of a remote core."),
        this);
    headerLabel->setWordWrap(true);
    mainLayout->addWidget(headerLabel);

    // Connection group
    auto* group = new QGroupBox(tr("Remote Core"), this);
    auto* formLayout = new QFormLayout(group);

    m_addressEdit = new QLineEdit(this);
    m_addressEdit->setPlaceholderText(QStringLiteral("192.168.1.100"));
    formLayout->addRow(tr("Address:"), m_addressEdit);

    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(4712);
    formLayout->addRow(tr("Port:"), m_portSpin);

    m_tokenEdit = new QLineEdit(this);
    m_tokenEdit->setPlaceholderText(tr("paste token here"));
    formLayout->addRow(tr("Token:"), m_tokenEdit);

    m_saveTokenCheck = new QCheckBox(tr("Save token"), this);
    m_saveTokenCheck->setChecked(true);
    formLayout->addRow(QString{}, m_saveTokenCheck);

    mainLayout->addWidget(group);
    mainLayout->addStretch();

    // Buttons
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    m_connectBtn = new QPushButton(tr("Connect"), this);
    m_connectBtn->setEnabled(false);
    m_connectBtn->setDefault(true);
    btnLayout->addWidget(m_connectBtn);

    m_exitBtn = new QPushButton(tr("Exit"), this);
    btnLayout->addWidget(m_exitBtn);
    mainLayout->addLayout(btnLayout);

    // Enable Connect only when both address and token are non-empty
    auto updateConnectBtn = [this]() {
        m_connectBtn->setEnabled(!m_addressEdit->text().trimmed().isEmpty()
                                 && !m_tokenEdit->text().trimmed().isEmpty());
    };
    connect(m_addressEdit, &QLineEdit::textChanged, this, updateConnectBtn);
    connect(m_tokenEdit, &QLineEdit::textChanged, this, updateConnectBtn);

    connect(m_connectBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_exitBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_addressEdit->setFocus();
}

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

QString CoreConnectDialog::address() const
{
    return m_addressEdit->text().trimmed();
}

uint16_t CoreConnectDialog::port() const
{
    return static_cast<uint16_t>(m_portSpin->value());
}

QString CoreConnectDialog::token() const
{
    return m_tokenEdit->text().trimmed();
}

bool CoreConnectDialog::saveToken() const
{
    return m_saveTokenCheck->isChecked();
}

} // namespace eMule
