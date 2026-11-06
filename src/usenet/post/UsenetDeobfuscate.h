#pragma once

/// @file UsenetDeobfuscate.h
/// @brief Giving an obfuscated release its job name back.
///
/// Posters hide releases behind random names (`a3f9…e1.mkv`). PAR2 restores the
/// real names when its packets carry them; when they carry the random ones too,
/// or there is no PAR2, the payload would be published as noise. The job name
/// (the NZB's, or the indexer's title) is the only useful name left.
///
/// Same rules as SABnzbd's deobfuscate step, written fresh:
///   1. pick the clearly biggest file: alone, or more than 3x the next one;
///   2. only if its base name looks obfuscated (looksObfuscatedName());
///   3. it becomes `<job name>.<ext>`, and every file sharing its old base name
///      keeps its own tail: `x.srt`, `x-sample.mkv`, `x.eng.srt`.
///
/// Pure: answers names, never touches the disk, so staging can publish under
/// the new name while the queue keeps the original path as the file's source.

#include <QHash>
#include <QString>
#include <QStringList>

namespace eMule::usenet {

/// True for a base name (no extension) that carries no words: 32 hex digits,
/// 40+ hex-and-dots, `abc.xyz…`, or a run with no case mix and no separators.
[[nodiscard]] bool looksObfuscatedName(const QString& baseName);

/// Source path -> new bare file name, for the files of @p payload to rename.
/// Empty when nothing qualifies, @p jobName itself looks obfuscated, or the
/// payload is a disc structure (VIDEO_TS, BDMV), whose names are fixed.
[[nodiscard]] QHash<QString, QString> deobfuscatedNames(const QStringList& payload,
                                                        const QString& jobName);

} // namespace eMule::usenet
