#pragma once

/// @file TestHelpers.h
/// @brief Shared utilities for all eMule Qt unit tests.
///
/// Provides convenience macros, temporary directory management,
/// and mock data factories used across test suites.

#include <QTest>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QTemporaryDir>
#include <QString>
#include <QByteArray>

#include <memory>
#include <random>

#ifdef Q_OS_UNIX
#include <arpa/inet.h>
#endif
#ifdef Q_OS_WIN
#include <winsock2.h>
#endif

#define EMULE_STRINGIFY_(x) #x // stringifies already expanded result
#define EMULE_STRINGIFY(x) EMULE_STRINGIFY_(x) // force preprocessor to expand x

namespace eMule::testing {

/// RAII temporary directory that self-cleans on destruction.
/// Use this instead of raw QTemporaryDir for deterministic cleanup.
class TempDir {
public:
    TempDir()
    {
        QVERIFY2(m_dir.isValid(), "Failed to create temporary directory");
    }

    [[nodiscard]] QString path() const { return m_dir.path(); }

    [[nodiscard]] QString filePath(const QString& name) const
    {
        return m_dir.filePath(name);
    }

private:
    QTemporaryDir m_dir;
};

/// Return the path to the test data directory (set by CMake).
inline QString testDataDir()
{
    return QStringLiteral(EMULE_STRINGIFY(EMULE_TEST_DATA_DIR));
}

/// Return the path to the project-level data/ directory (set by CMake).
inline QString projectDataDir()
{
    return QStringLiteral(EMULE_STRINGIFY(EMULE_PROJECT_DATA_DIR));
}

/// Generate a QByteArray filled with random bytes.
inline QByteArray randomBytes(int size)
{
    QByteArray data(size, Qt::Uninitialized);
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);
    for (int i = 0; i < size; ++i) {
        data[i] = static_cast<char>(dist(rng));
    }
    return data;
}

/// Generate a 16-byte MD4-sized hash filled with a pattern.
inline QByteArray fakeHash16(std::uint8_t pattern = 0xAB)
{
    return QByteArray(16, static_cast<char>(pattern));
}

/// Parse a simple KEY=VALUE .env file into a QMap.
inline QMap<QString, QString> loadEnvFile(const QString& path)
{
    QMap<QString, QString> env;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return env;

    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        const auto eq = line.indexOf(u'=');
        if (eq < 1)
            continue;
        env.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    return env;
}

// ---------------------------------------------------------------------------
// Archive fixtures
// ---------------------------------------------------------------------------
//
// Built in-process rather than checked in as binaries, so the bytes under test are
// visible in the test source and no fixture files can drift out of sync.

/// CRC-32 (the ZIP/gzip polynomial).
inline uint32_t archiveCrc32(const QByteArray& data)
{
    static uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j)
                c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0u);
            table[i] = c;
        }
        initialized = true;
    }
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < data.size(); ++i)
        crc = table[(crc ^ static_cast<uint8_t>(data[i])) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/// One member of a generated ZIP.
struct ZipMember {
    QByteArray name;
    QByteArray content;
};

/// Minimal multi-member ZIP generator (store method, no compression).
inline QByteArray buildMinimalZip(const QList<ZipMember>& members)
{
    QByteArray zip;
    struct Placed { uint32_t offset, crc, size; };
    QList<Placed> placed;

    auto put = [&zip](const auto& value) {
        zip.append(reinterpret_cast<const char*>(&value), sizeof(value));
    };

    for (const ZipMember& m : members) {
        Placed p{static_cast<uint32_t>(zip.size()), archiveCrc32(m.content),
                 static_cast<uint32_t>(m.content.size())};
        placed.append(p);

        put(uint32_t{0x04034b50});                        // local header signature
        put(uint16_t{20});                                // version needed
        put(uint16_t{0});                                 // flags
        put(uint16_t{0});                                 // method: store
        put(uint16_t{0});                                 // mod time
        put(uint16_t{0});                                 // mod date
        put(p.crc);
        put(p.size);                                      // compressed size
        put(p.size);                                      // uncompressed size
        put(static_cast<uint16_t>(m.name.size()));
        put(uint16_t{0});                                 // extra length
        zip.append(m.name);
        zip.append(m.content);
    }

    const auto cdStart = static_cast<uint32_t>(zip.size());
    for (int i = 0; i < members.size(); ++i) {
        const ZipMember& m = members[i];
        const Placed& p = placed[i];

        put(uint32_t{0x02014b50});                        // central directory signature
        put(uint16_t{20});                                // version made by
        put(uint16_t{20});                                // version needed
        put(uint16_t{0});                                 // flags
        put(uint16_t{0});                                 // method
        put(uint16_t{0});                                 // mod time
        put(uint16_t{0});                                 // mod date
        put(p.crc);
        put(p.size);
        put(p.size);
        put(static_cast<uint16_t>(m.name.size()));
        put(uint16_t{0});                                 // extra length
        put(uint16_t{0});                                 // comment length
        put(uint16_t{0});                                 // disk start
        put(uint16_t{0});                                 // internal attrs
        put(uint32_t{0});                                 // external attrs
        put(p.offset);
        zip.append(m.name);
    }
    const auto cdSize = static_cast<uint32_t>(zip.size()) - cdStart;

    put(uint32_t{0x06054b50});                            // end of central directory
    put(uint16_t{0});                                     // disk number
    put(uint16_t{0});                                     // disk with CD
    put(static_cast<uint16_t>(members.size()));
    put(static_cast<uint16_t>(members.size()));
    put(cdSize);
    put(cdStart);
    put(uint16_t{0});                                     // comment length

    return zip;
}

/// Single-member convenience overload.
inline QByteArray buildMinimalZip(const QByteArray& filename, const QByteArray& content)
{
    return buildMinimalZip(QList<ZipMember>{{filename, content}});
}

/// Load the project-root .env file and set each key as a process env var
/// (only if not already set, so explicit env vars still win).
inline void loadProjectEnv()
{
    const auto env = loadEnvFile(QStringLiteral(EMULE_STRINGIFY(EMULE_PROJECT_DATA_DIR) "/../.env"));
    for (auto it = env.cbegin(); it != env.cend(); ++it) {
        const QByteArray key = it.key().toUtf8();
        if (qEnvironmentVariableIsEmpty(key.constData()))
            qputenv(key.constData(), it.value().toUtf8());
    }
}

} // namespace eMule::testing