#pragma once

/// @file IndexerCapsStore.h
/// @brief The `t=caps` cache: one YAML sidecar per indexer.
///
/// Not preferences.yml. A caps document carries a category tree that runs to a
/// hundred nodes, and preferences.yml is a file people open and edit by hand;
/// burying the settings under a wall of generated category ids would make it
/// unusable. Capabilities also belong to the indexer rather than to the user, so
/// they are a cache to be refreshed, not a setting to be preserved.
///
/// Follows UsenetQueueStore's shape: a directory under configDir(), one file per
/// entity, keyed by a filename-safe slug.

#include "IndexerCaps.h"
#include "IndexerConfig.h"

#include <QString>

namespace eMule::indexer {

class IndexerCapsStore {
public:
    /// `<configDir>/Indexers`, created on demand.
    [[nodiscard]] static QString directory();

    [[nodiscard]] static QString pathFor(const IndexerConfig& config);

    /// Load the cached document. False when there is none, or it is unreadable —
    /// neither is an error worth surfacing, because a missing cache only means
    /// the next search goes out ungated.
    [[nodiscard]] static bool load(const IndexerConfig& config, IndexerCaps& out);

    static bool save(const IndexerConfig& config, const IndexerCaps& caps);

    /// Drop the sidecar for an account the user deleted, so a later account
    /// reusing the name does not inherit a stranger's category tree.
    static void remove(const IndexerConfig& config);

    /// Whether @p caps is old enough to re-probe. An empty probedAt means never
    /// probed, which is stale by definition.
    [[nodiscard]] static bool isStale(const IndexerCaps& caps, int refreshDays);
};

} // namespace eMule::indexer
