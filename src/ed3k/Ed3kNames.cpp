/// @file Ed3kNames.cpp
/// @brief Readable names in the daemon — implementation.

#include "Ed3kNames.h"

#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDir>
#include <QDnsLookup>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cstring>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

/// owners of the base's in-memory fetches
const QString kListOwner = QStringLiteral("list");
const QString kSiteOwner = QStringLiteral("site");
/// host-side mirrors (not credentials): followed lists, petnames
const QString kListsFile = QStringLiteral("name-lists.dat");
const QString kPetnamesFile = QStringLiteral("name-petnames.dat");
/// websites kept across restarts (§6.4, Ed3kSiteCache)
const QString kSiteCacheDir = QStringLiteral("site-cache");
/// §5.1: an answer that has not come within 10 s is no answer
constexpr int kDnsTimeoutMs = 10'000;

std::string_view viewOf(const QByteArray& b)
{
    return {b.constData(), std::size_t(b.size())};
}

QString textOf(const std::string& s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

Ed3kNames::How howOf(const ffi::event::NameHow& h)
{
    return Ed3kNames::How{int(h.how), h.a, h.b, textOf(h.title)};
}

/// a TSV field
QString field(QString s)
{
    s.replace(QLatin1Char('\t'), QLatin1Char(' '));
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return s;
}

} // namespace

Ed3kNames::Ed3kNames(Ed3kSession& session, const QString& configDir, QObject* parent)
    : Ed3kJoinBase(session, configDir, QStringLiteral("names"), parent)
    , m_dns(systemResolver())
    , m_dnsGen(std::make_shared<int>(0))
{
    // the base takes blocks and streams; the names events are taken here
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kNames::onNameEvent, Qt::DirectConnection);
    m_cache = std::make_unique<Ed3kSiteCache>(joinDir(kSiteCacheDir), kSiteDiskMax);
    loadLocal();
    if (m_session.isRunning())
        start();
}

Ed3kNames::~Ed3kNames() = default;

void Ed3kNames::start()
{
    if (m_started)
        return;
    beginStart();
    m_dnsGen = std::make_shared<int>(0);
}

void Ed3kNames::stop()
{
    if (!m_started)
        return;
    if (auto* e = m_session.engine())
        for (const auto& [topic, key] : m_siteTopics)
            (void)e->setUnfollow(topic);
    closeAll();
    m_dnsGen.reset();   // late DNS answers of this run are dropped
    m_resolved.clear();
    m_sites.clear();
    m_siteTopics.clear();
    m_members.clear();
    m_memberBytes = 0;
}

void Ed3kNames::setDnsResolver(DnsResolver resolver)
{
    m_dns = resolver ? std::move(resolver) : systemResolver();
}

Ed3kNames::DnsResolver Ed3kNames::systemResolver()
{
    return [](const QString& qname, DnsDone done) {
        auto* lookup = new QDnsLookup(QDnsLookup::TXT, qname);
        auto* timer = new QTimer(lookup);
        timer->setSingleShot(true);
        auto finish = std::make_shared<DnsDone>(std::move(done));
        QObject::connect(timer, &QTimer::timeout, lookup, [lookup, finish] {
            if (*finish)
                std::exchange(*finish, nullptr)(std::nullopt);
            lookup->abort();
            lookup->deleteLater();
        });
        QObject::connect(lookup, &QDnsLookup::finished, lookup, [lookup, finish] {
            lookup->deleteLater();
            if (!*finish)
                return;
            auto cb = std::exchange(*finish, nullptr);
            // no record: an empty list; no answer: nullopt (ffi.md "Names")
            if (lookup->error() == QDnsLookup::NotFoundError) {
                cb(DnsAnswer{});
                return;
            }
            if (lookup->error() != QDnsLookup::NoError) {
                cb(std::nullopt);
                return;
            }
            DnsAnswer a;
            std::optional<quint32> ttl;
            for (const QDnsTextRecord& r : lookup->textRecords()) {
                QByteArray joined;   // the strings of one record without a separator (§5.1)
                for (const QByteArray& v : r.values())
                    joined += v;
                a.records.emplace_back(joined.constData(), std::size_t(joined.size()));
                ttl = ttl ? std::min(*ttl, r.timeToLive()) : r.timeToLive();
            }
            a.ttl = ttl.value_or(0);
            // secure stays false: the AD bit of an unvalidated resolver proves nothing (§5.3)
            cb(std::move(a));
        });
        timer->start(kDnsTimeoutMs);
        lookup->lookup();
    };
}

