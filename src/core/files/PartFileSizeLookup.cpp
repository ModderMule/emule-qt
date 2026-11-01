#include "pch.h"
#include "files/PartFileSizeLookup.h"

#include "app/AppContext.h"
#include "files/PartFile.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/Kademlia.h"
#include "prefs/Preferences.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "search/SearchParams.h"
#include "search/SearchQueue.h"
#include "search/SearchStarter.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "transfer/DownloadQueue.h"
#include "utils/OtherFunctions.h"
#include "utils/TimeUtils.h"

#include <QPointer>

namespace eMule {

namespace {

// One `ed2k::<hash>` search on the connected server. Internal: the GUI never sees it.
class ServerSizeSearch : public QObject {
public:
    ServerSizeSearch(SearchList& list, uint32 searchID, QByteArray hash, uint64 low,
                     uint64 high, PartFileSizeLookup::Done done)
        : m_list(list), m_searchID(searchID), m_hash(std::move(hash)), m_low(low), m_high(high)
        , m_done(std::move(done))
    {
        connect(&list, &SearchList::tabHeaderUpdated, this, [this](uint32 id) {
            if (id == m_searchID)
                evaluate(false);
        });
        connect(&list, &SearchList::searchStateChanged, this, [this](const SearchStatus& s) {
            if (s.searchID == m_searchID
                && (s.state == SearchRunState::Finished || s.state == SearchRunState::Failed))
                evaluate(true);
        });
    }

    void cancel()
    {
        m_done = nullptr;
        close();
    }

private:
    void evaluate(bool over)
    {
        if (!m_done)
            return;
        const auto* hash = reinterpret_cast<const uint8*>(m_hash.constData());
        const SearchFile* found = m_list.searchFileByHash(hash, m_searchID);
        const uint64 size = found ? static_cast<uint64>(found->fileSize()) : 0;
        const bool inRange = size > m_low && size < m_high;
        if (!inRange && !over)
            return;   // more answers may come

        QString reason;
        if (!inRange) {
            const Server* server = theApp.serverConnect ? theApp.serverConnect->currentServer()
                                                        : nullptr;
            const QString name = server ? server->name() : QStringLiteral("?");
            reason = found
                ? QStringLiteral("server %1 reported %2, not a size this hashset allows")
                      .arg(name).arg(size)
                : QStringLiteral("server %1 returned no size (it may not support ed2k:: hash "
                                 "searches)").arg(name);
        }
        auto done = std::move(m_done);
        m_done = nullptr;
        close();
        done(inRange ? size : 0, reason);
    }

    void close()
    {
        // Deferred: this runs inside the list's own signal
        QTimer::singleShot(0, this, [this] {
            removeSearch(m_list, m_searchID);
            deleteLater();
        });
    }

