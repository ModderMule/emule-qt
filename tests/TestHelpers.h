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