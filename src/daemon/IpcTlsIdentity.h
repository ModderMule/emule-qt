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
    bool replacedEc = false;   ///< old EC pair the TLS backend can't serve was regenerated
};

/// Load the identity from @p configDir, creating a self-signed one on first run
/// (RSA 3072, 20 years: the GUI pins it, so it must not expire). An EC pair from
/// older builds is regenerated when Qt's TLS backend isn't OpenSSL: Secure
/// Transport / Schannel can't serve EC keys. Empty on failure, with @p error set.
[[nodiscard]] IpcTlsIdentity loadOrCreateIpcTlsIdentity(const QString& configDir,
                                                       QString* error = nullptr);

} // namespace eMule