bool Ed3kNames::isRunning() const
{
    return m_session.isRunning();
}

ffi::Result<std::uint64_t> Ed3kNames::resolve(const QString& text)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray t = text.trimmed().toUtf8();
    auto id = e->nameResolve(m_session.now(), viewOf(t));
    pumpSession();
    return id;
}

ffi::Result<Ed3kNames::Resolved> Ed3kNames::open(std::uint64_t id)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const auto it = m_resolved.find(id);
    if (it == m_resolved.end())
        return std::unexpected(ffi::Error::NotFound);
    if (auto ok = e->nameOpen(m_session.now(), id); !ok)
        return std::unexpected(ok.error());
    Resolved r = std::move(it->second);
    m_resolved.erase(it);
    pumpSession();
    emit namesChanged();   // a new pin
    return r;
}

std::optional<Ed3kNames::Resolved> Ed3kNames::resolution(std::uint64_t id) const
{
    const auto it = m_resolved.find(id);
    if (it == m_resolved.end())
        return std::nullopt;
    return it->second;
}

std::vector<Ed3kNames::Pin> Ed3kNames::pins()
{
    std::vector<Pin> out;
    auto* e = m_session.engine();
    if (!e)
        return out;
    const auto names = e->namePins();
    if (!names)
        return out;
    for (const std::string& n : *names) {
        const auto p = e->namePin(n);
        if (!p)
            continue;
        out.push_back(Pin{textOf(n), p->zone, p->since, howOf(p->how), p->vouched, textOf(p->link)});
    }
    return out;
}

ffi::Result<> Ed3kNames::unpin(const QString& name)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray n = name.toUtf8();
    auto ok = e->nameUnpin(m_session.now(), viewOf(n));
    pumpSession();
    if (ok)
        emit namesChanged();
    return ok;
}

ffi::Result<> Ed3kNames::vouch(const QString& name, bool on)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray n = name.toUtf8();
    auto ok = e->nameVouch(m_session.now(), viewOf(n), on);
    pumpSession();
    if (ok)
        emit namesChanged();
    return ok;
}

ffi::Result<> Ed3kNames::setPetname(const QString& text, const ffi::Key32& zone, bool shareable)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray t = text.toUtf8();
    auto ok = e->namePetnameSet(m_session.now(), viewOf(t), zone, shareable);
    pumpSession();
    if (!ok)
        return ok;
    std::erase_if(m_petnames, [&](const Petname& p) { return p.text == text; });
    m_petnames.push_back(Petname{text, zone, shareable});
    savePetnames();
    emit namesChanged();
    return {};
}

ffi::Result<> Ed3kNames::removePetname(const QString& text)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray t = text.toUtf8();
    auto ok = e->namePetnameRemove(m_session.now(), viewOf(t));
    pumpSession();
    // one the core no longer has is gone here too
    if (std::erase_if(m_petnames, [&](const Petname& p) { return p.text == text; }) > 0) {
        savePetnames();
        emit namesChanged();
    }
    return ok;
}

ffi::Result<std::uint32_t> Ed3kNames::followList(const QString& link, const QString& label)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray l = link.trimmed().toUtf8();
    const QByteArray b = label.toUtf8();
    auto id = e->nameListFollow(m_session.now(), viewOf(l), viewOf(b));
    pumpSession();
    if (!id)
        return id;
    std::erase_if(m_lists, [&](const List& x) { return x.id == *id; });
    m_lists.push_back(List{*id, link.trimmed(), label, 0, false});
    saveLists();
    emit namesChanged();
    return id;
}

ffi::Result<> Ed3kNames::unfollowList(std::uint32_t id)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    auto ok = e->nameListUnfollow(m_session.now(), id);
    pumpSession();
    if (std::erase_if(m_lists, [&](const List& x) { return x.id == id; }) > 0) {
        saveLists();
        emit namesChanged();
    }
    return ok;
}

