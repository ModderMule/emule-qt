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
#include "utils/OtherFunctions.h"
#include "utils/ArgStr.h"

#include <QPointer>
#include <QRegularExpression>

#include <algorithm>
#include <limits>

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
    // was: connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kSearch::onEventPolled, Qt::DirectConnection);
    // node key's events as before, the anonymous node's from keyedEventPolled: each once
    connect(&m_session, &Ed3kSession::eventPolled, this,
            [this](const ::ed3k::Event& ev, bool* handled) { onEventPolled(ev, ::ed3k::KeyKind::Node, handled); },
            Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::keyedEventPolled, this,
            [this](const ::ed3k::Event& ev, ::ed3k::KeyKind key, bool* handled) {
                if (key == ::ed3k::KeyKind::Anon)
                    onEventPolled(ev, key, handled);
            },
            Qt::DirectConnection);
    // declined-records.md §2: rows of a root or MD4 listed now are hidden, others shown again
    connect(&m_session, &Ed3kSession::declinedChanged, this, [this] { (void)rejudge(); }, Qt::QueuedConnection);
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

QString Ed3kSearch::noteAuthor(const QString& nick, const QByteArray& publisher)
{
    if (nick.isEmpty())
        return {};
    return QStringLiteral("%1 (%2)").arg(nick, QString::fromLatin1(publisher.left(4).toHex()));
}

bool Ed3kSearch::searchNotes(const QByteArray& root)
{
    auto* e = m_session.engine();
    if (!e || root.size() != 32 || m_noteTimeouts.contains(root))
        return false;
    ::ed3k::Root r;
    std::copy_n(reinterpret_cast<const std::uint8_t*>(root.constData()), r.bytes.size(), r.bytes.begin());
    // the same key as keyword searches: while hiding, through a last hop (hops.md §1)
    const bool anon = m_session.hide().keyForUse() == ::ed3k::KeyKind::Anon;
    auto lookup = anon ? e->anonSearchNotes(m_session.now(), r) : e->searchNotes(m_session.now(), r);
    if (!lookup) {
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k notes search refused (error %1)").arg(int(lookup.error())); });
        return false;
    }
    m_noteLookups[lookupKey(*lookup, anon ? ::ed3k::KeyKind::Anon : ::ed3k::KeyKind::Node)] = root;
    auto timeout = std::make_unique<QTimer>();
    timeout->setSingleShot(true);
    connect(timeout.get(), &QTimer::timeout, this, [this, root] {
        std::erase_if(m_noteLookups, [&root](const auto& kv) { return kv.second == root; });
        endNotes(root);
    });
    timeout->start(kTimeoutMs);
    m_noteTimeouts[root] = std::move(timeout);
    if (theApp.searchList)
        theApp.searchList->setNotesSearchStatus(reinterpret_cast<const uint8*>(root.constData()), true);
    logEd3kDebug([&]() -> QString { return QStringLiteral(">>> ed3k notes search %1 (lookup %2)").arg(QString::fromLatin1(root.toHex())).arg(*lookup); });
    QTimer::singleShot(0, this, [this] {
        if (m_session.isRunning())
            m_session.pump();
    });
    return true;
}

