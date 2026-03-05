#pragma once

/// @file CoreConnectDialog.h
/// @brief Last-resort dialog for connecting to a remote eMule daemon
///        when no local emulecored binary is found.

#include <QDialog>
#include <QString>

class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPushButton;

namespace eMule {

class CoreConnectDialog : public QDialog {
    Q_OBJECT

public:
    explicit CoreConnectDialog(QWidget* parent = nullptr);

    [[nodiscard]] QString  address() const;
    [[nodiscard]] uint16_t port() const;
    [[nodiscard]] QString  token() const;
    [[nodiscard]] bool     saveToken() const;

private:
    QLineEdit*   m_addressEdit = nullptr;
    QSpinBox*    m_portSpin    = nullptr;
    QLineEdit*   m_tokenEdit   = nullptr;
    QCheckBox*   m_saveTokenCheck = nullptr;
    QPushButton* m_connectBtn  = nullptr;
    QPushButton* m_exitBtn     = nullptr;
};

} // namespace eMule
