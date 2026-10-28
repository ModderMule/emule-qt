#pragma once

/// @file Ed3kSiteCache.h
/// @brief Website manifests and members kept on disk across restarts (name.md §6.4), for
/// Ed3kNames: one file each under Config/ed3k/site-cache/, up to a total (2 GiB by
/// default); the ones used longest ago go first (LRU by file modification time, touched on
/// each use). Not secrets: what a visitor of the site fetched anyway.
///
/// - `s-<site key>`: the newest manifest held of a set (siteKey of Ed3kNames).
/// - `m-<root hex>`: a member's bytes. Checked against its root when read back; a file that
///   is not (a broken disk, an edit) is removed, so only verified bytes are sent (§6.3).

#include "Ed3kFfi.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>

namespace eMule::ed3k {

class Ed3kSiteCache {
public:
    static constexpr std::uint64_t kDefaultMax = 2ULL * 1024 * 1024 * 1024;

    explicit Ed3kSiteCache(QString dir, std::uint64_t max = kDefaultMax);

    /// The total kept at most (tests make it small); shrinks at once.
    void setMax(std::uint64_t max);
    [[nodiscard]] std::uint64_t max() const { return m_max; }
    /// What the files take now.
    [[nodiscard]] std::uint64_t bytes() const { return m_bytes; }
    [[nodiscard]] const QString& dir() const { return m_dir; }

    /// Keeps @p manifest as the one of @p site (written over an older one).
    void putManifest(const QString& site, const QByteArray& manifest);
    /// The manifest kept of @p site, touched; nullopt: none.
    [[nodiscard]] std::optional<QByteArray> manifest(const QString& site);
    /// Keeps a verified member (nothing when kept already: touched then).
    void putMember(const ::ed3k::Root& root, const QByteArray& data);
    /// The bytes of a member kept, checked against @p root and touched; nullopt: none or broken.
    [[nodiscard]] std::optional<QByteArray> member(const ::ed3k::Root& root);
    [[nodiscard]] bool hasMember(const ::ed3k::Root& root) const;

private:
    void scan();
    void put(const QString& name, const QByteArray& data);
    std::optional<QByteArray> read(const QString& name);
    void remove(const QString& name);
    /// The oldest go until the total is at most m_max; @p keep stays.
    void evict(const QString& keep = {});
    [[nodiscard]] QString path(const QString& name) const;
    [[nodiscard]] static QString memberName(const ::ed3k::Root& root);
    [[nodiscard]] static QString manifestName(const QString& site);

    QString m_dir;
    std::uint64_t m_max = kDefaultMax;
    std::uint64_t m_bytes = 0;
};

} // namespace eMule::ed3k
