#pragma once

/// @file ArchiveContentsHook.h
/// @brief Where an unpacker hands a set it read to the ed3k contents record (hoster.md §6.1),
/// while its volumes still exist. Set by the daemon; empty: nobody publishes.

#include <QStringList>

#include <functional>

namespace eMule {

/// Called on the unpacker's thread with the set's volumes in order and its passwords; it
/// reads and hashes the set there (blocking), then publishes on its own thread.
using ArchiveContentsHook = std::function<void(const QStringList& volumes, const QStringList& passwords)>;

void setArchiveContentsHook(ArchiveContentsHook hook);
/// Runs the hook if one is set.
void readArchiveContents(const QStringList& volumes, const QStringList& passwords);

} // namespace eMule
