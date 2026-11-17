#pragma once

/// @file PhonePairDialog.h
/// @brief Pairing code for the qDonkey phone app: a QR code and a qdonkey://pair link with
///        the core's address, port, IPC certificate fingerprint and (optionally) token.

#include "dialogs/RememberedSizeDialog.h"

#include <QString>
#include <QStringList>

#include <cstdint>

class QCheckBox;
class QLabel;
class QLineEdit;

namespace eMule {

/// The ed3k tunnel (libs/qDonkey/docs/ed3k-tunnel.md), from GetIpcIdentity; empty nodeId:
/// none (ed3k off, ipc.tunnel off, an older core)
struct PhonePairTunnel {
    QString nodeId;         ///< 32 hex
    QString pairSecret;     ///< 32 hex, 10 min, one use
    QStringList endpoints;  ///< "ip:port" public
    QStringList relays;     ///< "<node hex>@ip:port"
    uint16_t udpPort = 0;   ///< the node's port at host (LAN)
};

class PhonePairDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    using Tunnel = PhonePairTunnel;

    /// @p fingerprint: "AB:CD:.." of the core's IPC certificate, empty = unknown (the phone
    /// pins on first connect then)
    PhonePairDialog(const QString& host, uint16_t port, const QString& token, const QString& fingerprint,
                    const Tunnel& tunnel = {}, QWidget* parent = nullptr);

    /// qdonkey://pair?h=&p=&fp=&t= and the tunnel's n=&ps=&u=&e=&r= ; empty values left out.
    /// The pair secret goes with the token only (both give control).
    [[nodiscard]] static QString pairLink(const QString& host, uint16_t port, const QString& token,
                                          const QString& fingerprint, const Tunnel& tunnel = {});

private:
    void refresh();

    QString m_host;
    uint16_t m_port = 0;
    QString m_token;
    QString m_fingerprint;
    Tunnel m_tunnel;
    QLabel* m_qrLabel = nullptr;
    QLineEdit* m_linkEdit = nullptr;
    QCheckBox* m_tokenCheck = nullptr;
};

} // namespace eMule
