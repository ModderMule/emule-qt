#include "pch.h"
/// @file SearchList.cpp
/// @brief Search result manager — port of MFC CSearchList.

#include "search/SearchList.h"
#include "search/SearchStarter.h"
#include "app/AppContext.h"
#include "kademlia/KadSearch.h"
#include "search/SeenFileIndex.h"
#include "client/UpDownClient.h"
#include "crypto/AICHData.h"
#include "protocol/Tag.h"
#include "utils/SafeFile.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QRegularExpression>


namespace eMule {

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

SearchList::SearchList(QObject* parent)
    : QObject(parent)
    , m_queue(new SearchQueue(defaultSearchQueueBackend(*this), this))
{
    connect(m_queue, &SearchQueue::stateChanged, this, &SearchList::searchStateChanged);
}

SearchList::~SearchList() = default;

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------

uint32 SearchList::newSearch(const QString& resultFileType, const SearchParams& params,
                             uint32 forcedID, bool takeEd2kRouting)
{
    const bool ed2k = params.type == SearchType::Ed2kServer || params.type == SearchType::Ed2kGlobal;
    if (!ed2k || takeEd2kRouting)
        m_resultFileType = resultFileType;
    m_currentSearchID = takeSearchID(forcedID);

    // Only an ED2K search takes over the server-answer routing. A Kad search started
    // while a global sweep is still walking the server list must not steal the UDP
    // answers still arriving for it, nor wipe the set of servers we asked — without
    // that set those answers are dropped as unsolicited.
    // MFC: CSearchList::NewSearch — srchybrid/SearchList.cpp:152-156.
    if (ed2k && takeEd2kRouting) {
        m_currentEd2kSearchID = m_currentSearchID;
        m_curED2KSentRequestsIPs.clear();
        m_udpServerRecords.clear();
    }

    SearchListEntry entry;
    entry.searchID = m_currentSearchID;
    m_fileLists.push_back(std::move(entry));

    m_foundFilesCount[m_currentSearchID] = 0;
    m_foundSourcesCount[m_currentSearchID] = 0;

    return m_currentSearchID;
}

void SearchList::noteSeen(SearchFile& file, const SearchListEntry& entry, bool newToSearch)
{
    SeenFileIndex* index = theApp.seenFileIndex;
    // A meta row (torrent / Usenet) has no eD2K hash to remember it by.
    if (!index || !index->isActive() || file.isMetaResult())
        return;

    if (newToSearch) {
        // Before this sighting is added, so a file met for the first time is not
        // already "seen"; and relative to the search, so another result of the same
        // search cannot make it so either.
        const SeenFileIndex::Info known = index->lookup(file.fileHash());
        file.setSeen(known.seenBefore(entry.startedAt), known.names, known.firstSeen);
    }
    index->note(file.fileHash(), file.fileName(), static_cast<uint64>(file.fileSize()),
                QDateTime::currentSecsSinceEpoch());
}

uint32 SearchList::takeSearchID(uint32 forcedID)
{
    // One counter for every search of the client (see kad::Search::reserveSearchID):
    // with a counter of its own this list handed out ids a Kad lookup already had,
    // and stopping the one stopped the other.
    if (forcedID == 0)
        return kad::Search::reserveSearchID();
    kad::Search::noteSearchIDUsed(forcedID);
    return forcedID;
}

uint32 SearchList::reserveSearch(uint32 forcedID)
{
    const uint32 searchID = takeSearchID(forcedID);

    SearchListEntry entry;
    entry.searchID = searchID;
    m_fileLists.push_back(std::move(entry));
    m_foundFilesCount[searchID] = 0;
    m_foundSourcesCount[searchID] = 0;
    return searchID;
}

void SearchList::beginSearch(uint32 searchID, const QString& resultFileType, bool ed2k)
{
    m_resultFileType = resultFileType;
    m_currentSearchID = searchID;
    if (ed2k) {
        // As in newSearch(): a new ED2K search owns the server answers from here on.
        m_currentEd2kSearchID = searchID;
        m_curED2KSentRequestsIPs.clear();
        m_udpServerRecords.clear();
    }
}

void SearchList::releaseEd2kRouting(uint32 searchID)
{
    if (searchID != 0 && m_currentEd2kSearchID == searchID)
        m_currentEd2kSearchID = 0;
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
                                      uint32 completeSources,
                                      const std::vector<Tag>& metaTags,
                                      uint32 fromIP)
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
    entry->kad = true;

