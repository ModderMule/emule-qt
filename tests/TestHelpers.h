#pragma once

/// @file TestHelpers.h
/// @brief Shared utilities for all eMule Qt unit tests.
///
/// Provides convenience macros, temporary directory management,
/// and mock data factories used across test suites.

#include <QTest>
#include <QDir>
#include <QTemporaryDir>
#include <QString>
#include <QByteArray>

#include <memory>
#include <random>

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
    return QStringLiteral(EMULE_TEST_DATA_DIR);
}

/// Return the path to the project-level data/ directory (set by CMake).
inline QString projectDataDir()
{
    return QStringLiteral(EMULE_PROJECT_DATA_DIR);
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

} // namespace eMule::testing