bool Ed3kSearch::searchNames(const QByteArray& root, const QString& name, std::uint64_t size)
{
    auto* e = m_session.engine();
    if (!e || root.size() != 32 || m_nameTimeouts.contains(root))
        return false;
    const bool anon = m_session.hide().keyForUse() == ::ed3k::KeyKind::Anon;
    NameRun& run = m_nameRuns[root];
    run = NameRun{.size = size};
    const QStringList words = nameWords(name);
    int started = startNameWords(root, words);
    // the sources' records carry the name each publisher gives the file (Protocol.md §10.3)
    ::ed3k::Root r;
    std::copy_n(reinterpret_cast<const std::uint8_t*>(root.constData()), 32, r.bytes.begin());
    if (auto lookup = anon ? e->anonSearchSources(m_session.now(), r) : e->searchSources(m_session.now(), r)) {
        m_nameLookups[lookupKey(*lookup, anon ? ::ed3k::KeyKind::Anon : ::ed3k::KeyKind::Node)] = root;
        ++started;
    }
    if (started == 0) {
        m_nameRuns.erase(root);
        return false;
    }
    auto timeout = std::make_unique<QTimer>();
    timeout->setSingleShot(true);
    connect(timeout.get(), &QTimer::timeout, this, [this, root] {
        std::erase_if(m_nameLookups, [&root](const auto& kv) { return kv.second == root; });
        if (!retryNames(root))
            endNames(root);
    });
    timeout->start(kTimeoutMs);
    m_nameTimeouts[root] = std::move(timeout);
    logEd3kDebug([&]() -> QString { return QStringLiteral(">>> ed3k names search %1: %2").arg(QString::fromLatin1(root.toHex()), words.join(u' ')); });
    QTimer::singleShot(0, this, [this] {
        if (m_session.isRunning())
            m_session.pump();
    });
    return true;
}

std::vector<Ed3kSearch::FoundName> Ed3kSearch::namesOf(const QByteArray& root) const
{
    std::vector<FoundName> out;
    if (const auto it = m_names.find(root); it != m_names.end())
        for (const auto& [name, rec] : it->second) {
            out.push_back(rec.shown);
            out.back().publishers = int(rec.publishers.size());
        }
    std::ranges::stable_sort(out, std::greater{}, &FoundName::publishers);
    return out;
}

QStringList Ed3kSearch::nameWords(const QString& name, int count)
{
    // the keywords a publisher derives: words of letters and digits, the extension left out
    const QString base = name.contains(u'.') ? name.left(name.lastIndexOf(u'.')) : name;
    QStringList words;
    for (const QString& w : base.toLower().split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), Qt::SkipEmptyParts))
        if (w.size() >= 3 && !words.contains(w))
            words << w;
    std::ranges::stable_sort(words, std::greater{}, &QString::size);
    return words.mid(0, count);
}

std::map<QByteArray, Ed3kSearch::Availability> Ed3kSearch::availabilityOf(const std::vector<::ed3k::FoundFile>& files)
{
    std::map<QByteArray, Availability> out;
    // one record per publisher: each one a source; counts it sends may overlap, so the most
    std::map<QByteArray, Availability> publishers;
    for (const auto& f : files) {
        const QByteArray root = bytesOf(f.root.bytes);
        auto& a = out[root];
        a.complete = std::max({a.complete, f.sources, f.sites});
        a.partial = std::max(a.partial, f.partial);
        // older publishers sent 0/0 for a file they hold complete
        auto& p = publishers[root];
        ++(f.sources > 0 || f.partial == 0 ? p.complete : p.partial);
    }
    for (const auto& [root, p] : publishers) {
        auto& a = out[root];
        a.complete = std::max(a.complete, p.complete);
        a.partial = std::max(a.partial, p.partial);
    }
    return out;
}