    // Build a SearchFile from the Kad result data
    auto* file = new SearchFile();
    // Before the counts: Kad takes the max of them, eD2K the sum
    file->setKadResult(true);
    file->setFileHash(fileHash);
    if (!name.isEmpty())
        file->setFileName(name, true);
    file->setFileSize(size);
    if (!type.isEmpty())
        file->setFileType(type);
    file->addSources(sources);
    file->addCompleteSources(completeSources);
    file->setSearchID(searchID);
    // Attach the imported Kad media/format metadata so the result carries the
    // same bitrate/length/codec/artist/album tags as an ED2K hit — the IPC
    // serializer already reads these from the file's tag list.
    // Publish info and the AICH votes are properties of the result, never tags: a tag
    // would travel into the download and get published (MFC Search.cpp:1112-1114).
    QByteArray aichVotes;
    for (const auto& tag : metaTags) {
        // A Kad tag names itself by one-byte string or by id, depending on the reader
        const QByteArray key = tag.name().isEmpty() && tag.nameId() != 0
            ? QByteArray(1, static_cast<char>(tag.nameId())) : tag.name();
        if (key == QByteArrayLiteral(TAG_PUBLISHINFO)) {
            if (tag.isInt())
                file->setKadPublishInfo(tag.intValue());
        } else if (key == QByteArrayLiteral(TAG_KADAICHHASHRESULT)) {
            if (tag.isBsob())
                aichVotes = tag.blobValue();
        } else {
            file->addTagUnique(tag);
        }
    }
    if (AICHHash aichHash; acceptedKadAICHHash(aichVotes, file->kadPublishInfo(), aichHash)) {
        file->fileIdentifier().setAICHHash(aichHash);
        // The popularity in the blob is the node's own figure; the node is the voter.
        if (fromIP != 0)
            file->addAICHVoter(Address::fromHostOrder(fromIP));
    }

    addToList(file, false, 0);
    emit tabHeaderUpdated(searchID);
}

// ---------------------------------------------------------------------------
// Result processing — TCP
// ---------------------------------------------------------------------------

bool SearchList::processSearchAnswer(const uint8* packet, uint32 size,
                                     bool optUTF8, const Endpoint& server)
{
    // SearchFile keeps the ed2k (IPv4) form; an IPv6 server records 0 there.
    // The search this answers is over (or was closed): the results are nobody's.
    if (m_currentEd2kSearchID == 0) {
        logServerVerbose(QStringLiteral("TCP search answer from %1 dropped — no server "
                                        "search is waiting for one").arg(server.toString()));
        return false;
    }

    const uint32 serverIP = server.address().toNetworkUint32();
    const uint16 serverPort = server.port();
    SafeMemFile data(packet, size);

    // A broken record ends the answer, not the server session: what was read so far
    // stays (MFC ServerSocket.cpp:586-600 keeps the connection for OP_SEARCHRESULT).
    uint32 resultCount = 0;
    uint32 parsed = 0;
    bool moreResults = false;
    try {
        resultCount = data.readUInt32();
        for (; parsed < resultCount; ++parsed) {
            auto* file = new SearchFile(data, optUTF8, serverIP, serverPort);
            file->setSearchID(m_currentEd2kSearchID);
            addToList(file, false, 0);
        }

        // Check for trailing "more results" flag. When set, the server capped the
        // result set — surface it to the user so they can narrow the query (#19). MFC
        // routes the same flag to the search window (ServerSocket.cpp:360-363).
        if (data.position() < data.length())
            moreResults = data.readUInt8() != 0;
    } catch (const FileException& ex) {
        logWarning(QStringLiteral("Malformed search answer from %1 — kept %2 of %3 result(s): %4")
                       .arg(server.toString()).arg(parsed).arg(resultCount)
                       .arg(QString::fromUtf8(ex.what())));
    }
    if (moreResults) {
        logInfo(QStringLiteral("Server returned only part of the matches — more results "
                               "are available; narrow your search to see them."));
    }

    logServerVerbose(QStringLiteral("TCP search answer from %1 — parsed %2 result(s), moreResults=%3")
                         .arg(server.toString()).arg(parsed).arg(moreResults));

    emit tabHeaderUpdated(m_currentEd2kSearchID);
    return moreResults;
}

