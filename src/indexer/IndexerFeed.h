#pragma once

/// @file IndexerFeed.h
/// @brief Alias of the stored feed record into eMule::indexer.
///
/// The record lives in core/prefs, because Preferences owns the file format and
/// the at-rest encryption of a URL feed's embedded API key, and core may not
/// depend on eMule::Indexer. Same one-line aliasing header IndexerConfig.h makes.

#include "prefs/IndexerFeed.h"

namespace eMule::indexer {

using eMule::IndexerFeed;
using eMule::IndexerFeedKind;
using eMule::indexerFeedKindFromString;
using eMule::indexerFeedKindToString;

} // namespace eMule::indexer
