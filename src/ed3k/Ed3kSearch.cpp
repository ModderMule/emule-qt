/// @file Ed3kSearch.cpp
/// @brief ed3k keyword search for the search queue — implementation.

#include "Ed3kSearch.h"

#include "Ed3kSession.h"

#include "app/AppContext.h"
#include "protocol/Tag.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "search/SearchQueue.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <algorithm>

namespace eMule::ed3k {

namespace {

QByteArray bytesOf(std::span<const std::uint8_t> b)
{
    return QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size()));
}

} // namespace

Ed3kSearch::Ed3kSearch(Ed3kSession& session, QObject* parent)
    : QObject(parent)
    , m_session(session)
{
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kSearch::onEventPolled, Qt::DirectConnection);
}

Ed3kSearch::~Ed3kSearch() = default;

void Ed3kSearch::setSinks(ResultSink results, FinishSink finished)
{
    m_results = std::move(results);
    m_finished = std::move(finished);
}

bool Ed3kSearch::isRunning(uint32 searchID) const
{
    return m_runs.contains(searchID);
}

std::unique_ptr<SearchFile> Ed3kSearch::toSearchFile(const ::ed3k::FoundFile& found)
{
    if (found.size == 0 || found.name.empty())
        return nullptr;
    auto file = std::make_unique<SearchFile>();
    file->setFileName(QString::fromUtf8(found.name.data(), qsizetype(found.name.size())), false, true, true);
    file->setFileSize(EMFileSize(found.size));
    // the list keys rows by a 16-byte hash: the root's first half (never an MD4 of eD2K)
    file->setFileHash(found.root.bytes.data());
    file->setSourceCount(found.sources);
    // partial: sources that hold part of it; the rest are complete
    file->setCompleteSourceCount(found.sources >= found.partial ? found.sources - found.partial : 0);
    if (found.duration != 0)
        file->addTagUnique(Tag(FT_MEDIA_LENGTH, uint32(found.duration)));
    if (found.bitrate != 0)
        file->addTagUnique(Tag(FT_MEDIA_BITRATE, uint32(found.bitrate)));
    file->setEd3k(bytesOf(found.root.bytes), found.ed2k ? bytesOf(*found.ed2k) : QByteArray());
    // AUTHOR of filter lists: the record's publisher (filter-list.md §1)
    file->setEd3kPublisher(bytesOf(found.publisher.bytes));
    return file;
}

bool Ed3kSearch::applyVerdict(SearchFile& file, const Verdict& v)
{
    SearchFile::Ed3kJudged shown = v.shown;
    shown.hidden = v.judged == Judged::Hide;
    const bool wasMarked = file.ed3kJudged().hidden || file.ed3kJudged().verdict != 0;
    const bool changed = !(file.ed3kJudged() == shown);
    file.setEd3kJudged(shown);
    // a marked or hidden row sorts as spam; a row no list judges any more is spam no longer
    if (v.judged != Judged::None)
        file.setSpamRating(SEARCH_SPAM_THRESHOLD);
    else if (wasMarked && changed)
        file.setSpamRating(0);
    return changed;
}

int Ed3kSearch::rejudge()
{
    if (!m_judge || !theApp.searchList)
        return 0;
    int changed = 0;
    theApp.searchList->updateResults([&](SearchFile* f) {
        if (!f->isEd3kResult())
            return false;
        ::ed3k::FoundFile found;
        std::copy_n(reinterpret_cast<const std::uint8_t*>(f->ed3kRoot().constData()), found.root.bytes.size(), found.root.bytes.begin());
        if (f->ed3kEd2k().size() == 16) {
            found.ed2k.emplace();
            std::copy_n(reinterpret_cast<const std::uint8_t*>(f->ed3kEd2k().constData()), 16, found.ed2k->begin());
        }
        if (f->ed3kPublisher().size() == qsizetype(found.publisher.bytes.size()))
            std::copy_n(reinterpret_cast<const std::uint8_t*>(f->ed3kPublisher().constData()), found.publisher.bytes.size(),
                        found.publisher.bytes.begin());
        found.size = std::uint64_t(f->fileSize());
        found.name = f->fileName().toStdString();
        if (!applyVerdict(*f, m_judge(found)))
            return false;
        ++changed;
        return true;
    });
    return changed;
}

::ed3k::Filter Ed3kSearch::filterOf(const SearchParams& params)
{
    ::ed3k::Filter f;
    QString ext = params.extension.trimmed();
    if (ext.startsWith(QLatin1Char('.')))
        ext.remove(0, 1);
    f.ext = ext.toUtf8().toStdString();
    f.minSize = params.minSize;
    f.maxSize = params.maxSize;
    f.minSources = params.availability;
    return f;
}