// ---------------------------------------------------------------------------
// Result processing — a peer's shared file list
// ---------------------------------------------------------------------------

uint32 SearchList::processClientSharedFiles(UpDownClient& sender, const uint8* packet,
                                            uint32 size, const QString& directory)
{
    // MFC SearchList.cpp:182-220. One tab per peer, reused across answers; a closed
    // tab (entry removed) gets a fresh ID so the GUI opens a new one.
    uint32 searchID = sender.searchID();
    if (searchID == 0 || !findEntry(searchID)) {
        searchID = takeSearchID(0);
        sender.setSearchID(searchID);

        SearchListEntry newEntry;
        newEntry.searchID = searchID;
        newEntry.title = sender.userName();
        newEntry.clientSharedFiles = true;
        m_fileLists.push_back(std::move(newEntry));
        m_foundFilesCount[searchID] = 0;
        m_foundSourcesCount[searchID] = 0;
    }

    const uint32 senderIP = sender.userAddress().toNetworkUint32();
    const uint32 serverIP = sender.serverAddress().toNetworkUint32();

    SafeMemFile data(packet, size);
    try {
        for (uint32 results = size >= 4 ? data.readUInt32() : 0; results > 0; --results) {
            auto* file = new SearchFile(data, sender.unicodeSupport(), serverIP,
                                        sender.serverPort(), directory);
            if (file->isLargeFile() && !sender.supportsLargeFiles()) {
                logDebug(QStringLiteral("Client offers large file (%1) but did not announce support for it - ignoring file")
                             .arg(file->fileName()));
                delete file;
                continue;
            }
            // The peer itself is the source, whatever ID it wrote into the record.
            if (senderIP != 0 && sender.userPort() != 0)
                file->addClient({senderIP, sender.userPort(), serverIP, sender.serverPort()});
            // MFC SearchList.cpp:218 offers this for videos; this client also answers
            // for images.
            const ED2KFileType type = getED2KFileTypeID(file->fileName());
            file->setPreviewPossible(sender.supportsPreview()
                                     && (type == ED2KFileType::Video || type == ED2KFileType::Image));
            file->setSearchID(searchID);
            addToList(file, true);
        }
    } catch (const FileException& ex) {
        // The files read so far stay listed; the peer keeps its connection.
        logWarning(QStringLiteral("Malformed shared file list from %1: %2")
                       .arg(sender.userName(), QString::fromUtf8(ex.what())));
    }

    emit tabHeaderUpdated(searchID);
    return searchID;
}

// ---------------------------------------------------------------------------
// Result processing — UDP (single result per packet)
// ---------------------------------------------------------------------------

