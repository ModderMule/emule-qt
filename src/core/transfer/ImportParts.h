#pragma once

/// @file ImportParts.h
/// @brief Import completed parts from a source file into a PartFile.
///
/// Reads each incomplete part from the source at the corresponding offset,
/// computes MD4 hash, and writes matching parts to the PartFile.

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

} // namespace eMule
