#include "pch.h"
/// @file SearchList.cpp
/// @brief Search result manager — port of MFC CSearchList.

#include "search/SearchList.h"
#include "protocol/Tag.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

SearchList::SearchList(QObject* parent)
    : QObject(parent)
{
}

SearchList::~SearchList() = default;

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------

uint32 SearchList::newSearch(const QString& resultFileType, const SearchParams& /*params*/)
{
    m_resultFileType = resultFileType;
    m_currentSearchID = m_nextSearchID++;

    SearchListEntry entry;
    entry.searchID = m_currentSearchID;
    m_fileLists.push_back(std::move(entry));

    m_foundFilesCount[m_currentSearchID] = 0;
    m_foundSourcesCount[m_currentSearchID] = 0;

    // Clear per-session UDP tracking
    m_curED2KSentRequestsIPs.clear();
    m_udpServerRecords.clear();

    return m_currentSearchID;
}

void SearchList::clear()
{
    for (auto& entry : m_fileLists) {
        for (auto& file : entry.files)
            emit resultAboutToBeRemoved(file.get());
    }
    m_fileLists.clear();
    m_foundFilesCount.clear();
    m_foundSourcesCount.clear();
    m_udpServerRecords.clear();
    m_curED2KSentRequestsIPs.clear();
}

void SearchList::removeResults(uint32 searchID)
{
    auto it = std::find_if(m_fileLists.begin(), m_fileLists.end(),
                           [searchID](const SearchListEntry& e) { return e.searchID == searchID; });
    if (it == m_fileLists.end())
        return;

    for (auto& file : it->files)
        emit resultAboutToBeRemoved(file.get());

    m_fileLists.erase(it);
    m_foundFilesCount.erase(searchID);
    m_foundSourcesCount.erase(searchID);
}

void SearchList::removeResult(SearchFile* file)
{
    if (!file)
        return;

    const uint32 searchID = file->searchID();
    auto* entry = findEntry(searchID);
    if (!entry)
        return;

    // If this is a parent, remove all children first
    if (file->listChildCount() > 0) {
        for (auto* child : file->listChildren()) {
            emit resultAboutToBeRemoved(child);
            // Children are owned in the same list
            auto childIt = std::find_if(entry->files.begin(), entry->files.end(),
                                         [child](const auto& ptr) { return ptr.get() == child; });
            if (childIt != entry->files.end())
                entry->files.erase(childIt);
        }
    }

    // If this is a child, remove from parent's child list
    if (file->listParent()) {
        auto& siblings = const_cast<std::list<SearchFile*>&>(file->listParent()->listChildren());
        siblings.remove(file);
    }

    emit resultAboutToBeRemoved(file);
    auto fileIt = std::find_if(entry->files.begin(), entry->files.end(),
                                [file](const auto& ptr) { return ptr.get() == file; });
    if (fileIt != entry->files.end())
        entry->files.erase(fileIt);
}

// ---------------------------------------------------------------------------
// Kad result processing
// ---------------------------------------------------------------------------

void SearchList::addKadKeywordResult(uint32 searchID, const uint8* fileHash,
                                      const QString& name, uint64 size,
                                      const QString& type, uint32 sources,
                                      uint32 completeSources)
{
    auto* entry = findEntry(searchID);
    if (!entry) {
        // Create an entry if none exists for this search ID
        SearchListEntry newEntry;
        newEntry.searchID = searchID;
        m_fileLists.push_back(std::move(newEntry));
        entry = &m_fileLists.back();
        m_foundFilesCount[searchID] = 0;
        m_foundSourcesCount[searchID] = 0;
    }

    // Build a SearchFile from the Kad result data
    auto* file = new SearchFile();
    file->setFileHash(fileHash);
    if (!name.isEmpty())
        file->setFileName(name, true);
    file->setFileSize(size);
    if (!type.isEmpty())
        file->setFileType(type);
    file->addSources(sources);
    file->addCompleteSources(completeSources);
    file->setSearchID(searchID);

    addToList(file, false, 0);
    emit tabHeaderUpdated(searchID);
}

// ---------------------------------------------------------------------------
// Result processing — TCP
// ---------------------------------------------------------------------------

