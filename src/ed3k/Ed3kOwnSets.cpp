/// @file Ed3kOwnSets.cpp
/// @brief The user's own ed3k sets — implementation.

#include "Ed3kOwnSets.h"

#include "Ed3kSealedOpener.h"
#include "Ed3kSession.h"
#include "Ed3kSets.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

#include <algorithm>
#include <set>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

const QString kStateFile = QStringLiteral("own-sets.json");
const QString kOwnDir = QStringLiteral("own-sets");
/// FileSet.id: 16 random bytes (a joint head and a paid scope take at most 16, file-set.md §5.1)
constexpr int kIdBytes = 16;
/// a seller's change (new offer) coalesced before a version goes out
constexpr int kOfferDelayMs = 1500;

QString hex(std::span<const std::uint8_t> b)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size())).toHex());
}

template <std::size_t N>
std::optional<std::array<std::uint8_t, N>> fromHex(const QString& text)
{
    const QByteArray b = QByteArray::fromHex(text.toLatin1());
    if (b.size() != qsizetype(N))
        return std::nullopt;
    std::array<std::uint8_t, N> out{};
    std::memcpy(out.data(), b.constData(), N);
    return out;
}

ffi::Bytes bytesOf(const QByteArray& b)
{
    ffi::Bytes out(std::size_t(b.size()));
    std::memcpy(out.data(), b.constData(), out.size());
    return out;
}

QByteArray qbytes(std::span<const std::byte> b)
{
    return QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size()));
}

std::string utf8(const QString& s)
{
    const QByteArray b = s.toUtf8();
    return std::string(b.constData(), std::size_t(b.size()));
}

template <std::size_t N>
std::array<std::uint8_t, N> randomBytes()
{
    std::array<std::uint8_t, N> out{};
    for (auto& b : out)
        b = std::uint8_t(QRandomGenerator::system()->bounded(256));
    return out;
}

/// the set key file of a set id (hex): a secret
QString keyName(const QString& idHex)
{
    return idHex + QStringLiteral(".key");
}

QString secretName(const QString& idHex)
{
    return idHex + QStringLiteral(".secret");
}

} // namespace

/// A version being made: its members, their hashes, the key that seals new ones.
struct Ed3kOwnSets::Build {
    quint64 gen = 0;
    bool restore = false;
    bool changed = false;     ///< a member added, dropped or not carried
    bool again = false;       ///< another version asked meanwhile
    std::vector<Member> members;
    std::vector<bool> carried;
    std::vector<std::optional<FileHash>> hashes;
    std::size_t next = 0;
    std::optional<std::uint64_t> epoch;   ///< paid: the epoch new members are sealed in
    std::unique_ptr<Secret> kData;        ///< paid or hidden: seals new members
    QString sealing;                      ///< the .tmp being sealed
};

Ed3kOwnSets::Ed3kOwnSets(Ed3kSession& session, const QString& configDir, QObject* parent)
    : Ed3kJoinBase(session, configDir, QStringLiteral("ownsets"), parent)
    , m_sealer(std::make_unique<Ed3kSealedSealer>())
{
    m_offerTimer.setSingleShot(true);
    connect(&m_offerTimer, &QTimer::timeout, this, &Ed3kOwnSets::checkOffers);
    load();
    if (m_session.isRunning())
        start();
}

Ed3kOwnSets::~Ed3kOwnSets() = default;

void Ed3kOwnSets::start()
{
    if (m_started)
        return;
    beginStart();
    // every published set again: the version kept when nothing changed (its head re-signed)
    for (auto& [id, s] : m_sets)
        if (s.published || (s.kind == Kind::Paid && !s.scope.isEmpty() && s.seq == 0))
            startBuild(s, true);
}

void Ed3kOwnSets::stop()
{
    if (!m_started)
        return;
    m_offerTimer.stop();
    m_builds.clear();
    // half-sealed members: their .part goes with the sealer (made again for the next start)
    m_sealer = std::make_unique<Ed3kSealedSealer>();
    closeAll();
    m_serve.clear();
    m_serveOf.clear();
    for (auto& [id, s] : m_sets)
        s.manifest.reset();
}

void Ed3kOwnSets::setSeller(Ed3kPaidSeller* seller)
{
    if (m_seller)
        disconnect(m_seller, nullptr, this, nullptr);
    m_seller = seller;
    if (m_seller)
        connect(m_seller, &Ed3kPaidSeller::changed, this, &Ed3kOwnSets::onSellerChanged);
}

