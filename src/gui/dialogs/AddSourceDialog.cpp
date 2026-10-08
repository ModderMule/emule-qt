#include "pch.h"
/// @file AddSourceDialog.cpp
/// @brief "Add Sources" for one download — port of MFC CAddSourceDlg.

#include "dialogs/AddSourceDialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace eMule {

AddSourceDialog::AddSourceDialog(const QString& fileName, QWidget* parent)
    : QDialog(parent)
{
    // MFC appends the file to the caption (AddSourceDlg.cpp:66-68)
    setWindowTitle(fileName.isEmpty() ? tr("Add Sources")
                                      : tr("Add Sources (%1)").arg(fileName));

    // Layout of IDD_ADDSOURCE (srchybrid/emule.rc:1176-1193)
    auto* group = new QGroupBox(tr("Source Type"), this);
    auto* grid = new QGridLayout(group);

    m_sourceRadio = new QRadioButton(tr("Source"), group);
    m_sourceRadio->setChecked(true);
    grid->addWidget(m_sourceRadio, 0, 0);
    grid->addWidget(new QLabel(tr("User IP"), group), 0, 1, Qt::AlignRight);
    m_addressEdit = new QLineEdit(group);
    grid->addWidget(m_addressEdit, 0, 2);
    grid->addWidget(new QLabel(tr("Port"), group), 0, 3, Qt::AlignRight);
    m_portEdit = new QLineEdit(group);
    m_portEdit->setValidator(new QIntValidator(1, 65535, m_portEdit));
    m_portEdit->setMaximumWidth(70);
    grid->addWidget(m_portEdit, 0, 4);

    m_urlRadio = new QRadioButton(tr("URL"), group);
    grid->addWidget(m_urlRadio, 1, 0);
    grid->addWidget(new QLabel(tr("URL"), group), 1, 1, Qt::AlignRight);
    m_urlEdit = new QLineEdit(group);
    grid->addWidget(m_urlEdit, 1, 2, 1, 3);
    grid->setColumnStretch(2, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* addButton = buttons->addButton(tr("Add"), QDialogButtonBox::ActionRole);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(group);
    layout->addWidget(buttons);
    setMinimumWidth(480);

    connect(m_sourceRadio, &QRadioButton::toggled, this, &AddSourceDialog::updateFields);
    connect(addButton, &QPushButton::clicked, this, &AddSourceDialog::submit);
    // OK adds what is typed, then closes (MFC OnOK calls OnBnClickedButton1)
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        submit();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateFields();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void AddSourceDialog::updateFields()
{
    const bool source = m_sourceRadio->isChecked();
    m_addressEdit->setEnabled(source);
    m_portEdit->setEnabled(source);
    m_urlEdit->setEnabled(!source);
    (source ? m_addressEdit : m_urlEdit)->setFocus();
}

bool AddSourceDialog::submit()
{
    if (m_urlRadio->isChecked()) {
        const QString url = m_urlEdit->text().trimmed();
        if (url.isEmpty())
            return false;
        emit sourceEntered(true, url, 0);
        return true;
    }

    const QString address = m_addressEdit->text().trimmed();
    // A port typed with the address wins over the port field; without either there
    // is nothing to dial.
    if (address.isEmpty() || (!address.contains(u':') && m_portEdit->text().isEmpty()))
        return false;
    emit sourceEntered(false, address, m_portEdit->text().toInt());
    return true;
}

} // namespace eMule
