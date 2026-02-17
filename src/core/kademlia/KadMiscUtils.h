#pragma once

/// @file KadMiscUtils.h
/// @brief Kad utility functions: IP formatting, keyword hashing, word splitting.

#include "kademlia/KadUInt128.h"

#include <QString>

#include <vector>

namespace eMule::kad {

/// Format a uint32 IP address (host byte order) as dotted string.
QString ipToString(uint32 ip);

/// Compute MD4 hash of a UTF-8 keyword.
void getKeywordHash(const QString& keyword, UInt128& outHash);

/// Get the UTF-8 bytes of a keyword.
QByteArray getKeywordBytes(const QString& keyword);

/// Split a string into words using Kad keyword delimiter characters.
void getWords(const QString& str, std::vector<QString>& outWords);

/// Lowercase a tag string using Unicode standard case mapping.
QString kadTagStrToLower(const QString& str);

} // namespace eMule::kad
