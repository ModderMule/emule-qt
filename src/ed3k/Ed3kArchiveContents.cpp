#include "Ed3kArchiveContents.h"

#include "Ed3kSession.h"
#include "Ed3kSetManifest.h"
#include "Ed3kUsenetJoin.h"
#include "nzb/NzbFile.h"
#include "archive/ArchiveReader.h"
#include "archive/ExternalUnpacker.h"
#include "post/UsenetUnpacker.h"
#include "utils/ArgStr.h"

#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <array>
#include <memory>

namespace eMule::ed3k {

namespace ev = ffi::event;

namespace {

/// file-set.md §2.1: entries of one manifest
constexpr std::size_t kMaxEntries = 65536;
/// alias.md §3: roots tried per name
constexpr std::size_t kMaxCandidates = 3;
/// found listings kept for the unpack check (§6.1)
constexpr std::size_t kMaxFoundListings = 256;

std::span<const std::uint8_t> bytesOf(const QByteArray& b)
{
    return {reinterpret_cast<const std::uint8_t*>(b.constData()), std::size_t(b.size())};
}

std::span<const std::byte> spanOf(const char* data, qint64 len)
{
    return {reinterpret_cast<const std::byte*>(data), std::size_t(len)};
}

/// Entries only: the volumes reached the tap already.
class EntriesOnly : public ArchiveContentsTap {
public:
    explicit EntriesOnly(ArchiveContentsTap& tap) : m_tap(tap) {}
    [[nodiscard]] int volumesWanted() const override { return 0; }
    void volumeData(int, const char*, qint64) override {}
    void volumeEnd(int) override {}
    void entryBegin(const QString& path) override { m_tap.entryBegin(path); }
    void entryData(const char* data, qint64 len) override { m_tap.entryData(data, len); }
    void entryEnd() override { m_tap.entryEnd(); }
    void spoil() override { m_tap.spoil(); }
    void commit(const QString&, const QString&) override {}

private:
    ArchiveContentsTap& m_tap;
};

} // namespace

Ed3kArchiveContents::Ed3kArchiveContents(Ed3kSession& session, const QString& configDir, QObject* parent)
    : Ed3kJoinBase(session, configDir, QStringLiteral("contents"), parent)
{
}

Ed3kArchiveContents::~Ed3kArchiveContents()
{
    // no callbacks after the object is gone
    for (auto& [id, l] : m_lookups)
        delete l.timer;
}

void Ed3kArchiveContents::start()
{
    if (m_started || !m_session.isRunning())
        return;
    beginStart();
    // what was published before the restart: manifests shared, records again
    shareKept();
    pumpSession();
}

void Ed3kArchiveContents::stop()
{
    if (!m_started)
        return;
    std::vector<quint32> ids;
    for (const auto& [id, l] : m_lookups)
        ids.push_back(id);
    for (const quint32 id : ids)
        finish(id, true);
    m_searches.clear();
    closeAll();
}

void Ed3kArchiveContents::setPublish(bool on)
{
    m_publish = on;
    setHoldBack(!on);
}

// ---------------------------------------------------------------------------
// Ed3kContentsTap
// ---------------------------------------------------------------------------

Ed3kContentsTap::Ed3kContentsTap(Publish publish, int volumes)
    : m_publish(std::move(publish))
    , m_volumeCount(std::max(1, volumes))
{
    m_volumeHashers.resize(std::size_t(m_volumeCount));
    for (auto& h : m_volumeHashers)
        h = std::make_unique<ffi::Hasher>();
    m_volumes.resize(std::size_t(m_volumeCount));
}

void Ed3kContentsTap::volumeData(int index, const char* data, qint64 len)
{
    if (index < 0 || index >= m_volumeCount)
        return;
    auto& h = m_volumeHashers[std::size_t(index)];
    if (h && !h->update(spanOf(data, len)))
        h.reset();
}

void Ed3kContentsTap::volumeEnd(int index)
{
    if (index < 0 || index >= m_volumeCount)
        return;
    auto& h = m_volumeHashers[std::size_t(index)];
    if (!h)
        return;
    auto done = h->finish();
    h.reset();
    if (!done)
        return;
    const auto bytes = done->hashes.bytes();
    m_volumes[std::size_t(index)] = VolumeHash{done->root, done->size, ffi::Bytes(bytes.begin(), bytes.end())};
}

void Ed3kContentsTap::entryBegin(const QString& path)
{
    m_entryPath = path;
    m_entry = m_spoiled ? nullptr : std::make_unique<ffi::Hasher>();
}

void Ed3kContentsTap::entryData(const char* data, qint64 len)
{
    if (m_entry && !m_entry->update(spanOf(data, len)))
        spoil();
}

void Ed3kContentsTap::entryEnd()
{
    if (!m_entry)
        return;
    auto done = m_entry->finish();
    m_entry.reset();
    if (!done || m_entries.size() >= kMaxEntries) {
        spoil();
        return;
    }
    m_entries.push_back({m_entryPath, done->size, done->root});
}

void Ed3kContentsTap::spoil()
{
    m_spoiled = true;
    m_entry.reset();
}

void Ed3kContentsTap::restartEntries()
{
    m_spoiled = false;
    m_entry.reset();
    m_entries.clear();
}

std::optional<VolumeHash> Ed3kContentsTap::volume(int index) const
{
    if (index < 0 || index >= m_volumeCount)
        return std::nullopt;
    return m_volumes[std::size_t(index)];
}

std::optional<ArchiveListing> Ed3kContentsTap::listing(const QString& firstVolumeName, const QString& password) const
{
    if (m_spoiled || m_entries.empty() || !m_volumes.front())
        return std::nullopt;
    ArchiveListing listing;
    listing.first = m_volumes.front()->root;
    listing.firstName = firstVolumeName;
    listing.password = password;
    listing.entries = m_entries;
    // a path the canonical manifest refuses: nothing to publish (hoster.md §6.1)
    std::vector<ffi::ContentEntry> check;
    check.reserve(listing.entries.size());
    for (const auto& e : listing.entries)
        check.push_back({e.path.toStdString(), e.size, e.root});
    if (!ffi::contentsManifest(check))
        return std::nullopt;
    return listing;
}

void Ed3kContentsTap::commit(const QString& firstVolumeName, const QString& password)
{
    if (!m_publish)
        return;
    if (auto l = listing(firstVolumeName, password))
        m_publish(std::move(*l));
}

// ---------------------------------------------------------------------------
// Ed3kArchiveContents
// ---------------------------------------------------------------------------

std::optional<QString> Ed3kArchiveContents::readSet(const QStringList& volumes, const QStringList& passwords,
                                                     const QString& externalTool, Ed3kContentsTap& tap,
                                                     const std::atomic<bool>* cancel)
{
    if (volumes.isEmpty())
        return std::nullopt;
    QStringList tries;
    for (const QString& pw : passwords)
        if (!pw.isEmpty() && !tries.contains(pw))
            tries << pw;

    // headers only: libarchive or the tool
    ArchiveReader probe;
    probe.setPassphrases(tries);
    const bool listed = probe.open(volumes);
    const bool viaTool = !listed ? probe.encryptionBlocked()
                                 : probe.hasEncryptedEntries() && !probe.formatName().contains(QLatin1String("ZIP"), Qt::CaseInsensitive);
    bool volumesFed = false;
    if (listed && !viaTool) {
        ArchiveReader reader;
        reader.setPassphrases(tries);
        reader.setVolumes(volumes);
        reader.setContentsTap(&tap);
        const bool ok = reader.readEntries([cancel](int, const QString&, const char*, qint64, bool) { return !(cancel && cancel->load()); });
        if (ok && !reader.encryptionBlocked())
            return reader.hasEncryptedEntries() ? ArchiveReader::openingPassphrase(volumes, tries) : QString();
        if (!reader.encryptionBlocked())
            return std::nullopt;
        volumesFed = true;   // drained by the reader: the tool adds entries only
        tap.restartEntries();
    } else if (!listed && !viaTool) {
        (void)ArchiveReader::tapVolumes(volumes, tap, cancel);   // a verify still wants them
        return std::nullopt;
    }

    ExternalUnpacker tool(externalTool);
    QList<ExternalUnpacker::Member> scanned;
    for (int i = 0; i < probe.entryCount(); ++i)
        if (!probe.entryIsDir(i))
            scanned.append({probe.entryName(i), qint64(probe.entrySize(i))});
    EntriesOnly entriesOnly(tap);
    ArchiveContentsTap& target = volumesFed ? static_cast<ArchiveContentsTap&>(entriesOnly) : tap;
    bool fed = volumesFed;
    if (tool.available()) {
        for (const QString& pw : std::as_const(tries)) {
            if (cancel && cancel->load())
                break;
            QString error;
            const auto members = !scanned.isEmpty()                                     ? scanned
                                 : tool.toolKind() == ExternalUnpacker::Kind::SevenZip ? tool.listMembers(volumes, pw, error)
                                                                                       : QList<ExternalUnpacker::Member>{};
            if (members.isEmpty())
                continue;
            // the volumes go to the tap once: later tries feed entries only
            ArchiveContentsTap& t = fed ? static_cast<ArchiveContentsTap&>(entriesOnly) : target;
            const auto r = tool.extractStreamed(volumes, {}, pw, members, {}, &t);
            fed = true;
            if (r.ok())
                return pw;
            tap.restartEntries();
            if (r.outcome != ExternalUnpacker::Outcome::WrongPassword)
                break;
        }
    }
    tap.spoil();
    if (!fed)
        (void)ArchiveReader::tapVolumes(volumes, tap, cancel);
    return std::nullopt;
}

std::optional<ArchiveListing> Ed3kArchiveContents::readArchive(const QStringList& volumes, const QStringList& passwords,
                                                               const std::atomic<bool>* cancel, const QString& externalTool)
{
    Ed3kContentsTap tap;
    const auto password = readSet(volumes, passwords, externalTool, tap, cancel);
    if (!password || (cancel && cancel->load()))
        return std::nullopt;
    return tap.listing(QFileInfo(volumes.first()).fileName(), *password);
}

bool Ed3kArchiveContents::publishListing(const ArchiveListing& listing)
{
    // §6.1: what was read here is the truth; a listing found for it is checked first
    (void)checkListing(listing);
    auto* e = m_session.engine();
    if (!m_started || !e || listing.entries.empty())
        return false;
    std::vector<ffi::ContentEntry> entries;
    entries.reserve(listing.entries.size());
    for (const auto& en : listing.entries)
        entries.push_back({en.path.toStdString(), en.size, en.root});
    const auto manifest = ffi::contentsManifest(entries);
    if (!manifest) {
        logDebug([&]() -> QString {
            return QStringLiteral("%1: no contents manifest (error %2)").arg(listing.firstName, errorName(manifest.error()));
        });
        return false;
    }
    const QString owner = keptOwner(listing.first);
    const QString name = safeName(listing.firstName, QStringLiteral("archive")) + QStringLiteral(".contents.ed3kset");
    const QByteArray file(reinterpret_cast<const char*>(manifest->file.data()), qsizetype(manifest->file.size()));
    keepBlob(owner, file, name, manifest->root);
    if (!isShared(manifest->root)) {
        logWarning(QStringLiteral("contents of %1 not shared").arg(listing.firstName));
        return false;
    }
    const QByteArray n = name.toUtf8();
    const QByteArray pw = listing.password.toUtf8();
    auto alias = ffi::contentsAlias(listing.first, manifest->root, std::uint64_t(file.size()),
                                    std::string_view(n.constData(), std::size_t(n.size())), manifest->files, manifest->total,
                                    std::string_view(pw.constData(), std::size_t(pw.size())));
    if (!alias) {
        logDebug([&]() -> QString { return QStringLiteral("%1: no contents record (error %2)").arg(listing.firstName, errorName(alias.error())); });
        return false;
    }
    const std::vector<ffi::Bytes> records{*alias};
    keepRecords(owner, std::nullopt, name, records);
    if (!m_holdBack) {
        if (auto ok = e->publishAlias(m_session.now(), records); !ok)
            logDebug([&]() -> QString { return QStringLiteral("contents record of %1 not published (error %2)").arg(listing.firstName, errorName(ok.error())); });
    }
    ++m_publishedCount;
    logInfo(QStringLiteral("contents of %1 published: %2 file(s)").arg(argStr(listing.firstName), argStr(listing.entries.size())));
    pumpSession();
    return true;
}

std::optional<ffi::ContentsCheck> Ed3kArchiveContents::checkListing(const ArchiveListing& listing)
{
    const auto it = m_foundListings.find(listing.first);
    if (it == m_foundListings.end())
        return std::nullopt;
    const FoundListing found = std::move(it->second);
    m_foundListings.erase(it);
    const auto check = compareListing(found.manifest, listing.entries);
    if (!check) {
        logDebug([&]() -> QString { return QStringLiteral("contents of %1 not checked: manifest unreadable").arg(listing.firstName); });
        return std::nullopt;
    }
    if (check->matches()) {
        logInfo(QStringLiteral("contents of %1 checked: as listed").arg(listing.firstName));
        return check;
    }
    ++m_mismatchCount;
    static constexpr std::array<const char*, 5> kWhat{"match", "manifest", "entry differs", "entry not listed", "member missing"};
    const char* what = check->kind < kWhat.size() ? kWhat.at(check->kind) : "mismatch";
    logWarning(QStringLiteral("contents of %1 false (%2, #%3): listing dropped, %4 publisher(s) ignored from now on")
                   .arg(argStr(listing.firstName), QLatin1String(what), argStr(check->index), argStr(found.publishers.size())));
    // was: aliasSearchBlame(now, search, root): the b3 search ended long before the unpack
    if (auto* e = m_session.engine(); e && !found.publishers.empty())
        if (auto ok = e->aliasBan(m_session.now(), found.publishers); !ok)
            logDebug([&]() -> QString { return QStringLiteral("ban of %1's publishers failed (error %2)").arg(listing.firstName, errorName(ok.error())); });
    return check;
}

std::optional<ffi::ContentsCheck> Ed3kArchiveContents::compareListing(const QByteArray& manifest, const std::vector<ArchiveEntry>& entries)
{
    std::vector<ffi::ContentEntry> list;
    list.reserve(entries.size());
    for (const auto& en : entries)
        list.push_back({en.path.toStdString(), en.size, en.root});
    const auto check = ffi::contentsCheck(std::span(reinterpret_cast<const std::byte*>(manifest.constData()), std::size_t(manifest.size())), list);
    return check ? std::optional(*check) : std::nullopt;
}

QList<int> Ed3kArchiveContents::firstVolumes(const QStringList& names)
{
    QList<int> out;
    for (const QList<int>& set : volumeSets(names))
        out << set.constFirst();
    return out;
}

QList<QList<int>> Ed3kArchiveContents::volumeSets(const QStringList& names)
{
    struct Set {
        int first = 0;                     ///< the set's opening position
        QList<std::pair<int, int>> parts;  ///< (volume position, name index)
    };
    QHash<QString, Set> sets;
    for (int i = 0; i < names.size(); ++i) {
        const auto pos = usenet::UsenetUnpacker::volumePositionOf(QFileInfo(names.at(i)).fileName());
        if (pos.index < 0)
            continue;
        // a torrent's member path: one set per directory
        Set& set = sets[QFileInfo(names.at(i)).path() + u'/' + pos.baseName];
        set.first = pos.first;
        set.parts.append({pos.index, i});
    }
    QList<QList<int>> out;
    for (Set& set : sets) {
        std::ranges::sort(set.parts);
        if (set.parts.constFirst().first > set.first)
            continue;   // headless
        QList<int> indices;
        for (const auto& [position, index] : std::as_const(set.parts))
            indices << index;
        out << indices;
    }
    std::ranges::sort(out, {}, [](const QList<int>& set) { return set.constFirst(); });
    return out;
}

std::vector<QByteArray> Ed3kArchiveContents::nzbFileIds(const QByteArray& nzbText, const QList<int>& indices)
{
    usenet::NzbInfo info;
    QString error;
    if (!usenet::NzbFile::parse(nzbText, info, error))
        return std::vector<QByteArray>(std::size_t(indices.size()));
    return nzbFileIds(info, indices);
}

std::vector<QByteArray> Ed3kArchiveContents::nzbFileIds(const usenet::NzbInfo& info, const QList<int>& indices)
{
    std::vector<QByteArray> out;
    out.reserve(std::size_t(indices.size()));
    // the ids come from the buffer form of the core (usenet-join-host.md)
    const QByteArray nzb = Ed3kUsenetJoin::nzbBuffer(info);
    for (const int f : indices) {
        QByteArray id(32, '\0');
        if (f < 0
            || ed3k_usenet_nzb_file_id(reinterpret_cast<const std::uint8_t*>(nzb.constData()), std::size_t(nzb.size()),
                                       std::uint64_t(f), reinterpret_cast<std::uint8_t*>(id.data()))
                   != ED3K_OK)
            id.clear();
        out.push_back(id);
    }
    return out;
}

// -- lookups ---------------------------------------------------------------

std::optional<quint32> Ed3kArchiveContents::lookup(Request request, Done done, int timeoutMs)
{
    if (!m_started || !m_session.engine() || m_session.contactCount() == 0 || request.volumes.empty())
        return std::nullopt;
    const quint32 id = m_nextLookup++;
    Lookup& l = m_lookups[id];
    l.id = id;
    l.request = std::move(request);
    l.done = std::move(done);
    l.found.resize(l.request.volumes.size());
    l.candidates.resize(l.request.volumes.size());
    l.timer = new QTimer;
    l.timer->setSingleShot(true);
    connect(l.timer, &QTimer::timeout, this, [this, id] { finish(id, true); });
    l.timer->start(timeoutMs);

    bool torrentWanted = false;
    for (int i = 0; i < int(l.request.volumes.size()); ++i) {
        const Volume& v = l.request.volumes[std::size_t(i)];
        l.found[std::size_t(i)].name = v.name;
        if (v.root) {
            startContents(l, i);
        } else if (!v.url.isEmpty()) {
            const auto linkId = ffi::linkId(v.url.toStdString());
            if (!linkId || !startSearch(l, i, Stage::Resolve, "url", *linkId, ED3K_REL_SAME))
                settle(l, i, State::NoRoot);
        } else if (v.nzbFileId.size() == 32) {
            if (!startSearch(l, i, Stage::Resolve, "nzb-file", bytesOf(v.nzbFileId), ED3K_REL_SAME))
                settle(l, i, State::NoRoot);
        } else if (!l.request.btih.isEmpty() || !l.request.btmh.isEmpty()) {
            torrentWanted = true;
        } else {
            settle(l, i, State::NoRoot);
        }
    }
    if (torrentWanted) {
        const bool v1 = l.request.btih.size() == 20;
        const QByteArray& hash = v1 ? l.request.btih : l.request.btmh;
        l.torrentPending = startSearch(l, -1, Stage::Torrent, v1 ? "btih" : "btmh", bytesOf(hash), ED3K_REL_SET);
        if (!l.torrentPending)
            for (std::size_t i = 0; i < l.found.size(); ++i)
                if (l.found[i].state == State::Pending && !l.request.volumes[i].root)
                    settle(l, int(i), State::NoRoot);
    }
    pumpSession();
    // everything settled at once (no names to look up): answered on the next turn
    QTimer::singleShot(0, this, [this, id] { finishIfDone(id); });
    return id;
}

void Ed3kArchiveContents::cancelLookup(quint32 id)
{
    auto it = m_lookups.find(id);
    if (it == m_lookups.end())
        return;
    it->second.done = {};
    finish(id, true);
}

QString Ed3kArchiveContents::stateName(State s)
{
    switch (s) {
    case State::Pending: return QStringLiteral("pending");
    case State::Found: return QStringLiteral("found");
    case State::NotPublished: return QStringLiteral("notPublished");
    case State::NoRoot: return QStringLiteral("noRoot");
    case State::Failed: return QStringLiteral("failed");
    case State::TimedOut: return QStringLiteral("timedOut");
    }
    return {};
}

// -- Ed3kJoinBase ------------------------------------------------------------

std::vector<std::uint64_t> Ed3kArchiveContents::wantedBlocks(const Download&) const
{
    return {};   // blobs only
}

void Ed3kArchiveContents::onFileBlock(const Download&, const ffi::event::Block&)
{
}

void Ed3kArchiveContents::onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data)
{
    const auto id = lookupOf(owner);
    auto it = id ? m_lookups.find(*id) : m_lookups.end();
    if (it == m_lookups.end())
        return;
    Lookup& l = it->second;
    if (member == kTorrentManifest) {
        onTorrentManifest(l, data);
        finishIfDone(l.id);
        return;
    }
    if (member < 0 || std::size_t(member) >= l.found.size())
        return;
    Found& f = l.found[std::size_t(member)];
    const auto m = Ed3kSetManifest::parse(data);
    auto& cands = l.candidates[std::size_t(member)];
    const auto cand = std::ranges::find_if(cands, [&](const Candidate& c) { return c.root == root; });
    if (!m || m->files.empty()) {
        logDebug([&]() -> QString { return QStringLiteral("contents manifest of %1 refused").arg(f.name); });
        if (cand != cands.end())
            cands.erase(cand);
        tryNextContents(l, member);
        finishIfDone(l.id);
        return;
    }
    f.manifest = root;
    std::vector<ffi::PublisherKey> publishers;
    if (cand != cands.end()) {
        f.sites = cand->sites;
        f.password = cand->password;
        publishers = cand->publishers;
    }
    // §6.1: kept for the check when this archive is unpacked here
    if (f.first) {
        if (m_foundListings.size() >= kMaxFoundListings && !m_foundListings.contains(*f.first))
            m_foundListings.erase(m_foundListings.begin());
        m_foundListings[*f.first] = FoundListing{f.name, data, std::move(publishers)};
    }
    f.entries.clear();
    for (const auto& e : m->files)
        if (!e.isSet && !e.isSealed())
            f.entries.push_back({e.path, e.size, e.root});
    settle(l, member, State::Found);
    finishIfDone(l.id);
}

