#pragma once

/// @file IpcTlsIdentity.h
/// @brief The core's IPC TLS certificate: ipc-cert.pem / ipc-key.pem in the config dir.

#include <QSslCertificate>
#include <QSslKey>
#include <QString>

namespace eMule {

struct IpcTlsIdentity {
    QSslCertificate cert;
    QSslKey key;
};

/// Load the identity from @p configDir, creating a self-signed one on first run
/// (EC P-256, 20 years: the GUI pins it, so it must not expire). Empty on failure,
/// with @p error set.
[[nodiscard]] IpcTlsIdentity loadOrCreateIpcTlsIdentity(const QString& configDir,
                                                       QString* error = nullptr);

} // namespace eMule