ffi::Result<> Ed3kNames::moveList(std::uint32_t id, std::uint32_t place)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    auto ok = e->nameListMove(m_session.now(), id, place);
    pumpSession();
    if (!ok)
        return ok;
    const auto it = std::ranges::find_if(m_lists, [&](const List& x) { return x.id == id; });
    if (it != m_lists.end()) {
        List l = *it;
        m_lists.erase(it);
        m_lists.insert(m_lists.begin() + std::min<std::ptrdiff_t>(place, std::ptrdiff_t(m_lists.size())), l);
        saveLists();
    }
    emit namesChanged();
    return {};
}

std::optional<Ed3kNames::SetRef> Ed3kNames::parseSetLink(const QString& link)
{
    const QString l = link.trimmed();
    if (!l.startsWith(QLatin1String("ed3k:set?"), Qt::CaseInsensitive))
        return std::nullopt;
    const QUrlQuery q(l.mid(9));
    // file-set.md §5.4: key, id, path once each; a hidden (s) or paid set is no website here
    for (const char* once : {"key", "id", "path"})
        if (q.allQueryItemValues(QString::fromLatin1(once)).size() > 1)
            return std::nullopt;
    if (q.hasQueryItem(QStringLiteral("s")) || !q.hasQueryItem(QStringLiteral("id")))
        return std::nullopt;
    const auto key = fromBase32(q.queryItemValue(QStringLiteral("key")));
    const auto id = fromBase32(q.queryItemValue(QStringLiteral("id")));
    const auto k = key ? keyFrom(*key) : std::nullopt;
    if (!k || !id || id->isEmpty() || id->size() > 32)
        return std::nullopt;
    return SetRef{*k, *id, q.queryItemValue(QStringLiteral("path"), QUrl::FullyDecoded)};
}

std::optional<Ed3kNames::SetRef> Ed3kNames::firstSet(const QStringList& links)
{
    for (const QString& l : links)
        if (auto s = parseSetLink(l))
            return s;
    return std::nullopt;
}

QByteArray Ed3kNames::siteManifest(const ffi::Key32& key, const QByteArray& id) const
{
    // const auto it = m_sites.find(siteKey(key, id));
    // return it == m_sites.end() ? QByteArray() : it->second.manifest;
    // the one of this run, else the one kept on disk (§6.4: served until a newer one is in)
    const QString k = siteKey(key, id);
    if (const auto it = m_sites.find(k); it != m_sites.end() && !it->second.manifest.isEmpty())
        return it->second.manifest;
    return m_cache->manifest(k).value_or(QByteArray());
}

bool Ed3kNames::siteFollowed(const ffi::Key32& key, const QByteArray& id) const
{
    const auto it = m_sites.find(siteKey(key, id));
    return it != m_sites.end() && it->second.topic.has_value();
}

bool Ed3kNames::followSite(const ffi::Key32& key, const QByteArray& id)
{
    auto* e = m_session.engine();
    if (!e || !m_started)
        return false;
    const QString k = siteKey(key, id);
    Site& s = m_sites[k];
    s.key = key;
    s.id = id;
    if (s.topic)
        return true;
    // was: nothing held is named: the manifests live in memory for this run (ToDo §6.4 in keepMember)
    // (done: the manifest kept on disk is named as held, only newer heads are fetched)
    // auto topic = e->setFollow(m_session.now(), key, spanOf(id), {}, nullptr, ffi::SetPolicy::Ask);
    const QByteArray held = siteManifest(key, id);
    std::optional<ffi::Root> heldRoot;
    if (!held.isEmpty()) {
        ffi::Hasher h;
        if (h.update(spanOf(held)))
            if (auto done = h.finish())
                heldRoot = done->root;
    }
    auto topic = heldRoot ? e->setFollow(m_session.now(), key, spanOf(id), spanOf(held), &*heldRoot, ffi::SetPolicy::Ask)
                          : e->setFollow(m_session.now(), key, spanOf(id), {}, nullptr, ffi::SetPolicy::Ask);
    if (!topic) {
        logDebug(QStringLiteral("website set not followed (error %1)").arg(errorName(topic.error())));
        return false;
    }
    s.topic = *topic;
    m_siteTopics[*topic] = k;
    pumpSession();
    return true;
}

void Ed3kNames::setSiteManifest(const ffi::Key32& key, const QByteArray& id, const QByteArray& manifest)
{
    Site& s = m_sites[siteKey(key, id)];
    s.key = key;
    s.id = id;
    s.manifest = manifest;
    m_cache->putManifest(siteKey(key, id), manifest);
    emit siteChanged(toBytes(key), id);
}