std::optional<Ed3kJoinBase::FileRead> Ed3kArchiveContents::sharedRead(const Shared&, std::uint64_t, std::uint32_t) const
{
    return std::nullopt;   // blobs only, served from memory
}

void Ed3kArchiveContents::onOtherEvent(const ffi::Event& event)
{
    if (const auto* found = std::get_if<ev::AliasFound>(&event); found && m_searches.contains(found->search))
        onAliasFound(*found);
}

bool Ed3kArchiveContents::keepsOwner(const QString& owner) const
{
    return owner.startsWith(u'c');
}

bool Ed3kArchiveContents::sharesKeptOwner(const QString& owner) const
{
    return m_publish && owner.startsWith(u'c');
}

bool Ed3kArchiveContents::hasOwner(const QString&) const
{
    return false;   // nothing saved per owner beyond the kept blobs
}

void Ed3kArchiveContents::saveOwner(const QString&)
{
}

// -- private -----------------------------------------------------------------

QString Ed3kArchiveContents::ownerOf(quint32 lookup)
{
    return QStringLiteral("L%1").arg(lookup);
}

std::optional<quint32> Ed3kArchiveContents::lookupOf(const QString& owner)
{
    if (!owner.startsWith(u'L'))
        return std::nullopt;
    bool ok = false;
    const quint32 id = owner.mid(1).toUInt(&ok);
    return ok ? std::optional(id) : std::nullopt;
}

