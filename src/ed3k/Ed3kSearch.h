#pragma once

/// @file Ed3kSearch.h
/// @brief ed3k keyword search (Protocol.md §10.3) for the search queue: SearchType::Ed3k
/// reaches it through MetaSearchService (the runner of the Meta searches), it asks the
/// engine (Engine::search with the size, extension and source filters) and turns the
/// KadFound files into SearchFile rows (ed3k rows: root, size, name, sources).
///
/// - A KadFound of another lookup is left alone (the joins' source searches).
/// - One answer per lookup; a search without one ends after kTimeoutMs.
/// - The rows and the end go to sinks: the daemon's SearchList by default, tests set their own.

#include "Ed3kFfi.h"

#include "search/MetaSearchRunner.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <map>
#include <memory>

namespace eMule {
class SearchFile;
}

namespace eMule::ed3k {

class Ed3kSession;

class Ed3kSearch : public QObject, public MetaSearchRunner {
    Q_OBJECT

public:
    static constexpr int kTimeoutMs = 90 * 1000;

    using ResultSink = std::function<void(uint32 searchID, std::unique_ptr<SearchFile> file)>;
    using FinishSink = std::function<void(uint32 searchID, const QString& error)>;

    explicit Ed3kSearch(Ed3kSession& session, QObject* parent = nullptr);
    ~Ed3kSearch() override;

    /// Where rows and ends go; unset: theApp.searchList and its queue.
    void setSinks(ResultSink results, FinishSink finished);

    /// Filter lists (filter-list.md §3, Ed3kFilters::judge): Mark shows the row as spam, Hide
    /// drops it (only when the user set the list to hide). Unset: every row as found.
    enum class Judged { None, Mark, Hide };
    using Judge = std::function<Judged(const ::ed3k::FoundFile& found)>;
    void setJudge(Judge judge) { m_judge = std::move(judge); }
    /// A search still waits for its answer (tests).
    [[nodiscard]] bool isRunning(uint32 searchID) const;

    /// One found file as a search row (nullptr: no name or size); ed3k root and MD4 kept.
    [[nodiscard]] static std::unique_ptr<SearchFile> toSearchFile(const ::ed3k::FoundFile& found);
    /// The search's filters as the engine takes them.
    [[nodiscard]] static ::ed3k::Filter filterOf(const SearchParams& params);

    // -- MetaSearchRunner --

    void startMetaSearch(uint32 searchID, SearchType type, const SearchParams& params,
                         const enodemeta::pb::SearchRequest& request,
                         std::vector<MetaSearchCandidate> candidates) override;
    /// No pages: ends at once.
    void continueMetaSearch(uint32 searchID) override;
    void cancelMetaSearch(uint32 searchID) override;

private:
    struct Run {
        std::uint64_t lookup = 0;
        std::unique_ptr<QTimer> timeout;
    };

    void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void finish(uint32 searchID, const QString& error);
    /// Reported after the call returns (the queue is still sending the search).
    void finishLater(uint32 searchID, const QString& error);
    void addResult(uint32 searchID, std::unique_ptr<SearchFile> file);

    Ed3kSession& m_session;
    std::map<uint32, Run> m_runs;
    std::map<std::uint64_t, uint32> m_lookups;
    ResultSink m_results;
    FinishSink m_finished;
    Judge m_judge;
};

} // namespace eMule::ed3k
