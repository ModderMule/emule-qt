#pragma once

/// @file PhonePairDialog.h
/// @brief Pairing code for the qDonkey phone app: a QR code and a qdonkey://pair link with
///        the core's address, port, IPC certificate fingerprint and (optionally) token.

#include "dialogs/RememberedSizeDialog.h"

#include <QString>

#include <cstdint>

class QCheckBox;
class QLabel;
class QLineEdit;

namespace eMule {

class PhonePairDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    /// @p fingerprint: "AB:CD:.." of the core's IPC certificate, empty = unknown (the phone
    /// pins on first connect then)
    PhonePairDialog(const QString& host, uint16_t port, const QString& token, const QString& fingerprint,
                    QWidget* parent = nullptr);

    /// qdonkey://pair?h=&p=&fp=&t= ; empty token or fingerprint left out
    [[nodiscard]] static QString pairLink(const QString& host, uint16_t port, const QString& token,
                                          const QString& fingerprint);

private:
    void refresh();

    QString m_host;
    uint16_t m_port = 0;
    QString m_token;
    QString m_fingerprint;
    QLabel* m_qrLabel = nullptr;
    QLineEdit* m_linkEdit = nullptr;
    QCheckBox* m_tokenCheck = nullptr;
};

} // namespace eMule