QString Ed3kArchiveContents::keptOwner(const ffi::Root& first)
{
    return u'c' + hexOf(first.bytes);
}

bool Ed3kArchiveContents::startSearch(Lookup& l, int volume, Stage stage, std::string_view scheme,
                                      std::span<const std::uint8_t> id, std::uint32_t rel)
{
    auto* e = m_session.engine();
    if (!e)
        return false;
    const auto search = e->aliasSearchStart(m_session.now(), scheme, id, rel);
    if (!search)
        return false;
    m_searches[*search] = {l.id, volume, stage};
    return true;
}

void Ed3kArchiveContents::startContents(Lookup& l, int volume)
{
    const auto& root = l.request.volumes[std::size_t(volume)].root;
    if (!root) {
        settle(l, volume, State::NoRoot);
        return;
    }
    l.found[std::size_t(volume)].first = *root;
    if (!startSearch(l, volume, Stage::Contents, "b3", root->bytes, ED3K_REL_SET))
        settle(l, volume, State::Failed);
}

void Ed3kArchiveContents::tryNextContents(Lookup& l, int volume)
{
    auto& cands = l.candidates[std::size_t(volume)];
    while (!cands.empty()) {
        const Candidate c = cands.front();
        if (fetchBlob(c.root, c.size, ownerOf(l.id), volume)) {
            pumpSession();
            return;
        }
        cands.erase(cands.begin());
    }
    settle(l, volume, State::NotPublished);
}

