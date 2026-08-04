#pragma once

/// @file KadMiscUtils.h
/// @brief Kad utility functions: IP formatting, keyword hashing, word splitting.

#include "kademlia/KadUInt128.h"

#include <QString>

#include <vector>

namespace eMule::kad {

/// Minimum length (in UTF-8 bytes) a word must have to be usable as a Kad
/// keyword. Shorter words are dropped by getWords() and can therefore never
/// become a search target.
inline constexpr qsizetype kMinKadKeywordBytes = 3;

/// Format a uint32 IP address (host byte order) as dotted string.
QString ipToString(uint32 ip);

/// Compute MD4 hash of a UTF-8 keyword.
void getKeywordHash(const QString& keyword, UInt128& outHash);

/// Get the UTF-8 bytes of a keyword.
QByteArray getKeywordBytes(const QString& keyword);

/// Split a string into words using Kad keyword delimiter characters.
void getWords(const QString& str, std::vector<QString>& outWords);

/// The keyword a Kad search for @p expression will be indexed under: the first
/// word of the lowercased expression. Empty when the expression yields no usable
/// word. Callers building the search-terms blob must use the same keyword the
/// search target was hashed from, so both go through this.
[[nodiscard]] QString kadSearchKeyword(const QString& expression);

/// Lowercase a tag string using Unicode standard case mapping.
QString kadTagStrToLower(const QString& str);

} // namespace eMule::kad