void SearchList::processUDPSearchAnswer(const uint8* packet, uint32 size,
                                        bool optUTF8, const Endpoint& server)
{
    if (m_currentEd2kSearchID == 0)
        return;   // the sweep this answers is over

    // Validate server was in our request list
    if (!m_curED2KSentRequestsIPs.contains(server.address())) {
        logServerVerbose(QStringLiteral("UDP search answer from %1 DROPPED — sender not in our sent-request set")
                             .arg(server.toString()));
        return;
    }

    // SearchFile and the spam records keep the ed2k (IPv4) form; 0 for an IPv6 server.
    const uint32 serverIP = server.address().toNetworkUint32();
    const uint16 serverPort = server.port();

    SafeMemFile data(packet, size);
    uint32 parsedResults = 0;

    // A single UDP datagram can contain multiple concatenated search results,
    // each separated by an OP_EDONKEYPROT + OP_GLOBSEARCHRES header.
    // (Matches MFC eMule: srchybrid/UDPSocket.cpp do-while loop)
    do {
        auto* file = new SearchFile(data, optUTF8, serverIP, serverPort);
        file->setSearchID(m_currentEd2kSearchID);

        auto& record = m_udpServerRecords[serverIP];
        record.totalResults++;
        ++parsedResults;

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

    logServerVerbose(QStringLiteral("UDP search answer from %1 — parsed %2 result(s)")
                         .arg(server.toString()).arg(parsedResults));

    emit tabHeaderUpdated(m_currentEd2kSearchID);
}

// ---------------------------------------------------------------------------
// Core: addToList — deduplication, parent/child grouping
// ---------------------------------------------------------------------------

void SearchList::addToList(SearchFile* rawFile, bool clientResponse,
                           uint32 fromUDPServerIP)
{
    std::unique_ptr<SearchFile> fileOwner(rawFile);

    // eNode meta row whose tags contradict its hash (plan §8.1)
    if (fileOwner->isInvalidMetaResult())
        return;

    // Apply file type filter; a peer's list is shown whole (MFC SearchList.cpp:406)
    if (!clientResponse && !m_resultFileType.isEmpty() && !fileOwner->fileType().isEmpty()) {
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

        // A file is Kad-origin only while every answer for it says so. One server that
        // has the file itself makes it a plain server result, in either arrival order.
        if (!parent->isKadOrigin() || !fileOwner->isKadOrigin()) {
            clearKadOrigin(fileOwner.get());
            clearKadOrigin(parent);
            for (auto* child : parent->listChildren())
                clearKadOrigin(child);
        }

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

        const uint32 addedSources = fileOwner->sourceCount();

        if (matchingChild) {
            // Two answers for one name disagreeing on the AICH root: trust neither, and
            // remember it so a third cannot bring one back (MFC SearchList.cpp:489-503).
            if (fileOwner->fileIdentifier().hasAICHHash()) {
                FileIdentifier& childId = matchingChild->fileIdentifier();
                if (childId.hasAICHHash()) {
                    if (childId.getAICHHash() != fileOwner->fileIdentifier().getAICHHash()) {
                        matchingChild->setFoundMultipleAICH();
                        childId.clearAICHHash();
                        matchingChild->clearAICHVoters();
                    }
                } else if (!matchingChild->hasFoundMultipleAICH()) {
                    childId.setAICHHash(fileOwner->fileIdentifier().getAICHHash());
                }
                // Who stands behind the root the child now has.
                if (childId.hasAICHHash()) {
                    for (const Address& voter : fileOwner->aichVoters())
                        matchingChild->addAICHVoter(voter);
                    if (!fileOwner->isKadResult() || fileOwner->isAICHVouchedDirectly())
                        matchingChild->setAICHVouchedDirectly();
                }
            }

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
            noteSeen(*fileOwner, *entry, /*newToSearch*/ false);
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

        // The parent carries a root only when every child that has one agrees on it
        // (MFC SearchList.cpp:548-600). It is seeded as Verified by a download.
        const AICHHash* agreed = nullptr;
        bool conflict = false;
        for (const auto* child : parent->listChildren()) {
            const FileIdentifier& childId = child->fileIdentifier();
            if (childId.hasAICHHash()) {
                if (!agreed)
                    agreed = &childId.getAICHHash();
                else if (*agreed != childId.getAICHHash())
                    conflict = true;
            } else if (child->hasFoundMultipleAICH()) {
                conflict = true;
            }
        }
        if (agreed && !conflict)
            parent->fileIdentifier().setAICHHash(*agreed);
        else
            parent->fileIdentifier().clearAICHHash();

        // Update spam rating
        if (!clientResponse) {
            doSpamRating(parent, true, true,
                         fromUDPServerIP != 0, fromUDPServerIP);
        }

        // Update found sources count
        m_foundSourcesCount[searchID] += addedSources;

        emit resultUpdated(parent);

    } else {
        // --- No parent found — new top-level entry ---

        SearchFile* newFile = fileOwner.get();
        noteSeen(*newFile, *entry, /*newToSearch*/ true);
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

// ---------------------------------------------------------------------------
// Kad notes
// ---------------------------------------------------------------------------

bool SearchList::addNotes(const uint8* fileHash, const QByteArray& publisherId,
                          uint8 rating, const QString& comment)
{
    if (!fileHash)
        return false;

    // Every list, not just the active tab: the user may have several searches
    // open and the same file can appear in more than one of them.
    bool added = false;
    for (const auto& entry : m_fileLists) {
        for (const auto& file : entry.files) {
            if (!md4equ(file->fileHash(), fileHash))
                continue;
            file->addKadNote(publisherId, rating, comment);
            added = true;
            emit resultUpdated(file.get());
        }
    }
    return added;
}

void SearchList::setNotesSearchStatus(const uint8* fileHash, bool running)
{
    if (!fileHash)
        return;

    for (const auto& entry : m_fileLists) {
        for (const auto& file : entry.files) {
            if (!md4equ(file->fileHash(), fileHash))
                continue;
            file->setKadCommentSearchRunning(running);
            emit resultUpdated(file.get());
        }
    }
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
    file.writeUInt8(2); // version; 2 adds the per-search Kad byte

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
        file.writeUInt8(entry.kad ? 1 : 0);

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
    if (version != 1 && version != 2)
        return;

    const uint32 searchCount = file.readUInt32();
    for (uint32 s = 0; s < searchCount; ++s) {
        const uint32 searchID = file.readUInt32();
        // v1 had no search type: its Kad results come back as eD2K ones
        const bool kad = version >= 2 && file.readUInt8() != 0;
        const uint32 fileCount = file.readUInt32();

        SearchListEntry entry;
        entry.searchID = searchID;
        entry.kad = kad;

        for (uint32 i = 0; i < fileCount; ++i) {
            auto searchFile = std::make_unique<SearchFile>(file, true, 0, 0, QString(), kad);
            searchFile->setSearchID(searchID);
            entry.files.push_back(std::move(searchFile));
        }

        m_fileLists.push_back(std::move(entry));
        m_foundFilesCount[searchID] = fileCount;

        // Update next search ID to be beyond any loaded
        kad::Search::noteSearchIDUsed(searchID);
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

// ---------------------------------------------------------------------------
// clearKadOrigin — private; turn a Kad-origin row into a plain server result
// ---------------------------------------------------------------------------

void SearchList::clearKadOrigin(SearchFile* file)
{
    if (!file->isKadOrigin())
        return;
    file->setKadOrigin(false);
    // The tag too, or a stored search would restore the flag from it
    file->deleteTag(FT_META_NETWORK);
}

// ---------------------------------------------------------------------------
// acceptedKadAICHHash — private; the one AICH hash a Kad result may carry
// ---------------------------------------------------------------------------

bool SearchList::acceptedKadAICHHash(const QByteArray& votes, uint32 publishInfo, AICHHash& out)
{
    // TAG_KADAICHHASHRESULT: count, then (popularity, hash[20]) each — MFC Search.cpp:1129-1143.
    // A short blob is a corrupt one: nothing from it is used.
    if (votes.isEmpty())
        return false;
    const auto* p = reinterpret_cast<const uint8*>(votes.constData());
    const qsizetype entrySize = 1 + static_cast<qsizetype>(kAICHHashSize);
    const uint8 count = p[0];
    if (votes.size() < 1 + count * entrySize)
        return false;

    uint32 found = 0;
    uint8 popularity = 0;
    for (uint8 i = 0; i < count; ++i) {
        const uint8* e = p + 1 + i * entrySize;
        if (e[0] == 0)
            continue;
        ++found;
        popularity = e[0];
        out = AICHHash(e + 1);
    }

    // Exactly one hash, reported by more than a third of the publishers, is as good as
    // a link's. Several, or a rare one, are ignored: a wrong AICH hash breaks recovery,
    // and MD4 alone still downloads the file (MFC SearchList.cpp:784-803).
    const uint8 publishers = static_cast<uint8>((publishInfo >> 16) & 0xFF);
    return found == 1 && publishers > 0 && publishers / popularity <= 3;
}

} // namespace eMule