void Ed3kArchiveContents::onAliasFound(const ev::AliasFound& found)
{
    const Search s = m_searches[found.search];
    m_searches.erase(found.search);
    auto* e = m_session.engine();
    std::vector<::ed3k::Engine::AliasCandidate> cands;
    if (e && found.roots > 0) {
        while (cands.size() < kMaxCandidates) {
            auto next = e->aliasSearchNext(found.search);
            if (!next || !*next)
                break;
            cands.push_back(**next);
        }
    }
    if (e)
        (void)e->aliasSearchEnd(found.search);
    auto it = m_lookups.find(s.lookup);
    if (it == m_lookups.end())
        return;
    Lookup& l = it->second;
    switch (s.stage) {
    case Stage::Resolve:
        if (cands.empty()) {
            settle(l, s.volume, State::NoRoot);
        } else {
            l.request.volumes[std::size_t(s.volume)].root = cands.front().root;
            startContents(l, s.volume);
        }
        break;
    case Stage::Torrent:
        l.torrentPending = false;
        if (cands.empty() || !fetchBlob(cands.front().root, cands.front().size, ownerOf(l.id), kTorrentManifest)) {
            for (std::size_t i = 0; i < l.found.size(); ++i)
                if (l.found[i].state == State::Pending && !l.request.volumes[i].root)
                    settle(l, int(i), State::NoRoot);
        } else {
            l.torrentPending = true;
        }
        break;
    case Stage::Contents: {
        auto& list = l.candidates[std::size_t(s.volume)];
        list.clear();
        for (const auto& c : cands)
            if (c.rel == ED3K_REL_SET)
                // was: list.push_back({c.root, c.size, c.sites, QString::fromStdString(c.password)});
                list.push_back({c.root, c.size, c.sites, QString::fromStdString(c.password), c.publishers});
        tryNextContents(l, s.volume);
        break;
    }
    }
    pumpSession();
    finishIfDone(s.lookup);
}

