#pragma once

/// @file PublishKeywordList.h
/// @brief Kad keyword publishing list — port of MFC CPublishKeyword/CPublishKeywordList.
///
/// Manages per-keyword file references for Kademlia keyword publishing.
/// Each keyword maps to a Kad ID (MD4 of lowercase UTF-8) and a set of files.

#include "kademlia/KadUInt128.h"
#include "utils/Types.h"

#include <QString>

#include <ctime>
#include <list>
#include <vector>

namespace eMule {

class KnownFile;

// ---------------------------------------------------------------------------
// PublishKeyword — a single keyword with its Kad ID and file references
// ---------------------------------------------------------------------------

class PublishKeyword {
public:
    explicit PublishKeyword(const QString& keyword);

    [[nodiscard]] const QString& keyword() const { return m_keyword; }
    [[nodiscard]] const kad::UInt128& kadID() const { return m_kadID; }
    [[nodiscard]] const std::vector<KnownFile*>& fileRefs() const { return m_files; }
    [[nodiscard]] int refCount() const { return static_cast<int>(m_files.size()); }

    void addRef(KnownFile* file);
    void removeRef(KnownFile* file);

    /// Rotate the first @p count entries to the back (round-robin publishing).
    void rotateReferences(int count);

    [[nodiscard]] time_t nextPublishTime() const { return m_nextPublishTime; }
    void setNextPublishTime(time_t t) { m_nextPublishTime = t; }

    [[nodiscard]] uint32 publishedCount() const { return m_publishedCount; }
    void incPublishedCount() { ++m_publishedCount; }
    void resetPublishedCount() { m_publishedCount = 0; }

private:
    QString m_keyword;
    kad::UInt128 m_kadID;
    std::vector<KnownFile*> m_files;
    time_t m_nextPublishTime = 0;
    uint32 m_publishedCount = 0;
};

// ---------------------------------------------------------------------------
// PublishKeywordList — manages all keywords for keyword publishing
// ---------------------------------------------------------------------------

class PublishKeywordList {
public:
    PublishKeywordList() = default;

    /// Extract words from file name and add file references.
    void addKeywords(KnownFile* file);

    /// Remove file references from all keywords.
    void removeKeywords(KnownFile* file);

    /// Get the next keyword for round-robin publishing.
    /// Returns nullptr when the list is exhausted (call resetNextKeyword).
    PublishKeyword* getNextKeyword();

    /// Reset the round-robin iterator to the beginning.
    void resetNextKeyword();

    /// Remove keywords with zero file references.
    void purgeUnreferencedKeywords();

    /// Clear all keywords.
    void removeAllKeywords();

    [[nodiscard]] time_t nextPublishTime() const { return m_nextPublishTime; }
    void setNextPublishTime(time_t t) { m_nextPublishTime = t; }

    [[nodiscard]] int keywordCount() const { return static_cast<int>(m_keywords.size()); }

private:
    std::list<PublishKeyword> m_keywords;
    std::list<PublishKeyword>::iterator m_nextKeywordIter = m_keywords.end();
    time_t m_nextPublishTime = 0;
};

} // namespace eMule
