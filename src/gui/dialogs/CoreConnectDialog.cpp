#include "pch.h"
/// @file CoreConnectDialog.cpp
/// @brief Last-resort dialog for connecting to a remote eMule daemon.

#include "dialogs/CoreConnectDialog.h"
#include "utils/DialogSizing.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace eMule {

CoreConnectDialog::CoreConnectDialog(QWidget* parent)
    : RememberedSizeDialog(parent)
{
    setWindowTitle(tr("Connect to Core"));
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

    auto* mainLayout = new QVBoxLayout(this);

    // Header
    m_headerLabel = new QLabel(
        tr("Could not find a local eMule core.\n"
           "Enter the address and authentication token of a remote core."),
        this);
    m_headerLabel->setWordWrap(true);
    DialogSizing::enableHeightForWidth(m_headerLabel);
    mainLayout->addWidget(m_headerLabel);

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
    m_retryLastBtn = new QPushButton(tr("Retry Last Core"), this);
    m_retryLastBtn->setToolTip(tr("Connect once more to the core that last worked"));
    m_retryLastBtn->setVisible(false);
    btnLayout->addWidget(m_retryLastBtn);

    m_connectBtn = new QPushButton(tr("Connect"), this);
    m_connectBtn->setEnabled(false);
    m_connectBtn->setDefault(true);
    btnLayout->addWidget(m_connectBtn);

    m_exitBtn = new QPushButton(tr("Exit"), this);
    btnLayout->addWidget(m_exitBtn);
    mainLayout->addLayout(btnLayout);

    // Enable Connect only when both address and token are non-empty
    auto updateConnectBtn = [this]() {
        // A core on this machine trusts loopback without a token
        const QString addr = m_addressEdit->text().trimmed();
        const bool loopback = addr.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0
                              || QHostAddress(addr).isLoopback();
        m_connectBtn->setEnabled(!addr.isEmpty()
                                 && (loopback || !m_tokenEdit->text().trimmed().isEmpty()));
    };
    connect(m_addressEdit, &QLineEdit::textChanged, this, updateConnectBtn);
    connect(m_tokenEdit, &QLineEdit::textChanged, this, updateConnectBtn);

    connect(m_connectBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_exitBtn, &QPushButton::clicked, this, &QDialog::reject);
    // Repeatable: stays open, closed by the caller once connected
    connect(m_retryLastBtn, &QPushButton::clicked, this, &CoreConnectDialog::retryLastCoreRequested);

    m_addressEdit->setFocus();

    DialogSizing::applySize(this, QSize(380, 280), QSize(380, 280));
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

void CoreConnectDialog::setTarget(const QString& address, uint16_t port, const QString& token)
{
    m_addressEdit->setText(address);
    if (port != 0)
        m_portSpin->setValue(port);
    m_tokenEdit->setText(token);
}

void CoreConnectDialog::setLastCoreAvailable(bool available)
{
    m_retryLastBtn->setVisible(available);
    if (!available)
        return;
    m_headerLabel->setText(tr("Lost the connection to the eMule core.\n"
                              "Retry the last core, or enter the address and "
                              "authentication token of another one."));
    m_connectBtn->setDefault(false);
    m_retryLastBtn->setDefault(true);
    m_retryLastBtn->setFocus();
}

} // namespace eMule
