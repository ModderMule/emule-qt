#pragma once

/// @file ArchiveContentsHook.h
/// @brief Where an unpacker hands what it reads to the ed3k contents record (hoster.md §6.1).
///
/// One read: the reader that unpacks (or verifies) a set feeds a tap with the volume bytes
/// and every entry's bytes as they pass; the tap hashes them on that same thread. The caller
/// commits the tap only once the unpack or verify succeeded. Set by the daemon; no hook: no tap.

#include <QString>

#include <functional>
#include <memory>

namespace eMule {

class ArchiveContentsTap {
public:
    virtual ~ArchiveContentsTap() = default;

    /// Volumes the tap wants complete, counted from the first (1: the first volume only).
    [[nodiscard]] virtual int volumesWanted() const { return 1; }
    /// Bytes of volume @p index, each volume from its start, in order, each byte once.
    /// May come from another thread than the entry calls (never concurrently per volume).
    virtual void volumeData(int index, const char* data, qint64 len) = 0;
    virtual void volumeEnd(int index) = 0;

    /// A regular-file entry, path with '/' (ArchiveReader::normalizedEntryPath).
    virtual void entryBegin(const QString& path) = 0;
    virtual void entryData(const char* data, qint64 len) = 0;
    virtual void entryEnd() = 0;
    /// An entry the record cannot hold, or the read broke: no listing from this tap.
    virtual void spoil() = 0;

    /// The set unpacked or verified: publish. @p password opened it, empty: none needed.
    virtual void commit(const QString& firstVolumeName, const QString& password) = 0;
};

using ArchiveContentsHook = std::function<std::unique_ptr<ArchiveContentsTap>()>;

void setArchiveContentsHook(ArchiveContentsHook hook);
/// A new tap when someone publishes, else null.
[[nodiscard]] std::shared_ptr<ArchiveContentsTap> archiveContentsTap();

} // namespace eMule
