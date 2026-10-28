/// @file Ed3kSets.cpp
/// @brief Followed file sets — implementation.

#include "Ed3kSets.h"

#include "Ed3kDownloads.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cstring>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

/// inner sets below a set at most (file-set.md §2.5 has the core's limits; this only stops a loop)
constexpr int kDepthMax = 8;
const QString kSetsFile = QStringLiteral("sets.json");

QByteArray bytesOf(const ffi::Bytes& b)
{
    return QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size()));
}

std::span<const std::byte> spanOf(const QByteArray& b)
{
    return {reinterpret_cast<const std::byte*>(b.constData()), std::size_t(b.size())};
}

QString hexOf(std::span<const std::uint8_t> b)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size())).toHex());
}

std::optional<ffi::Root> rootOf(const QString& hex)
{
    const QByteArray raw = QByteArray::fromHex(hex.toLatin1());
    ffi::Root r;
    if (raw.size() != qsizetype(r.bytes.size()))
        return std::nullopt;
    std::memcpy(r.bytes.data(), raw.constData(), r.bytes.size());
    return r;
}

/// whether @p path or a directory above it is in @p skipped
bool isSkipped(const QSet<QString>& skipped, const QString& path)
{
    if (skipped.contains(path))
        return true;
    for (qsizetype i = path.indexOf(QLatin1Char('/')); i > 0; i = path.indexOf(QLatin1Char('/'), i + 1))
        if (skipped.contains(path.left(i)))
            return true;
    return false;
}

} // namespace

Ed3kSets::Ed3kSets(Ed3kSession& session, Ed3kDownloads& downloads, const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_downloads(downloads)
    , m_configDir(configDir)
{
    load();
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kSets::onEventPolled, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kSets::onSessionStatus);
    // member progress shows in the list; a member's end gives the next its turn
    connect(&m_downloads, &Ed3kDownloads::changed, this, [this] {
        if (m_sets.empty())
            return;
        for (auto& [id, s] : m_sets)
            pumpQueue(s);
        markChanged();
    });
    connect(&m_downloads, &Ed3kDownloads::completed, this, [this](const ffi::Root& root, const QString&) { pumpOwnerOf(root); });
    connect(&m_downloads, &Ed3kDownloads::failed, this, [this](const ffi::Root& root, const QString&) { pumpOwnerOf(root); });
    connect(&m_downloads, &Ed3kDownloads::removed, this, [this](const ffi::Root&) {
        for (auto& [id, s] : m_sets)
            pumpQueue(s);
    });
    onSessionStatus();
}

Ed3kSets::~Ed3kSets() = default;

std::expected<std::uint32_t, QString> Ed3kSets::follow(const QString& link, Policy policy)
{
    const QString text = link.trimmed();
    const auto parsed = parseLink(text);
    if (!parsed)
        return std::unexpected(tr("This is not a valid ed3k set link."));
    // followed already: the policy given now holds
    for (auto& [id, s] : m_sets) {
        const bool same = parsed->hidden ? s.link == text
                                         : (!s.parsed.hidden && s.parsed.key == parsed->key && s.parsed.id == parsed->id
                                            && s.parsed.b3 == parsed->b3);
        if (same) {
            setPolicy(id, policy);
            return id;
        }
    }
    Set s;
    s.id = m_nextId++;
    s.link = text;
    s.parsed = *parsed;
    s.policy = policy;
    s.title = parsed->name;
    auto [it, inserted] = m_sets.emplace(s.id, std::move(s));
    // the link of a hidden set is a credential: never in a log line
    logEd3k(QStringLiteral("ed3k: set %1 followed (%2)").arg(it->first).arg(it->second.parsed.hidden ? QStringLiteral("hidden") : hexOf(it->second.parsed.key).left(8)));
    if (m_attached)
        (void)followOnEngine(it->second);
    save();
    markChanged();
    return it->first;
}