std::expected<QString, QString> Ed3kOwnSets::create(const QString& title, const QString& folder, const QStringList& files, Kind kind)
{
    if (!m_started || !m_session.isRunning())
        return std::unexpected(tr("ed3k is not running."));
    if (kind != Kind::Open && kind != Kind::Hidden && kind != Kind::Paid)
        return std::unexpected(tr("Unknown kind of set."));
    if (kind == Kind::Paid && !m_seller)
        return std::unexpected(tr("Selling is not available."));
    Set s;
    s.folder = folder.isEmpty() ? QString() : QDir(folder).absolutePath();
    for (const QString& f : files)
        s.files << QFileInfo(f).absoluteFilePath();
    if (!s.folder.isEmpty() && !QFileInfo(s.folder).isDir())
        return std::unexpected(tr("The folder is not there."));
    if (scan(s).empty())
        return std::unexpected(tr("There are no files to publish."));
    s.title = title.trimmed();
    if (s.title.isEmpty())
        s.title = s.folder.isEmpty() ? QFileInfo(s.files.value(0)).completeBaseName() : QFileInfo(s.folder).fileName();
    s.kind = kind;
    s.created = m_session.now().unixS;
    const auto id = randomBytes<kIdBytes>();
    s.id = QByteArray(reinterpret_cast<const char*>(id.data()), kIdBytes);
    const QString idHex = QString::fromLatin1(s.id.toHex());
    QDir().mkpath(ownDir());
    // the set key (and a hidden set's link secret): secrets, owner-only, never logged
    auto seed = loadOrCreateSecret(ownDir(), keyName(idHex), {});
    if (!seed)
        return std::unexpected(tr("The key of the set could not be made."));
    wipe(std::span<std::uint8_t>{*seed});
    if (kind == Kind::Hidden) {
        auto secret = loadOrCreateSecret(ownDir(), secretName(idHex), {});
        if (!secret) {
            QFile::remove(QDir(ownDir()).filePath(keyName(idHex)));
            return std::unexpected(tr("The key of the set could not be made."));
        }
        wipe(std::span<std::uint8_t>{*secret});
    }
    auto [it, inserted] = m_sets.emplace(s.id, std::move(s));
    logEd3k(QStringLiteral("ed3k: own set %1 created (%2)").arg(idHex, it->second.title));
    save();
    // a paid one goes out once sold (sell())
    if (kind != Kind::Paid)
        startBuild(it->second, false);
    markChanged();
    return idHex;
}

std::expected<void, QString> Ed3kOwnSets::publishNewVersion(const QString& id)
{
    Set* s = find(id);
    if (!s)
        return std::unexpected(tr("This set is not yours."));
    if (!m_started)
        return std::unexpected(tr("ed3k is not running."));
    if (s->kind == Kind::Paid && s->scope.isEmpty())
        return std::unexpected(tr("A paid set goes out once it is sold."));
    startBuild(*s, false);
    markChanged();
    return {};
}

std::expected<void, QString> Ed3kOwnSets::retitle(const QString& id, const QString& title)
{
    Set* s = find(id);
    if (!s)
        return std::unexpected(tr("This set is not yours."));
    if (title.trimmed().isEmpty())
        return std::unexpected(tr("The set needs a title."));
    s->title = title.trimmed();
    save();
    if (s->published)
        return publishNewVersion(id);
    markChanged();
    return {};
}

std::expected<void, QString> Ed3kOwnSets::unpublish(const QString& id)
{
    Set* s = find(id);
    if (!s)
        return std::unexpected(tr("This set is not yours."));
    m_builds.erase(ownerOf(s->id));
    if (auto* e = m_session.engine(); e && s->manifest) {
        // file-set.md: no keywords, MEMBER records nor head stored again; what Kad holds runs out
        if (auto r = e->setUnpublish(m_session.now(), *s->manifest); !r && r.error() != ffi::Error::NotFound)
            logWarning(QStringLiteral("set %1 not unpublished (error %2)").arg(id, errorName(r.error())));
    }
    unshareSet(*s);
    s->published = false;
    s->head.reset();
    logEd3k(QStringLiteral("ed3k: own set %1 unpublished").arg(id));
    save();
    pumpSession();
    markChanged();
    return {};
}

std::expected<QString, QString> Ed3kOwnSets::sell(const QString& id, const Ed3kPaidSeller::Terms& terms)
{
    Set* s = find(id);
    if (!s)
        return std::unexpected(tr("This set is not yours."));
    if (!m_seller)
        return std::unexpected(tr("Selling is not available."));
    if (s->kind == Kind::Hidden)
        // ToDo: a paid hidden set (subscription.md §8.3, kind 3) is not offered
        return std::unexpected(tr("A hidden set cannot be sold."));
    if (!s->scope.isEmpty() && m_seller->info(s->scope))
        return m_seller->setTerms(s->scope, terms);
    auto seed = keyOf(*s);
    if (!seed)
        return std::unexpected(tr("The key of the set is missing."));
    // the set key is the publisher (subscription.md §2: scope = kind 2, key, FileSet.id)
    auto scope = m_seller->sell(Ed3kPaidSeller::Kind::Set, *seed, s->id, s->title, terms);
    wipe(std::span<std::uint8_t>{*seed});
    if (!scope)
        return std::unexpected(scope.error());
    s->scope = *scope;
    s->kind = Kind::Paid;
    save();
    // §8.1: the next version seals every member with the epoch of now
    startBuild(*s, false);
    markChanged();
    return *scope;
}

