/// @file PublishKeywordList.cpp
/// @brief Kad keyword publishing list — port of MFC CPublishKeyword/CPublishKeywordList.

#include "files/PublishKeywordList.h"
#include "files/KnownFile.h"
#include "kademlia/KadMiscUtils.h"

#include <algorithm>

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

void PublishKeyword::addRef(KnownFile* file)
{
    if (!file)
        return;
    if (std::ranges::find(m_files, file) != m_files.end())
        return;
    m_files.push_back(file);
}

void PublishKeyword::removeRef(KnownFile* file)
{
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

    std::vector<QString> words;
    kad::getWords(file->fileName(), words);

    for (const auto& word : words) {
        QString lower = kad::kadTagStrToLower(word);

        // Find existing keyword or create new one
        auto it = std::ranges::find_if(m_keywords,
            [&lower](const PublishKeyword& kw) {
                return kad::kadTagStrToLower(kw.keyword()) == lower;
            });

        if (it != m_keywords.end()) {
            it->addRef(file);
        } else {
            m_keywords.emplace_back(word);
            m_keywords.back().addRef(file);
        }
    }
}

void PublishKeywordList::removeKeywords(KnownFile* file)
{
    if (!file)
        return;

    for (auto it = m_keywords.begin(); it != m_keywords.end(); ) {
        it->removeRef(file);
        if (it->refCount() == 0) {
            if (m_nextKeywordIter == it)
                ++m_nextKeywordIter;
            it = m_keywords.erase(it);
        } else {
            ++it;
        }
    }
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

void PublishKeywordList::purgeUnreferencedKeywords()
{
    for (auto it = m_keywords.begin(); it != m_keywords.end(); ) {
        if (it->refCount() == 0) {
            if (m_nextKeywordIter == it)
                ++m_nextKeywordIter;
            it = m_keywords.erase(it);
        } else {
            ++it;
        }
    }
}

void PublishKeywordList::removeAllKeywords()
{
    m_keywords.clear();
    m_nextKeywordIter = m_keywords.end();
}

} // namespace eMule