bool Ed3kSets::unfollow(std::uint32_t id)
{
    auto it = m_sets.find(id);
    if (it == m_sets.end())
        return false;
    Set& s = it->second;
    if (auto* e = m_session.engine(); e && s.topic)
        (void)e->setUnfollow(*s.topic);
    std::erase_if(m_mountTopics, [id](const auto& kv) { return kv.second.first == id; });
    // the manifests go, the member files stay (file-set.md §5.3: nothing is deleted)
    for (const auto* v : {&s.applied, &s.pending})
        if (*v)
            QFile::remove(QDir(setsDir()).filePath(hexOf((*v)->root.bytes) + QStringLiteral(".manifest")));
    for (const auto& [root, file] : s.inner)
        QFile::remove(QDir(setsDir()).filePath(hexOf(root.bytes) + QStringLiteral(".manifest")));
    m_sets.erase(it);
    logEd3k(QStringLiteral("ed3k: set %1 no longer followed").arg(id));
    save();
    markChanged();
    m_session.pump();
    return true;
}

bool Ed3kSets::setPolicy(std::uint32_t id, Policy policy)
{
    Set* s = find(id);
    if (!s)
        return false;
    s->policy = policy;
    save();
    // a version waiting for the user is taken now
    if (policy == Policy::Download && s->pending)
        applyVersion(id);
    markChanged();
    return true;
}

bool Ed3kSets::setSkipped(std::uint32_t id, const QStringList& paths)
{
    Set* s = find(id);
    if (!s)
        return false;
    s->skipped = QSet<QString>(paths.begin(), paths.end());
    // members left out now: their unfinished downloads stop (part files go, nothing complete)
    if (s->applied) {
        for (const Ed3kSetEntry& m : flatten(*s, s->applied->file)) {
            if (!isSkipped(s->skipped, m.path))
                continue;
            const auto d = m_downloads.info(m.root);
            if (d && d->owner == ownerOf(id) && d->state != Ed3kDownloadInfo::State::Complete)
                m_downloads.cancel(m.root);
        }
    }
    save();
    markChanged();
    return true;
}

bool Ed3kSets::applyVersion(std::uint32_t id)
{
    Set* s = find(id);
    if (!s || (!s->pending && !s->applied))
        return false;
    if (s->pending) {
        if (s->applied && s->applied->root != s->pending->root)
            QFile::remove(QDir(setsDir()).filePath(hexOf(s->applied->root.bytes) + QStringLiteral(".manifest")));
        s->applied = std::move(s->pending);
        s->pending.reset();
        logEd3k(QStringLiteral("ed3k: set %1 version %2 taken").arg(id).arg(s->applied->seq));
    }
    download(*s);
    save();
    markChanged();
    return true;
}

bool Ed3kSets::downloadParts(std::uint32_t id, const std::optional<ffi::Key32>& author)
{
    Set* s = find(id);
    auto* e = m_session.engine();
    if (!s || !s->topic || !e)
        return false;
    const auto ok = e->setPartDownload(m_session.now(), *s->topic, author ? &*author : nullptr);
    m_session.pump();
    if (!ok)
        logEd3kDebug(QStringLiteral("ed3k: parts of set %1 not downloaded (error %2)").arg(id).arg(int(ok.error())));
    return ok.has_value();
}

std::vector<Ed3kSets::Info> Ed3kSets::list() const
{
    std::vector<Info> out;
    out.reserve(m_sets.size());
    for (const auto& [id, s] : m_sets)
        out.push_back(infoOf(s));
    return out;
}

std::optional<Ed3kSets::Info> Ed3kSets::info(std::uint32_t id) const
{
    const Set* s = find(id);
    return s ? std::optional(infoOf(*s)) : std::nullopt;
}

bool Ed3kSets::isRunning() const
{
    return m_session.isRunning();
}

QString Ed3kSets::ownerOf(std::uint32_t id)
{
    return QStringLiteral("set:%1").arg(id);
}

QString Ed3kSets::setLink(const ffi::Key32& key, const QByteArray& id, const QString& name)
{
    QString link = QStringLiteral("ed3k:set?key=%1&id=%2")
                       .arg(base32(key), base32(std::span(reinterpret_cast<const std::uint8_t*>(id.constData()), std::size_t(id.size()))));
    if (!name.isEmpty())
        link += QStringLiteral("&dn=") + QString::fromLatin1(QUrl::toPercentEncoding(name));
    return link;
}

