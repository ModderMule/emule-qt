/// @file Ed3kFilters.cpp
/// @brief Shared filter lists in the daemon — implementation.

#include "Ed3kFilters.h"

#include "Ed3kSession.h"
#include "Ed3kSets.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QStringList>

#include <algorithm>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

/// owner of the base's in-memory fetches (member = the list's handle)
const QString kFetchOwner = QStringLiteral("filter");
/// the own verdicts mirrored (the core lists none); not a credential
const QString kOwnFile = QStringLiteral("filter-own.dat");
/// the key the own list's set is signed with (§2): a secret, never logged
const QString kOwnKeyFile = QStringLiteral("filter-own.key");
/// the version published last, shared again at start
const QString kOwnListFile = QStringLiteral("filter-own.ed3kflt");
const QString kOwnManifestFile = QStringLiteral("filter-own.ed3kset");
/// set id bytes (file-set.md §5.1: 1 to 32)
constexpr int kSetIdBytes = 16;
/// changes of the own verdicts coalesced before a new version goes out
constexpr int kPublishDelayMs = 3000;

QString textOf(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

/// a TSV field
QString field(QString s)
{
    s.replace(QLatin1Char('\t'), QLatin1Char(' '));
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return s;
}

bool marks(ffi::FilterVerdict v)
{
    return v == ffi::FilterVerdict::Fake || v == ffi::FilterVerdict::Harmful || v == ffi::FilterVerdict::Spam;
}

} // namespace

Ed3kFilters::Ed3kFilters(Ed3kSession& session, const QString& configDir, QObject* parent)
    : Ed3kJoinBase(session, configDir, QStringLiteral("filters"), parent)
{
    // the base takes blocks and streams; the filter events are taken here
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kFilters::onFilterEvent, Qt::DirectConnection);
    m_pubTimer.setSingleShot(true);
    connect(&m_pubTimer, &QTimer::timeout, this, [this] {
        if (isRunning() && !m_pubId.isEmpty())
            (void)republish();
    });
    loadOwn();
    if (m_session.isRunning())
        start();
}

Ed3kFilters::~Ed3kFilters() = default;

void Ed3kFilters::start()
{
    if (m_started)
        return;
    beginStart();
    // the own list's mode lives in filter.dat too; set again in case it was lost
    if (auto* e = m_session.engine(); e && (m_ownHide || m_ownRefuse))
        (void)e->filterSetMode(m_session.now(), ED3K_FILTER_OWN, m_ownHide, m_ownRefuse);
    if (!m_pubId.isEmpty())
        restorePublished();
}

void Ed3kFilters::stop()
{
    if (!m_started)
        return;
    m_pubTimer.stop();
    closeAll();
    m_pubList.reset();
    m_pubManifest.reset();
}

bool Ed3kFilters::isRunning() const
{
    return m_started && m_session.isRunning();
}

std::vector<Ed3kFilters::List> Ed3kFilters::lists()
{
    std::vector<List> out;
    auto* e = m_session.engine();
    if (!e)
        return out;
    const auto raw = e->filterLists();
    if (!raw)
        return out;
    for (const ed3k_filter_info& i : *raw) {
        List l;
        l.id = i.filter;
        l.hide = (i.mode & ED3K_FILTER_HIDE) != 0;
        l.refuse = (i.mode & ED3K_FILTER_REFUSE) != 0;
        l.byHand = i.by_hand != 0;
        if (i.has_set != 0) {
            EntryId set{};
            std::copy(std::begin(i.set), std::end(i.set), set.begin());
            l.set = set;
        }
        if (i.has_seq != 0)
            l.seq = i.seq;
        l.files = i.files;
        l.entries = i.entries;
        l.waiting = i.waiting;
        if (const auto label = e->filterLabel(i.filter))
            l.label = textOf(label->text());
        out.push_back(std::move(l));
    }
    return out;
}

ffi::Result<std::uint32_t> Ed3kFilters::follow(const QString& link, const QString& label)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray l = link.trimmed().toUtf8();
    const QByteArray t = label.trimmed().toUtf8();
    // §3: only lists the user chose; BUSY at 32 followed
    auto id = e->filterFollow(m_session.now(), std::string_view(l.constData(), std::size_t(l.size())),
                              std::string_view(t.constData(), std::size_t(t.size())));
    if (id)
        logEd3k(QStringLiteral("ed3k: filter list %1 followed (%2)").arg(*id).arg(label));
    pumpSession();
    emit filtersChanged();
    return id;
}