bool Ed3kOwnSets::has(const QString& id) const
{
    return find(id) != nullptr;
}

std::vector<Ed3kOwnSets::Info> Ed3kOwnSets::list() const
{
    std::vector<Info> out;
    out.reserve(m_sets.size());
    for (const auto& [id, s] : m_sets)
        out.push_back(infoOf(s));
    return out;
}

std::optional<Ed3kOwnSets::Info> Ed3kOwnSets::info(const QString& id) const
{
    const Set* s = find(id);
    if (!s)
        return std::nullopt;
    return infoOf(*s);
}

QString Ed3kOwnSets::linkOf(const QString& id) const
{
    const Set* s = find(id);
    if (!s)
        return {};
    if (s->kind == Kind::Hidden)
        return hiddenLink(*s);
    auto seed = keyOf(*s);
    if (!seed)
        return {};
    const auto key = ffi::keyPublic(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    return key ? Ed3kSets::setLink(*key, s->id, s->title) : QString();
}

std::optional<ffi::Topic> Ed3kOwnSets::headOf(const QString& id) const
{
    const Set* s = find(id);
    return s ? s->head : std::nullopt;
}

std::optional<ffi::Root> Ed3kOwnSets::manifestOf(const QString& id) const
{
    const Set* s = find(id);
    return s ? s->manifest : std::nullopt;
}

QString Ed3kOwnSets::statePath() const
{
    return QDir(stateDir(m_configDir)).filePath(kStateFile);
}

QString Ed3kOwnSets::setDir(const QString& id) const
{
    return QDir(ownDir()).filePath(id);
}

// -- Ed3kJoinBase ------------------------------------------------------------

std::vector<std::uint64_t> Ed3kOwnSets::wantedBlocks(const Download& d) const
{
    Q_UNUSED(d)
    return {};   // nothing downloaded here
}

void Ed3kOwnSets::onFileBlock(const Download& d, const ffi::event::Block& b)
{
    Q_UNUSED(d) Q_UNUSED(b)
}

void Ed3kOwnSets::onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data)
{
    Q_UNUSED(owner) Q_UNUSED(member) Q_UNUSED(root) Q_UNUSED(data)
}

std::optional<Ed3kJoinBase::FileRead> Ed3kOwnSets::sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const
{
    const auto it = m_serve.find(s.member);
    if (it == m_serve.end() || len == 0)
        return std::nullopt;
    return FileRead{it->second.first, offset, len, qint64(it->second.second), -1};
}

bool Ed3kOwnSets::sharedGone(const Shared& s) const
{
    const auto it = m_serve.find(s.member);
    return it == m_serve.end() || !QFileInfo::exists(it->second.first);
}

bool Ed3kOwnSets::hasOwner(const QString& owner) const
{
    return std::ranges::any_of(m_sets, [&](const auto& kv) { return ownerOf(kv.first) == owner; });
}

