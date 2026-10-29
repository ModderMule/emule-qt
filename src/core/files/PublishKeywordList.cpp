#include "pch.h"
/// @file PublishKeywordList.cpp
/// @brief Kad keyword publishing list — port of MFC CPublishKeyword/CPublishKeywordList.

#include "files/PublishKeywordList.h"
#include "files/KnownFile.h"
#include "kademlia/KadMiscUtils.h"


namespace eMule {

// ===========================================================================
// PublishKeyword
// ===========================================================================

PublishKeyword::PublishKeyword(const QString& keyword)
    : m_keyword(keyword)
{
    QString lower = kad::kadTagStrToLower(keyword);
    kad::getKeywordHash(lower, m_kadID);
}

bool PublishKeyword::addRef(KnownFile* file)
{
    if (!file)
        return false;
    if (!m_fileSet.insert(file).second)
        return false;
    m_files.push_back(file);
    return true;
}

void PublishKeyword::removeRef(KnownFile* file)
{
    if (m_fileSet.erase(file) == 0)
        return;
    auto it = std::ranges::find(m_files, file);
    if (it != m_files.end())
        m_files.erase(it);
}

void PublishKeyword::rotateReferences(int count)
{
    if (count <= 0 || m_files.size() <= 1)
        return;
    int n = std::min(count, static_cast<int>(m_files.size()));
    std::rotate(m_files.begin(), m_files.begin() + n, m_files.end());
}

// ===========================================================================
// PublishKeywordList
// ===========================================================================

void PublishKeywordList::addKeywords(KnownFile* file)
{
    if (!file)
        return;

    bool wasEmpty = m_keywords.empty();

    // KnownFile::setFileName keeps this list up to date and, for a collection with an
    // author key, prepends that key — which is what makes "Search Author's Collections"
    // findable (srchybrid/SharedFileList.cpp:709). Fall back to the file name for any
    // path that set a name without going through KnownFile::setFileName, so a file can
    // never end up unpublished.
    std::vector<QString> words = file->kadKeywords();
    if (words.empty())
        kad::getWords(file->fileName(), words);

    std::vector<QString>& registered = m_wordsOf[file];
    for (const auto& word : words) {
        const QString lower = kad::kadTagStrToLower(word);

        // Find existing keyword or create new one
        auto it = m_index.constFind(lower);
        if (it == m_index.constEnd()) {
            m_keywords.emplace_back(word);
            it = m_index.insert(lower, std::prev(m_keywords.end()));
            m_nextPublishTime = 0;
        }
        // A published keyword that gains a file is due again on the next lap. MIN2S(30)
        // is MFC's absolute "long ago", not a delay (SharedFileList.cpp:220-227).
        PublishKeyword& kw = **it;
        const int unitsBefore = PublishKeyword::publishUnits(kw.refCount());
        if (kw.addRef(file)) {
            m_publishUnits += PublishKeyword::publishUnits(kw.refCount()) - unitsBefore;
            if (kw.nextPublishTime() > MIN2S(30))
                kw.setNextPublishTime(MIN2S(30));
        }
        // find, not ranges::contains: GCC 12 (Ubuntu 22.04 build) lacks it
        if (std::ranges::find(registered, lower) == registered.end())
            registered.push_back(lower);
    }

    // Initialize the round-robin iterator when first keywords are added.
    // Without this, m_nextKeywordIter is singular.
    if (wasEmpty && !m_keywords.empty())
        m_nextKeywordIter = m_keywords.begin();
}

void PublishKeywordList::removeKeywords(KnownFile* file)
{
    if (!file)
        return;

    const auto wordsIt = m_wordsOf.find(file);
    if (wordsIt == m_wordsOf.end())
        return;

    for (const QString& lower : wordsIt->second) {
        const auto it = m_index.constFind(lower);
        if (it == m_index.constEnd())
            continue;
        const KeywordIter kw = *it;
        const int unitsBefore = PublishKeyword::publishUnits(kw->refCount());
        kw->removeRef(file);
        m_publishUnits += PublishKeyword::publishUnits(kw->refCount()) - unitsBefore;
        if (kw->refCount() == 0)
            eraseKeyword(kw);
    }
    m_wordsOf.erase(wordsIt);
}

PublishKeyword* PublishKeywordList::getNextKeyword()
{
    if (m_keywords.empty())
        return nullptr;

    if (m_nextKeywordIter == m_keywords.end())
        return nullptr;

    auto* kw = &(*m_nextKeywordIter);
    ++m_nextKeywordIter;
    return kw;
}

void PublishKeywordList::resetNextKeyword()
{
    m_nextKeywordIter = m_keywords.begin();
}

PublishKeyword* PublishKeywordList::nextDueKeyword(int maxLook,
                                                  const std::function<bool(PublishKeyword&)>& due)
{
    const int looks = std::min(maxLook, keywordCount());
    for (int i = 0; i < looks; ++i) {
        if (m_nextKeywordIter == m_keywords.end())
            m_nextKeywordIter = m_keywords.begin();
        PublishKeyword& kw = *m_nextKeywordIter++;
        if (due(kw))
            return &kw;
    }
    return nullptr;
}

void PublishKeywordList::removeAllKeywordReferences()
{
    for (auto& kw : m_keywords)
        kw.removeAllReferences();
    m_wordsOf.clear();
    m_publishUnits = 0;
}

void PublishKeywordList::purgeUnreferencedKeywords()
{
    for (auto it = m_keywords.begin(); it != m_keywords.end(); ) {
        if (it->refCount() == 0)
            it = eraseKeyword(it);
        else
            ++it;
    }
}

void PublishKeywordList::removeAllKeywords()
{
    m_keywords.clear();
    m_index.clear();
    m_wordsOf.clear();
    m_publishUnits = 0;
    m_nextKeywordIter = m_keywords.end();
}

PublishKeywordList::KeywordIter PublishKeywordList::eraseKeyword(KeywordIter it)
{
    if (m_nextKeywordIter == it)
        ++m_nextKeywordIter;
    m_index.remove(kad::kadTagStrToLower(it->keyword()));
    m_publishUnits -= PublishKeyword::publishUnits(it->refCount());
    m_nextPublishTime = 0;
    return m_keywords.erase(it);
}

} // namespace eMule
