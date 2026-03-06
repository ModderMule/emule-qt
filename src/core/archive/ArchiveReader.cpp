#include "pch.h"
/// @file ArchiveReader.cpp
/// @brief Unified archive reader using libarchive.

#include "archive/ArchiveReader.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <archive.h>
#include <archive_entry.h>

namespace eMule {

// ---------------------------------------------------------------------------
// Impl — pimpl for libarchive state
// ---------------------------------------------------------------------------

struct ArchiveReader::Impl {
    struct Entry {
        QString name;
        uint64 size = 0;
        qint64 mtimeSecs = 0;
        uint16 mode = 0;
        bool isDir = false;
    };

    std::vector<Entry> entries;
    QString filePath;
    bool opened = false;
};

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

ArchiveReader::ArchiveReader()
    : m_impl(std::make_unique<Impl>())
{
}

ArchiveReader::~ArchiveReader()
{
    close();
}

// ---------------------------------------------------------------------------
// open — scan archive entries
// ---------------------------------------------------------------------------

bool ArchiveReader::open(const QString& filePath)
{
    close();

    auto* ar = archive_read_new();
    if (!ar)
        return false;

    archive_read_support_format_all(ar);
    archive_read_support_filter_all(ar);

    const QByteArray pathUtf8 = filePath.toUtf8();
    int result = archive_read_open_filename(ar, pathUtf8.constData(), 10240);
    if (result != ARCHIVE_OK) {
        logWarning(QStringLiteral("ArchiveReader: cannot open '%1': %2")
                       .arg(filePath, QString::fromUtf8(archive_error_string(ar))));
        archive_read_free(ar);
        return false;
    }

    // Iterate all entries to build index
    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(ar, &entry) == ARCHIVE_OK) {
        Impl::Entry e;
        const char* pathname = archive_entry_pathname_utf8(entry);
        if (!pathname)
            pathname = archive_entry_pathname(entry);
        e.name = QString::fromUtf8(pathname);
        e.size = static_cast<uint64>(archive_entry_size(entry));
        e.mtimeSecs = static_cast<qint64>(archive_entry_mtime(entry));
        e.mode = static_cast<uint16>(archive_entry_perm(entry));
        e.isDir = (archive_entry_filetype(entry) == AE_IFDIR);
        m_impl->entries.push_back(std::move(e));
        archive_read_data_skip(ar);
    }

    archive_read_free(ar);
    m_impl->filePath = filePath;
    m_impl->opened = true;
    return true;
}

// ---------------------------------------------------------------------------
// close
// ---------------------------------------------------------------------------

void ArchiveReader::close()
{
    m_impl->entries.clear();
    m_impl->filePath.clear();
    m_impl->opened = false;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

bool ArchiveReader::isOpen() const
{
    return m_impl->opened;
}

int ArchiveReader::entryCount() const
{
    return static_cast<int>(m_impl->entries.size());
}

QString ArchiveReader::entryName(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_impl->entries.size()))
        return {};
    return m_impl->entries[static_cast<size_t>(index)].name;
}

uint64 ArchiveReader::entrySize(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_impl->entries.size()))
        return 0;
    return m_impl->entries[static_cast<size_t>(index)].size;
}

QDateTime ArchiveReader::entryMtime(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_impl->entries.size()))
        return {};
    auto secs = m_impl->entries[static_cast<size_t>(index)].mtimeSecs;
    return (secs > 0) ? QDateTime::fromSecsSinceEpoch(secs) : QDateTime{};
}

bool ArchiveReader::entryIsDir(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_impl->entries.size()))
        return false;
    return m_impl->entries[static_cast<size_t>(index)].isDir;
}

uint16 ArchiveReader::entryMode(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_impl->entries.size()))
        return 0;
    return m_impl->entries[static_cast<size_t>(index)].mode;
}

QStringList ArchiveReader::entryNames() const
{
    QStringList result;
    result.reserve(static_cast<int>(m_impl->entries.size()));
    for (const auto& e : m_impl->entries)
        result.append(e.name);
    return result;
}

// ---------------------------------------------------------------------------
// extractEntry — extract a single entry by index
// ---------------------------------------------------------------------------

bool ArchiveReader::extractEntry(int index, const QString& destPath)
{
    if (!m_impl->opened || index < 0 || index >= static_cast<int>(m_impl->entries.size()))
        return false;

    auto* ar = archive_read_new();
    if (!ar)
        return false;

    archive_read_support_format_all(ar);
    archive_read_support_filter_all(ar);

    const QByteArray pathUtf8 = m_impl->filePath.toUtf8();
    if (archive_read_open_filename(ar, pathUtf8.constData(), 10240) != ARCHIVE_OK) {
        archive_read_free(ar);
        return false;
    }

    struct archive_entry* entry = nullptr;
    int currentIndex = 0;
    bool success = false;

    while (archive_read_next_header(ar, &entry) == ARCHIVE_OK) {
        if (currentIndex == index) {
            // Ensure parent directory exists
            QFileInfo destInfo(destPath);
            QDir().mkpath(destInfo.absolutePath());

            QFile outFile(destPath);
            if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                break;
            }

            static constexpr int kBufSize = 65536;
            char buf[kBufSize];
            for (;;) {
                auto readSize = archive_read_data(ar, buf, kBufSize);
                if (readSize < 0) {
                    break;
                }
                if (readSize == 0) {
                    success = true;
                    break;
                }
                if (outFile.write(buf, readSize) != readSize) {
                    break;
                }
            }
            break;
        }
        archive_read_data_skip(ar);
        ++currentIndex;
    }

    archive_read_free(ar);
    return success;
}

// ---------------------------------------------------------------------------
// extractAll — extract all entries to a directory
// ---------------------------------------------------------------------------

bool ArchiveReader::extractAll(const QString& destDir)
{
    if (!m_impl->opened)
        return false;

    QDir().mkpath(destDir);

    bool allOk = true;
    for (int i = 0; i < static_cast<int>(m_impl->entries.size()); ++i) {
        const QString entryPath = destDir + u'/' + m_impl->entries[static_cast<size_t>(i)].name;
        if (!extractEntry(i, entryPath))
            allOk = false;
    }
    return allOk;
}

} // namespace eMule