void Ed3kOwnSets::saveOwner(const QString& owner)
{
    Q_UNUSED(owner)
    save();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kOwnSets::onSellerChanged()
{
    markChanged();   // sold, stopped
    m_offerTimer.start(kOfferDelayMs);
}

void Ed3kOwnSets::checkOffers()
{
    if (!m_started || !m_seller)
        return;
    // a newer offer (terms, a stop, a new season): FileSet.offer of a new version (§8.2)
    for (auto& [id, s] : m_sets) {
        if (s.kind != Kind::Paid || s.scope.isEmpty() || !s.published || m_builds.contains(ownerOf(s.id)))
            continue;
        const QByteArray offer = m_seller->offerOf(s.scope);
        if (!offer.isEmpty() && offer != s.offer)
            startBuild(s, false);
    }
}

void Ed3kOwnSets::startBuild(Set& s, bool restore)
{
    const QString owner = ownerOf(s.id);
    if (const auto it = m_builds.find(owner); it != m_builds.end()) {
        it->second->again = true;
        return;
    }
    if (!m_started)
        return;
    const auto files = scan(s);
    if (files.empty()) {
        s.error = tr("There are no files to publish.");
        emit versionDone(QString::fromLatin1(s.id.toHex()), s.seq, false);
        markChanged();
        return;
    }
    static quint64 gens = 0;
    auto b = std::make_unique<Build>();
    b->gen = ++gens;
    b->restore = restore && s.seq > 0;
    if (s.kind == Kind::Paid) {
        auto key = (m_seller && !s.scope.isEmpty()) ? m_seller->sealKey(s.scope) : std::nullopt;
        if (!key) {
            s.error = tr("The set is not sold here (no key for its members).");
            emit versionDone(QString::fromLatin1(s.id.toHex()), s.seq, false);
            markChanged();
            return;
        }
        b->epoch = key->epoch;
        b->kData = std::make_unique<Secret>(key->kData);
        wipe(std::span<std::uint8_t>{key->kData});
    } else if (s.kind == Kind::Hidden) {
        const QString link = hiddenLink(s);
        auto keys = ffi::hiddenLinkParse(utf8(link));
        if (!keys) {
            s.error = tr("The key of the set is missing.");
            emit versionDone(QString::fromLatin1(s.id.toHex()), s.seq, false);
            markChanged();
            return;
        }
        b->kData = std::make_unique<Secret>(keys->kData);
        wipe(std::span<std::uint8_t>{keys->kData});
    }
    std::set<QString> paths;
    for (const auto& [path, file] : files) {
        Member m;
        m.path = path;
        m.file = file;
        const QFileInfo fi(file);
        m.fileSize = fi.size();
        m.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
        paths.insert(path);
        const auto old = std::ranges::find_if(s.members, [&](const Member& o) { return o.path == path && o.file == file; });
        const bool sameMode = old != s.members.end()
                              && (s.kind == Kind::Paid ? old->epoch.has_value() : s.kind == Kind::Hidden ? old->sealed : !old->epoch && !old->sealed);
        // §8.1: a member kept keeps epoch and root, not resealed
        if (sameMode && old->fileSize == m.fileSize && old->mtimeMs == m.mtimeMs
            && (old->sealedPath.isEmpty() || QFileInfo::exists(old->sealedPath))) {
            m.root = old->root;
            m.size = old->size;
            m.epoch = old->epoch;
            m.sealed = old->sealed;
            m.sealedPath = old->sealedPath;
            b->carried.push_back(true);
        } else {
            if (s.kind == Kind::Paid)
                m.epoch = b->epoch;
            m.sealed = s.kind == Kind::Hidden;
            b->carried.push_back(false);
            b->changed = true;
        }
        b->members.push_back(std::move(m));
    }
    if (std::ranges::any_of(s.members, [&](const Member& o) { return !paths.contains(o.path); }))
        b->changed = true;
    b->hashes.resize(b->members.size());
    s.error.clear();
    m_builds[owner] = std::move(b);
    markChanged();
    buildStep(owner);
}

void Ed3kOwnSets::buildStep(const QString& owner)
{
    const auto it = m_builds.find(owner);
    if (it == m_builds.end())
        return;
    Build& b = *it->second;
    if (b.next >= b.members.size()) {
        finishBuild(owner);
        return;
    }
    const std::size_t i = b.next;
    Member& m = b.members[i];
    const quint64 gen = b.gen;
    const auto current = [this, owner, gen]() -> Build* {
        const auto f = m_builds.find(owner);
        return f != m_builds.end() && f->second->gen == gen ? f->second.get() : nullptr;
    };
    const bool seal = (m.epoch || m.sealed) && m.sealedPath.isEmpty();
    if (!seal) {
        const QString path = m.sealedPath.isEmpty() ? m.file : m.sealedPath;
        hashInBackground(path, [this, owner, i, current](std::optional<FileHash> h) {
            Build* b = current();
            if (!b)
                return;
            Member& m = b->members[i];
            if (!h) {
                buildFailed(owner, tr("\"%1\" could not be read.").arg(m.path));
                return;
            }
            if (b->carried[i] && h->root != m.root) {
                b->changed = true;
                b->carried[i] = false;
                if (!m.sealedPath.isEmpty()) {
                    // a sealed copy that changed: sealed again as a new member
                    m.sealedPath.clear();
                    buildStep(owner);
                    return;
                }
            }
            m.root = h->root;
            m.size = h->size;
            b->hashes[i] = std::move(h);
            ++b->next;
            buildStep(owner);
        });
        return;
    }
    // a new sealed member: nonce ‖ ChaCha20(k_data, nonce, plain) into .sealed/ (§8.1, §4.2)
    const QString dir = QDir(setDir(QString::fromLatin1(findOwner(owner)->id.toHex()))).filePath(QStringLiteral(".sealed"));
    QDir().mkpath(dir);
    const auto tag = randomBytes<6>();
    b.sealing = QDir(dir).filePath(QStringLiteral("%1-%2.tmp").arg(i).arg(hex(tag)));
    const auto nonce = randomBytes<12>();
    const QString tmp = b.sealing;
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_sealer.get(), &Ed3kSealedSealer::sealed, this, [this, owner, i, tmp, current, conn](const QString& path, bool ok) {
        if (path != tmp)
            return;
        disconnect(*conn);
        Build* b = current();
        if (!b) {
            QFile::remove(tmp);
            return;
        }
        if (!ok) {
            buildFailed(owner, tr("\"%1\" could not be sealed.").arg(b->members[i].path));
            return;
        }
        hashInBackground(tmp, [this, owner, i, tmp, current](std::optional<FileHash> h) {
            Build* b = current();
            if (!b || !h) {
                QFile::remove(tmp);
                if (b)
                    buildFailed(owner, tr("A sealed member could not be read."));
                return;
            }
            // hidden-set.md §4.3: the sealed file is named by its root on the wire
            const auto wire = ffi::sealedName(h->root);
            if (!wire) {
                QFile::remove(tmp);
                buildFailed(owner, tr("A sealed member could not be named."));
                return;
            }
            const QString final = QDir(QFileInfo(tmp).absolutePath()).filePath(QString::fromStdString(*wire));
            if (QFileInfo::exists(final))
                QFile::remove(final);
            if (!QFile::rename(tmp, final)) {
                QFile::remove(tmp);
                buildFailed(owner, tr("A sealed member could not be kept."));
                return;
            }
            Member& m = b->members[i];
            m.sealedPath = final;
            m.root = h->root;
            m.size = h->size;
            b->hashes[i] = std::move(h);
            b->sealing.clear();
            ++b->next;
            buildStep(owner);
        });
    });
    if (!m_sealer->seal(m.file, tmp, b.kData->expose(), nonce)) {
        disconnect(*conn);
        buildFailed(owner, tr("\"%1\" could not be sealed.").arg(m.path));
    }
}

