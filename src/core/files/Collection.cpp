#include "pch.h"
/// @file Collection.cpp
/// @brief eMule collection file container — port of MFC CCollection.

#include "files/Collection.h"
#include "files/CollectionFile.h"
#include "protocol/Tag.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <openssl/evp.h>
#include <openssl/x509.h>

namespace eMule {

static constexpr QLatin1StringView kCollectionExt(".emulecollection");

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

Collection::Collection()
{
    m_name = QStringLiteral("New Collection");
}

Collection::~Collection() = default;

// ---------------------------------------------------------------------------
// hasCollectionExtension
// ---------------------------------------------------------------------------

bool Collection::hasCollectionExtension(const QString& fileName)
{
    return fileName.endsWith(kCollectionExt, Qt::CaseInsensitive);
}

// ---------------------------------------------------------------------------
// authorKeyHashString / authorKeyString
// ---------------------------------------------------------------------------

QString Collection::authorKeyHashString() const
{
    if (m_authorKey.isEmpty())
        return {};
    return QString::fromLatin1(
        QCryptographicHash::hash(m_authorKey, QCryptographicHash::Md5).toHex()).toUpper();
}

QString Collection::authorKeyString() const
{
    if (m_authorKey.isEmpty())
        return {};
    return QString::fromLatin1(m_authorKey.toHex());
}

// ---------------------------------------------------------------------------
// copyFrom
// ---------------------------------------------------------------------------

void Collection::copyFrom(const Collection& other)
{
    m_name = other.m_name;
    m_authorName = other.m_authorName;
    m_authorKey = other.m_authorKey;
    m_textFormat = other.m_textFormat;

    m_files.clear();
    for (const auto& [key, cf] : other.m_files) {
        auto copy = std::make_unique<CollectionFile>(cf.get());
        m_files.emplace(key, std::move(copy));
    }
}

// ---------------------------------------------------------------------------
// initFromFile
// ---------------------------------------------------------------------------

bool Collection::initFromFile(const QString& filePath, const QString& fileName)
{
    m_files.clear();

    // Try binary format first
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    bool binaryLoaded = false;

    {
        QByteArray data = file.readAll();
        file.close();

        if (data.size() >= 8) {
            SafeMemFile mem(reinterpret_cast<const uint8*>(data.constData()),
                              static_cast<uint32>(data.size()));

            const uint32 version = mem.readUInt32();
            if (version == kCollectionFileVersion1 || version == kCollectionFileVersion2) {
                // Read header tags
                const uint32 headerTagCount = mem.readUInt32();
                for (uint32 i = 0; i < headerTagCount; ++i) {
                    Tag tag(mem, true);
                    switch (tag.nameId()) {
                    case FT_FILENAME:
                        if (tag.isStr())
                            m_name = tag.strValue();
                        break;
                    case FT_COLLECTIONAUTHOR:
                        if (tag.isStr())
                            m_authorName = tag.strValue();
                        break;
                    case FT_COLLECTIONAUTHORKEY:
                        if (tag.isBlob())
                            m_authorKey = tag.blobValue();
                        break;
                    default:
                        break;
                    }
                }

                // Read file entries
                const uint32 fileCount = mem.readUInt32();
                for (uint32 i = 0; i < fileCount; ++i) {
                    auto cf = std::make_unique<CollectionFile>(mem);
                    if (cf->fileSize() > 0 && !cf->fileName().isEmpty()) {
                        QByteArray key(reinterpret_cast<const char*>(cf->fileHash()), 16);
                        m_files.emplace(key, std::move(cf));
                    }
                }

                // Verify RSA signature if author key is present
                if (!m_authorKey.isEmpty()) {
                    const qint64 signedLen = mem.position();
                    if (!verifySignature(data, signedLen)) {
                        logWarning(QStringLiteral("Collection \"%1\": RSA signature verification failed — "
                                   "clearing author info").arg(m_name));
                        m_authorKey.clear();
                        m_authorName.clear();
                    }
                }

                binaryLoaded = true;
            }
        }
    }

    if (!binaryLoaded) {
        // Try text format (ed2k links, one per line)
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;

        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;

            auto cf = std::make_unique<CollectionFile>();
            if (cf->initFromLink(line)) {
                QByteArray key(reinterpret_cast<const char*>(cf->fileHash()), 16);
                m_files.emplace(key, std::move(cf));
            }
        }
        file.close();

        // Use filename (without extension) as collection name
        QString displayName = fileName;
        if (hasCollectionExtension(displayName))
            displayName.chop(kCollectionExt.size());
        m_name = displayName;
        m_textFormat = true;
    }

    return !m_files.empty();
}

// ---------------------------------------------------------------------------
// writeToFile
// ---------------------------------------------------------------------------

bool Collection::writeToFile(const QString& filePath, EVP_PKEY* signKey)
{
    if (m_textFormat)
        return writeText(filePath);
    return writeBinary(filePath, signKey);
}

// ---------------------------------------------------------------------------
// writeBinary
// ---------------------------------------------------------------------------