std::unique_ptr<SearchFile> Ed3kSearch::toSearchFile(const ::ed3k::FoundFile& found, const Availability* avail)
{
    if (found.size == 0 || found.name.empty())
        return nullptr;
    auto file = std::make_unique<SearchFile>();
    file->setFileName(QString::fromUtf8(found.name.data(), qsizetype(found.name.size())), false, true, true);
    file->setFileSize(EMFileSize(found.size));
    // the list keys rows by a 16-byte hash: the root's first half (never an MD4 of eD2K)
    file->setFileHash(found.root.bytes.data());
    // was: sources as the total, complete = sources - partial (ABI: sources are the complete ones)
    const Availability own{found.sources, found.partial};
    const Availability& a = avail ? *avail : own;
    file->setSourceCount(a.complete + a.partial);
    file->setCompleteSourceCount(a.complete);
    if (found.duration != 0)
        file->addTagUnique(Tag(FT_MEDIA_LENGTH, uint32(found.duration)));
    if (found.bitrate != 0)
        file->addTagUnique(Tag(FT_MEDIA_BITRATE, uint32(found.bitrate)));
    // the record's media tags (§5.4): the columns eD2K rows fill from their tags
    using Found = ::ed3k::FoundFile;
    static constexpr std::pair<std::string Found::*, uint8> kMedia[] = {
        {&Found::artist, FT_MEDIA_ARTIST}, {&Found::album, FT_MEDIA_ALBUM}, {&Found::title, FT_MEDIA_TITLE}, {&Found::codec, FT_MEDIA_CODEC}};
    for (const auto& [field, tag] : kMedia) {
        const std::string& v = found.*field;
        if (const QString s = QString::fromUtf8(v.data(), qsizetype(v.size())).trimmed(); !s.isEmpty())
            file->addTagUnique(Tag(tag, s));
    }
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
    // was: if (!m_judge || !theApp.searchList)
    if (!theApp.searchList)
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
        // was: if (!applyVerdict(*f, m_judge(found)))
        if (!applyVerdict(*f, judge(found)))
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
    // a file type is the list of its extensions (Protocol.md §10.3 `exts`)
    f.exts = ed2kFileTypeExtensions(params.fileType).join(QLatin1Char(' ')).toStdString();
    f.minDuration = params.minLength;
    f.minBitrate = params.minBitrate;
    f.codec = params.codec.trimmed().toUtf8().toStdString();
    return f;
}

bool Ed3kSearch::isChannelFile(const ::ed3k::FoundFile& found)
{
    const QString name = QString::fromUtf8(found.name.data(), qsizetype(found.name.size()));
    return found.size > 0 && found.size <= Ed3kLiveFiles::kFileMax && name.endsWith(Ed3kLiveFiles::kSuffix, Qt::CaseInsensitive);
}

void Ed3kSearch::applyChannel(SearchFile& file, const Ed3kLiveChannelInfo* info)
{
    QString title = info ? info->title.trimmed() : QString();
    if (title.isEmpty()) {
        title = file.fileName();
        if (title.endsWith(Ed3kLiveFiles::kSuffix, Qt::CaseInsensitive))
            title.chop(Ed3kLiveFiles::kSuffix.size());
    }
    file.addTagUnique(Tag(FT_MEDIA_TITLE, title));
    if (!info)
        return;
    if (info->kbps != 0)
        file.addTagUnique(Tag(FT_MEDIA_BITRATE, uint32(info->kbps)));
    // the codec column: media and codecs, the language after them
    QStringList codec;
    if (!info->media.isEmpty())
        codec << info->media;
    if (!info->codecs.isEmpty())
        codec << info->codecs;
    QString shown = codec.join(QLatin1Char(' '));
    if (!info->lang.isEmpty())
        shown += QStringLiteral(" [%1]").arg(info->lang);
    if (!shown.isEmpty())
        file.addTagUnique(Tag(FT_MEDIA_CODEC, shown.trimmed()));
}