QString Ed3kSets::statePath() const
{
    return QDir(stateDir(m_configDir)).filePath(kSetsFile);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kSets::onEventPolled(const ffi::Event& event, bool* handled)
{
    namespace e = ffi::event;
    QString mount;
    if (const auto* h = std::get_if<e::SetHead>(&event)) {
        if (Set* s = setOfTopic(h->set)) {
            *handled = true;
            logEd3kDebug(QStringLiteral("ed3k: set %1 has version %2, fetching it").arg(s->id).arg(h->seq));
        }
    } else if (const auto* hh = std::get_if<e::SetHiddenHead>(&event)) {
        if (Set* s = setOfTopic(hh->set)) {
            *handled = true;
            logEd3kDebug(QStringLiteral("ed3k: hidden set %1 has version %2, fetching it").arg(s->id).arg(hh->seq));
        }
    } else if (const auto* m = std::get_if<e::SetManifest>(&event)) {
        if (Set* s = setOfTopic(m->set, &mount)) {
            *handled = true;
            if (mount.isEmpty())
                onManifest(*s, m->root, m->file, m->seq);
            else
                onMountManifest(*s, mount, m->root, m->file);
        }
    } else if (const auto* r = std::get_if<e::SetManifestRefused>(&event)) {
        if (Set* s = setOfTopic(r->set)) {
            *handled = true;
            s->error = QStringLiteral("a version was refused (%1)").arg(int(r->why));
            logEd3kDebug(QStringLiteral("ed3k: set %1: %2").arg(s->id).arg(s->error));
            markChanged();
        }
    } else if (const auto* in = std::get_if<e::SetInner>(&event)) {
        if (Set* s = setOfTopic(in->set)) {
            *handled = true;
            onInner(*s, in->root, in->file);
        }
    } else if (const auto* mt = std::get_if<e::SetMount>(&event)) {
        if (Set* s = setOfTopic(mt->set, &mount)) {
            *handled = true;
            // a mount below a mount keeps the full path
            const QString path = mount.isEmpty() ? QString::fromStdString(mt->path) : mount + QLatin1Char('/') + QString::fromStdString(mt->path);
            m_mountTopics[mt->mounted] = {s->id, path};
            markChanged();
        }
    } else if (const auto* p = std::get_if<e::SetPart>(&event)) {
        if (Set* s = setOfTopic(p->set)) {
            *handled = true;
            Part part{p->author, p->seq, QString::fromStdString(p->dir), QString::fromStdString(p->name), 0, 0};
            if (const auto pm = Ed3kSetManifest::parse(bytesOf(p->file))) {
                part.files = int(pm->files.size());
                for (const auto& f : pm->files)
                    part.size += f.size;
            }
            std::erase_if(s->parts, [&](const Part& x) { return x.author == p->author; });
            s->parts.push_back(part);
            markChanged();
        }
    } else if (const auto* sm = std::get_if<e::SetMember>(&event)) {
        if (Set* s = setOfTopic(sm->set)) {
            *handled = true;
            onMember(*s, *sm);
        }
    } else if (const auto* ms = std::get_if<e::SetMemberSources>(&event)) {
        if (setOfTopic(ms->set)) {
            *handled = true;
            // the manifest's sources that have it: asked first (§3, §4.1)
            if (m_downloads.contains(ms->member))
                for (const auto& src : ms->sources)
                    m_downloads.addSourceTo(ms->member, src);
        }
    }
}

void Ed3kSets::onSessionStatus()
{
    // followed once the node has contacts: a HEAD search without any is tried again only
    // after 5 min (the core's SEARCH_RETRY_S)
    if (m_session.isRunning() && !m_attached && m_session.contactCount() > 0)
        attach();
    else if (!m_session.isRunning() && m_attached) {
        m_attached = false;
        for (auto& [id, s] : m_sets)
            s.topic.reset();
        m_mountTopics.clear();
        markChanged();
    }
}

void Ed3kSets::attach()
{
    m_attached = true;
    for (auto& [id, s] : m_sets) {
        (void)followOnEngine(s);
        pumpQueue(s);
    }
    markChanged();
}

void Ed3kSets::pumpOwnerOf(const ffi::Root& root)
{
    const auto d = m_downloads.info(root);
    for (auto& [id, s] : m_sets)
        if (!d || d->owner == ownerOf(id))
            pumpQueue(s);
}

bool Ed3kSets::followOnEngine(Set& s)
{
    auto* e = m_session.engine();
    if (!e)
        return false;
    const auto t = m_session.now();
    // the engine only tells; members are chosen here (see the header)
    constexpr auto kEngine = ffi::SetPolicy::Ask;
    ffi::Result<ffi::Topic> topic = std::unexpected(ffi::Error::State);
    if (s.parsed.hidden) {
        const QByteArray l = s.link.toUtf8();
        topic = e->setFollowHidden(t, std::string_view(l.constData(), std::size_t(l.size())), {}, nullptr, kEngine);
    } else if (s.parsed.b3) {
        topic = e->setFollowPinned(t, s.parsed.key, spanOf(s.parsed.id), *s.parsed.b3, s.parsed.sz, kEngine);
    } else {
        // the newest version held: only newer heads are fetched
        const auto& held = s.pending ? s.pending : s.applied;
        if (held)
            topic = e->setFollow(t, s.parsed.key, spanOf(s.parsed.id), spanOf(held->file), &held->root, kEngine);
        if (!topic)
            topic = e->setFollow(t, s.parsed.key, spanOf(s.parsed.id), {}, nullptr, kEngine);
    }
    if (!topic) {
        s.error = QStringLiteral("not followed (error %1)").arg(int(topic.error()));
        logEd3kWarning(QStringLiteral("ed3k: set %1 %2").arg(s.id).arg(s.error));
        return false;
    }
    s.topic = *topic;
    s.error.clear();
    m_session.pump();
    return true;
}

Ed3kSets::Set* Ed3kSets::setOfTopic(const ffi::Topic& topic, QString* mountPath)
{
    for (auto& [id, s] : m_sets)
        if (s.topic == topic)
            return &s;
    if (const auto it = m_mountTopics.find(topic); it != m_mountTopics.end()) {
        if (mountPath)
            *mountPath = it->second.second;
        return find(it->second.first);
    }
    return nullptr;
}

Ed3kSets::Set* Ed3kSets::find(std::uint32_t id)
{
    const auto it = m_sets.find(id);
    return it == m_sets.end() ? nullptr : &it->second;
}

const Ed3kSets::Set* Ed3kSets::find(std::uint32_t id) const
{
    const auto it = m_sets.find(id);
    return it == m_sets.end() ? nullptr : &it->second;
}

void Ed3kSets::onManifest(Set& s, const ffi::Root& root, const ffi::Bytes& file, std::uint64_t seq)
{
    const QByteArray f = bytesOf(file);
    const auto m = Ed3kSetManifest::parse(f);
    if (!m) {
        logEd3kWarning(QStringLiteral("ed3k: set %1: a version that cannot be read").arg(s.id));
        return;
    }
    // the same again (a hidden set followed anew after a restart)
    if ((s.applied && s.applied->root == root) || (s.pending && s.pending->root == root))
        return;
    storeManifest(root, f);
    if (!m->title.isEmpty())
        s.title = m->title;
    s.error.clear();
    Version v{root, seq, f};
    if (s.policy == Policy::Download) {
        if (s.pending)
            QFile::remove(QDir(setsDir()).filePath(hexOf(s.pending->root.bytes) + QStringLiteral(".manifest")));
        s.pending = std::move(v);
        applyVersion(s.id);
        return;
    }
    // Ask: the user decides (file-set.md §2.3, §5.3)
    if (s.pending)
        QFile::remove(QDir(setsDir()).filePath(hexOf(s.pending->root.bytes) + QStringLiteral(".manifest")));
    s.pending = std::move(v);
    logEd3k(QStringLiteral("ed3k: set %1 has a new version %2 (waiting for the user)").arg(s.id).arg(seq));
    save();
    const Info i = infoOf(s);
    emit newVersion(s.id, s.title, seq, i.added, i.changed, i.removed);
    markChanged();
}

void Ed3kSets::onMountManifest(Set& s, const QString& path, const ffi::Root& root, const ffi::Bytes& file)
{
    const QByteArray f = bytesOf(file);
    if (!Ed3kSetManifest::parse(f))
        return;
    storeManifest(root, f);
    s.inner[root] = f;
    s.mounts[path] = root;
    logEd3kDebug(QStringLiteral("ed3k: set %1: a new version of the mount at %2").arg(s.id).arg(path));
    // ToDo: a mount's new version under Policy::Ask is shown, downloaded on the next apply
    if (s.policy == Policy::Download)
        download(s);
    save();
    markChanged();
}

void Ed3kSets::onInner(Set& s, const ffi::Root& root, const ffi::Bytes& file)
{
    const QByteArray f = bytesOf(file);
    if (!Ed3kSetManifest::parse(f))
        return;
    storeManifest(root, f);
    s.inner[root] = f;
    // of the version applied: its members come now
    if (s.applied && !s.pending)
        download(s);
    save();
    markChanged();
}

void Ed3kSets::onMember(Set& s, const ffi::event::SetMember& m)
{
    // a member the core opened (open parts after downloadParts): written at `local`
    const QString target = ed3kSetLocalPath(setDir(s), QString::fromStdString(m.local));
    if (target.isEmpty())
        return;
    // was: added here at once (now addMember: queued in the set's order)
    (void)addMember(s, m.root, m.create ? 0 : m.size, target);
    pumpQueue(s);
    save();
    markChanged();
}

void Ed3kSets::download(Set& s)
{
    if (!s.applied)
        return;
    if (s.dir.isEmpty()) {
        s.dir = setDir(s);
        save();
    }
    int added = 0;
    for (const Ed3kSetEntry& m : flatten(s, s.applied->file)) {
        // an inner set not fetched yet has nothing to download
        if (m.isSet || isSkipped(s.skipped, m.path))
            continue;
        const QString local = ed3kSetLocalPath(s.dir, m.path);
        if (!local.isEmpty() && addMember(s, m.root, m.size, local))
            ++added;
    }
    if (added > 0)
        logEd3k(QStringLiteral("ed3k: set %1: %2 member download(s) added").arg(s.id).arg(added));
    pumpQueue(s);
}

bool Ed3kSets::addMember(Set& s, const ffi::Root& root, std::uint64_t size, const QString& local)
{
    const QFileInfo fi(local);
    // a member of size 0 is created without a download (§2.3); nothing is overwritten (§5.3)
    if (size == 0) {
        if (!fi.exists()) {
            QDir().mkpath(fi.absolutePath());
            QFile f(local);
            (void)f.open(QIODevice::WriteOnly);
        }
        return false;
    }
    if (m_downloads.contains(root) || (fi.exists() && fi.size() == qint64(size)))
        return false;
    // was: one member at a time (§4.2, Protocol.md §12.5: one file stream per uploader; a
    // second request to the same node replaces the first): added paused, resumed in turn
    // (pumpQueue). Now every member at once: members of one source take turns at it
    // (Ed3kJoinBase's turns), those of other sources do not wait. pumpQueue still resumes the
    // members queued in a sets.json of before.
    Ed3kDownloadSpec spec;
    spec.root = root;
    spec.size = size;
    spec.name = fi.fileName();
    spec.targetPath = local;
    spec.owner = ownerOf(s.id);
    // spec.paused = true;
    if (auto ok = m_downloads.add(spec); !ok) {
        logEd3kDebug(QStringLiteral("ed3k: set %1: a member not added (%2)").arg(s.id).arg(ok.error()));
        return false;
    }
    // s.queued.push_back(root);
    return true;
}

void Ed3kSets::pumpQueue(Set& s)
{
    // queued ones the user took over (resumed, cancelled) leave the queue
    std::erase_if(s.queued, [this](const ffi::Root& r) {
        const auto d = m_downloads.info(r);
        return !d || d->state != Ed3kDownloadInfo::State::Paused;
    });
    if (s.queued.empty() || !m_attached)
        return;
    const QString owner = ownerOf(s.id);
    for (const auto& d : m_downloads.list())
        if (d.owner == owner && (d.state == Ed3kDownloadInfo::State::Downloading || d.state == Ed3kDownloadInfo::State::Verifying))
            return;   // its turn is running
    const ffi::Root next = s.queued.front();
    s.queued.erase(s.queued.begin());
    m_downloads.resume(next);
    save();
}

std::vector<Ed3kSetEntry> Ed3kSets::flatten(const Set& s, const QByteArray& file) const
{
    std::vector<Ed3kSetEntry> out;
    flattenInto(s, file, {}, 0, out);
    return out;
}

void Ed3kSets::flattenInto(const Set& s, const QByteArray& file, const QString& prefix, int depth, std::vector<Ed3kSetEntry>& out) const
{
    const auto m = Ed3kSetManifest::parse(file);
    if (!m)
        return;
    for (Ed3kSetEntry e : m->files) {
        e.path = prefix + e.path;
        if (e.isSet || e.mount) {
            ffi::Root inner = e.root;
            if (const auto it = s.mounts.find(e.path); it != s.mounts.end())
                inner = it->second;
            const auto held = s.inner.find(inner);
            if (held != s.inner.end() && depth < kDepthMax) {
                // shown as a subfolder
                flattenInto(s, held->second, e.path + QLatin1Char('/'), depth + 1, out);
                continue;
            }
            e.isSet = true;
        }
        out.push_back(std::move(e));
    }
}

Ed3kSets::Info Ed3kSets::infoOf(const Set& s) const
{
    Info i;
    i.id = s.id;
    i.hidden = s.parsed.hidden;
    i.link = s.parsed.hidden ? QString() : s.link;
    i.title = s.title.isEmpty() ? tr("ed3k set %1").arg(s.id) : s.title;
    i.authorKey = s.parsed.hidden ? QString() : hexOf(s.parsed.key);
    i.pinned = s.parsed.b3.has_value();
    i.followed = s.topic.has_value();
    i.policy = s.policy;
    if (s.applied)
        i.seq = s.applied->seq;
    if (s.pending)
        i.pendingSeq = s.pending->seq;
    i.dir = s.dir.isEmpty() ? setDir(s) : s.dir;
    i.error = s.error;
    i.parts = s.parts;
    const auto& shown = s.pending ? s.pending : s.applied;
    if (!shown)
        return i;
    const auto members = flatten(s, shown->file);
    for (const Ed3kSetEntry& m : members) {
        if (m.isSet)
            continue;
        Member out;
        out.path = m.path;
        out.root = m.root;
        out.size = m.size;
        out.skipped = isSkipped(s.skipped, m.path);
        out.local = ed3kSetLocalPath(i.dir, m.path);
        i.totalSize += m.size;
        const bool queued = std::ranges::find(s.queued, m.root) != s.queued.end();
        if (const auto d = m_downloads.info(m.root); d && !queued) {
            using S = Ed3kDownloadInfo::State;
            out.done = d->done;
            out.state = d->state == S::Complete  ? MemberState::Complete
                        : d->state == S::Failed  ? MemberState::Failed
                        : d->state == S::Paused  ? MemberState::Paused
                                                 : MemberState::Downloading;
        } else if (const QFileInfo fi(out.local); !out.local.isEmpty() && fi.exists() && fi.size() == qint64(m.size)) {
            out.state = MemberState::Complete;
            out.done = m.size;
        } else if (out.skipped) {
            out.state = MemberState::Skipped;
        }
        i.members.push_back(std::move(out));
    }
    // the pending version against the applied one (§5.3 step 4)
    if (s.pending) {
        std::map<QString, ffi::Root> before;
        QSet<QString> rootsBefore;
        if (s.applied)
            for (const auto& m : flatten(s, s.applied->file))
                if (!m.isSet) {
                    before[m.path] = m.root;
                    rootsBefore.insert(hexOf(m.root.bytes));
                }
        QSet<QString> now;
        for (const auto& m : members) {
            if (m.isSet)
                continue;
            now.insert(m.path);
            const auto it = before.find(m.path);
            if (it == before.end()) {
                // a root held under another path is moved, not added
                if (!rootsBefore.contains(hexOf(m.root.bytes)))
                    i.added.append(m.path);
            } else if (it->second != m.root) {
                i.changed.append(m.path);
            }
        }
        for (const auto& [path, root] : before)
            if (!now.contains(path))
                i.removed.append(path);
    }
    return i;
}

QString Ed3kSets::setDir(const Set& s) const
{
    if (!s.dir.isEmpty())
        return s.dir;
    QString name = s.title.isEmpty() ? s.parsed.name : s.title;
    if (name.isEmpty())
        name = QStringLiteral("ed3k set %1").arg(s.id);
    return QDir(m_incomingDir).filePath(ed3kSafeName(name));
}

std::optional<Ed3kSets::Link> Ed3kSets::parseLink(const QString& link)
{
    static const QString kPrefix = QStringLiteral("ed3k:set?");
    if (!link.startsWith(kPrefix, Qt::CaseInsensitive))
        return std::nullopt;
    const QUrlQuery q(link.mid(kPrefix.size()));
    const auto items = q.queryItems(QUrl::FullyDecoded);
    // §5.4: key, id, dn, path (and s, paid) at most once
    for (const QString k : {QStringLiteral("key"), QStringLiteral("id"), QStringLiteral("dn"), QStringLiteral("path"),
                            QStringLiteral("s"), QStringLiteral("paid"), QStringLiteral("b3"), QStringLiteral("sz")})
        if (std::ranges::count_if(items, [&](const auto& kv) { return kv.first == k; }) > 1)
            return std::nullopt;
    Link out;
    const auto key = fromBase32(q.queryItemValue(QStringLiteral("key")));
    if (!key || key->size() != qsizetype(out.key.size()))
        return std::nullopt;
    std::memcpy(out.key.data(), key->constData(), out.key.size());
    out.name = q.queryItemValue(QStringLiteral("dn"), QUrl::FullyDecoded);
    const bool s = q.hasQueryItem(QStringLiteral("s"));
    const bool paid = q.queryItemValue(QStringLiteral("paid")) == QLatin1String("1");
    const bool hasId = q.hasQueryItem(QStringLiteral("id"));
    if (s || paid) {
        // a hidden set (hidden-set.md); with id as well: refused (§5.4)
        if (hasId)
            return std::nullopt;
        out.hidden = true;
        return out;
    }
    const auto id = hasId ? fromBase32(q.queryItemValue(QStringLiteral("id"))) : std::nullopt;
    if (!id || id->isEmpty() || id->size() > 32)
        return std::nullopt;
    out.id = *id;
    // chaintube.md §3.1: b3 and sz pin one version
    if (q.hasQueryItem(QStringLiteral("b3"))) {
        const auto b3 = fromBase32(q.queryItemValue(QStringLiteral("b3")));
        bool ok = false;
        out.sz = q.queryItemValue(QStringLiteral("sz")).toULongLong(&ok);
        ffi::Root r;
        if (!b3 || b3->size() != qsizetype(r.bytes.size()) || !ok)
            return std::nullopt;
        std::memcpy(r.bytes.data(), b3->constData(), r.bytes.size());
        out.b3 = r;
    }
    return out;
}

void Ed3kSets::markChanged()
{
    if (m_changePending)
        return;
    m_changePending = true;
    QTimer::singleShot(300, this, [this] {
        m_changePending = false;
        emit changed();
    });
}

void Ed3kSets::load()
{
    const QByteArray raw = readBlob(statePath());
    if (raw.isEmpty())
        return;
    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    m_nextId = std::max<std::uint32_t>(1, std::uint32_t(root.value(QStringLiteral("next")).toInteger(1)));
    const auto versionOf = [this](const QJsonValue& v) -> std::optional<Version> {
        const QJsonObject o = v.toObject();
        const auto r = rootOf(o.value(QStringLiteral("root")).toString());
        if (!r)
            return std::nullopt;
        QByteArray file = loadManifest(*r);
        if (file.isEmpty())
            return std::nullopt;
        return Version{*r, std::uint64_t(o.value(QStringLiteral("seq")).toString().toULongLong()), std::move(file)};
    };
    for (const QJsonValue& v : root.value(QStringLiteral("sets")).toArray()) {
        const QJsonObject o = v.toObject();
        Set s;
        s.id = std::uint32_t(o.value(QStringLiteral("id")).toInteger());
        s.link = o.value(QStringLiteral("link")).toString();
        const auto parsed = parseLink(s.link);
        if (s.id == 0 || !parsed || m_sets.contains(s.id))
            continue;
        s.parsed = *parsed;
        s.policy = o.value(QStringLiteral("policy")).toInt() == int(Policy::Download) ? Policy::Download : Policy::Ask;
        s.title = o.value(QStringLiteral("title")).toString();
        s.dir = o.value(QStringLiteral("dir")).toString();
        for (const QJsonValue& p : o.value(QStringLiteral("skipped")).toArray())
            s.skipped.insert(p.toString());
        s.applied = versionOf(o.value(QStringLiteral("applied")));
        s.pending = versionOf(o.value(QStringLiteral("pending")));
        for (const QJsonValue& r : o.value(QStringLiteral("inner")).toArray())
            if (const auto root = rootOf(r.toString()))
                if (QByteArray f = loadManifest(*root); !f.isEmpty())
                    s.inner[*root] = std::move(f);
        for (const QJsonValue& r : o.value(QStringLiteral("queued")).toArray())
            if (const auto root = rootOf(r.toString()))
                s.queued.push_back(*root);
        const QJsonObject mounts = o.value(QStringLiteral("mounts")).toObject();
        for (auto it = mounts.begin(); it != mounts.end(); ++it)
            if (const auto r = rootOf(it.value().toString()))
                s.mounts[it.key()] = *r;
        m_nextId = std::max(m_nextId, s.id + 1);
        m_sets.emplace(s.id, std::move(s));
    }
}

void Ed3kSets::save() const
{
    QJsonArray sets;
    const auto versionOf = [](const std::optional<Version>& v) {
        return v ? QJsonValue(QJsonObject{{QStringLiteral("root"), hexOf(v->root.bytes)}, {QStringLiteral("seq"), QString::number(v->seq)}})
                 : QJsonValue();
    };
    for (const auto& [id, s] : m_sets) {
        QJsonArray inner;
        for (const auto& [root, f] : s.inner)
            inner.append(hexOf(root.bytes));
        QJsonArray queued;
        for (const auto& root : s.queued)
            queued.append(hexOf(root.bytes));
        QJsonObject mounts;
        for (const auto& [path, root] : s.mounts)
            mounts.insert(path, hexOf(root.bytes));
        QStringList skipped(s.skipped.begin(), s.skipped.end());
        skipped.sort();
        sets.append(QJsonObject{
            {QStringLiteral("id"), qint64(s.id)},
            {QStringLiteral("link"), s.link},
            {QStringLiteral("policy"), int(s.policy)},
            {QStringLiteral("title"), s.title},
            {QStringLiteral("dir"), s.dir},
            {QStringLiteral("skipped"), QJsonArray::fromStringList(skipped)},
            {QStringLiteral("applied"), versionOf(s.applied)},
            {QStringLiteral("pending"), versionOf(s.pending)},
            {QStringLiteral("inner"), inner},
            {QStringLiteral("mounts"), mounts},
            {QStringLiteral("queued"), queued},
        });
    }
    const QByteArray out = QJsonDocument(QJsonObject{{QStringLiteral("next"), qint64(m_nextId)}, {QStringLiteral("sets"), sets}})
                               .toJson(QJsonDocument::Compact);
    QDir().mkpath(stateDir(m_configDir));
    // links of hidden sets are credentials: owner-only (writeBlob)
    if (!writeBlob(statePath(), spanOf(out)))
        logEd3kWarning(QStringLiteral("ed3k: %1 not written").arg(statePath()));
}

void Ed3kSets::storeManifest(const ffi::Root& root, const QByteArray& file) const
{
    // under the root the core named (a hidden set's: the sealed file's)
    QDir().mkpath(setsDir());
    (void)writeBlob(QDir(setsDir()).filePath(hexOf(root.bytes) + QStringLiteral(".manifest")), spanOf(file));
}

QByteArray Ed3kSets::loadManifest(const ffi::Root& root) const
{
    return readBlob(QDir(setsDir()).filePath(hexOf(root.bytes) + QStringLiteral(".manifest")));
}

QString Ed3kSets::setsDir() const
{
    return QDir(stateDir(m_configDir)).filePath(QStringLiteral("sets"));
}

} // namespace eMule::ed3k