std::optional<QByteArray> Ed3kNames::siteMember(const ffi::Root& root)
{
    const auto it = std::ranges::find_if(m_members, [&](const auto& m) { return m.first == root; });
    if (it == m_members.end()) {
        // kept on disk (an earlier run, or gone from memory): checked against its root there
        auto kept = m_cache->member(root);
        if (!kept)
            return std::nullopt;
        keepMember(root, *kept, true);
        return kept;
    }
    m_members.splice(m_members.end(), m_members, it);   // the newest now
    return m_members.back().second;
}

std::optional<QByteArray> Ed3kNames::siteMemberRange(const ffi::Root& root, std::uint64_t offset, std::uint64_t len)
{
    if (len == 0)
        return std::nullopt;
    if (const Download* d = downloadOf(root); d && d->inMemory) {
        // a fetch in memory: only the blocks there (each verified under the root, §6.3)
        if (offset >= d->size || len > d->size - offset || d->memory.size() != qsizetype(d->size))
            return std::nullopt;
        for (std::uint64_t b = offset / kBlock * kBlock; b < offset + len; b += kBlock)
            if (!d->got.contains(b))
                return std::nullopt;
        return d->memory.mid(qsizetype(offset), qsizetype(len));
    }
    const auto whole = siteMember(root);
    if (!whole || offset >= std::uint64_t(whole->size()) || len > std::uint64_t(whole->size()) - offset)
        return std::nullopt;
    return whole->mid(qsizetype(offset), qsizetype(len));
}

bool Ed3kNames::fetchSiteMember(const ffi::Root& root, std::uint64_t size)
{
    if (!m_started || size == 0 || size > kSiteMemberMax)
        return false;
    if (downloading(root))
        return true;
    if (!fetchBlob(root, size, kSiteOwner, 0))
        return false;
    pumpSession();
    return true;
}

QByteArray Ed3kNames::toBytes(std::span<const std::uint8_t> bytes)
{
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
}

std::optional<ffi::Key32> Ed3kNames::keyFrom(const QByteArray& bytes)
{
    ffi::Key32 key{};
    if (bytes.size() != qsizetype(key.size()))
        return std::nullopt;
    std::memcpy(key.data(), bytes.constData(), key.size());
    return key;
}

std::optional<ffi::Key32> Ed3kNames::keyFromText(const QString& text)
{
    const QString t = text.trimmed();
    if (t.size() == 64) {
        const QByteArray raw = QByteArray::fromHex(t.toLatin1());
        if (raw.size() == 32)
            return keyFrom(raw);
    }
    const auto raw = fromBase32(t);
    return raw ? keyFrom(*raw) : std::nullopt;
}

// -- the base's hooks (in-memory fetches only) --------------------------------

std::vector<std::uint64_t> Ed3kNames::wantedBlocks(const Download& d) const
{
    Q_UNUSED(d)
    return {};
}

void Ed3kNames::onFileBlock(const Download& d, const ffi::event::Block& b)
{
    Q_UNUSED(d) Q_UNUSED(b)
}

void Ed3kNames::onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data)
{
    auto* e = m_session.engine();
    if (!e)
        return;
    if (owner == kListOwner) {
        // §4.5: the core checks the root, the magic and the limits
        if (auto ok = e->nameListFile(m_session.now(), root, spanOf(data)); !ok)
            logDebug(QStringLiteral("list file of list %1 not taken (error %2)").arg(member).arg(errorName(ok.error())));
        pumpSession();
        return;
    }
    if (owner == kSiteOwner) {
        keepMember(root, data);
        emit siteMemberReady(toBytes(root.bytes));
    }
}

std::optional<Ed3kJoinBase::FileRead> Ed3kNames::sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const
{
    // only blobs from memory are shared here
    Q_UNUSED(s) Q_UNUSED(offset) Q_UNUSED(len)
    return std::nullopt;
}

bool Ed3kNames::hasOwner(const QString& owner) const
{
    Q_UNUSED(owner)
    return false;
}

