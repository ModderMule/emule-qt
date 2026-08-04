#include "pch.h"
/// @file ArchiveUnpack.cpp
/// @brief Content-sniffing archive unwrapping for downloaded config files.

#include "archive/ArchiveUnpack.h"
#include "utils/Log.h"

#include <QFileInfo>

#include <archive.h>
#include <archive_entry.h>

#include <vector>

namespace eMule {

namespace {

/// One member, already read into memory.
struct Member {
    QString    name;
    QByteArray data;
};

/// Configure a reader for the container formats a list mirror can plausibly serve.
///
/// Two deliberate departures from ArchiveReader's `support_format_all()`:
///
/// **`raw` is added**, and it is the load-bearing call. A bare `ipfilter.dat.gz` is a
/// *filter* wrapped around a stream that is not an archive in any container sense; with
/// formats alone libarchive applies the gzip filter, finds no container, and reports the
/// whole download as unreadable. `raw` presents the post-filter bytes as a single entry,
/// which is exactly what a gzipped single file is. It also bids on *everything*, so a
/// plain file "opens" too — harmless here because isPassthrough() turns that back into
/// "not an archive", but the reason ArchiveReader must not enable it (ArchivePreviewPanel
/// would start offering to preview every ordinary file as a one-entry archive).
///
/// **The format set is curated rather than `_all`.** `_all` enables mtree, whose bidder
/// accepts any text file whose first line starts with `#` — which is precisely the shape
/// of every public blocklist ("# fullbogons-ipv6", "# Level 1"). A gzipped list was being
/// detected as an mtree archive and yielding entries with no content, so the download
/// succeeded and installed an empty filter. Registering only what we can actually receive
/// keeps that class of misdetection out. Filters stay on `_all`: gzip/bzip2/xz/zstd
/// detection is magic-byte based and has no such ambiguity.
void configureReader(struct archive* ar)
{
    archive_read_support_filter_all(ar);

    archive_read_support_format_zip(ar);
    archive_read_support_format_7zip(ar);
    archive_read_support_format_rar(ar);
    archive_read_support_format_rar5(ar);
    archive_read_support_format_tar(ar);
    archive_read_support_format_raw(ar);
}

/// True when libarchive only matched because `raw` accepts anything — i.e. the input was
/// not compressed and not a container, so the caller must get its bytes back untouched.
[[nodiscard]] bool isPassthrough(struct archive* ar)
{
    return archive_format(ar) == ARCHIVE_FORMAT_RAW
        && archive_filter_count(ar) <= 1
        && archive_filter_code(ar, 0) == ARCHIVE_FILTER_NONE;
}

/// Read the current entry's contents, refusing to exceed @p maxBytes.
/// Returns false and sets @p error on a read failure or on hitting the cap.
bool readCurrentEntry(struct archive* ar, uint64 maxBytes, QByteArray& out, QString& error)
{
    out.clear();
    for (;;) {
        const void* buf = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;

        const int r = archive_read_data_block(ar, &buf, &size, &offset);
        if (r == ARCHIVE_EOF)
            return true;
        if (r < ARCHIVE_WARN) {
            error = QString::fromUtf8(archive_error_string(ar));
            if (error.isEmpty())
                error = QStringLiteral("corrupt archive data");
            out.clear();
            return false;
        }

        if (static_cast<uint64>(out.size()) + size > maxBytes) {
            error = QStringLiteral("uncompressed size exceeds %1 MB limit")
                        .arg(maxBytes / (1024 * 1024));
            out.clear();
            return false;
        }
        out.append(static_cast<const char*>(buf), static_cast<qsizetype>(size));
    }
}

/// Pick the member to hand back: first case-insensitive file-name match from
/// @p preferredNames, else the largest one. Mirrors MFC, which looks for ipfilter.dat /
/// guarding.p2p / guardian.p2p by name before giving up (srchybrid/PPgSecurity.cpp:246-250).
[[nodiscard]] const Member* selectMember(const std::vector<Member>& members,
                                         const QStringList& preferredNames)
{
    for (const QString& wanted : preferredNames) {
        for (const Member& m : members) {
            if (QFileInfo(m.name).fileName().compare(wanted, Qt::CaseInsensitive) == 0)
                return &m;
        }
    }

    const Member* largest = nullptr;
    for (const Member& m : members) {
        if (!largest || m.data.size() > largest->data.size())
            largest = &m;
    }
    return largest;
}

} // namespace

// ---------------------------------------------------------------------------
// unwrapDownload
// ---------------------------------------------------------------------------

UnwrapResult unwrapDownload(const QByteArray& data, const QStringList& preferredNames,
                            uint64 maxBytes)
{
    UnwrapResult result;

    // Nothing to sniff. Hand it straight back so callers keep their own "empty download"
    // diagnostics rather than seeing a confusing archive error.
    if (data.isEmpty()) {
        result.data = data;
        return result;
    }

    auto* ar = archive_read_new();
    if (!ar) {
        result.data = data;
        return result;
    }
    configureReader(ar);

    if (archive_read_open_memory(ar, data.constData(),
                                 static_cast<size_t>(data.size())) != ARCHIVE_OK) {
        archive_read_free(ar);
        result.data = data;   // not an archive
        return result;
    }

    std::vector<Member> members;
    QString readError;
    bool truncated = false;

    struct archive_entry* entry = nullptr;
    for (;;) {
        const int r = archive_read_next_header(ar, &entry);
        if (r == ARCHIVE_EOF)
            break;
        if (r < ARCHIVE_WARN) {
            // A container we could open but not walk. If we already recovered members,
            // keep them and note the truncation; otherwise fall through to passthrough.
            truncated = true;
            break;
        }

        // Decided on the first header, once libarchive has committed to a format.
        if (members.empty() && isPassthrough(ar)) {
            archive_read_free(ar);
            result.data = data;
            return result;
        }

        if (archive_entry_filetype(entry) == AE_IFDIR) {
            archive_read_data_skip(ar);
            continue;
        }

        Member m;
        const char* pathname = archive_entry_pathname_utf8(entry);
        if (!pathname)
            pathname = archive_entry_pathname(entry);
        m.name = pathname ? QString::fromUtf8(pathname) : QString();

        if (!readCurrentEntry(ar, maxBytes, m.data, readError)) {
            archive_read_free(ar);
            result.wasArchive = true;
            result.error = readError;
            return result;   // data stays empty — a failed unpack, not a passthrough
        }
        members.push_back(std::move(m));
    }

    archive_read_free(ar);

    if (members.empty()) {
        // Opened as an archive but yielded nothing usable. Garbage and truncated input
        // both land here; returning the original bytes lets the caller's own parser
        // produce the error message, which is more useful than "bad archive".
        result.data = data;
        return result;
    }

    const Member* chosen = selectMember(members, preferredNames);
    result.data = chosen->data;
    result.entryName = chosen->name;
    result.wasArchive = true;

    if (truncated) {
        logWarning(QStringLiteral("Archive was truncated — using the %1 member(s) recovered")
                       .arg(members.size()));
    }
    return result;
}

} // namespace eMule