void Ed3kSearch::startMetaSearch(uint32 searchID, SearchType type, const SearchParams& params,
                                 const enodemeta::pb::SearchRequest& request, std::vector<MetaSearchCandidate> candidates)
{
    Q_UNUSED(request) Q_UNUSED(candidates)
    auto* e = m_session.engine();
    if (!e) {
        finishLater(searchID, tr("ed3k is not running."));
        return;
    }
    const QByteArray query = params.expression.trimmed().toUtf8();
    const bool live = type == SearchType::Ed3kLive;
    auto filter = filterOf(params);
    if (live) {
        // channel files only (§1.2): their extension, at most 64 KiB
        filter.ext = "ed3klive";
        filter.exts.clear();
        filter.maxSize = filter.maxSize == 0 ? Ed3kLiveFiles::kFileMax : std::min<std::uint64_t>(filter.maxSize, Ed3kLiveFiles::kFileMax);
    }
    // hops.md §1 global switch: while hiding the anonymous node searches (through a last hop).
    // was: ToDo: channel searches under the anonymous key (live has no anonymous FFI): node key
    // const bool anon = !live && m_session.hide().hiding();
    // (done: channel searches too; the files are fetched and watched under the same key)
    const bool anon = m_session.hide().keyForUse() == ::ed3k::KeyKind::Anon;
    const std::string_view text(query.constData(), std::size_t(query.size()));
    // was: auto lookup = e->search(m_session.now(), text, filter);
    auto lookup = anon ? e->anonSearch(m_session.now(), text, filter) : e->search(m_session.now(), text, filter);
    if (!lookup) {
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k search %1 refused (error %2)").arg(argStr(searchID), argStr(int(lookup.error()))); });
        finishLater(searchID, tr("ed3k refused the search (no keyword of at least 3 characters?)."));
        return;
    }
    const std::uint64_t keyed = lookupKey(*lookup, anon ? ::ed3k::KeyKind::Anon : ::ed3k::KeyKind::Node);
    Run run;
    // was: run.lookup = *lookup;
    run.lookup = keyed;
    run.live = live;
    run.timeout = std::make_unique<QTimer>();
    run.timeout->setSingleShot(true);
    connect(run.timeout.get(), &QTimer::timeout, this, [this, searchID] { finish(searchID, {}); });
    run.timeout->start(kTimeoutMs);
    // was: m_lookups[*lookup] = searchID;
    m_lookups[keyed] = searchID;
    m_runs[searchID] = std::move(run);
    logEd3kDebug([&]() -> QString { return QStringLiteral(">>> ed3k search %1: \"%2\" (lookup %3%4)").arg(argStr(searchID), argStr(params.expression), argStr(*lookup),
                          argStr(anon ? QStringLiteral(", anonymous") : QString())); });
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

void Ed3kSearch::onEventPolled(const ::ed3k::Event& event, ::ed3k::KeyKind key, bool* handled)
{
    Q_UNUSED(handled)   // KadFound needs no answer; others' lookups stay theirs
    const auto* found = std::get_if<::ed3k::event::KadFound>(&event);
    if (!found)
        return;
    if (const auto n = m_noteLookups.find(lookupKey(found->lookup, key)); n != m_noteLookups.end()) {
        const QByteArray root = n->second;
        m_noteLookups.erase(n);
        for (const auto& note : found->notes) {
            const QByteArray publisher = bytesOf(note.publisher.bytes);
            const QString text = QString::fromUtf8(note.text.data(), qsizetype(note.text.size()));
            const std::uint32_t rating = std::min<std::uint32_t>(note.rating, 5);
            const QString nick = QString::fromUtf8(note.nick.data(), qsizetype(note.nick.size())).trimmed();
            // kept for the download's Details (no search row needed)
            if (!m_found.contains(root)) {
                m_notesOrder.push_back(root);
                if (m_notesOrder.size() > kNotesKept) {
                    m_found.erase(m_notesOrder.front());
                    m_notesOrder.pop_front();
                }
            }
            m_found[root][publisher] = FoundNote{publisher, rating, text, nick};
            if (m_notes)
                m_notes(root, publisher, rating, text, nick);
            else if (theApp.searchList)
                (void)theApp.searchList->addNotes(reinterpret_cast<const uint8*>(root.constData()), publisher, uint8(rating), text,
                                                  noteAuthor(nick, publisher));
        }
        logEd3kDebug([&]() -> QString { return QStringLiteral("<<< ed3k notes search %1: %2 note(s)").arg(QString::fromLatin1(root.toHex())).arg(found->notes.size()); });
        // reported from the event loop, not inside the pump
        QTimer::singleShot(0, this, [this, root] { endNotes(root); });
        return;
    }
    if (const auto n = m_nameLookups.find(lookupKey(found->lookup, key)); n != m_nameLookups.end()) {
        const QByteArray root = n->second;
        m_nameLookups.erase(n);
        onNamesFound(root, *found);
        return;
    }
    // was: const auto it = m_lookups.find(found->lookup);
    const auto it = m_lookups.find(lookupKey(found->lookup, key));
    if (it == m_lookups.end())
        return;
    const uint32 searchID = it->second;
    const auto run = m_runs.find(searchID);
    if (run != m_runs.end() && run->second.live) {
        onLiveFound(searchID, run->second, *found);
        m_lookups.erase(it);
        return;
    }
    int added = 0;
    const auto avail = availabilityOf(found->files);
    for (const auto& f : found->files) {
        // filter lists (batch 12, G): marked rows shown as spam, hidden ones kept hidden
        // was: hidden rows were dropped (if (judged == Judged::Hide) continue;)
        // was: const Verdict judged = m_judge ? m_judge(f) : Verdict{};
        const Verdict judged = judge(f);
        const auto a = avail.find(bytesOf(f.root.bytes));
        if (auto row = toSearchFile(f, a != avail.end() ? &a->second : nullptr)) {
            (void)applyVerdict(*row, judged);
            addResult(searchID, std::move(row));
            ++added;
        }
    }
    logEd3kDebug([&]() -> QString { return QStringLiteral("<<< ed3k search %1: %2 file(s) from %3 node(s)").arg(argStr(searchID), argStr(added), argStr(found->nodes)); });
    // the lookup is over; reported from the event loop, not inside the pump
    m_lookups.erase(it);
    if (run != m_runs.end())
        run->second.timeout->stop();
    QTimer::singleShot(0, this, [this, searchID] { finish(searchID, {}); });
}

std::vector<Ed3kSearch::FoundNote> Ed3kSearch::notesOf(const QByteArray& root) const
{
    std::vector<FoundNote> out;
    if (const auto it = m_found.find(root); it != m_found.end())
        for (const auto& [publisher, note] : it->second)
            out.push_back(note);
    return out;
}

void Ed3kSearch::endNotes(const QByteArray& root)
{
    // may run from the timer's own timeout: not deleted under its emit
    if (const auto it = m_noteTimeouts.find(root); it != m_noteTimeouts.end()) {
        it->second.release()->deleteLater();
        m_noteTimeouts.erase(it);
    }
    if (theApp.searchList)
        theApp.searchList->setNotesSearchStatus(reinterpret_cast<const uint8*>(root.constData()), false);
}

void Ed3kSearch::onLiveFound(uint32 searchID, Run& run, const ::ed3k::event::KadFound& found)
{
    int candidates = 0;
    const auto avail = availabilityOf(found.files);
    for (const auto& f : found.files) {
        if (!isChannelFile(f))
            continue;
        const auto a = avail.find(bytesOf(f.root.bytes));
        auto row = toSearchFile(f, a != avail.end() ? &a->second : nullptr);
        if (!row)
            continue;
        // was: (void)applyVerdict(*row, m_judge ? m_judge(f) : Verdict{});
        (void)applyVerdict(*row, judge(f));
        ++candidates;
        if (!m_liveFetch) {
            applyChannel(*row, nullptr);
            addResult(searchID, std::move(row));
            continue;
        }
        // the channel file decides (magic) and fills the row; std::function wants a copyable hold
        ++run.fetching;
        auto held = std::make_shared<std::unique_ptr<SearchFile>>(std::move(row));
        QPointer<Ed3kSearch> self(this);
        m_liveFetch(f.root, f.size, [self, searchID, held](std::optional<QByteArray> file) {
            if (self)
                self->onChannelFile(searchID, std::move(*held), file);
        });
    }
    logEd3kDebug([&]() -> QString { return QStringLiteral("<<< ed3k channel search %1: %2 channel file(s) from %3 node(s)").arg(argStr(searchID), argStr(candidates), argStr(found.nodes)); });
    // the run may be gone (a fetch answered at once and it ended): looked up again
    const auto again = m_runs.find(searchID);
    if (again == m_runs.end())
        return;
    again->second.answered = true;
    if (again->second.fetching > 0) {
        again->second.timeout->start(kLiveFetchMs + 5000);   // the fetches end before
        return;
    }
    again->second.timeout->stop();
    QTimer::singleShot(0, this, [this, searchID] { finish(searchID, {}); });
}

std::uint64_t Ed3kSearch::lookupKey(std::uint64_t lookup, ::ed3k::KeyKind key)
{
    return key == ::ed3k::KeyKind::Anon ? (lookup | (std::uint64_t{1} << 63)) : lookup;
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

void Ed3kSearch::onChannelFile(uint32 searchID, std::unique_ptr<SearchFile> row, const std::optional<QByteArray>& file)
{
    const auto it = m_runs.find(searchID);
    if (it == m_runs.end())
        return;   // cancelled or timed out
    --it->second.fetching;
    if (!file) {
        applyChannel(*row, nullptr);   // not fetched: what its name says
        addResult(searchID, std::move(row));
    } else if (const auto info = Ed3kLiveFiles::parse(*file)) {
        applyChannel(*row, &*info);
        addResult(searchID, std::move(row));
    } else {
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k channel search %1: %2 is no channel file").arg(argStr(searchID), argStr(row->fileName())); });
    }
    if (it->second.answered && it->second.fetching <= 0) {
        it->second.timeout->stop();
        QTimer::singleShot(0, this, [this, searchID] { finish(searchID, {}); });
    }
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

Ed3kSearch::Verdict Ed3kSearch::judge(const ::ed3k::FoundFile& found) const
{
    // declined-records.md §2 "Downloads": a root or eD2K hash of the decline lists hides the row
    const auto& d = m_session.declined();
    const bool byRoot = d.listsRoot(found.root);
    if (byRoot || (found.ed2k && d.listsEd2k(*found.ed2k))) {
        Verdict v;
        v.judged = Judged::Hide;
        v.shown.by = QString::fromLatin1(kDeclineFile);
        v.shown.reason = byRoot ? QStringLiteral("its root is declined") : QStringLiteral("its eD2K hash is declined");
        return v;
    }
    return m_judge ? m_judge(found) : Verdict{};
}

void Ed3kSearch::onNamesFound(const QByteArray& root, const ::ed3k::event::KadFound& found)
{
    int kept = 0;
    // a source search: the name each source's publisher gave (§10.3), no tags
    for (const auto& src : found.sources) {
        if (src.name.empty())
            continue;
        nameRecords(root, QString::fromUtf8(src.name.data(), qsizetype(src.name.size()))).publishers.insert(bytesOf(src.id.bytes));
        ++kept;
    }
    for (const auto& f : found.files) {
        if (bytesOf(f.root.bytes) != root || f.name.empty())
            continue;
        const QString name = QString::fromUtf8(f.name.data(), qsizetype(f.name.size()));
        NameRecords& rec = nameRecords(root, name);
        rec.publishers.insert(bytesOf(f.publisher.bytes));
        // a claim of the publisher (§5.4): the first one that names a tag
        const auto take = [](QString& field, const std::string& v) {
            if (field.isEmpty() && !v.empty())
                field = QString::fromUtf8(v.data(), qsizetype(v.size()));
        };
        take(rec.shown.artist, f.artist);
        take(rec.shown.album, f.album);
        take(rec.shown.title, f.title);
        take(rec.shown.codec, f.codec);
        if (rec.shown.duration == 0)
            rec.shown.duration = f.duration;
        if (rec.shown.bitrate == 0)
            rec.shown.bitrate = f.bitrate;
        ++kept;
    }
    logEd3kDebug([&]() -> QString { return QStringLiteral("<<< ed3k names search %1: %2 record(s) of the root").arg(QString::fromLatin1(root.toHex())).arg(kept); });
    // the last of its lookups ends it; from the event loop, not inside the pump
    const bool more = std::ranges::any_of(m_nameLookups, [&root](const auto& kv) { return kv.second == root; });
    if (!more)
        QTimer::singleShot(0, this, [this, root] {
            if (!retryNames(root))
                endNames(root);
        });
}

int Ed3kSearch::startNameWords(const QByteArray& root, const QStringList& words)
{
    auto* e = m_session.engine();
    const auto run = m_nameRuns.find(root);
    if (!e || run == m_nameRuns.end())
        return 0;
    const bool anon = m_session.hide().keyForUse() == ::ed3k::KeyKind::Anon;
    ::ed3k::Filter filter;
    filter.minSize = run->second.size;
    filter.maxSize = run->second.size;
    int started = 0;
    for (const QString& word : words) {
        run->second.asked.insert(word);
        const QByteArray text = word.toUtf8();
        const std::string_view query(text.constData(), std::size_t(text.size()));
        auto lookup = anon ? e->anonSearch(m_session.now(), query, filter) : e->search(m_session.now(), query, filter);
        if (!lookup) {
            logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k names search \"%1\" refused (error %2)").arg(word).arg(int(lookup.error())); });
            continue;
        }
        m_nameLookups[lookupKey(*lookup, anon ? ::ed3k::KeyKind::Anon : ::ed3k::KeyKind::Node)] = root;
        ++started;
    }
    return started;
}

