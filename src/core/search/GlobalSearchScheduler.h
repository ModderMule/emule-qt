#pragma once

/// @file GlobalSearchScheduler.h
/// @brief Paced ED2K global (UDP) search — the network half of MFC's CSearchResultsWnd.
///
/// A global search asks every server in the list over UDP. MFC never sends that as
/// a burst: it waits for the locally connected server to answer the TCP request (or
/// for a 50 s timeout), then queries **one server per 750 ms** until the list has
/// been walked once. This class owns that state machine. The GUI half of
/// CSearchResultsWnd — tabs, icons, the progress bar — lives in the Qt GUI and is
/// fed through IPC, so only the timers and the server rotation are here.

#include "utils/Types.h"

#include <QByteArray>
#include <QObject>

class QTimer;

namespace eMule {

class Server;
class ServerList;

// ---------------------------------------------------------------------------
// nextGlobalSearchTarget — pick the next server of a sweep
// ---------------------------------------------------------------------------

/// Advance @p list's search rotation to the next server worth asking.
///
/// Skips the currently connected server (it already answered the TCP request) and
/// any server that has failed more than @p deadServerRetries times. @p examined
/// counts candidates across the *whole* sweep, not per call, so the sweep ends after
/// a single pass over the list — this is MFC's `m_servercount`, including its
/// pre-increment, which is why a sweep asks at most `serverCount - 1` servers.
///
/// @return The server to query, or nullptr when the sweep is over.
/// MFC: CSearchResultsWnd::OnTimer / TimerGlobalSearch — srchybrid/SearchResultsWnd.cpp:256-263.
[[nodiscard]] Server* nextGlobalSearchTarget(ServerList& list, const Server* connected,
                                             uint32 deadServerRetries, uint32& examined);

// ---------------------------------------------------------------------------
// GlobalSearchScheduler
// ---------------------------------------------------------------------------

class GlobalSearchScheduler : public QObject {
    Q_OBJECT

public:
    explicit GlobalSearchScheduler(QObject* parent = nullptr);
    ~GlobalSearchScheduler() override;

    /// Begin a global search. The caller has already sent the TCP request to the
    /// connected server, if there was one.
    ///
    /// @param searchTerms      Encoded search tree, as handed to buildGlobalSearchPacket().
    /// @param is64BitSearch    Search carries a >4 GiB size condition; servers without
    ///                         large-file UDP support are skipped.
    /// @param awaitLocalAnswer A TCP request went out — hold the sweep until the
    ///                         server answers or the timeout expires, as MFC does.
    ///                         False starts sweeping immediately, which is how a
    ///                         Kad-only session (no server connection, so no TCP
    ///                         request to wait for) still gets a global search.
    void start(uint32 searchID, QByteArray searchTerms, bool is64BitSearch,
               bool awaitLocalAnswer);

    /// Stop the sweep and forget its state. Safe to call when nothing is running.
    void cancel();

    /// Stop the sweep only if it belongs to @p searchID.
    void cancelSearch(uint32 searchID);

    [[nodiscard]] bool isRunning() const;

    /// Search the sweep belongs to, 0 when idle.
    [[nodiscard]] uint32 searchID() const { return m_searchID; }

    /// The connected server answered our TCP request — start sweeping now.
    /// MFC: CSearchResultsWnd::LocalEd2kSearchEnd — srchybrid/SearchResultsWnd.cpp:455-470.
    void onLocalAnswerReceived();

    /// Wired to SearchList::tabHeaderUpdated. MFC gives up on the rest of the server
    /// list once a search has collected more than MAX_RESULTS hits, on the grounds
    /// that the query was too broad to be worth flooding every server with.
    /// MFC: CSearchResultsWnd::AddEd2kSearchResults — srchybrid/SearchResultsWnd.cpp:1552-1556.
    void onResultCountChanged(uint32 searchID);

signals:
    /// @param asked   servers examined so far, @param total the whole server list.
    /// Emitted once more with running=false when the sweep ends, so a listener can
    /// hide its progress display without tracking the end condition itself.
    void progress(uint32 searchID, uint32 asked, uint32 total, bool running);

private:
    void beginSweep();
    void onSweepTick();
    void emitProgress(bool running);

    QTimer* m_localTimeout = nullptr;   ///< single-shot; the local server is slow
    QTimer* m_sweepTimer = nullptr;     ///< repeating; one server per tick

    QByteArray m_searchTerms;
    uint32 m_searchID = 0;
    uint32 m_examined = 0;              ///< MFC m_servercount
    bool m_is64BitSearch = false;
};

} // namespace eMule