void Ed3kOwnSets::buildFailed(const QString& owner, const QString& error)
{
    Set* s = findOwner(owner);
    m_builds.erase(owner);
    if (!s)
        return;
    s->error = error;
    logWarning(QStringLiteral("set %1: no new version: %2").arg(QString::fromLatin1(s->id.toHex()), error));
    emit versionDone(QString::fromLatin1(s->id.toHex()), s->seq, false);
    markChanged();
}

void Ed3kOwnSets::finishBuild(const QString& owner)
{
    Set* s = findOwner(owner);
    const auto it = m_builds.find(owner);
    if (!s || it == m_builds.end())
        return;
    Build& b = *it->second;
    const std::vector<Member> old = s->members;
    const std::optional<ffi::Root> oldManifest = s->manifest;
    shareMembers(*s, b);
    QString error;
    if (!publishVersion(*s, b, &error)) {
        buildFailed(owner, error);
        return;
    }
    // what the version before had and this one has not: no longer shared, sealed copies gone
    std::set<ffi::Root> now;
    for (const Member& m : b.members)
        now.insert(m.root);
    for (const Member& m : old) {
        if (now.contains(m.root))
            continue;
        const bool elsewhere = std::ranges::any_of(m_sets, [&](const auto& kv) {
            return kv.first != s->id && std::ranges::any_of(kv.second.members, [&](const Member& o) { return o.root == m.root; });
        });
        if (!elsewhere) {
            unshare(m.root);
            if (const auto sv = m_serveOf.find(m.root); sv != m_serveOf.end()) {
                m_serve.erase(sv->second);
                m_serveOf.erase(sv);
            }
        }
        if (!m.sealedPath.isEmpty())
            QFile::remove(m.sealedPath);
    }
    if (oldManifest && s->manifest != oldManifest)
        unshare(*oldManifest);
    s->members = std::move(b.members);
    s->published = true;
    s->error.clear();
    const bool again = b.again;
    m_builds.erase(it);
    save();
    pumpSession();
    const QString idHex = QString::fromLatin1(s->id.toHex());
    logEd3k(QStringLiteral("ed3k: own set %1 version %2 published (%3 file(s))").arg(idHex).arg(s->seq).arg(s->members.size()));
    emit versionDone(idHex, s->seq, true);
    markChanged();
    if (again)
        startBuild(*s, false);
}

