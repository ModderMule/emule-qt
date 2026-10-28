/// @file IpcTlsIdentity.cpp
/// @brief The core's IPC TLS certificate — implementation.

#include "IpcTlsIdentity.h"

#include "crypto/SelfSignedCert.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

namespace eMule {

namespace {

constexpr long kValidDays = 20L * 365;

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeFile(const QString& path, const QByteArray& data, bool secret)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (secret)
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(data);
    if (!f.commit())
        return false;
    return !secret || QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

} // namespace

IpcTlsIdentity loadOrCreateIpcTlsIdentity(const QString& configDir, QString* error)
{
    const QDir dir(configDir);
    const QString certPath = dir.filePath(QStringLiteral("ipc-cert.pem"));
    const QString keyPath = dir.filePath(QStringLiteral("ipc-key.pem"));
    const auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return IpcTlsIdentity{QSslCertificate(), QSslKey()};
    };

    if (QFile::exists(certPath) && QFile::exists(keyPath)) {
        IpcTlsIdentity id{QSslCertificate(readAll(certPath), QSsl::Pem),
                          QSslKey(readAll(keyPath), QSsl::Ec, QSsl::Pem)};
        if (!id.cert.isNull() && !id.key.isNull())
            return id;
        return fail(QStringLiteral("%1 / %2 unreadable — delete both to regenerate")
                        .arg(certPath, keyPath));
    }

    const auto pem = generateSelfSignedPem("emulecored", kValidDays);
    if (!pem)
        return fail(QStringLiteral("cannot generate a certificate"));
    if (!dir.exists() && !QDir().mkpath(configDir))
        return fail(QStringLiteral("cannot create %1").arg(configDir));
    if (!writeFile(keyPath, pem->keyPem, true) || !writeFile(certPath, pem->certPem, false))
        return fail(QStringLiteral("cannot write %1").arg(certPath));
    return IpcTlsIdentity{QSslCertificate(pem->certPem, QSsl::Pem),
                          QSslKey(pem->keyPem, QSsl::Ec, QSsl::Pem)};
}

} // namespace eMule
