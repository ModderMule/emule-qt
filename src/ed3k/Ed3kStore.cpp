/// @file Ed3kStore.cpp
/// @brief Key file and state blobs of the ed3k node — implementation.

#include "Ed3kStore.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QSaveFile>

#include <openssl/crypto.h>
#include <openssl/rand.h>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace eMule::ed3k {

namespace {

constexpr auto kOwnerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

/// Creates @p path exclusively with owner-only permissions and writes @p seed.
bool createKeyFile(const QString& path, const Seed& seed, QString* error)
{
#ifdef Q_OS_UNIX
    // O_EXCL: never replaces a file; 0600 from the first byte on
    const QByteArray native = QFile::encodeName(path);
    const int fd = ::open(native.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        if (error)
            *error = QStringLiteral("create %1: errno %2").arg(path).arg(errno);
        return false;
    }
    std::size_t done = 0;
    while (done < seed.size()) {
        const ssize_t n = ::write(fd, seed.data() + done, seed.size() - done);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            break;
        }
        done += static_cast<std::size_t>(n);
    }
    const bool synced = ::fsync(fd) == 0;
    ::close(fd);
    if (done != seed.size() || !synced) {
        ::unlink(native.constData());
        if (error)
            *error = QStringLiteral("write %1 failed").arg(path);
        return false;
    }
    return true;
#else
    // ToDo: owner-only DACL on Windows (SetNamedSecurityInfoW, as packmule-node disk.rs)
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error)
            *error = QStringLiteral("create %1: %2").arg(path, f.errorString());
        return false;
    }
    f.setPermissions(kOwnerOnly);
    const qint64 n = f.write(reinterpret_cast<const char*>(seed.data()), qint64(seed.size()));
    const bool ok = n == qint64(seed.size()) && f.flush();
    f.close();
    if (!ok) {
        f.remove();
        if (error)
            *error = QStringLiteral("write %1 failed").arg(path);
    }
    return ok;
#endif
}

} // namespace

QString stateDir(const QString& configDir)
{
    return QDir(configDir).filePath(QStringLiteral("ed3k"));
}

std::optional<Seed> loadOrCreateKey(const QString& dir, QString* error)
{
    return loadOrCreateSecret(dir, QStringLiteral("node.key"), {}, error);
}

std::optional<Seed> loadOrCreateSecret(const QString& dir, const QString& name,
                                       const std::function<bool(const Seed&)>& accept, QString* error)
{
    if (!QDir().mkpath(dir)) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(dir);
        return std::nullopt;
    }
    const QString path = QDir(dir).filePath(name);
    Seed seed{};
    if (QFile::exists(path)) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("read %1: %2").arg(path, f.errorString());
            return std::nullopt;
        }
        QByteArray raw = f.read(qint64(seed.size()) + 1);
        const bool sized = raw.size() == qsizetype(seed.size());
        if (sized)
            std::copy(raw.cbegin(), raw.cend(), reinterpret_cast<char*>(seed.data()));
        wipe(raw);
        if (!sized) {
            // never replaced: the node ID of the user is in it
            if (error)
                *error = QStringLiteral("%1 is not 32 bytes; not replaced").arg(path);
            return std::nullopt;
        }
        return seed;
    }
    // a few tries: a credit key must be below the order of the curve (all but ~2^-128)
    for (int tries = 0;; ++tries) {
        if (RAND_bytes(seed.data(), int(seed.size())) != 1) {
            wipe(seed);
            if (error)
                *error = QStringLiteral("no random bytes for the key");
            return std::nullopt;
        }
        if (!accept || accept(seed))
            break;
        if (tries >= 8) {
            wipe(seed);
            if (error)
                *error = QStringLiteral("no usable key from the random bytes");
            return std::nullopt;
        }
    }
    if (!createKeyFile(path, seed, error)) {
        wipe(seed);
        return std::nullopt;
    }
    return seed;
}

std::optional<Seed> loadSecret(const QString& dir, const QString& name)
{
    QFile f(QDir(dir).filePath(name));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    Seed seed{};
    QByteArray raw = f.read(qint64(seed.size()) + 1);
    const bool sized = raw.size() == qsizetype(seed.size());
    if (sized)
        std::copy(raw.cbegin(), raw.cend(), reinterpret_cast<char*>(seed.data()));
    wipe(raw);
    if (!sized)
        return std::nullopt;
    return seed;
}

bool replaceSecret(const QString& dir, const QString& name, const Seed& secret, QString* error)
{
    if (!QDir().mkpath(dir)) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(dir);
        return false;
    }
    // QSaveFile: a temporary file made owner-only before the first byte, then renamed
    // ToDo: owner-only DACL on Windows (as createKeyFile)
    const QString path = QDir(dir).filePath(name);
    if (!writeBlob(path, std::as_bytes(std::span{secret}))) {
        if (error)
            *error = QStringLiteral("write %1 failed").arg(path);
        return false;
    }
    return true;
}

QByteArray readBlob(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

bool writeBlob(const QString& path, std::span<const std::byte> data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.setPermissions(kOwnerOnly);
    if (f.write(reinterpret_cast<const char*>(data.data()), qint64(data.size())) != qint64(data.size())) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

void wipe(std::span<std::uint8_t> buf)
{
    if (!buf.empty())
        OPENSSL_cleanse(buf.data(), buf.size());
}

void wipe(QByteArray& buf)
{
    if (!buf.isEmpty())
        OPENSSL_cleanse(buf.data(), std::size_t(buf.size()));
    buf.clear();
}

} // namespace eMule::ed3k
