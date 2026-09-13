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

namespace {

constexpr size_t kBlockSize = 10240;

// ---------------------------------------------------------------------------
// Live volume set — one continuous stream over volumes that arrive over time
//
// libarchive's own multi-file support (archive_read_append_callback_data) needs
// the volume count up front, which a download does not have. It does not need
// to: a volume set *is* the concatenation of its volumes, so a single client
// that rolls from one volume to the next at EOF presents exactly the same bytes
// and libarchive never sees a boundary.
//
// No seek callback is offered, which keeps the readers in streaming mode. RAR4,
// RAR5 and zip all read that way; 7z needs its footer and cannot be extracted
// before it exists anyway.
// ---------------------------------------------------------------------------

struct LiveClient {
    ArchiveVolumeSource* source = nullptr;
    QFile file;
    int index = -1;
    QByteArray buffer;
};

/// How much a member may grow between progress reports.
///
/// Each report costs a flush, so tying it to the 64 KiB read block would mean
/// 600k flushes on a 40 GB release for a number nobody reads that often. One
/// MiB is finer than any player's read-ahead and cheap at any release size.
constexpr qint64 kReportBytes = 1024 * 1024;

[[nodiscard]] bool liveOpenNextVolume(LiveClient* c)
{
    QString path;
    if (!c->source->volumePath(c->index + 1, path))
        return false;   // end of set, or cancelled

    c->file.close();
    c->file.setFileName(path);
    if (!c->file.open(QIODevice::ReadOnly)) {
        logWarning(QStringLiteral("ArchiveReader: cannot read volume '%1'").arg(path));
        return false;
    }
    ++c->index;
    return true;
}

la_ssize_t liveRead(struct archive* a, void* clientData, const void** buffer)
{
    Q_UNUSED(a);
    auto* c = static_cast<LiveClient*>(clientData);
    *buffer = c->buffer.constData();

    for (;;) {
        if (c->file.isOpen()) {
            const qint64 got = c->file.read(c->buffer.data(), c->buffer.size());
            if (got < 0)
                return -1;
            if (got > 0)
                return la_ssize_t(got);
            c->file.close();   // this volume is spent; roll to the next
        }
        if (!liveOpenNextVolume(c))
            return 0;          // real end of stream
    }
}

int liveClose(struct archive* a, void* clientData)
{
    Q_UNUSED(a);
    static_cast<LiveClient*>(clientData)->file.close();
    return ARCHIVE_OK;
}

} // namespace

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
    QStringList extracted;        ///< what the last extractAll*() wrote
    ProgressSink sink;            ///< optional; see setProgressSink()
    std::unique_ptr<LiveClient> live;
    QStringList volumes;          ///< every volume, in order; [0] is what messages name
    ArchiveVolumeSource* source = nullptr;   ///< set instead of `volumes` for a live set
    QString filePath;
    QString passphrase;
    QString formatName;
    QStringList rejected;
    QString lastError;
    bool encrypted = false;
    bool encryptionBlocked = false;
    bool wrongPassphrase = false;
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
    return open(QStringList{filePath});
}

bool ArchiveReader::open(const QStringList& volumes)
{
    if (volumes.isEmpty())
        return false;

    close();
    m_impl->volumes = volumes;
    m_impl->filePath = volumes.first();
    return scanEntries();
}

// ---------------------------------------------------------------------------
// close
// ---------------------------------------------------------------------------

