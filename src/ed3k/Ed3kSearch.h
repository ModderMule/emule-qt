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
/// - SearchType::Ed3kLive (live.md §1.2): the same search, only channel files (name ends in
///   `.ed3klive`, ≤ 64 KiB); each is fetched (setLiveFetch) and kept only with the magic, its
///   title, kbps, media/codecs and lang in the row; one not fetched in kLiveFetchMs shows what
///   its name says.

#include "Ed3kFfi.h"
#include "Ed3kLiveFiles.h"

#include "search/MetaSearchRunner.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <map>
#include <memory>
#include <optional>

#include "search/SearchFile.h"

namespace eMule::ed3k {

class Ed3kSession;

class Ed3kSearch : public QObject, public MetaSearchRunner {
    Q_OBJECT

public:
    static constexpr int kTimeoutMs = 90 * 1000;
    /// a channel file of an Ed3kLive search fetched within this, else shown from its name
    static constexpr int kLiveFetchMs = 20 * 1000;

    using ResultSink = std::function<void(uint32 searchID, std::unique_ptr<SearchFile> file)>;
    using FinishSink = std::function<void(uint32 searchID, const QString& error)>;

    explicit Ed3kSearch(Ed3kSession& session, QObject* parent = nullptr);
    ~Ed3kSearch() override;

    /// Where rows and ends go; unset: theApp.searchList and its queue.
    void setSinks(ResultSink results, FinishSink finished);
    /// Ed3kLive rows: fetches a channel file (Ed3kLiveFiles::fetch: empty = not one, nullopt =
    /// not fetched). Unset: rows from the name alone.
    using LiveFetch = std::function<void(const ::ed3k::Root& root, std::uint64_t size, Ed3kLiveFiles::Fetched done)>;
    void setLiveFetch(LiveFetch fetch) { m_liveFetch = std::move(fetch); }

    /// Filter lists (filter-list.md §3, Ed3kFilters::judge): Mark shows the row as spam, Hide
    /// keeps it hidden (only when the user set the list to hide; the GUI shows it on request).
    /// Unset: every row as found.
    /// was: Hide dropped the row (batch 12); §3 wants what was hidden shown on request
    enum class Judged { None, Mark, Hide };
    /// What a row shows: the kind, and the verdict with who said it (SearchFile::Ed3kJudged).
    struct Verdict {
        Judged judged = Judged::None;
        SearchFile::Ed3kJudged shown;
    };
    using Judge = std::function<Verdict(const ::ed3k::FoundFile& found)>;
    void setJudge(Judge judge) { m_judge = std::move(judge); }
    /// Judges every ed3k row of the search lists again (a verdict, a mode or a set author
    /// changed); changed rows are stamped for the GUI. The rows changed.
    int rejudge();
    /// The judgement of @p found put on @p file; true when it changed.
    static bool applyVerdict(SearchFile& file, const Verdict& v);
    /// A search still waits for its answer (tests).
    [[nodiscard]] bool isRunning(uint32 searchID) const;

    /// Sources of one root over the records of an answer (one record per publisher).
    struct Availability {
        std::uint32_t complete = 0;
        std::uint32_t partial = 0;
    };
    /// Per root (32 bytes): complete = the most of publishers holding it complete, any
    /// record's own count and the sites an index node counted; a record with sources and
    /// partial both 0 (older publishers) counts its publisher as complete.
    [[nodiscard]] static std::map<QByteArray, Availability> availabilityOf(const std::vector<::ed3k::FoundFile>& files);
    /// One found file as a search row (nullptr: no name or size); ed3k root and MD4 kept.
    /// Sources: @p avail when given, else the record's own (sources complete + partial).
    [[nodiscard]] static std::unique_ptr<SearchFile> toSearchFile(const ::ed3k::FoundFile& found, const Availability* avail = nullptr);
    /// The search's filters as the engine takes them.
    [[nodiscard]] static ::ed3k::Filter filterOf(const SearchParams& params);
    /// A found file that may be a channel file (Ed3kLive): name and size (§1.2).
    [[nodiscard]] static bool isChannelFile(const ::ed3k::FoundFile& found);
    /// A channel row: title (from @p info or the name), kbps as bitrate, media, codecs and
    /// lang as the codec.
    static void applyChannel(SearchFile& file, const Ed3kLiveChannelInfo* info);

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
        bool live = false;       ///< Ed3kLive: channel files only
        int fetching = 0;        ///< channel files still fetched
        bool answered = false;   ///< the lookup's KadFound came
    };

    // was: void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onEventPolled(const ::ed3k::Event& event, ::ed3k::KeyKind key, bool* handled);
    /// m_lookups key: the anonymous node's lookup ids get bit 63 (both nodes count from 1).
    [[nodiscard]] static std::uint64_t lookupKey(std::uint64_t lookup, ::ed3k::KeyKind key);
    void finish(uint32 searchID, const QString& error);
    /// Reported after the call returns (the queue is still sending the search).
    void finishLater(uint32 searchID, const QString& error);
    void addResult(uint32 searchID, std::unique_ptr<SearchFile> file);
    /// The KadFound of an Ed3kLive search: channel files fetched, rows when they are there.
    void onLiveFound(uint32 searchID, Run& run, const ::ed3k::event::KadFound& found);
    /// An Ed3kLive row's channel file arrived (or not).
    void onChannelFile(uint32 searchID, std::unique_ptr<SearchFile> row, const std::optional<QByteArray>& file);

    Ed3kSession& m_session;
    std::map<uint32, Run> m_runs;
    std::map<std::uint64_t, uint32> m_lookups;   ///< by lookupKey()
    ResultSink m_results;
    FinishSink m_finished;
    Judge m_judge;
    LiveFetch m_liveFetch;
};

} // namespace eMule::ed3k