bool Collection::writeBinary(const QString& filePath, EVP_PKEY* signKey)
{
    // Determine version: v2 if any file exceeds 4 GB
    bool needsLargeFile = false;
    for (const auto& [key, cf] : m_files) {
        if (static_cast<uint64>(cf->fileSize()) > UINT32_MAX) {
            needsLargeFile = true;
            break;
        }
    }

    SafeMemFile mem;
    mem.writeUInt32(needsLargeFile ? kCollectionFileVersion2 : kCollectionFileVersion1);

    // Header tags: name always; author + key if signing
    const bool hasSigning = !m_authorKey.isEmpty();
    mem.writeUInt32(hasSigning ? 3u : 1u);

    Tag(FT_FILENAME, m_name).writeNewEd2kTag(mem, UTF8Mode::Raw);

    if (hasSigning) {
        Tag(FT_COLLECTIONAUTHOR, m_authorName).writeNewEd2kTag(mem, UTF8Mode::Raw);
        Tag(FT_COLLECTIONAUTHORKEY, m_authorKey).writeNewEd2kTag(mem);
    }

    // File entries
    mem.writeUInt32(static_cast<uint32>(m_files.size()));
    for (const auto& [key, cf] : m_files)
        cf->writeCollectionInfo(mem);

    // RSA signature
    if (signKey) {
        const QByteArray& payload = mem.buffer();
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx)
            return false;

        bool ok = false;
        do {
            if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, signKey) <= 0)
                break;
            if (EVP_DigestSignUpdate(ctx, payload.constData(),
                                     static_cast<size_t>(payload.size())) <= 0)
                break;
            size_t sigLen = 0;
            if (EVP_DigestSignFinal(ctx, nullptr, &sigLen) <= 0)
                break;
            QByteArray signature(static_cast<qsizetype>(sigLen), '\0');
            if (EVP_DigestSignFinal(ctx, reinterpret_cast<unsigned char*>(signature.data()),
                                    &sigLen) <= 0)
                break;
            signature.resize(static_cast<qsizetype>(sigLen));
            mem.write(signature.constData(), signature.size());
            ok = true;
        } while (false);

        EVP_MD_CTX_free(ctx);
        if (!ok)
            return false;
    }

    // Write to disk
    QFile outFile(filePath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    const QByteArray& buf = mem.buffer();
    return outFile.write(buf) == buf.size();
}

// ---------------------------------------------------------------------------
// writeText
// ---------------------------------------------------------------------------

bool Collection::writeText(const QString& filePath)
{
    QFile outFile(filePath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;

    QTextStream out(&outFile);
    for (const auto& [key, cf] : m_files) {
        out << QStringLiteral("ed2k://|file|%1|%2|%3|/")
                   .arg(cf->fileName())
                   .arg(static_cast<uint64>(cf->fileSize()))
                   .arg(md4str(cf->fileHash()))
            << u'\n';
    }
    return true;
}

// ---------------------------------------------------------------------------
// verifySignature
// ---------------------------------------------------------------------------

bool Collection::verifySignature(const QByteArray& data, qint64 signedLen)
{
    if (m_authorKey.isEmpty() || signedLen <= 0 || signedLen >= data.size())
        return false;

    const auto* message = reinterpret_cast<const unsigned char*>(data.constData());
    const auto msgLen = static_cast<size_t>(signedLen);
    const auto* sig = reinterpret_cast<const unsigned char*>(data.constData() + signedLen);
    const auto sigLen = static_cast<size_t>(data.size() - signedLen);

    // Load public key from DER
    const auto* keyData = reinterpret_cast<const unsigned char*>(m_authorKey.constData());
    EVP_PKEY* pubKey = d2i_PUBKEY(nullptr, &keyData, m_authorKey.size());
    if (!pubKey)
        return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    bool ok = false;
    if (ctx) {
        if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pubKey) > 0
            && EVP_DigestVerifyUpdate(ctx, message, msgLen) > 0
            && EVP_DigestVerifyFinal(ctx, sig, sigLen) == 1)
        {
            ok = true;
        }
        EVP_MD_CTX_free(ctx);
    }
    EVP_PKEY_free(pubKey);
    return ok;
}

// ---------------------------------------------------------------------------
// addFile
// ---------------------------------------------------------------------------

CollectionFile* Collection::addFile(const AbstractFile* file, bool clone)
{
    if (!file)
        return nullptr;

    QByteArray key(reinterpret_cast<const char*>(file->fileHash()), 16);
    if (m_files.contains(key))
        return m_files[key].get();

    if (clone) {
        auto cf = std::make_unique<CollectionFile>(file);
        auto* ptr = cf.get();
        m_files.emplace(key, std::move(cf));
        return ptr;
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// removeFile
// ---------------------------------------------------------------------------

void Collection::removeFile(const uint8* hash)
{
    if (!hash)
        return;
    QByteArray key(reinterpret_cast<const char*>(hash), 16);
    m_files.erase(key);
}

} // namespace eMule
