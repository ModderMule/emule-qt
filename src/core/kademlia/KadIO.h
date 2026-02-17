#pragma once

/// @file KadIO.h
/// @brief Kad-specific I/O free functions operating on FileDataIO.
///
/// Kad tag wire format differs from ED2K: always uses string name (no 0x80
/// numeric ID shortcut), TAGTYPE_UINT auto-sizes on write.  These functions
/// extend the existing FileDataIO with Kad serialization.

#include "kademlia/KadUInt128.h"
#include "protocol/Tag.h"
#include "utils/SafeFile.h"

#include <QByteArray>
#include <QString>

#include <vector>

namespace eMule::kad::io {

// -- UInt128 ------------------------------------------------------------------
UInt128 readUInt128(FileDataIO& f);
void writeUInt128(FileDataIO& f, const UInt128& val);

// -- BSOB (binary small object) ----------------------------------------------
QByteArray readBsob(FileDataIO& f);
void writeBsob(FileDataIO& f, const QByteArray& data);

// -- Float --------------------------------------------------------------------
float readFloat(FileDataIO& f);
void writeFloat(FileDataIO& f, float val);

// -- UTF-8 strings ------------------------------------------------------------
QString readStringUTF8(FileDataIO& f, bool optACP = false);
void writeStringUTF8(FileDataIO& f, const QString& str);

// -- Kad tags -----------------------------------------------------------------
Tag readKadTag(FileDataIO& f, bool optACP = false);
void writeKadTag(FileDataIO& f, const Tag& tag);
std::vector<Tag> readKadTagList(FileDataIO& f, bool optACP = false);
void writeKadTagList(FileDataIO& f, const std::vector<Tag>& tags);

} // namespace eMule::kad::io
