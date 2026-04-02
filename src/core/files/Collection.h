#pragma once

/// @file Collection.h
/// @brief eMule collection (.emulecollection) file container.
///
/// Port of MFC CCollection (srchybrid/Collection.h/.cpp).
/// Supports binary format (v1/v2 with tags) and text format (ed2k links).
/// Supports read-only + basic write with RSA signature verification.

#include "utils/Types.h"

#include <QByteArray>
#include <QString>

#include <map>
#include <memory>

struct evp_pkey_st;  // forward-declare OpenSSL EVP_PKEY

namespace eMule {

class AbstractFile;
class CollectionFile;
class FileDataIO;

inline constexpr uint32 kCollectionFileVersion1 = 0x01;
inline constexpr uint32 kCollectionFileVersion2 = 0x02;  // large files

class Collection {
public:
    Collection();
    ~Collection();

    Collection(const Collection&) = delete;
    Collection& operator=(const Collection&) = delete;

    /// Load a collection from file (binary or text format).
    /// @param filePath  Full path to the .emulecollection file.
    /// @param fileName  Display name (used as fallback collection name for text format).
    /// @return true on success.
    bool initFromFile(const QString& filePath, const QString& fileName);

    /// Write collection to file (binary or text depending on m_textFormat).
    /// @param filePath  Destination path.
    /// @param signKey   Optional RSA key for signing (binary format only).
    /// @return true on success.
    bool writeToFile(const QString& filePath, evp_pkey_st* signKey = nullptr);

    /// Deep-copy another collection's contents into this one.
    void copyFrom(const Collection& other);

    /// Add a file to the collection.
    /// @param file       Source file to add.
    /// @param clone      If true, create a deep copy (CollectionFile from AbstractFile).
    /// @return Pointer to the added CollectionFile, or nullptr on failure.
    CollectionFile* addFile(const AbstractFile* file, bool clone = true);

    /// Remove a file from the collection by hash.
    void removeFile(const uint8* hash);

    /// @return The number of files in the collection.
    [[nodiscard]] int fileCount() const { return static_cast<int>(m_files.size()); }

    /// Iterate all files.
    [[nodiscard]] const std::map<QByteArray, std::unique_ptr<CollectionFile>>& files() const { return m_files; }

    /// @return true if file has .emulecollection extension.
    static bool hasCollectionExtension(const QString& fileName);

    /// @return MD5 hash of author key as uppercase hex string.
    [[nodiscard]] QString authorKeyHashString() const;

    /// @return Author public key as hex string (for Kad keyword publishing).
    [[nodiscard]] QString authorKeyString() const;

    QString m_name;
    QString m_authorName;
    bool    m_textFormat = false;
    QByteArray m_authorKey;   ///< DER-encoded RSA public key

private:
    bool writeBinary(const QString& filePath, evp_pkey_st* signKey);
    bool writeText(const QString& filePath);
    bool verifySignature(const QByteArray& data, qint64 signedLen);

    std::map<QByteArray, std::unique_ptr<CollectionFile>> m_files;
};

} // namespace eMule