void Ed3kSearch::startMetaSearch(uint32 searchID, SearchType type, const SearchParams& params,
                                 const enodemeta::pb::SearchRequest& request, std::vector<MetaSearchCandidate> candidates)
{
    Q_UNUSED(type) Q_UNUSED(request) Q_UNUSED(candidates)
    auto* e = m_session.engine();
    if (!e) {
        finishLater(searchID, tr("ed3k is not running."));
        return;
    }
    const QByteArray query = params.expression.trimmed().toUtf8();
    auto lookup = e->search(m_session.now(), std::string_view(query.constData(), std::size_t(query.size())), filterOf(params));
    if (!lookup) {
        logEd3kDebug(QStringLiteral("ed3k search %1 refused (error %2)").arg(searchID).arg(int(lookup.error())));
        finishLater(searchID, tr("ed3k refused the search (no keyword of at least 3 characters?)."));
        return;
    }
    Run run;
    run.lookup = *lookup;
    run.timeout = std::make_unique<QTimer>();
    run.timeout->setSingleShot(true);
    connect(run.timeout.get(), &QTimer::timeout, this, [this, searchID] { finish(searchID, {}); });
    run.timeout->start(kTimeoutMs);
    m_lookups[*lookup] = searchID;
    m_runs[searchID] = std::move(run);
    logEd3kDebug(QStringLiteral(">>> ed3k search %1: \"%2\" (lookup %3)").arg(searchID).arg(params.expression).arg(*lookup));
    // from outside the pump: send it now
    QTimer::singleShot(0, this, [this] {
        if (m_session.isRunning())
            m_session.pump();
    });
}

void Ed3kSearch::continueMetaSearch(uint32 searchID)
{
    finishLater(searchID, {});
}

void Ed3kSearch::cancelMetaSearch(uint32 searchID)
{
    const auto it = m_runs.find(searchID);
    if (it == m_runs.end())
        return;
    m_lookups.erase(it->second.lookup);
    m_runs.erase(it);
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kSearch::onEventPolled(const ::ed3k::Event& event, bool* handled)
{
    Q_UNUSED(handled)   // KadFound needs no answer; others' lookups stay theirs
    const auto* found = std::get_if<::ed3k::event::KadFound>(&event);
    if (!found)
        return;
    const auto it = m_lookups.find(found->lookup);
    if (it == m_lookups.end())
        return;
    const uint32 searchID = it->second;
    int added = 0;
    for (const auto& f : found->files) {
        // filter lists (batch 12, G): marked rows shown as spam, hidden ones kept hidden
        // was: hidden rows were dropped (if (judged == Judged::Hide) continue;)
        const Verdict judged = m_judge ? m_judge(f) : Verdict{};
        if (auto row = toSearchFile(f)) {
            (void)applyVerdict(*row, judged);
            addResult(searchID, std::move(row));
            ++added;
        }
    }
    logEd3kDebug(QStringLiteral("<<< ed3k search %1: %2 file(s) from %3 node(s)").arg(searchID).arg(added).arg(found->nodes));
    // the lookup is over; reported from the event loop, not inside the pump
    m_lookups.erase(it);
    if (const auto run = m_runs.find(searchID); run != m_runs.end())
        run->second.timeout->stop();
    QTimer::singleShot(0, this, [this, searchID] { finish(searchID, {}); });
}

void Ed3kSearch::finish(uint32 searchID, const QString& error)
{
    const auto it = m_runs.find(searchID);
    if (it == m_runs.end())
        return;   // cancelled
    m_lookups.erase(it->second.lookup);
    m_runs.erase(it);
    if (m_finished)
        m_finished(searchID, error);
    else if (theApp.searchList)
        theApp.searchList->queue().onMetaSearchFinished(searchID, error);
}

void Ed3kSearch::finishLater(uint32 searchID, const QString& error)
{
    QTimer::singleShot(0, this, [this, searchID, error] {
        if (m_finished)
            m_finished(searchID, error);
        else if (theApp.searchList)
            theApp.searchList->queue().onMetaSearchFinished(searchID, error);
    });
}

void Ed3kSearch::addResult(uint32 searchID, std::unique_ptr<SearchFile> file)
{
    if (m_results) {
        m_results(searchID, std::move(file));
        return;
    }
    if (!theApp.searchList)
        return;
    theApp.searchList->addMetaSearchResult(searchID, file.release());
    emit theApp.searchList->tabHeaderUpdated(searchID);
}

} // namespace eMule::ed3k