ffi::Result<> Ed3kFilters::unfollow(std::uint32_t id)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    auto ok = e->filterUnfollow(m_session.now(), id);
    if (ok)
        logEd3k(QStringLiteral("ed3k: filter list %1 unfollowed").arg(id));
    pumpSession();
    emit filtersChanged();
    return ok;
}

ffi::Result<std::uint32_t> Ed3kFilters::openFile(const QString& label, const QByteArray& file)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    if (std::uint64_t(file.size()) > kListFileMax)
        return std::unexpected(ffi::Error::TooLarge);
    const QByteArray t = label.trimmed().toUtf8();
    auto id = e->filterOpen(m_session.now(), std::string_view(t.constData(), std::size_t(t.size())), spanOf(file));
    if (id)
        logEd3k(QStringLiteral("ed3k: filter list file opened by hand as %1 (%2)").arg(*id).arg(label));
    pumpSession();
    emit filtersChanged();
    return id;
}

ffi::Result<> Ed3kFilters::setMode(std::uint32_t id, bool hide, bool refuse)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    auto ok = e->filterSetMode(m_session.now(), id, hide, refuse);
    if (ok && id == ED3K_FILTER_OWN) {
        m_ownHide = hide;
        m_ownRefuse = refuse;
        saveOwn();
    }
    pumpSession();
    emit filtersChanged();
    return ok;
}

ffi::Result<> Ed3kFilters::setOwn(ffi::FilterSubject subject, const EntryId& id, ffi::FilterVerdict verdict, const QString& reason)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QByteArray r = reason.toUtf8();
    auto ok = e->filterSetOwn(m_session.now(), subject, id, verdict, std::string_view(r.constData(), std::size_t(r.size())));
    if (!ok)
        return ok;
    std::erase_if(m_own, [&](const Own& o) { return o.subject == subject && o.id == id; });
    if (verdict != ffi::FilterVerdict::None)
        m_own.push_back(Own{subject, id, verdict, reason});
    saveOwn();
    // §2: the published list follows the own verdicts
    schedulePublish();
    pumpSession();
    emit filtersChanged();
    return ok;
}

ffi::Result<ffi::Engine::FilterJudgement> Ed3kFilters::judgeOne(ffi::FilterSubject subject, const EntryId& id)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    return e->filterJudge(subject, id);
}

std::optional<Ed3kFilters::Judgement> Ed3kFilters::judge(const std::optional<ffi::Root>& root, const std::optional<EntryId>& ed2k,
                                                         const std::optional<ffi::NodeId>& author,
                                                         const std::optional<ffi::Key32>& setAuthor)
{
    std::vector<std::pair<ffi::FilterSubject, EntryId>> asks;
    if (root) {
        EntryId id{};   // a FILE entry is the first 16 bytes of the root (filter-list.md §1)
        std::copy_n(root->bytes.begin(), id.size(), id.begin());
        asks.emplace_back(ffi::FilterSubject::File, id);
    }
    if (ed2k)
        asks.emplace_back(ffi::FilterSubject::Ed2k, *ed2k);
    if (author)
        asks.emplace_back(ffi::FilterSubject::Author, author->bytes);
    // #18: the set's author too (§1): the core judges it with the row's first entry
    // (filterJudge's set author key, batch 13)
    // was (before the overload): asked as AUTHOR by its node ID, roomMemberId(*setAuthor)
    // if (setAuthor)
    //     if (const auto id = ffi::roomMemberId(*setAuthor); id && (!author || author->bytes != *id))
    //         asks.emplace_back(ffi::FilterSubject::Author, *id);
    auto* e = m_session.engine();
    if (!e)
        return std::nullopt;
    std::optional<Judgement> out;
    bool first = true;
    for (const auto& [subject, id] : asks) {
        const auto j = e->filterJudge(subject, id, first ? setAuthor : std::nullopt);
        first = false;
        if (!j || (j->said.empty() && !j->own && !j->hide && !j->refuse && !j->shown))
            continue;
        Judgement& r = out ? *out : out.emplace();
        // the core applied the modes, the own verdict and the user's choice to show it
        r.hide = r.hide || j->hide;
        r.refuse = r.refuse || j->refuse;
        r.shown = r.shown || j->shown;
        if (j->own && !r.own) {
            r.own = true;
            r.verdict = j->own->first;
            r.reason = textOf(j->own->second);
            r.by.clear();
        } else if (!r.own && r.verdict == ffi::FilterVerdict::None && !j->said.empty()) {
            const auto& s = j->said.front();
            r.verdict = s.verdict;
            r.by = textOf(s.title.empty() ? s.label : s.title);
            r.reason = textOf(s.reason);
        }
        const bool good = std::ranges::any_of(j->said, [](const auto& s) { return s.verdict == ffi::FilterVerdict::Good; });
        const bool bad = std::ranges::any_of(j->said, [](const auto& s) { return marks(s.verdict); });
        r.disagree = r.disagree || (good && bad);
    }
    if (out && out->shown) {
        out->hide = false;
        out->refuse = false;
    }
    return out;
}

