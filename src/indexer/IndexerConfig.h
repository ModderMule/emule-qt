#pragma once

/// @file IndexerConfig.h
/// @brief Alias of the stored indexer record into eMule::indexer.
///
/// The record itself lives in core/prefs, because Preferences owns the file
/// format and the at-rest encryption of the API key, and core may not depend on
/// eMule::Indexer. Same call, and the same one-line aliasing header, that
/// usenet/nntp/NewsServer.h makes for NewsServer.

#include "prefs/IndexerConfig.h"

namespace eMule::indexer {

using eMule::IndexerConfig;
using eMule::IndexerKind;
using eMule::indexerKindFromString;
using eMule::indexerKindToString;

} // namespace eMule::indexer