void Ed3kNames::saveOwner(const QString& owner)
{
    Q_UNUSED(owner)
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kNames::onNameEvent(const ffi::Event& ev, bool* handled)
{
    namespace e = ffi::event;
    if (const auto* q = std::get_if<e::NameDns>(&ev)) {
        *handled = true;
        onDns(*q);
    } else if (const auto* r = std::get_if<e::NameResolved>(&ev)) {
        *handled = true;
        onResolved(*r);
    } else if (const auto* f = std::get_if<e::NameFailed>(&ev)) {
        *handled = true;
        QList<QByteArray> ties;
        for (const auto& t : f->ties)
            ties.append(toBytes(t));
        logEd3kDebug(QStringLiteral("ed3k: name %1 not resolved (why %2)").arg(textOf(f->name)).arg(f->why));
        emit failed(f->id, textOf(f->name), int(f->why), ties);
    } else if (const auto* lf = std::get_if<e::NameListFetch>(&ev)) {
        *handled = true;
        onListFetch(*lf);
    } else if (const auto* u = std::get_if<e::NameListUpdated>(&ev)) {
        *handled = true;
        for (List& l : m_lists)
            if (l.id == u->list) {
                l.entries = u->entries;
                l.refused = false;
            }
        emit namesChanged();
    } else if (const auto* rf = std::get_if<e::NameListRefused>(&ev)) {
        *handled = true;
        for (List& l : m_lists)
            if (l.id == rf->list)
                l.refused = true;
        logEd3kWarning(QStringLiteral("ed3k: a file of name list %1 is no name list, not read").arg(rf->list));
        emit namesChanged();
    } else if (const auto* fl = std::get_if<e::NameFriendList>(&ev)) {
        *handled = true;
        emit friendListOffered(toBytes(fl->node.bytes), textOf(fl->link));
    } else if (const auto* sm = std::get_if<e::SetManifest>(&ev)) {
        if (m_siteTopics.contains(sm->set)) {
            *handled = true;
            onSiteManifest(sm->set, sm->file);
        }
    } else if (const auto* sr = std::get_if<e::SetManifestRefused>(&ev)) {
        if (m_siteTopics.contains(sr->set)) {
            *handled = true;
            logEd3kDebug(QStringLiteral("ed3k: a website manifest was refused (%1)").arg(int(sr->why)));
        }
    } else if (const auto* sc = std::get_if<e::StateChanged>(&ev); sc && sc->slot == ffi::State::Names) {
        emit namesChanged();   // the session saves the slot
    }
}

void Ed3kNames::onDns(const ffi::event::NameDns& q)
{
    const std::uint64_t id = q.id;
    const QString qname = textOf(q.qname);
    logEd3kDebug(QStringLiteral("ed3k: DNS TXT question for %1").arg(qname));
    QPointer<Ed3kNames> self(this);
    std::weak_ptr<int> gen = m_dnsGen;
    m_dns(qname, [self, gen, id](std::optional<DnsAnswer> a) {
        // a late answer of an earlier run is dropped
        if (!self || gen.expired())
            return;
        auto* e = self->m_session.engine();
        if (!e)
            return;
        const auto t = self->m_session.now();
        const auto ok = a ? e->nameDnsAnswer(t, id, a->records, a->secure, a->ttl) : e->nameDnsFailed(t, id);
        if (!ok && ok.error() != ffi::Error::Stale)
            logEd3kDebug(QStringLiteral("ed3k: DNS answer not taken (error %1)").arg(int(ok.error())));
        self->pumpSession();
    });
}

void Ed3kNames::onResolved(const ffi::event::NameResolved& n)
{
    Resolved r;
    r.id = n.id;
    r.name = textOf(n.name);
    r.zone = n.zone;
    r.label = textOf(n.label);
    for (const std::string& l : n.links)
        r.links.append(textOf(l));
    r.how = howOf(n.how);
    for (const auto& w : n.warnings)
        r.warnings.push_back(Seen{w.zone, howOf(w.how)});
    r.ties = n.ties;
    r.confirm = n.confirm;
    r.webDisagree = n.webDisagree;
    m_resolved[r.id] = r;
    // at most a few hundred kept: the oldest go
    while (m_resolved.size() > 256)
        m_resolved.erase(m_resolved.begin());
    logEd3kDebug(QStringLiteral("ed3k: name %1 resolved (how %2, %3 link(s))").arg(r.name).arg(r.how.how).arg(r.links.size()));
    emit resolved(r);
}

void Ed3kNames::onListFetch(const ffi::event::NameListFetch& f)
{
    // the core opened the download; the base finds sources and fetches it into memory
    if (f.size == 0 || f.size > kListFileMax) {
        logEd3kDebug(QStringLiteral("ed3k: list file %1 of %2 B not fetched").arg(textOf(f.name)).arg(f.size));
        return;
    }
    if (!m_started || downloading(f.root))
        return;
    if (!fetchBlob(f.root, f.size, kListOwner, int(f.list)))
        logEd3kDebug(QStringLiteral("ed3k: list file %1 not fetched").arg(textOf(f.name)));
}

void Ed3kNames::onSiteManifest(const ffi::Topic& topic, const ffi::Bytes& file)
{
    const auto t = m_siteTopics.find(topic);
    if (t == m_siteTopics.end())
        return;
    const auto it = m_sites.find(t->second);
    if (it == m_sites.end())
        return;
    // §6.3: a new version is served from the first request after its manifest is complete
    it->second.manifest = QByteArray(reinterpret_cast<const char*>(file.data()), qsizetype(file.size()));
    m_cache->putManifest(t->second, it->second.manifest);
    emit siteChanged(toBytes(it->second.key), it->second.id);
}

void Ed3kNames::keepMember(const ffi::Root& root, const QByteArray& data, bool fromDisk)
{
    if (std::ranges::any_of(m_members, [&](const auto& m) { return m.first == root; }))
        return;
    if (!fromDisk)
        m_cache->putMember(root, data);
    m_members.emplace_back(root, data);
    m_memberBytes += std::uint64_t(data.size());
    // §6.4: visitors serve the site (shared from memory while kept; no SOURCE record here)
    (void)shareBlob(data, QString(), root);
    while (m_memberBytes > kSiteCacheMax && m_members.size() > 1) {
        const auto& [old, bytes] = m_members.front();
        m_memberBytes -= std::min<std::uint64_t>(m_memberBytes, std::uint64_t(bytes.size()));
        unshare(old);
        m_members.pop_front();
    }
    // was: ToDo: keep website members and manifests across restarts, 2 GiB by default, the
    // sites viewed longest ago first (§6.4) (done: Ed3kSiteCache, LRU by last use)
}

QString Ed3kNames::siteKey(const ffi::Key32& key, const QByteArray& id)
{
    return QString::fromLatin1(toBytes(key).toHex()) + QLatin1Char('.') + QString::fromLatin1(id.toHex());
}

void Ed3kNames::loadLocal()
{
    // id<TAB>label<TAB>link per followed list, in the user's order
    const QByteArray lists = readBlob(localPath(kListsFile));
    for (const QByteArray& line : lists.split('\n')) {
        const QList<QByteArray> f = line.split('\t');
        if (f.size() < 3)
            continue;
        bool ok = false;
        const std::uint32_t id = f[0].toUInt(&ok);
        if (ok)
            m_lists.push_back(List{id, QString::fromUtf8(f[2]), QString::fromUtf8(f[1]), 0, false});
    }
    // text<TAB>zone hex<TAB>shareable
    const QByteArray pets = readBlob(localPath(kPetnamesFile));
    for (const QByteArray& line : pets.split('\n')) {
        const QList<QByteArray> f = line.split('\t');
        if (f.size() < 3)
            continue;
        if (const auto zone = keyFrom(QByteArray::fromHex(f[1])))
            m_petnames.push_back(Petname{QString::fromUtf8(f[0]), *zone, f[2] == "1"});
    }
}

void Ed3kNames::saveLists() const
{
    QByteArray out;
    for (const List& l : m_lists)
        out += QByteArray::number(l.id) + '\t' + field(l.label).toUtf8() + '\t' + field(l.link).toUtf8() + '\n';
    if (!writeBlob(localPath(kListsFile), spanOf(out)))
        logEd3kWarning(QStringLiteral("ed3k: %1 not written").arg(kListsFile));
}

void Ed3kNames::savePetnames() const
{
    QByteArray out;
    for (const Petname& p : m_petnames)
        out += field(p.text).toUtf8() + '\t' + toBytes(p.zone).toHex() + '\t' + (p.shareable ? "1" : "0") + '\n';
    if (!writeBlob(localPath(kPetnamesFile), spanOf(out)))
        logEd3kWarning(QStringLiteral("ed3k: %1 not written").arg(kPetnamesFile));
}

QString Ed3kNames::localPath(const QString& name) const
{
    return joinDir(name);
}

} // namespace eMule::ed3k