bool SearchList::processSearchAnswer(const uint8* packet, uint32 size,
                                     bool optUTF8,
                                     uint32 serverIP, uint16 serverPort)
{
    SafeMemFile data(packet, size);

    const uint32 resultCount = data.readUInt32();
    for (uint32 i = 0; i < resultCount; ++i) {
        auto* file = new SearchFile(data, optUTF8, serverIP, serverPort);
        file->setSearchID(m_currentSearchID);
        addToList(file, false, 0);
    }

    // Check for trailing "more results" flag
    bool moreResults = false;
    if (data.position() < data.length()) {
        moreResults = data.readUInt8() != 0;
    }

    emit tabHeaderUpdated(m_currentSearchID);
    return moreResults;
}

// ---------------------------------------------------------------------------
// Result processing — UDP (single result per packet)
// ---------------------------------------------------------------------------

void SearchList::processUDPSearchAnswer(const uint8* packet, uint32 size,
                                        bool optUTF8,
                                        uint32 serverIP, uint16 serverPort)
{
    // Validate server was in our request list
    if (m_curED2KSentRequestsIPs.find(serverIP) == m_curED2KSentRequestsIPs.end())
        return;

    SafeMemFile data(packet, size);

    // A single UDP datagram can contain multiple concatenated search results,
    // each separated by an OP_EDONKEYPROT + OP_GLOBSEARCHRES header.
    // (Matches MFC eMule: srchybrid/UDPSocket.cpp do-while loop)
    do {
        auto* file = new SearchFile(data, optUTF8, serverIP, serverPort);
        file->setSearchID(m_currentSearchID);

        auto& record = m_udpServerRecords[serverIP];
        record.totalResults++;

        addToList(file, false, serverIP);

        // Check for another concatenated result (proto + opcode header)
        qint64 remaining = data.length() - data.position();
        if (remaining >= 2) {
            uint8 proto = data.readUInt8();
            if (proto != OP_EDONKEYPROT) {
                data.seek(-1, SEEK_CUR);
                break;
            }
            uint8 opcode = data.readUInt8();
            if (opcode != OP_GLOBSEARCHRES) {
                data.seek(-2, SEEK_CUR);
                break;
            }
        }
    } while (data.position() < data.length());

    emit tabHeaderUpdated(m_currentSearchID);
}

// ---------------------------------------------------------------------------
// Core: addToList — deduplication, parent/child grouping
// ---------------------------------------------------------------------------