ffi::Result<> Ed3kFilters::showAnyway(const ffi::Root& root, bool on)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    EntryId id{};   // the file's entry (§1): over every list, the author's ones too (judge())
    std::copy_n(root.bytes.begin(), id.size(), id.begin());
    auto ok = e->filterShowAnyway(m_session.now(), ffi::FilterSubject::File, id, on);
    if (ok)
        logEd3kDebug(QStringLiteral("ed3k: filter: %1 shown anyway: %2").arg(hexOf(id)).arg(on));
    pumpSession();
    emit filtersChanged();
    return ok;
}

Ed3kFilters::Published Ed3kFilters::published() const
{
    if (m_pubId.isEmpty())
        return {};
    auto seed = loadSecret(stateDir(m_configDir), kOwnKeyFile);
    if (!seed)
        return {};
    const Secret key(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    const auto pub = ffi::roomMemberKey(key.expose());   // the Ed25519 public key of a seed
    if (!pub)
        return {};
    return Published{Ed3kSets::setLink(*pub, m_pubId, m_pubTitle), m_pubTitle};
}

std::expected<QString, ffi::Error> Ed3kFilters::publishOwn(const QString& title)
{
    if (!isRunning())
        return std::unexpected(ffi::Error::State);
    const QString t = title.trimmed();
    if (t.isEmpty() || t.contains(QLatin1Char('/')))
        return std::unexpected(ffi::Error::Argument);
    if (m_pubId.isEmpty()) {
        // a new set id once; kept so every version is one set (file-set.md §5.1)
        std::array<quint32, kSetIdBytes / 4> words{};
        QRandomGenerator::system()->fillRange(words.data(), qsizetype(words.size()));
        m_pubId = QByteArray(reinterpret_cast<const char*>(words.data()), kSetIdBytes);
    }
    m_pubTitle = t;
    saveOwn();
    if (!republish())
        return std::unexpected(ffi::Error::State);
    emit filtersChanged();
    return published().link;
}

void Ed3kFilters::unpublishOwn()
{
    m_pubTimer.stop();
    if (m_pubList)
        unshare(*m_pubList);
    if (m_pubManifest)
        unshare(*m_pubManifest);
    m_pubList.reset();
    m_pubManifest.reset();
    m_pubId.clear();
    m_pubTitle.clear();
    QFile::remove(localPath(kOwnListFile));
    QFile::remove(localPath(kOwnManifestFile));
    saveOwn();
    logEd3k(QStringLiteral("ed3k: own filter list no longer published"));
    emit filtersChanged();
}

QString Ed3kFilters::ownFileName()
{
    return kOwnFile;
}

// -- the base's hooks (in-memory fetches only) --------------------------------

std::vector<std::uint64_t> Ed3kFilters::wantedBlocks(const Download& d) const
{
    Q_UNUSED(d)
    return {};
}

void Ed3kFilters::onFileBlock(const Download& d, const ffi::event::Block& b)
{
    Q_UNUSED(d) Q_UNUSED(b)
}

void Ed3kFilters::onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data)
{
    auto* e = m_session.engine();
    if (!e || owner != kFetchOwner)
        return;
    // the core checks the root, the magic and the limits (§1, §2)
    const auto ok = e->filterFile(m_session.now(), root, spanOf(data));
    if (!ok)
        logDebug(QStringLiteral("list file of filter list %1 not taken (error %2)").arg(member).arg(errorName(ok.error())));
    pumpSession();
    emit listFileFetched(quint32(member), ok.has_value());
}

std::optional<Ed3kJoinBase::FileRead> Ed3kFilters::sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const
{
    Q_UNUSED(s) Q_UNUSED(offset) Q_UNUSED(len)
    return std::nullopt;   // nothing shared from here
}