    SearchList& m_list;
    uint32 m_searchID;
    QByteArray m_hash;
    uint64 m_low;
    uint64 m_high;
    PartFileSizeLookup::Done m_done;
};

} // namespace

PartFileSizeLookup::PartFileSizeLookup(QObject* parent)
    : PartFileSizeLookup(realBackend(), parent)
{
}

PartFileSizeLookup::PartFileSizeLookup(Backend backend, QObject* parent)
    : QObject(parent), m_backend(std::move(backend))
{
    m_tick.setInterval(kTickMs);
    connect(&m_tick, &QTimer::timeout, this, &PartFileSizeLookup::tick);
}

PartFileSizeLookup::~PartFileSizeLookup()
{
    stopHalves();
}

void PartFileSizeLookup::request(const QByteArray& hash, uint64 low, uint64 high)
{
    if (isPending(hash))
        return;
    m_queue.push_back({hash, low, high, 0});
    // Next turn: a file found wrong while loading isn't in the download queue yet
    QTimer::singleShot(0, this, [this] {
        if (!m_current)
            startNext();
    });
}

void PartFileSizeLookup::cancel(const QByteArray& hash)
{
    std::erase_if(m_queue, [&hash](const Job& j) { return j.hash == hash; });
    if (m_current && m_current->hash == hash) {
        stopHalves();
        m_current.reset();
        ++m_generation;
        startNext();
    }
}

bool PartFileSizeLookup::isPending(const QByteArray& hash) const
{
    if (m_current && m_current->hash == hash)
        return true;
    return std::ranges::any_of(m_queue, [&hash](const Job& j) { return j.hash == hash; });
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

PartFileSizeLookup::Backend PartFileSizeLookup::realBackend()
{
    Backend b;
    b.serverState = [] {
        if (!thePrefs.networkED2K())
            return Net::Disabled;
        return theApp.serverConnect && theApp.serverConnect->isConnected() && theApp.searchList
            ? Net::Ready : Net::Waiting;
    };
    b.kadState = [] {
        if (!thePrefs.kadEnabled())
            return Net::Disabled;
        const auto* kad = kad::Kademlia::instance();
        return kad && kad->isConnected() ? Net::Ready : Net::Waiting;
    };
    b.startServer = [](const QByteArray& hash, uint64 low, uint64 high,
                       Done done) -> std::function<void()> {
        // A link, not a bare "ed2k::<hex>": the parser turns it into that keyword
        const QString hex = md4str(reinterpret_cast<const uint8*>(hash.constData()));
        SearchParams params;
        params.type = SearchType::Ed2kServer;
        params.expression = QStringLiteral("ed2k://|file|%1|%2|%1|/").arg(hex).arg(low);
        params.searchTitle = hex;
        params.internal = true;
        // Queued behind the user's server search, never cancelling it. Sending it does
        // drop that search's pending "more results" page (SearchQueue::pump).
        const SearchStartResult started = startSearch(*theApp.searchList, params);
        if (!started.ok) {
            done(0, QStringLiteral("hash search refused: %1").arg(started.error));
            return nullptr;
        }
        QPointer search = new ServerSizeSearch(*theApp.searchList, started.searchID, hash, low,
                                               high, std::move(done));
        return [search] {
            if (search)
                search->cancel();
        };
    };
    b.startKad = [](const QByteArray& hash, uint64 low, uint64 high,
                    Done done) -> std::function<void()> {
        const kad::UInt128 target(reinterpret_cast<const uint8*>(hash.constData()));
        auto* search = kad::SearchManager::prepareLookup(kad::SearchType::File, false, target);
        if (!search) {
            done(0, QStringLiteral("Kad is already searching for this file"));
            return nullptr;
        }
        search->setSizeProbe(low, high, [done](uint64 size) {
            done(size, size ? QString() : QStringLiteral("no Kad source reported a size"));
        });
        const uint32 searchID = search->getSearchID();
        if (!kad::SearchManager::startSearch(search)) {
            delete search;   // its probe answers 0 on the way out
            return nullptr;
        }
        return [searchID] { kad::SearchManager::stopSearch(searchID, false); };
    };
    b.wanted = [](const QByteArray& hash) {
        const PartFile* file = theApp.downloadQueue
            ? theApp.downloadQueue->fileByID(reinterpret_cast<const uint8*>(hash.constData()))
            : nullptr;
        return file && file->isSizeContradicted();
    };
    b.apply = [](const QByteArray& hash, uint64 size, const QString& from) {
        if (auto* file = theApp.downloadQueue ? theApp.downloadQueue->fileByID(
                             reinterpret_cast<const uint8*>(hash.constData())) : nullptr)
            file->applyCorrectedSize(size, from);
    };
    b.fail = [](const QByteArray& hash, const QString& serverReason, const QString& kadReason) {
        if (auto* file = theApp.downloadQueue ? theApp.downloadQueue->fileByID(
                             reinterpret_cast<const uint8*>(hash.constData())) : nullptr)
            file->failSizeLookup(serverReason, kadReason);
    };
    return b;
}

void PartFileSizeLookup::startNext()
{
    while (!m_current && !m_queue.empty()) {
        Job job = std::move(m_queue.front());
        m_queue.pop_front();
        if (m_backend.wanted && !m_backend.wanted(job.hash))
            continue;
        job.waitDeadline = static_cast<qint64>(getTickCount()) + kNetWaitMs;
        m_current = std::move(job);
        m_server = {};
        m_kad = {};
        ++m_generation;
        tick();
    }
    if (!m_current)
        m_tick.stop();
    else if (m_current && !m_tick.isActive())
        m_tick.start();
}

void PartFileSizeLookup::tick()
{
    if (!m_current)
        return;
    // Each half may finish the lookup synchronously; check in between
    const uint32 gen = m_generation;
    advance(m_server, m_backend.serverState(), m_backend.startServer, true);
    if (gen != m_generation || !m_current)
        return;
    advance(m_kad, m_backend.kadState(), m_backend.startKad, false);
}

void PartFileSizeLookup::advance(Half& half, Net net, const Start& start, bool server)
{
    if (half.state != HalfState::Idle && half.state != HalfState::Waiting)
        return;
    const uint32 gen = m_generation;
    if (net == Net::Disabled) {
        halfDone(gen, server, 0, server ? QStringLiteral("eD2K disabled")
                                        : QStringLiteral("Kad disabled"));
        return;
    }
    if (net == Net::Waiting) {
        if (static_cast<qint64>(getTickCount()) < m_current->waitDeadline) {
            half.state = HalfState::Waiting;
            return;
        }
        halfDone(gen, server, 0, server ? QStringLiteral("not connected to a server")
                                        : QStringLiteral("Kad not connected"));
        return;
    }
    half.state = HalfState::Running;
    // Answers always arrive from the event loop: a Kad probe answers from inside the
    // search's destructor
    QPointer self(this);
    Done done = [self, gen, server](uint64 size, const QString& reason) {
        QMetaObject::invokeMethod(self, [self, gen, server, size, reason] {
            if (self)
                self->halfDone(gen, server, size, reason);
        }, Qt::QueuedConnection);
    };
    auto cancel = start(m_current->hash, m_current->low, m_current->high, std::move(done));
    if (gen == m_generation)
        half.cancel = std::move(cancel);
}

void PartFileSizeLookup::halfDone(uint32 generation, bool server, uint64 size,
                                  const QString& reason)
{
    if (generation != m_generation || !m_current)
        return;   // a lookup that already ended
    Half& half = server ? m_server : m_kad;
    if (half.state == HalfState::Failed || half.state == HalfState::Done)
        return;
    half.cancel = nullptr;
    if (size > m_current->low && size < m_current->high) {
        half.state = HalfState::Done;
        finish(std::pair{size, server ? QStringLiteral("server") : QStringLiteral("Kad")});
        return;
    }
    half.state = HalfState::Failed;
    half.reason = reason.isEmpty() ? QStringLiteral("no size in range") : reason;
    if (m_server.state == HalfState::Failed && m_kad.state == HalfState::Failed)
        finish(std::nullopt);
}

void PartFileSizeLookup::finish(std::optional<std::pair<uint64, QString>> found)
{
    stopHalves();
    const Job job = std::move(*m_current);
    const QString serverReason = m_server.reason;
    const QString kadReason = m_kad.reason;
    m_current.reset();
    ++m_generation;

    if (!m_backend.wanted || m_backend.wanted(job.hash)) {
        if (found) {
            if (m_backend.apply)
                m_backend.apply(job.hash, found->first, found->second);
        } else if (m_backend.fail) {
            m_backend.fail(job.hash, serverReason, kadReason);
        }
    }
    startNext();
}

void PartFileSizeLookup::stopHalves()
{
    // Moved out first: a cancel may call back
    for (Half* half : {&m_server, &m_kad}) {
        if (auto cancel = std::move(half->cancel)) {
            half->cancel = nullptr;
            cancel();
        }
    }
}

} // namespace eMule