void Ed3kArchiveContents::onTorrentManifest(Lookup& l, const QByteArray& data)
{
    l.torrentPending = false;
    const auto m = Ed3kSetManifest::parse(data);
    for (std::size_t i = 0; i < l.found.size(); ++i) {
        if (l.found[i].state != State::Pending || l.request.volumes[i].root)
            continue;
        const QString& want = l.request.volumes[i].name;
        std::optional<ffi::Root> root;
        if (m) {
            // the member path first, then its file name alone
            for (const auto& e : m->files)
                if (e.path == want)
                    root = e.root;
            if (!root)
                for (const auto& e : m->files)
                    if (QFileInfo(e.path).fileName() == QFileInfo(want).fileName())
                        root = e.root;
        }
        if (!root) {
            settle(l, int(i), State::NoRoot);
            continue;
        }
        l.request.volumes[i].root = root;
        startContents(l, int(i));
    }
    pumpSession();
}

void Ed3kArchiveContents::settle(Lookup& l, int volume, State state)
{
    if (volume < 0 || std::size_t(volume) >= l.found.size())
        return;
    l.found[std::size_t(volume)].state = state;
}

void Ed3kArchiveContents::finishIfDone(quint32 id)
{
    const auto it = m_lookups.find(id);
    if (it == m_lookups.end())
        return;
    const Lookup& l = it->second;
    if (l.torrentPending)
        return;
    if (std::ranges::any_of(l.found, [](const Found& f) { return f.state == State::Pending; }))
        return;
    finish(id, false);
}