bool Ed3kSearch::retryNames(const QByteArray& root)
{
    const auto run = m_nameRuns.find(root);
    if (run == m_nameRuns.end() || !m_nameTimeouts.contains(root))
        return false;
    const std::vector<FoundName> names = namesOf(root);
    const bool tagged = std::ranges::any_of(names, [](const FoundName& n) {
        return n.duration || n.bitrate || !n.artist.isEmpty() || !n.album.isEmpty() || !n.title.isEmpty() || !n.codec.isEmpty();
    });
    const int left = kNameRetryWords - run->second.retried;
    if (tagged || left <= 0)
        return false;
    // words of the other names, most published first, not asked yet
    QStringList words;
    for (const FoundName& n : names)
        for (const QString& w : nameWords(n.name, std::numeric_limits<int>::max()))
            if (!run->second.asked.contains(w) && !words.contains(w) && words.size() < std::min(left, kNameRetryRound))
                words << w;
    if (words.isEmpty())
        return false;
    run->second.retried += int(words.size());
    if (startNameWords(root, words) == 0)
        return false;
    m_nameTimeouts[root]->start(kTimeoutMs);
    logEd3kDebug([&]() -> QString { return QStringLiteral(">>> ed3k names search %1, no media tags yet: %2").arg(QString::fromLatin1(root.toHex()), words.join(u' ')); });
    QTimer::singleShot(0, this, [this] {
        if (m_session.isRunning())
            m_session.pump();
    });
    return true;
}

Ed3kSearch::NameRecords& Ed3kSearch::nameRecords(const QByteArray& root, const QString& name)
{
    if (!m_names.contains(root)) {
        m_namesOrder.push_back(root);
        if (m_namesOrder.size() > kNotesKept) {
            m_names.erase(m_namesOrder.front());
            m_namesOrder.pop_front();
        }
    }
    NameRecords& rec = m_names[root][name];
    rec.shown.name = name;
    return rec;
}

void Ed3kSearch::endNames(const QByteArray& root)
{
    m_nameRuns.erase(root);
    // may run from the timer's own timeout: not deleted under its emit
    if (const auto it = m_nameTimeouts.find(root); it != m_nameTimeouts.end()) {
        it->second.release()->deleteLater();
        m_nameTimeouts.erase(it);
    }
}

} // namespace eMule::ed3k
