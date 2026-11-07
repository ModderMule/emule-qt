#pragma once

/// @file CoreConnectDialog.h
/// @brief Dialog for connecting to a remote eMule daemon when no local
///        emulecored binary is found or the core is not reachable.

#include "dialogs/RememberedSizeDialog.h"
#include <QString>

class QLineEdit;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;

namespace eMule {

class CoreConnectDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    explicit CoreConnectDialog(QWidget* parent = nullptr);

    [[nodiscard]] QString  address() const;
    [[nodiscard]] uint16_t port() const;
    [[nodiscard]] QString  token() const;
    [[nodiscard]] bool     saveToken() const;

    /// Pre-fill the fields with the core we were connected to.
    void setTarget(const QString& address, uint16_t port, const QString& token);
    /// Lost-connection mode: other header text and a Retry Last Core button.
    void setLastCoreAvailable(bool available);

signals:
    /// Retry Last Core clicked; the dialog stays open until connected.
    void retryLastCoreRequested();

private:
    QLabel*      m_headerLabel = nullptr;
    QLineEdit*   m_addressEdit = nullptr;
    QSpinBox*    m_portSpin    = nullptr;
    QLineEdit*   m_tokenEdit   = nullptr;
    QCheckBox*   m_saveTokenCheck = nullptr;
    QPushButton* m_retryLastBtn = nullptr;
    QPushButton* m_connectBtn  = nullptr;
    QPushButton* m_exitBtn     = nullptr;
};

} // namespace eMule