void SearchList::addToList(SearchFile* rawFile, bool clientResponse,
                           uint32 fromUDPServerIP)
{
    std::unique_ptr<SearchFile> fileOwner(rawFile);

    // Apply file type filter
    if (!m_resultFileType.isEmpty() && !fileOwner->fileType().isEmpty()) {
        if (fileOwner->fileType() != m_resultFileType)
            return; // filtered out, unique_ptr will delete
    }

    // Compute name-without-keywords for spam detection
    if (fileOwner->nameWithoutKeywords().isEmpty()) {
        fileOwner->setNameWithoutKeywords(
            computeNameWithoutKeywords(fileOwner->fileName(), fileOwner->fileType()));
    }

    const uint32 searchID = fileOwner->searchID();
    auto* entry = findEntry(searchID);
    if (!entry) {
        // No entry for this search ID — shouldn't happen, but create one
        SearchListEntry newEntry;
        newEntry.searchID = searchID;
        m_fileLists.push_back(std::move(newEntry));
        entry = &m_fileLists.back();
    }

    // Look for existing parent with same hash
    SearchFile* parent = nullptr;
    for (auto& existing : entry->files) {
        if (existing->listParent() == nullptr &&
            md4equ(existing->fileHash(), fileOwner->fileHash()))
        {
            parent = existing.get();
            break;
        }
    }

    if (parent) {
        // --- Found existing parent with same hash ---

        // If parent has no children yet, create first child as copy of parent
        if (parent->listChildCount() == 0) {
            auto firstChild = std::make_unique<SearchFile>(parent);
            firstChild->setSearchID(searchID);
            firstChild->setListParent(parent);
            parent->addListChild(firstChild.get());
            entry->files.push_back(std::move(firstChild));
        }

        // Check if there's an existing child with the same filename
        SearchFile* matchingChild = nullptr;
        for (auto* child : parent->listChildren()) {
            if (child->fileName() == fileOwner->fileName()) {
                matchingChild = child;
                break;
            }
        }

        if (matchingChild) {
            // Merge sources into existing child
            matchingChild->addSources(fileOwner->sourceCount());
            matchingChild->addCompleteSources(fileOwner->completeSourceCount());

            // Merge client/server lists
            for (const auto& client : fileOwner->clients())
                matchingChild->addClient(client);
            for (const auto& server : fileOwner->servers())
                matchingChild->addServer(server);
        } else {
            // New child with different filename
            fileOwner->setListParent(parent);
            parent->addListChild(fileOwner.get());
            entry->files.push_back(std::move(fileOwner));
        }

        // Aggregate parent data: best source counts across all children
        uint32 bestSources = 0;
        uint32 bestComplete = 0;
        for (auto* child : parent->listChildren()) {
            bestSources = std::max(bestSources, child->sourceCount());
            bestComplete = std::max(bestComplete, child->completeSourceCount());
        }
        parent->addSources(bestSources);
        parent->addCompleteSources(bestComplete);

        // AICH consensus: if new file has AICH and parent doesn't, adopt it
        if (fileOwner && fileOwner->fileIdentifier().hasAICHHash() &&
            !parent->fileIdentifier().hasAICHHash())
        {
            parent->fileIdentifier().setAICHHash(fileOwner->fileIdentifier().getAICHHash());
        }

        // Update spam rating
        if (!clientResponse) {
            doSpamRating(parent, true, true,
                         fromUDPServerIP != 0, fromUDPServerIP);
        }

        // Update found sources count
        m_foundSourcesCount[searchID] += fileOwner ? fileOwner->sourceCount() : 0;

        emit resultUpdated(parent);

    } else {
        // --- No parent found — new top-level entry ---

        SearchFile* newFile = fileOwner.get();
        entry->files.push_back(std::move(fileOwner));

        // Update counters
        m_foundFilesCount[searchID]++;
        m_foundSourcesCount[searchID] += newFile->sourceCount();

        // Calculate spam rating
        if (!clientResponse) {
            doSpamRating(newFile, true, false,
                         fromUDPServerIP != 0, fromUDPServerIP);
        }

        emit resultAdded(newFile);
    }
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

SearchFile* SearchList::searchFileByHash(const uint8* hash, uint32 searchID) const
{
    const auto* entry = findEntry(searchID);
    if (!entry)
        return nullptr;

    for (const auto& file : entry->files) {
        if (file->listParent() == nullptr && md4equ(file->fileHash(), hash))
            return file.get();
    }
    return nullptr;
}

uint32 SearchList::resultCount(uint32 searchID) const
{
    const auto* entry = findEntry(searchID);
    if (!entry)
        return 0;

    uint32 count = 0;
    for (const auto& file : entry->files) {
        if (file->listParent() == nullptr)
            ++count;
    }
    return count;
}

uint32 SearchList::foundFiles(uint32 searchID) const
{
    auto it = m_foundFilesCount.find(searchID);
    return (it != m_foundFilesCount.end()) ? it->second : 0;
}

uint32 SearchList::foundSources(uint32 searchID) const
{
    auto it = m_foundSourcesCount.find(searchID);
    return (it != m_foundSourcesCount.end()) ? it->second : 0;
}

// ---------------------------------------------------------------------------
// Spam detection
// ---------------------------------------------------------------------------

void SearchList::doSpamRating(SearchFile* file,
                              bool countAsHit,
                              bool /*updateParent*/,
                              bool fromUDP,
                              uint32 fromUDPServerIP)
{
    if (!file)
        return;

    uint32 rating = 0;

    // --- Criterion 1: Hash hit (100 pts) ---
    MD4Key hashKey(file->fileHash());
    if (m_knownSpamHashes.contains(hashKey)) {
        if (m_knownSpamHashes[hashKey]) // true = spam
            rating += 100;
    }

    // --- Criterion 2: Filename match (80/50 pts) ---
    const QString& name = file->fileName();
    const QString& nameNoKw = file->nameWithoutKeywords();

    // Exact name match
    for (const auto& spamName : m_knownSpamNames) {
        if (name.compare(spamName, Qt::CaseInsensitive) == 0) {
            rating += 80;
            break;
        }
    }

    // Similar name match (Levenshtein distance)
    if (!nameNoKw.isEmpty()) {
        for (const auto& similarName : m_knownSimilarSpamNames) {
            uint32 dist = levenshteinDistance(nameNoKw, similarName);
            uint32 maxLen = std::max(static_cast<uint32>(nameNoKw.length()),
                                     static_cast<uint32>(similarName.length()));
            if (maxLen > 0) {
                double similarity = 1.0 - (static_cast<double>(dist) / maxLen);
                if (similarity >= 0.9) {
                    rating += 60;
                    break;
                } else if (similarity >= 0.8) {
                    rating += 50;
                    break;
                } else if (similarity >= 0.7) {
                    rating += 40;
                    break;
                }
            }
        }
    }

    // --- Criterion 3: Size similarity (10 pts) ---
    uint64 fSize = static_cast<uint64>(file->fileSize());
    for (uint64 spamSize : m_knownSpamSizes) {
        // Within 5% or 5MB
        uint64 threshold = std::max(spamSize / 20, uint64{5 * 1024 * 1024});
        uint64 diff = (fSize > spamSize) ? (fSize - spamSize) : (spamSize - fSize);
        if (diff <= threshold) {
            rating += 10;
            break;
        }
    }

    // --- Criterion 4: UDP server reputation (21/15/10 pts) ---
    if (fromUDP && fromUDPServerIP != 0) {
        auto udpIt = m_udpServerRecords.find(fromUDPServerIP);
        if (udpIt != m_udpServerRecords.end()) {
            const auto& rec = udpIt->second;
            if (rec.totalResults > 0) {
                double spamRatio = static_cast<double>(rec.spamResults) / rec.totalResults;
                if (rec.totalResults == 1 && rec.spamResults == 1)
                    rating += 21;
                else if (spamRatio > 0.6)
                    rating += 15;
                else if (spamRatio > 0.4)
                    rating += 10;
            }
        }
    }

    // --- Criterion 5: All-spam-servers (30 pts) ---
    if (!file->servers().empty()) {
        bool allSpam = true;
        for (const auto& server : file->servers()) {
            auto udpIt = m_udpServerRecords.find(server.ip);
            if (udpIt == m_udpServerRecords.end()) {
                allSpam = false;
                break;
            }
            const auto& rec = udpIt->second;
            if (rec.totalResults == 0 ||
                static_cast<double>(rec.spamResults) / rec.totalResults <= 0.6)
            {
                allSpam = false;
                break;
            }
        }
        if (allSpam && file->servers().size() > 1)
            rating += 30;
    }

    // --- Criterion 6: Source IP hit (39 pts) ---
    for (const auto& client : file->clients()) {
        if (m_knownSpamSourcesIPs.contains(client.ip)) {
            rating += 39;
            break;
        }
    }

    // --- Criterion 7: Heuristic — program/archive 100KB-10MB (39-60 pts) ---
    {
        const QString& ft = file->fileType();
        bool suspectType = (ft == QLatin1String(ED2KFTSTR_PROGRAM) ||
                            ft == QLatin1String(ED2KFTSTR_ARCHIVE));
        if (suspectType && fSize >= 100 * 1024 && fSize <= 10 * 1024 * 1024) {
            // Check for suspicious subnet patterns in source IPs
            std::unordered_map<uint32, int> subnetCounts;
            for (const auto& client : file->clients()) {
                // Use /24 subnet
                uint32 subnet = client.ip & 0xFFFFFF00u;
                subnetCounts[subnet]++;
            }
            bool suspicious = false;
            for (const auto& [subnet, count] : subnetCounts) {
                if (count >= 3) {
                    suspicious = true;
                    break;
                }
            }
            if (suspicious)
                rating += 39;
            else if (file->clients().size() == 1)
                rating += 45;
        }
    }

    file->setSpamRating(rating);

    // Update UDP server spam tracking
    if (countAsHit && fromUDP && fromUDPServerIP != 0) {
        auto& rec = m_udpServerRecords[fromUDPServerIP];
        if (file->isConsideredSpam())
            rec.spamResults++;
    }
}

void SearchList::markFileAsSpam(SearchFile* file, bool addToFilter)
{
    if (!file)
        return;

    file->setSpamRating(SEARCH_SPAM_THRESHOLD); // ensure it's marked

    if (addToFilter) {
        // Add hash
        MD4Key hashKey(file->fileHash());
        m_knownSpamHashes[hashKey] = true;

        // Add filename
        if (!file->fileName().isEmpty()) {
            if (!m_knownSpamNames.contains(file->fileName(), Qt::CaseInsensitive))
                m_knownSpamNames.append(file->fileName());
        }

        // Add name-without-keywords for similarity matching
        if (!file->nameWithoutKeywords().isEmpty()) {
            if (!m_knownSimilarSpamNames.contains(file->nameWithoutKeywords(), Qt::CaseInsensitive))
                m_knownSimilarSpamNames.append(file->nameWithoutKeywords());
        }

        // Add file size
        uint64 fSize = static_cast<uint64>(file->fileSize());
        if (fSize > 0) {
            if (std::find(m_knownSpamSizes.begin(), m_knownSpamSizes.end(), fSize) ==
                m_knownSpamSizes.end())
            {
                m_knownSpamSizes.push_back(fSize);
            }
        }

        // Add source IPs
        for (const auto& client : file->clients())
            m_knownSpamSourcesIPs[client.ip] = true;
    }

    emit spamStatusChanged(file);
}

void SearchList::markFileAsNotSpam(SearchFile* file, bool removeFromFilter)
{
    if (!file)
        return;

    file->setSpamRating(0);

    if (removeFromFilter) {
        // Remove hash from spam list (mark as not-spam)
        MD4Key hashKey(file->fileHash());
        m_knownSpamHashes[hashKey] = false;

        // Remove filename from spam lists
        m_knownSpamNames.removeAll(file->fileName());
        m_knownSimilarSpamNames.removeAll(file->nameWithoutKeywords());

        // Remove file size
        uint64 fSize = static_cast<uint64>(file->fileSize());
        std::erase(m_knownSpamSizes, fSize);

        // Remove source IPs
        for (const auto& client : file->clients())
            m_knownSpamSourcesIPs.erase(client.ip);
    }

    emit spamStatusChanged(file);
}

void SearchList::recalculateSpamRatings(uint32 searchID)
{
    auto* entry = findEntry(searchID);
    if (!entry)
        return;

    for (auto& file : entry->files) {
        if (file->listParent() == nullptr) {
            doSpamRating(file.get(), false, false, false, 0);

            bool wasSpam = file->isConsideredSpam();
            if (wasSpam != file->isConsideredSpam())
                emit spamStatusChanged(file.get());
        }
    }
}

// ---------------------------------------------------------------------------
// Persistence — search sessions
// ---------------------------------------------------------------------------

void SearchList::storeSearches(const QString& configDir) const
{
    const QString filePath = configDir + QStringLiteral("/searches.met");
    SafeFile file;
    if (!file.open(filePath, QIODevice::WriteOnly))
        return;

    file.writeUInt8(MET_HEADER_I64TAGS);
    file.writeUInt8(1); // version

    // Count non-empty search entries
    uint32 count = 0;
    for (const auto& entry : m_fileLists) {
        if (!entry.files.empty())
            ++count;
    }
    file.writeUInt32(count);

    for (const auto& entry : m_fileLists) {
        if (entry.files.empty())
            continue;

        // Write search params placeholder (we store searchID + file count)
        file.writeUInt32(entry.searchID);

        // Count top-level files (non-children)
        uint32 fileCount = 0;
        for (const auto& f : entry.files) {
            if (f->listParent() == nullptr)
                ++fileCount;
        }
        file.writeUInt32(fileCount);

        // Write each top-level file
        for (const auto& f : entry.files) {
            if (f->listParent() == nullptr)
                f->storeToFile(file);
        }
    }
}

void SearchList::loadSearches(const QString& configDir)
{
    const QString filePath = configDir + QStringLiteral("/searches.met");
    SafeFile file;
    if (!file.open(filePath, QIODevice::ReadOnly))
        return;

    const uint8 header = file.readUInt8();
    if (header != MET_HEADER_I64TAGS)
        return;

    const uint8 version = file.readUInt8();
    if (version != 1)
        return;

    const uint32 searchCount = file.readUInt32();
    for (uint32 s = 0; s < searchCount; ++s) {
        const uint32 searchID = file.readUInt32();
        const uint32 fileCount = file.readUInt32();

        SearchListEntry entry;
        entry.searchID = searchID;

        for (uint32 i = 0; i < fileCount; ++i) {
            auto searchFile = std::make_unique<SearchFile>(file, true);
            searchFile->setSearchID(searchID);
            entry.files.push_back(std::move(searchFile));
        }

        m_fileLists.push_back(std::move(entry));
        m_foundFilesCount[searchID] = fileCount;

        // Update next search ID to be beyond any loaded
        if (searchID >= m_nextSearchID)
            m_nextSearchID = searchID + 1;
    }
}

// ---------------------------------------------------------------------------
// Persistence — spam filter
// ---------------------------------------------------------------------------

void SearchList::saveSpamFilter(const QString& configDir) const
{
    const QString filePath = configDir + QStringLiteral("/searchspam.met");
    SafeFile file;
    if (!file.open(filePath, QIODevice::WriteOnly))
        return;

    file.writeUInt8(MET_HEADER_I64TAGS);
    file.writeUInt8(1); // version

    // Count total entries
    uint32 totalEntries = static_cast<uint32>(
        m_knownSpamHashes.size()
        + m_knownSpamSourcesIPs.size()
        + static_cast<std::size_t>(m_knownSpamNames.size())
        + static_cast<std::size_t>(m_knownSimilarSpamNames.size())
        + m_knownSpamSizes.size());
    file.writeUInt32(totalEntries);

    // Write spam hashes
    for (const auto& [key, isSpam] : m_knownSpamHashes) {
        Tag tagType(static_cast<uint8>(isSpam ? SP_FILEHASHSPAM : SP_FILEHASHNOSPAM),
                    key.data.data());
        tagType.writeNewEd2kTag(file);
    }

    // Write source IPs
    for (const auto& [ip, _] : m_knownSpamSourcesIPs) {
        Tag tag(static_cast<uint8>(SP_FILESOURCEIP), ip);
        tag.writeNewEd2kTag(file);
    }

    // Write spam names
    for (const auto& name : m_knownSpamNames) {
        Tag tag(static_cast<uint8>(SP_FILEFULLNAME), name);
        tag.writeNewEd2kTag(file, UTF8Mode::Raw);
    }

    // Write similar spam names
    for (const auto& name : m_knownSimilarSpamNames) {
        Tag tag(static_cast<uint8>(SP_FILESIMILARNAME), name);
        tag.writeNewEd2kTag(file, UTF8Mode::Raw);
    }

    // Write spam sizes
    for (uint64 size : m_knownSpamSizes) {
        Tag tag(static_cast<uint8>(SP_FILESIZE), size);
        tag.writeNewEd2kTag(file);
    }
}

void SearchList::loadSpamFilter(const QString& configDir)
{
    const QString filePath = configDir + QStringLiteral("/searchspam.met");
    SafeFile file;
    if (!file.open(filePath, QIODevice::ReadOnly))
        return;

    const uint8 header = file.readUInt8();
    if (header != MET_HEADER_I64TAGS)
        return;

    const uint8 version = file.readUInt8();
    if (version != 1)
        return;

    const uint32 entryCount = file.readUInt32();
    for (uint32 i = 0; i < entryCount; ++i) {
        Tag tag(file, false);

        switch (tag.nameId()) {
        case SP_FILEHASHSPAM:
            if (tag.isHash()) {
                MD4Key key(tag.hashValue());
                m_knownSpamHashes[key] = true;
            }
            break;

        case SP_FILEHASHNOSPAM:
            if (tag.isHash()) {
                MD4Key key(tag.hashValue());
                m_knownSpamHashes[key] = false;
            }
            break;

        case SP_FILESOURCEIP:
            if (tag.isInt())
                m_knownSpamSourcesIPs[tag.intValue()] = true;
            break;

        case SP_FILEFULLNAME:
            if (tag.isStr())
                m_knownSpamNames.append(tag.strValue());
            break;

        case SP_FILESIMILARNAME:
            if (tag.isStr())
                m_knownSimilarSpamNames.append(tag.strValue());
            break;

        case SP_FILESIZE:
            if (tag.isInt())
                m_knownSpamSizes.push_back(tag.intValue());
            else if (tag.isInt64(false))
                m_knownSpamSizes.push_back(tag.int64Value());
            break;

        default:
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

SearchListEntry* SearchList::findEntry(uint32 searchID)
{
    for (auto& entry : m_fileLists) {
        if (entry.searchID == searchID)
            return &entry;
    }
    return nullptr;
}

const SearchListEntry* SearchList::findEntry(uint32 searchID) const
{
    for (const auto& entry : m_fileLists) {
        if (entry.searchID == searchID)
            return &entry;
    }
    return nullptr;
}

QString SearchList::computeNameWithoutKeywords(const QString& name, const QString& /*fileType*/)
{
    // Strip file extension
    QString result = name;
    auto dotPos = result.lastIndexOf(u'.');
    if (dotPos > 0)
        result = result.left(dotPos);

    // Strip common bracket content: [xxx], (xxx), {xxx}
    static const QRegularExpression bracketRe(QStringLiteral(R"([\[\(\{][^\]\)\}]*[\]\)\}])"));
    result.remove(bracketRe);

    // Replace common separators with spaces
    result.replace(u'_', u' ');
    result.replace(u'-', u' ');
    result.replace(u'.', u' ');

    // Normalize whitespace
    result = result.simplified().toLower();

    return result;
}

} // namespace eMule
