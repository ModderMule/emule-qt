#pragma once

/// @file AddSourceDialog.h
/// @brief "Add Sources" for one download — port of MFC CAddSourceDlg.

#include <QDialog>

class QLineEdit;
class QRadioButton;

namespace eMule {

/// Lets the user name a source by address and port, or by URL. "Add" hands the
/// entry over and keeps the dialog open for the next one; "OK" adds and closes
/// (srchybrid/AddSourceDlg.cpp).
class AddSourceDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddSourceDialog(const QString& fileName, QWidget* parent = nullptr);

signals:
    /// @p url false: @p text is an address (or "address:port") and @p port the port
    /// field; true: @p text is an HTTP URL.
    void sourceEntered(bool url, const QString& text, int port);

private:
    void updateFields();
    /// False when the entry cannot be a source as typed.
    bool submit();

    QRadioButton* m_sourceRadio = nullptr;
    QRadioButton* m_urlRadio = nullptr;
    QLineEdit* m_addressEdit = nullptr;
    QLineEdit* m_portEdit = nullptr;
    QLineEdit* m_urlEdit = nullptr;
};

} // namespace eMule
