#pragma once
// Records this node won't hold by its own policy (ed3k-spec Protocol.md §10.3 "Declining"):
// root hashes and keywords, answered DECLINED by packmule as Kad storing node and index node.
//
// File <config>/ed3k/decline.txt, one entry per line, read at start and on every change:
//   # comment
//   <64 hex>        a BLAKE3 root: its file, keyword, comment, alias, set member and set head records
//   anything else   a keyword; several words on a line = all must be words of the file name

#include "Ed3kFfi.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace eMule::ed3k {

struct DeclineList {
    std::vector<::ed3k::Root> roots;
    QStringList keywords;
    [[nodiscard]] bool isEmpty() const { return roots.empty() && keywords.isEmpty(); }
};

inline constexpr auto kDeclineFile = "decline.txt";

/// Entries of decline.txt; bad lines are keywords, empty and # lines skipped.
[[nodiscard]] DeclineList parseDeclineList(const QString& text);
/// parseDeclineList of @p path; empty when missing or unreadable.
[[nodiscard]] DeclineList loadDeclineList(const QString& path);

} // namespace eMule::ed3k
