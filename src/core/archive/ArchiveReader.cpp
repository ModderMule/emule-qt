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
    QString passphrase;
    QString formatName;
    QStringList rejected;
    bool encrypted = false;
    bool opened = false;
};

namespace {

/// Names Windows treats as devices rather than files, whatever the extension.
/// Extracting to one of these does not create a file; it writes to the device.
constexpr const char* kReservedNames[] = {
    "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
};

[[nodiscard]] bool isReservedName(const QString& component)
{
    const QString stem = component.section(u'.', 0, 0).toUpper();
    for (const char* reserved : kReservedNames) {
        if (stem == QLatin1String(reserved))
            return true;
    }
    return false;
}

} // namespace

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

    // Before the open, or the format reader never sees it. Applies to ZIP and
    // 7z; for RAR libarchive can only flag the entries, never decrypt them.
    if (!m_impl->passphrase.isEmpty())
        archive_read_add_passphrase(ar, m_impl->passphrase.toUtf8().constData());

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
        if (archive_entry_is_encrypted(entry))
            m_impl->encrypted = true;
        m_impl->entries.push_back(std::move(e));
        archive_read_data_skip(ar);
    }

    if (const char* fmt = archive_format_name(ar))
        m_impl->formatName = QString::fromUtf8(fmt);

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
    m_impl->formatName.clear();
    m_impl->rejected.clear();
    m_impl->encrypted = false;
    m_impl->opened = false;
    // passphrase deliberately survives: it is set before open(), not by it.
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

// ---------------------------------------------------------------------------
// setPassphrase / accessors
// ---------------------------------------------------------------------------

void ArchiveReader::setPassphrase(const QString& passphrase)
{
    m_impl->passphrase = passphrase;
}

bool ArchiveReader::hasEncryptedEntries() const
{
    return m_impl->encrypted;
}

QString ArchiveReader::formatName() const
{
    return m_impl->formatName;
}

QStringList ArchiveReader::rejectedEntries() const
{
    return m_impl->rejected;
}

// ---------------------------------------------------------------------------
// safeEntryPath — the only sanctioned way to turn a member name into a path
// ---------------------------------------------------------------------------

QString ArchiveReader::safeEntryPath(const QString& destDir, const QString& entryName)
{
    if (entryName.isEmpty())
        return {};

    // Zip stores '/', RAR stores '\\'. Normalise before anything else, or a
    // backslash-separated '..\\..' walks straight past the checks below.
    QString name = entryName;
    name.replace(u'\\', u'/');

    // Absolute, UNC, or drive-qualified: no rewrite makes these safe.
    if (name.startsWith(u'/'))
        return {};
    if (name.size() >= 2 && name.at(1) == u':')
        return {};

    QStringList safeParts;
    for (const QString& rawPart : name.split(u'/', Qt::SkipEmptyParts)) {
        if (rawPart == QLatin1String("."))
            continue;
        if (rawPart == QLatin1String(".."))
            return {};                       // the traversal itself

        // Windows silently strips trailing dots and spaces, so "foo. " and "foo"
        // are the same file there — which is another way to land somewhere the
        // caller did not intend.
        QString part = rawPart;
        while (!part.isEmpty() && (part.endsWith(u'.') || part.endsWith(u' ')))
            part.chop(1);
        if (part.isEmpty())
            continue;

        // Reserved device names are renamed, not refused: a release containing
        // "aux.mkv" is odd but legitimate, and dropping the file would be a
        // worse answer than extracting it under a slightly different name.
        if (isReservedName(part)) {
            // Insert after the stem and keep the rest verbatim. section(u'.', 1)
            // would drop the separator and turn "aux.mkv" into "aux_mkv".
            const qsizetype stemLength = part.section(u'.', 0, 0).size();
            part.insert(stemLength, u'_');
        }

        safeParts.append(part);
    }

    if (safeParts.isEmpty())
        return {};

    return QDir(destDir).filePath(safeParts.join(u'/'));
}

// ---------------------------------------------------------------------------
// extractAll — one pass over the archive
//
// The per-entry path reopens and re-scans from the start for every member, which
// on a solid or multi-volume archive means re-reading every volume once per
// file. A release is dozens of files inside a set of volumes, so that is the
// difference between seconds and minutes.
// ---------------------------------------------------------------------------

bool ArchiveReader::extractAll(const QString& destDir)
{
    if (!m_impl->opened)
        return false;

    m_impl->rejected.clear();
    QDir().mkpath(destDir);

    auto* ar = archive_read_new();
    if (!ar)
        return false;

    archive_read_support_format_all(ar);
    archive_read_support_filter_all(ar);
    if (!m_impl->passphrase.isEmpty())
        archive_read_add_passphrase(ar, m_impl->passphrase.toUtf8().constData());

    const QByteArray pathUtf8 = m_impl->filePath.toUtf8();
    if (archive_read_open_filename(ar, pathUtf8.constData(), 10240) != ARCHIVE_OK) {
        logWarning(QStringLiteral("ArchiveReader: cannot reopen '%1' for extraction: %2")
                       .arg(m_impl->filePath, QString::fromUtf8(archive_error_string(ar))));
        archive_read_free(ar);
        return false;
    }

    bool allOk = true;
    struct archive_entry* entry = nullptr;

    while (archive_read_next_header(ar, &entry) == ARCHIVE_OK) {
        const char* pathname = archive_entry_pathname_utf8(entry);
        if (!pathname)
            pathname = archive_entry_pathname(entry);
        const QString rawName = QString::fromUtf8(pathname ? pathname : "");

        const QString destPath = safeEntryPath(destDir, rawName);
        if (destPath.isEmpty()) {
            // Not a failure of the archive — a refusal by us. Record it and keep
            // going: one hostile member should not cost the other forty.
            logWarning(QStringLiteral("ArchiveReader: refusing unsafe member '%1' in '%2'")
                           .arg(rawName, m_impl->filePath));
            m_impl->rejected.append(rawName);
            archive_read_data_skip(ar);
            continue;
        }

        if (archive_entry_filetype(entry) == AE_IFDIR) {
            QDir().mkpath(destPath);
            continue;
        }

        QDir().mkpath(QFileInfo(destPath).absolutePath());

        QFile outFile(destPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            logWarning(QStringLiteral("ArchiveReader: cannot write '%1'").arg(destPath));
            allOk = false;
            archive_read_data_skip(ar);
            continue;
        }

        static constexpr int kBufSize = 65536;
        char buf[kBufSize];
        for (;;) {
            const auto readSize = archive_read_data(ar, buf, kBufSize);
            if (readSize < 0) {
                logWarning(QStringLiteral("ArchiveReader: read error in '%1': %2")
                               .arg(rawName, QString::fromUtf8(archive_error_string(ar))));
                allOk = false;
                break;
            }
            if (readSize == 0)
                break;
            if (outFile.write(buf, readSize) != readSize) {
                allOk = false;
                break;
            }
        }
        outFile.close();
    }

    archive_read_free(ar);
    return allOk;
}

} // namespace eMule
