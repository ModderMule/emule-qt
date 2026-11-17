#pragma once
// Records this node won't hold by its own policy (ed3k-spec Protocol.md §10.3 "Declining"):
// root hashes, eD2K hashes, publisher keys, record kinds and keywords, answered DECLINED by
// packmule as Kad storing node and index node (Engine::kadDeclineLists).
//
// File <config>/ed3k/decline.txt, one entry per line, read at start and on every change:
//   # comment
//   <64 hex>                        a BLAKE3 root: its file, keyword, comment, alias, set member and set head records
//   ed2k:<32 hex>                   an eD2K hash: KEYWORD records of that file, ALIAS `ed2k` records
//   ed2k://|file|name|size|hash|/   the same, as an eD2K link
//   key:<64 hex>                    a publisher's public key: the signed records it authored
//   kind:<name or number>           a record kind (proto Kind: SOURCE, KEYWORD, ... or 1, 2, ...): all its records
//   anything else                   a keyword; several words on a line = all must be words of the file name
// A prefixed line with a bad value is skipped (DeclineList::invalid), never a keyword.
// Local uses (declined-records.md §2): own publishing skips listed roots / eD2K hashes
// (Ed3kJoinBase::publishShared, Ed3kShared::share; a list change withdraws or publishes again:
// Ed3kSession::declinedChanged); a download of one is added paused and the user told
// (Ed3kDownloads::declinedAdded); search rows hidden (Ed3kSearch::judge). Lists fetched from
// URLs (Ed3kDeclineUrls) are merged in.

#include "Ed3kFfi.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

namespace eMule::ed3k {

struct DeclineList {
    std::vector<::ed3k::Root> roots;
    QStringList keywords;
    std::vector<::ed3k::Ed2kHash> ed2k;
    std::vector<::ed3k::Key32> keys;
    std::vector<std::uint32_t> kinds;
    QStringList invalid;   ///< prefixed lines with a bad value: skipped, shown to the user

    // was: [[nodiscard]] bool isEmpty() const { return roots.empty() && keywords.isEmpty(); }
    [[nodiscard]] bool isEmpty() const
    {
        return roots.empty() && keywords.isEmpty() && ed2k.empty() && keys.empty() && kinds.empty();
    }
    [[nodiscard]] bool operator==(const DeclineList&) const = default;
    [[nodiscard]] bool listsRoot(const ::ed3k::Root& root) const;
    [[nodiscard]] bool listsEd2k(const ::ed3k::Ed2kHash& hash) const;
    /// @p md4: 16 B; any other size is not listed
    [[nodiscard]] bool listsEd2k(const QByteArray& md4) const;
    /// listsRoot(@p root) or listsEd2k(@p md4)
    [[nodiscard]] bool listsFile(const ::ed3k::Root& root, const QByteArray& md4 = {}) const;
    /// For Engine::kadDeclineLists.
    [[nodiscard]] ::ed3k::DeclineLists toFfi() const;
    /// Adds the entries of @p other not listed yet (invalid stays this list's own).
    void merge(const DeclineList& other);
};

inline constexpr auto kDeclineFile = "decline.txt";

/// Entries of decline.txt; unprefixed bad lines are keywords, empty and # lines skipped.
[[nodiscard]] DeclineList parseDeclineList(const QString& text);
/// parseDeclineList of @p path; empty when missing or unreadable.
[[nodiscard]] DeclineList loadDeclineList(const QString& path);
/// The proto Kind of @p name (case-insensitive) or of a decimal number; nullopt: unknown or 0 (NONE).
[[nodiscard]] std::optional<std::uint32_t> declineKindOf(const QString& name);

} // namespace eMule::ed3k