bool Ed3kFilters::hasOwner(const QString& owner) const
{
    Q_UNUSED(owner)
    return false;
}

void Ed3kFilters::saveOwner(const QString& owner)
{
    Q_UNUSED(owner)
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kFilters::onFilterEvent(const ffi::Event& ev, bool* handled)
{
    namespace e = ffi::event;
    if (const auto* f = std::get_if<e::FilterFetch>(&ev)) {
        *handled = true;
        onFetch(*f);
        return;
    }
    if (const auto* u = std::get_if<e::FilterUpdated>(&ev)) {
        logEd3k(QStringLiteral("ed3k: filter list %1 updated: %2 list file(s)").arg(u->filter).arg(u->files));
    } else if (const auto* t = std::get_if<e::FilterTooLarge>(&ev)) {
        // §2: the old version stays; the client says so
        logEd3kWarning(QStringLiteral("ed3k: filter list %1 is above the limits; the version before stays").arg(t->filter));
    } else if (const auto* i = std::get_if<e::FilterIgnored>(&ev)) {
        logEd3kDebug(QStringLiteral("ed3k: a file of filter list %1 is no list file, ignored (%2)")
                         .arg(i->filter).arg(hexOf(i->root.bytes).left(16)));
    } else if (std::holds_alternative<e::FilterChanged>(ev)) {
        // the session saves filter.dat; the lists may have moved
    } else {
        return;
    }
    *handled = true;
    emit filtersChanged();
}

void Ed3kFilters::onFetch(const ffi::event::FilterFetch& f)
{
    // the core picked the list files (§2); the base finds sources and fetches into memory
    if (f.size == 0 || f.size > kListFileMax) {
        logEd3kDebug(QStringLiteral("ed3k: filter list file %1 of %2 B not fetched").arg(textOf(f.name)).arg(f.size));
        return;
    }
    if (!m_started || downloading(f.root))
        return;
    if (!fetchBlob(f.root, f.size, kFetchOwner, int(f.filter)))
        logEd3kDebug(QStringLiteral("ed3k: filter list file %1 not fetched").arg(textOf(f.name)));
}

void Ed3kFilters::loadOwn()
{
    m_own.clear();
    const QByteArray data = readBlob(localPath(kOwnFile));
    for (const QString& line : QString::fromUtf8(data).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList f = line.split(QLatin1Char('\t'));
        if (f.size() >= 3 && f.at(0) == QLatin1StringView("mode")) {
            m_ownHide = f.at(1) == QLatin1StringView("1");
            m_ownRefuse = f.at(2) == QLatin1StringView("1");
            continue;
        }
        if (f.size() >= 3 && f.at(0) == QLatin1StringView("publish")) {
            m_pubId = QByteArray::fromHex(f.at(1).toLatin1());
            m_pubTitle = f.at(2);
            if (m_pubId.size() != kSetIdBytes)
                m_pubId.clear();
            continue;
        }
        if (f.size() < 3)
            continue;
        const int subject = f.at(0).toInt();
        const QByteArray id = QByteArray::fromHex(f.at(1).toLatin1());
        const int verdict = f.at(2).toInt();
        if (subject < 1 || subject > 3 || verdict < 1 || verdict > 4 || id.size() != 16)
            continue;
        Own o;
        o.subject = ffi::FilterSubject(subject);
        std::copy(id.cbegin(), id.cend(), reinterpret_cast<char*>(o.id.data()));
        o.verdict = ffi::FilterVerdict(verdict);
        o.reason = f.size() > 3 ? f.at(3) : QString();
        m_own.push_back(std::move(o));
    }
}

void Ed3kFilters::saveOwn() const
{
    QString out = QStringLiteral("mode\t%1\t%2\n").arg(m_ownHide ? 1 : 0).arg(m_ownRefuse ? 1 : 0);
    if (!m_pubId.isEmpty())
        out += QStringLiteral("publish\t%1\t%2\n").arg(QString::fromLatin1(m_pubId.toHex()), field(m_pubTitle));
    for (const Own& o : m_own)
        out += QStringLiteral("%1\t%2\t%3\t%4\n").arg(int(o.subject)).arg(hexOf(o.id)).arg(int(o.verdict)).arg(field(o.reason));
    const QByteArray data = out.toUtf8();
    QDir().mkpath(stateDir(m_configDir));
    if (!writeBlob(localPath(kOwnFile), spanOf(data)))
        logEd3kWarning(QStringLiteral("ed3k: %1 not written").arg(kOwnFile));
}

QString Ed3kFilters::localPath(const QString& name) const
{
    return QDir(stateDir(m_configDir)).filePath(name);
}

bool Ed3kFilters::republish()
{
    auto* e = m_session.engine();
    if (!e || m_pubId.isEmpty())
        return false;
    const auto t = m_session.now();
    const QByteArray title = m_pubTitle.toUtf8();
    if (auto ok = e->filterSetOwnTitle(t, std::string_view(title.constData(), std::size_t(title.size()))); !ok) {
        logWarning(QStringLiteral("own filter list: title not taken (error %1)").arg(errorName(ok.error())));
        return false;
    }
    // §1, §2: the list file, then a set holding it with a seq above the last
    auto file = e->filterOwnWrite(t);
    if (!file) {
        logWarning(QStringLiteral("own filter list not written (error %1)").arg(errorName(file.error())));
        return false;
    }
    const QByteArray list(reinterpret_cast<const char*>(file->bytes().data()), qsizetype(file->bytes().size()));
    ffi::Hasher h;
    if (!h.update(spanOf(list)))
        return false;
    const auto hashed = h.finish();
    if (!hashed)
        return false;
    auto seed = loadOrCreateSecret(stateDir(m_configDir), kOwnKeyFile, {});
    if (!seed) {
        logWarning(QStringLiteral("own filter list: no list key"));
        return false;
    }
    const Secret key(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    auto manifest = e->filterOwnManifest(t, key.expose(), spanOf(m_pubId), hashed->root, hashed->size);
    if (!manifest) {
        logWarning(QStringLiteral("own filter list: no manifest (error %1)").arg(errorName(manifest.error())));
        return false;
    }
    const QByteArray man(reinterpret_cast<const char*>(manifest->bytes().data()), qsizetype(manifest->bytes().size()));
    if (auto ok = shareAndPublish(list, man); !ok) {
        logWarning(QStringLiteral("own filter list not published (error %1)").arg(errorName(ok.error())));
        return false;
    }
    // kept: shared and published again at start
    (void)writeBlob(localPath(kOwnListFile), spanOf(list));
    (void)writeBlob(localPath(kOwnManifestFile), spanOf(man));
    logEd3k(QStringLiteral("ed3k: own filter list published (%1 verdict(s))").arg(m_own.size()));
    return true;
}

void Ed3kFilters::restorePublished()
{
    const QByteArray list = readBlob(localPath(kOwnListFile));
    const QByteArray man = readBlob(localPath(kOwnManifestFile));
    // a version kept: the same again (its head is re-signed by the core); else a new one
    if (!list.isEmpty() && !man.isEmpty() && shareAndPublish(list, man))
        return;
    (void)republish();
}

ffi::Result<> Ed3kFilters::shareAndPublish(const QByteArray& list, const QByteArray& manifest)
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(ffi::Error::State);
    const QString base = m_pubTitle.isEmpty() ? QStringLiteral("filter list") : m_pubTitle;
    const auto listRoot = shareBlob(list, base + QStringLiteral(".ed3kflt"), std::nullopt);
    const auto manRoot = shareBlob(manifest, base + QStringLiteral(".ed3kset"), std::nullopt);
    if (!listRoot || !manRoot)
        return std::unexpected(ffi::Error::State);
    auto seed = loadSecret(stateDir(m_configDir), kOwnKeyFile);
    if (!seed)
        return std::unexpected(ffi::Error::Io);
    const Secret key(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    const QByteArray n = (base + QStringLiteral(".ed3kset")).toUtf8();
    const auto ok = e->setPublish(m_session.now(), spanOf(manifest), *manRoot, std::string_view(n.constData(), std::size_t(n.size())),
                                  ffi::SetSigner::Seed, &key.expose());
    if (!ok) {
        unshare(*manRoot);
        if (*listRoot != m_pubList)
            unshare(*listRoot);
        return std::unexpected(ok.error());
    }
    // the version before is no longer shared (a member with unchanged root stays, §2)
    if (m_pubManifest && *m_pubManifest != *manRoot)
        unshare(*m_pubManifest);
    if (m_pubList && *m_pubList != *listRoot)
        unshare(*m_pubList);
    m_pubList = listRoot;
    m_pubManifest = manRoot;
    pumpSession();
    return {};
}

void Ed3kFilters::schedulePublish()
{
    if (!m_pubId.isEmpty())
        m_pubTimer.start(kPublishDelayMs);
}

} // namespace eMule::ed3k