void ArchiveReader::close()
{
    m_impl->entries.clear();
    m_impl->extracted.clear();
    m_impl->live.reset();
    m_impl->volumes.clear();
    m_impl->source = nullptr;
    m_impl->filePath.clear();
    m_impl->formatName.clear();
    m_impl->rejected.clear();
    m_impl->lastError.clear();
    m_impl->encrypted = false;
    m_impl->encryptionBlocked = false;
    m_impl->wrongPassphrase = false;
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

    auto* ar = openArchive("reopen for single-entry extraction");
    if (!ar)
        return false;

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

bool ArchiveReader::encryptionBlocked() const
{
    if (m_impl->encryptionBlocked)
        return true;

    // The header-encrypted case announces itself by failing, and is caught
    // above. The *data*-encrypted one does not: RAR4 with FHD_PASSWORD and 7z
    // with an AES codec both list their members perfectly happily and only fail
    // when something reads one — libarchive's `return ARCHIVE_FATAL` for it is
    // commented out on purpose, so that a caller can at least see the names.
    //
    // Waiting for that read would mean discovering it halfway through an
    // extraction, with half a release already written. ZIP is the one format
    // libarchive can actually decrypt, so for every other one an encrypted entry
    // is a refusal that has not happened yet.
    return m_impl->encrypted
           && !m_impl->formatName.contains(QLatin1String("ZIP"), Qt::CaseInsensitive);
}

bool ArchiveReader::wrongPassphrase() const
{
    return m_impl->wrongPassphrase;
}

QString ArchiveReader::lastError() const
{
    return m_impl->lastError;
}

QString ArchiveReader::formatName() const
{
    return m_impl->formatName;
}

QStringList ArchiveReader::extractedFiles() const
{
    return m_impl->extracted;
}

QStringList ArchiveReader::rejectedEntries() const
{
    return m_impl->rejected;
}

void ArchiveReader::setProgressSink(ProgressSink sink)
{
    m_impl->sink = std::move(sink);
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

    auto* ar = openArchive("reopen for extraction");
    if (!ar)
        return false;

    const bool ok = extractAllInto(ar, destDir);
    archive_read_free(ar);
    return ok;
}

bool ArchiveReader::extractAllFrom(ArchiveVolumeSource& source, const QString& destDir)
{
    close();
    m_impl->source = &source;

    auto* ar = openArchive("open live volume set");
    if (!ar) {
        m_impl->source = nullptr;
        m_impl->live.reset();
        return false;
    }

    // Bidding has read from volume one by now, so there is a real name to put
    // in any warning the extraction logs.
    m_impl->filePath = m_impl->live->file.fileName();

    // The format name is only known once a header has been read, and a live set
    // gets exactly one pass, so record it from the same handle we extract with.
    const bool ok = extractAllInto(ar, destDir);
    if (const char* fmt = archive_format_name(ar))
        m_impl->formatName = QString::fromUtf8(fmt);

    archive_read_free(ar);   // runs liveClose(), which closes the volume
    m_impl->source = nullptr;
    m_impl->live.reset();
    return ok;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

// The volume list is what makes a multi-volume set work at all. libarchive
// reads such a set as one byte stream over the client's files and never opens a
// sibling volume by name, so a single-file open stops at the first boundary.
::archive* ArchiveReader::openArchive(const char* what) const
{
    auto* ar = archive_read_new();
    if (!ar)
        return nullptr;

    archive_read_support_format_all(ar);
    archive_read_support_filter_all(ar);

    // Before the open, or the format reader never sees it. Applies to ZIP and
    // 7z; for RAR libarchive can only flag the entries, never decrypt them.
    if (!m_impl->passphrase.isEmpty())
        archive_read_add_passphrase(ar, m_impl->passphrase.toUtf8().constData());

    int result = ARCHIVE_FATAL;
    if (m_impl->source) {
        m_impl->live = std::make_unique<LiveClient>();
        m_impl->live->source = m_impl->source;
        m_impl->live->buffer.resize(qsizetype(kBlockSize));
        archive_read_set_read_callback(ar, liveRead);
        archive_read_set_close_callback(ar, liveClose);
        archive_read_set_callback_data(ar, m_impl->live.get());
        result = archive_read_open1(ar);
    } else {
        std::vector<QByteArray> utf8;
        std::vector<const char*> names;
        utf8.reserve(size_t(m_impl->volumes.size()));
        for (const QString& v : m_impl->volumes) {
            utf8.push_back(v.toUtf8());
            names.push_back(utf8.back().constData());
        }
        names.push_back(nullptr);
        result = archive_read_open_filenames(ar, names.data(), kBlockSize);
    }

    if (result != ARCHIVE_OK) {
        noteReadFailure(ar, result);
        logWarning(QStringLiteral("ArchiveReader: cannot %1 '%2': %3")
                       .arg(QString::fromLatin1(what), m_impl->filePath, m_impl->lastError));
        archive_read_free(ar);
        return nullptr;
    }
    return ar;
}

// A header loop that stops for any reason but ARCHIVE_EOF has failed, and until
// this existed nothing looked at *why*. That is how a header-encrypted RAR read
// as an empty-but-valid archive: libarchive returns ARCHIVE_FATAL on the very
// first header, the `== ARCHIVE_OK` loop exits without a single iteration, and
// an unpack of zero files then counted as a success — with the volumes deleted
// after it.
//
// archive_read_has_encrypted_entries() is the only signal available in that
// case, because no entry was ever handed out to carry the per-entry flag. It
// answers ARCHIVE_READ_FORMAT_ENCRYPTION_DONT_KNOW (-1) before the format has
// looked, so only a literal 1 means yes.
void ArchiveReader::noteReadFailure(::archive* ar, int status) const
{
    if (status == ARCHIVE_EOF)
        return;

    if (const char* err = archive_error_string(ar))
        m_impl->lastError = QString::fromUtf8(err);

    if (archive_read_has_encrypted_entries(ar) == 1)
        m_impl->encrypted = true;

    // The formats say so in words and in no other way — there is no error code
    // for "encrypted". libarchive's own strings, all four of them:
    //   rar.c    "RAR encryption support unavailable."
    //   rar5.c   "Encryption is not supported"
    //            "Reading encrypted data is not currently supported"
    //   7zip.c   "Crypto codec not supported yet (ID: 0x…)"
    //            "The %s is encrypted, but currently not supported"
    //   zip.c    "Incorrect passphrase" / "Too many incorrect passphrases"
    //            "Encrypted file is unsupported"
    static constexpr const char* kEncryptionWords[] = {
        "encryption", "encrypted", "Crypto codec", "passphrase",
    };
    for (const char* word : kEncryptionWords) {
        if (m_impl->lastError.contains(QLatin1String(word), Qt::CaseInsensitive)) {
            m_impl->encryptionBlocked = true;
            break;
        }
    }

    // ZIP is the only reader that can tell a wrong passphrase from a missing
    // one, because it is the only one that tries.
    if (m_impl->lastError.contains(QLatin1String("passphrase"), Qt::CaseInsensitive))
        m_impl->wrongPassphrase = true;
}

bool ArchiveReader::scanEntries()
{
    auto* ar = openArchive("open");
    if (!ar)
        return false;

    struct archive_entry* entry = nullptr;
    int status = ARCHIVE_OK;
    while ((status = archive_read_next_header(ar, &entry)) == ARCHIVE_OK) {
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

    // Before the free, which invalidates both the handle and its error string.
    noteReadFailure(ar, status);
    archive_read_free(ar);

    if (status != ARCHIVE_EOF) {
        // A header-encrypted set lands here having listed nothing. Reporting it
        // as an open failure is the whole point: the previous `return true`
        // handed the caller an archive that claimed to contain no files.
        logWarning(QStringLiteral("ArchiveReader: '%1' could not be listed: %2")
                       .arg(m_impl->filePath, m_impl->lastError));
        return false;
    }

    m_impl->opened = true;
    return true;
}

bool ArchiveReader::extractAllInto(::archive* ar, const QString& destDir)
{
    m_impl->rejected.clear();
    m_impl->extracted.clear();
    QDir().mkpath(destDir);

    bool allOk = true;
    struct archive_entry* entry = nullptr;

    // Members in archive order, the only ordering a live set has — and the
    // ordinal a preview of a still-extracting set is addressed by. Counted for
    // every header, including the ones refused below, so it stays stable
    // whatever the caller decides to skip.
    int entryIndex = -1;

    static constexpr int kBufSize = 65536;
    char buf[kBufSize];

    int status = ARCHIVE_OK;
    while ((status = archive_read_next_header(ar, &entry)) == ARCHIVE_OK) {
        ++entryIndex;
        const char* pathname = archive_entry_pathname_utf8(entry);
        if (!pathname)
            pathname = archive_entry_pathname(entry);
        const QString rawName = QString::fromUtf8(pathname ? pathname : "");

        if (archive_entry_is_encrypted(entry))
            m_impl->encrypted = true;

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

        // Not every format states a size up front — a streamed zip puts it in a
        // trailing data descriptor — and 0 is how the sink says so.
        const qint64 entrySize =
            archive_entry_size_is_set(entry) ? qint64(archive_entry_size(entry)) : 0;

        qint64 written = 0;
        qint64 reported = -1;

        // Flush, then report. The other way round advertises bytes that are
        // still in QFile's buffer, and a reader that trusts the number gets a
        // short read it cannot recover from inside one request.
        const auto report = [&] {
            if (!m_impl->sink)
                return;
            outFile.flush();
            reported = written;
            m_impl->sink(entryIndex, rawName, destPath, written, entrySize, /*finished*/ false);
        };

        for (;;) {
            const auto readSize = archive_read_data(ar, buf, kBufSize);
            if (readSize < 0) {
                // 7z and RAR5 only refuse encrypted *data*, having listed the
                // member happily — so this branch, not the header loop, is where
                // an encrypted 7z announces itself.
                noteReadFailure(ar, int(readSize));
                logWarning(QStringLiteral("ArchiveReader: read error in '%1': %2")
                               .arg(rawName, m_impl->lastError));
                allOk = false;
                break;
            }
            if (readSize == 0)
                break;
            const auto put = outFile.write(buf, readSize);
            if (put != readSize) {
                allOk = false;
                break;
            }
            written += put;
            // Bounded by bytes, not by blocks: a flush every 64 KiB is 600k
            // flushes on a 40 GB release. The first chunk always reports, so a
            // waiting reader starts as soon as there is anything to read.
            if (reported < 0 || written - reported >= kReportBytes)
                report();
        }
        outFile.close();
        // After close(), so "finished" really means the file is complete — and
        // extractedFiles() lists it from here on, which is what lets a cancelled
        // run delete a half-written member.
        if (m_impl->sink)
            m_impl->sink(entryIndex, rawName, destPath, written, entrySize, /*finished*/ true);
        m_impl->extracted.append(destPath);
    }

    // Same trap as scanEntries(): the loop condition swallowed every reason the
    // walk could stop, so an archive that failed on its first header extracted
    // nothing and reported success. A live set ends at ARCHIVE_EOF like any
    // other — ArchiveVolumeSource returning false is how it says "no more".
    if (status != ARCHIVE_EOF) {
        noteReadFailure(ar, status);
        logWarning(QStringLiteral("ArchiveReader: extraction of '%1' stopped: %2")
                       .arg(m_impl->filePath, m_impl->lastError));
        allOk = false;
    }

    return allOk;
}

} // namespace eMule
