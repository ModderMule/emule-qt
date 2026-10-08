#pragma once

/// @file ImportParts.h
/// @brief Import completed parts from a source file into a PartFile.
///
/// Reads each incomplete part from the source at the corresponding offset,
/// computes MD4 hash, and writes matching parts to the PartFile.

#include "utils/Types.h"

#include <QString>

#include <functional>

namespace eMule {

class PartFile;

/// Import completed parts from a source file into a PartFile.
/// Reads each incomplete part from the source at the corresponding offset,
/// computes MD4 hash, and writes matching parts to the PartFile.
///
/// @param partFile        Target PartFile to import parts into.
/// @param sourceFilePath  Path to a complete file to import from.
/// @param progressCallback Optional callback reporting percent complete [0..100].
/// @return Number of parts successfully imported, or 0 on error.
int importParts(PartFile* partFile, const QString& sourceFilePath,
                std::function<void(int percent)> progressCallback = nullptr);

/// Whether @p sourceFilePath can be imported into @p partFile at all: it exists
/// and has the file's size.
[[nodiscard]] bool canImportParts(const PartFile* partFile, const QString& sourceFilePath);

/// Import one part, when it is missing entirely and the source's data has its hash.
/// For callers that must not block: one part per turn of the event loop. The caller
/// runs PartFile::updateCompletedInfos() when it is done.
bool importPart(PartFile* partFile, const QString& sourceFilePath, uint32 part);

} // namespace eMule