void Ed3kArchiveContents::finish(quint32 id, bool timedOut)
{
    auto it = m_lookups.find(id);
    if (it == m_lookups.end())
        return;
    Lookup l = std::move(it->second);
    m_lookups.erase(it);
    if (l.timer) {
        l.timer->stop();
        l.timer->deleteLater();
    }
    auto* e = m_session.engine();
    for (auto s = m_searches.begin(); s != m_searches.end();) {
        if (s->second.lookup == id) {
            if (e)
                (void)e->aliasSearchEnd(s->first);
            s = m_searches.erase(s);
        } else {
            ++s;
        }
    }
    stopDownloads(ownerOf(id));
    if (timedOut)
        for (auto& f : l.found)
            if (f.state == State::Pending)
                f.state = State::TimedOut;
    markLocal(l.found);
    if (l.done)
        l.done(l.found);
}

QString Ed3kArchiveContents::localPathOf(const ffi::Root& root, std::uint64_t size) const
{
    return localCopyPath(m_session, root, size);
}

void Ed3kArchiveContents::markLocal(std::vector<Found>& found) const
{
    for (auto& f : found) {
        f.localPaths.clear();
        for (const auto& e : f.entries)
            f.localPaths.push_back(localPathOf(e.root, e.size));
    }
}

} // namespace eMule::ed3k