bool Ed3kOwnSets::publishVersion(Set& s, Build& b, QString* error)
{
    auto* e = m_session.engine();
    if (!e) {
        *error = tr("ed3k is not running.");
        return false;
    }
    auto seed = keyOf(s);
    if (!seed) {
        *error = tr("The key of the set is missing.");
        return false;
    }
    const Secret key(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    const QString idHex = QString::fromLatin1(s.id.toHex());
    const QString manifestPath = QDir(ownDir()).filePath(idHex + QStringLiteral(".manifest"));
    const QByteArray offer = (s.kind == Kind::Paid && m_seller) ? m_seller->offerOf(s.scope) : QByteArray();
    if (s.kind == Kind::Paid && offer.isEmpty()) {
        *error = tr("The set is not sold here (no offer).");
        return false;
    }
    const auto t = m_session.now();
    const QString link = s.kind == Kind::Hidden ? hiddenLink(s) : QString();

    // the version kept: shared and published again as it was (the core signs its head again)
    const auto publish = [&](const QByteArray& file) -> bool {
        if (s.kind == Kind::Hidden) {
            const auto hashed = [&]() -> std::optional<ffi::Root> {
                ffi::Hasher h;
                if (!h.update(spanOf(file)))
                    return std::nullopt;
                auto done = h.finish();
                return done ? std::optional(done->root) : std::nullopt;
            }();
            const auto wire = hashed ? ffi::sealedName(*hashed) : ffi::Result<std::string>(std::unexpected(ffi::Error::State));
            if (!wire)
                return false;
            const auto root = shareBlob(file, QString::fromStdString(*wire), hashed);
            if (!root)
                return false;
            auto head = e->setPublishHidden(t, utf8(link), key.expose(), spanOf(file), *root);
            if (!head) {
                logDebug([&]() -> QString { return QStringLiteral("hidden set %1 not published (error %2)").arg(idHex, errorName(head.error())); });
                if (root != s.manifest)
                    unshare(*root);
                return false;
            }
            s.manifest = root;
            s.head = *head;
            return true;
        }
        const QString name = safeName(s.title, QStringLiteral("set")) + QStringLiteral(".ed3kset");
        const auto root = shareBlob(file, name, std::nullopt);
        if (!root)
            return false;
        auto head = e->setPublish(t, spanOf(file), *root, utf8(name), ffi::SetSigner::Seed, &key.expose());
        if (!head) {
            logDebug([&]() -> QString { return QStringLiteral("set %1 not published (error %2)").arg(idHex, errorName(head.error())); });
            if (root != s.manifest)
                unshare(*root);
            return false;
        }
        s.manifest = root;
        s.head = *head;
        return true;
    };
    if (b.restore && !b.changed && offer == s.offer) {
        const QByteArray kept = readBlob(manifestPath);
        if (!kept.isEmpty() && publish(kept))
            return true;
    }

    // a new version: seq + 1 (file-set.md §5.1)
    ffi::SetFields f;
    f.title = utf8(s.title);
    f.created = s.created;
    f.id = bytesOf(s.id);
    f.seq = s.seq + 1;
    f.offer = bytesOf(offer);
    std::vector<ffi::SetMemberIn> in;
    in.reserve(b.members.size());
    for (const Member& m : b.members) {
        ffi::SetMemberIn x;
        x.root = m.root;
        x.size = m.size;
        x.path = utf8(m.path);
        x.epoch = m.epoch;
        x.sealed = m.sealed;
        in.push_back(std::move(x));
    }
    auto manifest = ffi::setManifestWrite(t.unixS, key.expose(), f, in);
    if (!manifest) {
        *error = manifest.error() == ffi::Error::TooLarge ? tr("The set is too large (file-set limits).")
                                                          : tr("The set could not be written (error %1).").arg(errorName(manifest.error()));
        return false;
    }
    QByteArray file = qbytes(manifest->bytes());
    if (s.kind == Kind::Hidden) {
        // hidden-set.md §4.1: the manifest sealed with the link's k_data, a fresh nonce
        auto sealed = ffi::hiddenSealManifest(utf8(link), randomBytes<12>(), manifest->bytes());
        if (!sealed) {
            *error = tr("The set could not be sealed (error %1).").arg(errorName(sealed.error()));
            return false;
        }
        file = qbytes(sealed->bytes());
    }
    if (!publish(file)) {
        *error = tr("The set could not be published.");
        return false;
    }
    s.seq = f.seq;
    s.offer = offer;
    QDir().mkpath(ownDir());
    if (!writeBlob(manifestPath, spanOf(file)))
        logWarning(QStringLiteral("set %1: the manifest was not kept").arg(idHex));
    return true;
}

void Ed3kOwnSets::shareMembers(Set& s, Build& b)
{
    auto* e = m_session.engine();
    if (!e)
        return;
    const QString owner = ownerOf(s.id);
    const auto t = m_session.now();
    for (std::size_t i = 0; i < b.members.size(); ++i) {
        const Member& m = b.members[i];
        const bool sealed = !m.sealedPath.isEmpty();
        QString name = QFileInfo(m.file).fileName();
        if (sealed) {
            // hidden-set.md §4.3: named by its root, no keywords
            const auto wire = ffi::sealedName(m.root);
            if (!wire)
                continue;
            name = QString::fromStdString(*wire);
        }
        const auto& h = b.hashes[i];
        if (!h || !serveFile(owner, m.root, m.size, name, sealed ? m.sealedPath : m.file, h->hashes))
            continue;
        const ffi::FileInfo info{m.root, m.size, utf8(name), std::nullopt, 0, 0, 0, 0, 0, 0};
        if (sealed) {
            // §8.1, §4.3: a SOURCE record, no KEYWORD or MEMBER record
            if (auto ok = e->publish(t, info, ffi::Publish::Source); !ok)
                logDebug([&]() -> QString { return QStringLiteral("a sealed member's source not published (error %1)").arg(errorName(ok.error())); });
        } else if (auto ok = publishShared(info); !ok) {
            logDebug([&]() -> QString { return QStringLiteral("member %1 not published (error %2)").arg(m.path, errorName(ok.error())); });
        }
    }
}

void Ed3kOwnSets::unshareSet(Set& s)
{
    unshareOwner(ownerOf(s.id));
    for (const Member& m : s.members)
        if (const auto sv = m_serveOf.find(m.root); sv != m_serveOf.end()) {
            m_serve.erase(sv->second);
            m_serveOf.erase(sv);
        }
    if (s.manifest)
        unshare(*s.manifest);
    s.manifest.reset();
}

std::vector<std::pair<QString, QString>> Ed3kOwnSets::scan(const Set& s) const
{
    std::vector<std::pair<QString, QString>> out;
    if (!s.folder.isEmpty()) {
        const QDir root(s.folder);
        // hidden files and folders are left out (no QDir::Hidden; dot names explicitly)
        QDirIterator it(s.folder, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString file = it.next();
            const QString rel = root.relativeFilePath(file);
            if (std::ranges::any_of(rel.split(QLatin1Char('/')), [](const QString& c) { return c.startsWith(QLatin1Char('.')); }))
                continue;
            out.emplace_back(rel, file);
        }
        std::ranges::sort(out);
        return out;
    }
    std::set<QString> names;
    for (const QString& file : s.files) {
        const QFileInfo fi(file);
        if (!fi.isFile())
            continue;
        // one directory: a name twice gets a number (file-set.md §2.2)
        QString name = fi.fileName();
        for (int n = 2; names.contains(name); ++n)
            name = fi.suffix().isEmpty() ? QStringLiteral("%1 (%2)").arg(fi.completeBaseName()).arg(n)
                                         : QStringLiteral("%1 (%2).%3").arg(fi.completeBaseName()).arg(n).arg(fi.suffix());
        names.insert(name);
        out.emplace_back(name, fi.absoluteFilePath());
    }
    return out;
}

std::optional<Seed> Ed3kOwnSets::keyOf(const Set& s) const
{
    return loadSecret(ownDir(), keyName(QString::fromLatin1(s.id.toHex())));
}

QString Ed3kOwnSets::hiddenLink(const Set& s) const
{
    auto seed = keyOf(s);
    auto secret = loadSecret(ownDir(), secretName(QString::fromLatin1(s.id.toHex())));
    QString out;
    if (seed && secret)
        if (const auto key = ffi::keyPublic(*seed))
            if (auto link = ffi::hiddenLinkWrite(*key, *secret, utf8(s.title)))
                out = QString::fromStdString(*link);
    if (seed)
        wipe(std::span<std::uint8_t>{*seed});
    if (secret)
        wipe(std::span<std::uint8_t>{*secret});
    return out;
}

Ed3kOwnSets::Info Ed3kOwnSets::infoOf(const Set& s) const
{
    Info i;
    i.id = QString::fromLatin1(s.id.toHex());
    i.title = s.title;
    i.folder = s.folder;
    if (s.members.empty()) {
        for (const auto& [path, file] : scan(s)) {
            ++i.files;
            i.size += std::uint64_t(std::max<qint64>(QFileInfo(file).size(), 0));
        }
    } else {
        i.files = int(s.members.size());
        for (const Member& m : s.members)
            i.size += std::uint64_t(std::max<qint64>(m.fileSize, 0));
    }
    i.seq = s.seq;
    i.kind = s.kind;
    i.published = s.published;
    i.building = m_builds.contains(ownerOf(s.id));
    i.scope = s.scope;
    if (m_seller && !s.scope.isEmpty())
        if (const auto sale = m_seller->info(s.scope)) {
            i.sold = !sale->stopped;
            i.stopped = sale->stopped;
        }
    // a hidden set's link is a credential: linkOf() only
    if (s.kind != Kind::Hidden)
        i.link = linkOf(i.id);
    i.error = s.error;
    return i;
}

QString Ed3kOwnSets::ownerOf(const QByteArray& id)
{
    return QStringLiteral("set:") + QString::fromLatin1(id.toHex());
}

Ed3kOwnSets::Set* Ed3kOwnSets::find(const QString& id)
{
    const auto it = m_sets.find(QByteArray::fromHex(id.toLatin1()));
    return it == m_sets.end() ? nullptr : &it->second;
}

const Ed3kOwnSets::Set* Ed3kOwnSets::find(const QString& id) const
{
    const auto it = m_sets.find(QByteArray::fromHex(id.toLatin1()));
    return it == m_sets.end() ? nullptr : &it->second;
}

Ed3kOwnSets::Set* Ed3kOwnSets::findOwner(const QString& owner)
{
    if (!owner.startsWith(QStringLiteral("set:")))
        return nullptr;
    return find(owner.mid(4));
}

bool Ed3kOwnSets::serveFile(const QString& owner, const ffi::Root& root, std::uint64_t size, const QString& name, const QString& path,
                            std::span<const std::byte> hashes)
{
    // eMule's share list serves it over ed3k already
    if (m_sharedElsewhere && m_sharedElsewhere(root))
        return true;
    int id = 0;
    if (const auto it = m_serveOf.find(root); it != m_serveOf.end()) {
        id = it->second;
        m_serve[id] = {path, size};
    } else {
        id = m_nextServe++;
        m_serve[id] = {path, size};
        m_serveOf[root] = id;
    }
    const ffi::SharedFile f{root, size, utf8(name), std::nullopt, std::nullopt, ffi::Have::complete(), hashes};
    if (shareMember(f, owner, id))
        return true;
    m_serve.erase(id);
    m_serveOf.erase(root);
    return false;
}

void Ed3kOwnSets::markChanged()
{
    if (m_changePending)
        return;
    m_changePending = true;
    QMetaObject::invokeMethod(this, [this] {
        m_changePending = false;
        emit changed();
    }, Qt::QueuedConnection);
}

void Ed3kOwnSets::load()
{
    const QJsonObject root = QJsonDocument::fromJson(readBlob(statePath())).object();
    for (const auto& v : root.value(QStringLiteral("sets")).toArray()) {
        const QJsonObject o = v.toObject();
        Set s;
        s.id = QByteArray::fromHex(o.value(QStringLiteral("id")).toString().toLatin1());
        if (s.id.size() != kIdBytes)
            continue;
        s.title = o.value(QStringLiteral("title")).toString();
        s.folder = o.value(QStringLiteral("folder")).toString();
        for (const auto& f : o.value(QStringLiteral("files")).toArray())
            s.files << f.toString();
        const int kind = o.value(QStringLiteral("kind")).toInt(1);
        s.kind = kind == 2 ? Kind::Hidden : kind == 3 ? Kind::Paid : Kind::Open;
        s.seq = std::uint64_t(o.value(QStringLiteral("seq")).toInteger());
        s.created = std::uint64_t(o.value(QStringLiteral("created")).toInteger());
        s.scope = o.value(QStringLiteral("scope")).toString();
        s.offer = QByteArray::fromBase64(o.value(QStringLiteral("offer")).toString().toLatin1());
        s.published = o.value(QStringLiteral("published")).toBool();
        for (const auto& mv : o.value(QStringLiteral("members")).toArray()) {
            const QJsonObject mo = mv.toObject();
            const auto r = fromHex<32>(mo.value(QStringLiteral("root")).toString());
            if (!r)
                continue;
            Member m;
            m.path = mo.value(QStringLiteral("path")).toString();
            m.file = mo.value(QStringLiteral("file")).toString();
            m.fileSize = mo.value(QStringLiteral("fileSize")).toInteger(-1);
            m.mtimeMs = mo.value(QStringLiteral("mtime")).toInteger(-1);
            m.root.bytes = *r;
            m.size = std::uint64_t(mo.value(QStringLiteral("size")).toInteger());
            if (mo.contains(QStringLiteral("epoch")))
                m.epoch = std::uint64_t(mo.value(QStringLiteral("epoch")).toInteger());
            m.sealed = mo.value(QStringLiteral("sealed")).toBool();
            m.sealedPath = mo.value(QStringLiteral("sealedPath")).toString();
            s.members.push_back(std::move(m));
        }
        m_sets.emplace(s.id, std::move(s));
    }
}

void Ed3kOwnSets::save() const
{
    QJsonArray sets;
    for (const auto& [id, s] : m_sets) {
        QJsonArray members;
        for (const Member& m : s.members) {
            QJsonObject mo{
                {QStringLiteral("path"), m.path},
                {QStringLiteral("file"), m.file},
                {QStringLiteral("fileSize"), m.fileSize},
                {QStringLiteral("mtime"), m.mtimeMs},
                {QStringLiteral("root"), hex(m.root.bytes)},
                {QStringLiteral("size"), qint64(m.size)},
                {QStringLiteral("sealed"), m.sealed},
                {QStringLiteral("sealedPath"), m.sealedPath},
            };
            if (m.epoch)
                mo.insert(QStringLiteral("epoch"), qint64(*m.epoch));
            members.append(mo);
        }
        sets.append(QJsonObject{
            {QStringLiteral("id"), QString::fromLatin1(s.id.toHex())},
            {QStringLiteral("title"), s.title},
            {QStringLiteral("folder"), s.folder},
            {QStringLiteral("files"), QJsonArray::fromStringList(s.files)},
            {QStringLiteral("kind"), int(s.kind)},
            {QStringLiteral("seq"), qint64(s.seq)},
            {QStringLiteral("created"), qint64(s.created)},
            {QStringLiteral("scope"), s.scope},
            {QStringLiteral("offer"), QString::fromLatin1(s.offer.toBase64())},
            {QStringLiteral("published"), s.published},
            {QStringLiteral("members"), members},
        });
    }
    const QByteArray out = QJsonDocument(QJsonObject{{QStringLiteral("sets"), sets}}).toJson(QJsonDocument::Compact);
    QDir().mkpath(stateDir(m_configDir));
    if (!writeBlob(statePath(), spanOf(out)))
        logEd3kWarning(QStringLiteral("ed3k: %1 not written").arg(kStateFile));
}

QString Ed3kOwnSets::ownDir() const
{
    return QDir(stateDir(m_configDir)).filePath(kOwnDir);
}

} // namespace eMule::ed3k
