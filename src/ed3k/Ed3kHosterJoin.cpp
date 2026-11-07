#include "Ed3kHosterJoin.h"

#include "Ed3kSession.h"
#include "Ed3kStore.h"
#include "decode/YencDecoder.h"
#include "post/UsenetUnpacker.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <array>
#include <tuple>
#include <cstring>
#include <utility>

namespace eMule::ed3k {

namespace ev = ffi::event;

namespace {

/// bytes per linkData call; the core holds at most a sealed chunk per stream
constexpr std::uint32_t kFeedChunk = 1024 * 1024;
/// an ask that failed for a reason of the moment is tried again this often at most
constexpr int kMaxTries = 5;
constexpr int kTickMs = 1000;
/// one item per link, at most this many
constexpr std::size_t kMaxItems = 4096;
/// a container file sent over IPC; DLCs are a few KiB
constexpr qsizetype kMaxContainerBytes = 4 * 1024 * 1024;
/// Click'n'Load posts read at once; any web page can post, so a few only
constexpr std::size_t kMaxCnlAsks = 4;
constexpr qsizetype kMaxCnlBytes = 1024 * 1024;
/// a read of the resolver's file that failed is tried again after this, at most this often
constexpr int kReadRetryMs = 200;
constexpr int kMaxReadFailures = 50;
/// a §4 lookup that has not ended by then: the link is used without a root
constexpr qint64 kLookupMs = 45 * 1000;
/// a hoster wait longer than this lets a peer's link stand in
constexpr qint64 kLongWaitMs = 5 * 60 * 1000;
/// resume only past this many bytes; below it, starting over costs less than the copy
constexpr std::uint64_t kMinResume = 1024 * 1024;
/// checkLinks(): checks at once, how long a result and its core link stay, how long a
/// check or a wait for a slot may take, how many results are kept
constexpr qint64 kCheckTtlMs = 30 * 60 * 1000;
constexpr qint64 kProbeTimeoutMs = 60 * 1000;
constexpr qint64 kProbeQueueMs = 2 * 60 * 1000;
constexpr std::size_t kMaxChecks = 4096;
/// an account's premium state is asked again after this
constexpr qint64 kAccountTtlMs = 6 * 3600 * 1000;
/// a shared link's recheck that could not tell is tried again after this (or recheckHours)
constexpr qint64 kRecheckRetryMs = 6 * 3600 * 1000;
/// hoster.md §2: dead-link reports from downloaders in this many sites check a shared link at once
constexpr std::size_t kReportsUntilCheck = 3;
/// an ALIAS waiting for its root to be shared (hashing after completion or a start): tried
/// each tick for this long
constexpr qint64 kAliasWaitMs = 2 * 3600 * 1000;
/// ALIAS records of a root per scheme the core keeps (alias.md §3); links linkShare takes
constexpr int kMaxAliasLinks = 4;
constexpr int kMaxShareLinks = 8;
/// an unfinished item in Temp: <key>.hoster.part and its state <key>.hoster.part.met (JSON)
const QString kPartSuffix = QStringLiteral(".hoster.part");
const QString kMetSuffix = QStringLiteral(".hoster.part.met");
const QString kMetFormat = QStringLiteral("ed3k-hoster-met");
constexpr int kMetVersion = 1;
/// a link's file with no usable name
const QString kNoName = QStringLiteral("download");

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

/// a new item's file name; its part file and .met in Temp
QString newKey()
{
    return QStringLiteral("%1").arg(QRandomGenerator::system()->generate64(), 16, 16, QLatin1Char('0'));
}

/// The first @p len bytes of @p from into @p to (a new file); for the background.
bool copyPrefix(const QString& from, const QString& to, std::uint64_t len)
{
    QFile::remove(to);
    // the whole file: a clone where the file system has them (APFS, btrfs)
    if (QFileInfo(from).size() == qint64(len))
        return QFile::copy(from, to);
    QFile in(from);
    QFile out(to);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly))
        return false;
    std::uint64_t left = len;
    while (left > 0) {
        const QByteArray b = in.read(qint64(std::min<std::uint64_t>(left, 4 * 1024 * 1024)));
        if (b.isEmpty() || out.write(b) != b.size())
            return false;
        left -= std::uint64_t(b.size());
    }
    return true;
}

std::optional<ffi::Root> rootFromHex(const QString& hex)
{
    const QByteArray b = QByteArray::fromHex(hex.toLatin1());
    if (b.size() != 32)
        return std::nullopt;
    ffi::Root r;
    std::memcpy(r.bytes.data(), b.constData(), 32);
    return r;
}

} // namespace

struct Ed3kHosterJoin::Entry {
    Item item;
    std::optional<ffi::Root> root;
    std::uint64_t link = 0;              ///< the core's link; 0 = none yet
    std::set<std::uint64_t> have;        ///< verified block offsets (rooted)
    std::optional<std::uint32_t> search; ///< §4 lookup running
    Ed3kResolver::TaskId task = 0;       ///< the resolver task of this item's link
    qint64 nextAskAt = 0;                ///< Queued/Waiting: ask again then
    int tries = 0;
    std::uint64_t peerLink = 0;          ///< a peer's link asked or open for this item
    std::set<std::uint64_t> peerTried;   ///< peers' links that failed for it
    QList<SharedLink> shared;            ///< §4.1: links the node vouches for, own first
    QStringList sharedOut;               ///< what linkShare was given last
    std::uint64_t lastDone = 0;          ///< for the speed
    bool expandAfterCheck = false;
    QString key;                         ///< unfinished: <key>.hoster.part(.met) in Temp
    bool metDirty = false;               ///< the .met is to be written
    QString hashType;                    ///< the hoster's checksum of the link (check or open), if any
    QString hash;
    bool hashing = false;                ///< checkHash() runs
    /// LinkVouched while hashing: applied once the checksum matched
    std::optional<std::pair<std::uint64_t, ffi::Root>> heldVouch;
};

Ed3kHosterJoin::Ed3kHosterJoin(Ed3kSession& session, const QString& configDir, QObject* parent)
    : Ed3kJoinBase(session, configDir, QStringLiteral("hoster"), parent)
{
    m_tick.setInterval(kTickMs);
    connect(&m_tick, &QTimer::timeout, this, &Ed3kHosterJoin::onTick);
    connect(&m_unpack, &Ed3kHosterUnpack::finished, this, &Ed3kHosterJoin::onUnpacked);
    connect(&m_unpack, &Ed3kHosterUnpack::progress, this, [this](const QString& first, int percent) {
        const QString arg = QString::number(percent);
        for (auto& [id, e] : m_items)
            if (e->item.reason == Reason::Unpacking && e->item.reasonArg != arg
                && Ed3kHosterUnpack::setKey(e->item.name) == Ed3kHosterUnpack::setKey(QFileInfo(first).fileName())
                && QFileInfo(e->item.file).absolutePath() == QFileInfo(first).absolutePath()) {
                e->item.reasonArg = arg;
                changed(*e);
            }
    });
    // the base's onOtherEvent cannot mark an event answered: a LinkResolve is taken here
    connect(&session, &Ed3kSession::eventPolled, this, [this](const ffi::Event& event, bool* handled) {
        if (const auto* r = std::get_if<ev::LinkResolve>(&event); r && m_started) {
            onResolve(*r);
            if (handled)
                *handled = true;
        }
    }, Qt::DirectConnection);
    load();
}

Ed3kHosterJoin::~Ed3kHosterJoin()
{
    if (m_started)
        stop();
}

void Ed3kHosterJoin::setResolver(Ed3kResolver* resolver)
{
    if (m_resolver)
        m_resolver->disconnect(this);
    m_resolver = resolver;
    m_resolverReady = m_resolver && m_resolver->available();
    if (m_resolver) {
        connect(m_resolver, &Ed3kResolver::taskEvent, this, &Ed3kHosterJoin::onResolverEvent);
        // items that came before the resolver (Java probe, plugin unpack) go on once it is ready
        connect(m_resolver, &Ed3kResolver::stateChanged, this, &Ed3kHosterJoin::retryNotReady);
    }
}

void Ed3kHosterJoin::setConfig(const Ed3kHosterPrefs& prefs, const QStringList& tempDirs, const QString& incomingDir)
{
    const bool allowed = prefs.enabled != m_prefs.enabled;
    const bool publishChanged = prefs.publishLinks != m_prefs.publishLinks || prefs.recheckHours != m_prefs.recheckHours
                             || prefs.reportRecheckHours != m_prefs.reportRecheckHours;
    const bool accountsChanged = prefs.accounts != m_prefs.accounts;
    const bool extractOff = m_prefs.extract && !prefs.extract;
    m_prefs = prefs;
    m_tempDirs.clear();
    for (const QString& d : tempDirs)
        if (!d.isEmpty() && !m_tempDirs.contains(d))
            m_tempDirs << d;
    m_incomingDir = incomingDir;
    scanTempDirs();   // .met files copied in meanwhile
    if (m_resolver)
        m_resolver->setAskPuzzles(prefs.askPuzzles);
    if (allowed && m_started) {
        if (auto* e = m_session.engine())
            (void)e->linkAllow(m_prefs.enabled);
        // switched on: what waited for it goes on
        if (m_prefs.enabled)
            for (auto& [id, e] : m_items)
                if (e->item.state == State::NeedsUser)
                    begin(*e);
    }
    // §4.1: publishing hands out the vouched links; switching it off withdraws them
    if (publishChanged && m_started) {
        for (auto& [id, e] : m_items)
            publishShared(*e);
        queueRechecks(nowMs());
    }
    if ((accountsChanged || allowed) && m_started)
        refreshAccounts();
    if (extractOff)
        releaseArchives();
}

void Ed3kHosterJoin::start()
{
    if (m_started)
        return;
    beginStart();
    // the resolver's scratch of the last run; nothing of it is used again
    QDir(QFileInfo(workDir(0)).path()).removeRecursively();
    scanTempDirs();
    if (auto* e = m_session.engine())
        (void)e->linkAllow(m_prefs.enabled);
    m_tick.start();
    for (auto& [id, e] : m_items) {
        switch (e->item.state) {
        case State::Complete:
        case State::Paused:
        case State::Gone:
        case State::Failed:
            break;
        case State::NoResolver:
            // not ready last run (e.g. Java not found yet): try again
            if (e->item.reason == Reason::ResolverNotReady)
                begin(*e);
            break;
        default:
            begin(*e);
        }
        // stale links wait for their recheck (queueRechecks), the rest go out now
        publishShared(*e);
    }
    queueRechecks(nowMs());
    refreshAccounts();
    pumpSession();
}

void Ed3kHosterJoin::stop()
{
    if (!m_started)
        return;
    m_tick.stop();
    for (auto& [id, e] : m_items) {
        release(*e, true);
        // the core forgets what it keeps refreshing with the node; start() publishes again
        for (SharedLink& l : e->shared) {
            l.aliasOut = false;
            l.checking = false;
        }
        e->sharedOut.clear();
    }
    m_aliasPending.clear();
    m_rootlessDone.clear();
    m_asks.clear();
    for (const auto& [link, a] : m_preparing)
        QDir(a.dir).removeRecursively();
    m_preparing.clear();
    m_matches.clear();
    m_searches.clear();
    m_peerLinks.clear();
    if (auto* eng = m_session.engine())
        for (const auto& [link, p] : m_probes)
            if (!p.adopted)
                (void)eng->linkRemove(link);
    m_probes.clear();
    m_probeQueue.clear();
    m_probeMatches.clear();
    m_accountChecks.clear();
    m_siteQueries.clear();
    std::erase_if(m_checks, [](const auto& c) { return c.second.status == LinkStatus::Checking; });
    save();
    closeAll();
}

QStringList Ed3kHosterJoin::linksIn(const QString& text)
{
    static const QRegularExpression re(QStringLiteral(R"((https?://[^\s<>"']+))"), QRegularExpression::CaseInsensitiveOption);
    QStringList out;
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const QString url = it.next().captured(1);
        if (url.size() <= 1024 && !out.contains(url))
            out << url;
    }
    return out;
}

bool Ed3kHosterJoin::digestKnown(const QString& type)
{
    static const QStringList known{QStringLiteral("md5"),    QStringLiteral("sha1"),   QStringLiteral("sha224"),
                                   QStringLiteral("sha256"), QStringLiteral("sha384"), QStringLiteral("sha512"),
                                   QStringLiteral("crc32"),  QStringLiteral("crc32c")};
    return known.contains(type);
}

QString Ed3kHosterJoin::fileDigest(const QString& path, const QString& type)
{
    if (!digestKnown(type))
        return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    constexpr qint64 kChunk = 1024 * 1024;
    const auto crcHex = [](quint32 v) { return QStringLiteral("%1").arg(v, 8, 16, QLatin1Char('0')); };
    if (type == u"crc32") {
        quint32 crc = 0;
        while (!f.atEnd())
            crc = usenet::yencCrc32(crc, f.read(kChunk));
        return f.error() == QFile::NoError ? crcHex(crc) : QString();
    }
    if (type == u"crc32c") {
        // Castagnoli, reflected poly 0x82F63B78
        static const auto table = [] {
            std::array<quint32, 256> t{};
            for (quint32 i = 0; i < 256; ++i) {
                quint32 c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1) ? (c >> 1) ^ 0x82F63B78u : c >> 1;
                t[i] = c;
            }
            return t;
        }();
        quint32 crc = 0xFFFFFFFFu;
        while (!f.atEnd())
            for (const char ch : f.read(kChunk))
                crc = table[(crc ^ quint8(ch)) & 0xFF] ^ (crc >> 8);
        return f.error() == QFile::NoError ? crcHex(crc ^ 0xFFFFFFFFu) : QString();
    }
    using A = QCryptographicHash::Algorithm;
    const A algo = type == u"md5"      ? A::Md5
                   : type == u"sha1"   ? A::Sha1
                   : type == u"sha224" ? A::Sha224
                   : type == u"sha256" ? A::Sha256
                   : type == u"sha384" ? A::Sha384
                                       : A::Sha512;
    QCryptographicHash h(algo);
    if (!h.addData(&f))
        return {};
    return QString::fromLatin1(h.result().toHex());
}

QList<quint32> Ed3kHosterJoin::addLinks(const QString& text, const QString& package, quint32 packageId)
{
    QList<QStringList> files;
    for (const QString& url : linksIn(text))
        files.append(QStringList{url});
    return addMirrored(files, package, packageId);
}

QList<quint32> Ed3kHosterJoin::addMirrored(const QList<QStringList>& files, const QString& package,
                                           quint32 packageId, const QStringList& passwords)
{
    // the package: an existing one by id, else a new one by name; one level only
    QString packageName;
    if (packageId != 0) {
        const auto it = std::ranges::find_if(m_items, [packageId](const auto& p) {
            return p.second->item.packageId == packageId;
        });
        if (it != m_items.end())
            packageName = it->second->item.package;
        else
            packageId = 0;   // gone meanwhile: a new one of that name, if any
    }
    if (packageId == 0 && !package.trimmed().isEmpty()) {
        // a folder name: "AC/DC" stays one name, not "DC"
        packageName = safeName(QString(package.trimmed()).replace(QLatin1Char('/'), QLatin1Char('_'))
                                                         .replace(QLatin1Char('\\'), QLatin1Char('_')), kNoName);
        packageId = m_nextPackageId++;
    }

    QList<quint32> ids;
    for (const QStringList& file : files) {
        QStringList urls;
        for (const QString& u : file)
            if (const QStringList l = linksIn(u); l.size() == 1 && !urls.contains(l.first()))
                urls << l.first();
        if (urls.isEmpty())
            continue;
        const bool known = std::ranges::any_of(m_items, [&urls](const auto& p) {
            const Item& it = p.second->item;
            return it.state != State::Failed && (urls.contains(it.url) || std::ranges::any_of(it.mirrors, [&urls](const QString& m) {
                return urls.contains(m);
            }));
        });
        if (known || m_items.size() >= kMaxItems)
            continue;
        urls = rankMirrors(urls);
        const QString url = urls.takeFirst();
        auto e = std::make_unique<Entry>();
        e->item.id = m_nextId++;
        e->item.url = url;
        e->item.mirrors = urls;
        e->item.host = QUrl(url).host();
        e->item.name = safeName(QUrl(url).fileName(), kNoName);
        e->item.added = nowMs();
        e->item.packageId = packageId;
        e->item.package = packageName;
        e->item.passwords = passwords;
        e->key = newKey();
        e->item.file = partPathFor(e->key);
        e->metDirty = true;
        Entry& ref = *e;
        m_items.emplace(ref.item.id, std::move(e));
        ids << ref.item.id;
        count(&HosterCounters::linksAdded);
        logInfo(QStringLiteral("link added for %1").arg(ref.item.host));
        changed(ref);
        if (m_started)
            begin(ref);
        else
            setState(ref, State::Queued, Reason::WaitingForNode);
    }
    save();
    pumpSession();
    return ids;
}

void Ed3kHosterJoin::pause(quint32 id)
{
    Entry* e = find(id);
    if (!e || e->item.state == State::Complete)
        return;
    release(*e, true);
    setState(*e, State::Paused);
    save();
    pumpSession();
}

void Ed3kHosterJoin::resume(quint32 id)
{
    Entry* e = find(id);
    if (!e || e->item.state == State::Complete)
        return;
    e->tries = 0;
    if (m_started)
        begin(*e);
    save();
    pumpSession();
}

bool Ed3kHosterJoin::setArchivePassword(quint32 id, const QString& password)
{
    Entry* e = find(id);
    if (!e || password.isEmpty() || e->item.state != State::Complete
        || (e->item.reason != Reason::UnpackPassword && e->item.reason != Reason::UnpackFailed))
        return false;
    const quint32 pkg = e->item.packageId;
    const QString dir = QFileInfo(e->item.file).absolutePath();
    bool any = false;
    for (auto& [oid, op] : m_items) {
        Item& it = op->item;
        const bool group = pkg != 0 ? it.packageId == pkg
                                    : it.packageId == 0 && it.state == State::Complete
                                          && QFileInfo(it.file).absolutePath() == dir;
        if (!group)
            continue;
        // first: the one the user knows; the container's after it
        it.passwords.removeAll(password);
        it.passwords.prepend(password);
        if (it.state == State::Complete && (it.reason == Reason::UnpackPassword || op.get() == e)) {
            it.reason = Reason::UnpackWaiting;
            it.reasonArg.clear();
        }
        changed(*op);
        any = true;
    }
    save();
    queueUnpacks();
    return any;
}

void Ed3kHosterJoin::retry(quint32 id)
{
    Entry* e = find(id);
    if (e && e->item.state == State::Complete
        && (e->item.reason == Reason::UnpackFailed || e->item.reason == Reason::UnpackPassword)) {
        // the archive's unpack again (a volume put in by hand, an unpacker installed)
        e->item.reason = Reason::UnpackWaiting;
        e->item.reasonArg.clear();
        changed(*e);
        save();
        queueUnpacks();
        return;
    }
    if (!e || e->item.state == State::Complete)
        return;
    QStringList all = e->item.tried;
    all << e->item.url << e->item.mirrors;
    // puzzles of its hosters are asked again
    QSet<QString> sites{siteOf(e->item.host)};
    for (const QString& u : std::as_const(all))
        sites.insert(siteOf(QUrl(u).host()));
    m_skipSites.removeIf([&sites, this](const QString& s) { return sites.contains(siteOf(s)); });
    if (!e->item.tried.isEmpty()) {
        // back to the first link; verified blocks stay (begin())
        release(*e, true);
        e->item.url = all.takeFirst();
        e->item.mirrors = all;
        e->item.tried.clear();
        e->item.host = QUrl(e->item.url).host();
        e->item.mirrorNo = 0;
        touch(*e);
    }
    e->tries = 0;
    e->nextAskAt = 0;
    logInfo(QStringLiteral("retry of %1 from %2").arg(e->item.name, e->item.host));
    if (m_started)
        begin(*e);
    save();
    pumpSession();
}

void Ed3kHosterJoin::remove(quint32 id, bool deleteFile)
{
    Entry* e = find(id);
    if (!e)
        return;
    const bool complete = e->item.state == State::Complete;
    // its links are not this node's to vouch for any more
    for (SharedLink& l : e->shared)
        l.offline = true;
    publishShared(*e);
    m_aliasPending.erase(id);
    release(*e, false);
    if (!complete)
        QFile::remove(metPathOf(*e));
    if (!complete || deleteFile) {
        QFile::remove(e->item.file);
        // a package folder goes with its last file (rmdir only takes an empty one)
        if (complete && !e->item.package.isEmpty())
            QDir().rmdir(QDir(m_incomingDir).filePath(e->item.package));
        if (complete)
            QDir().rmdir(archiveDirOf(e->item));
    }
    QDir(workDir(id)).removeRecursively();
    m_items.erase(id);
    emit itemRemoved(id);
    save();
    pumpSession();
}

void Ed3kHosterJoin::answerPuzzle(quint64 puzzle, const QString& text, bool skipHoster)
{
    if (m_resolver)
        m_resolver->answerPuzzle(puzzle, text);
    if (!text.isEmpty())
        count(&HosterCounters::puzzlesAnswered);
    const auto it = m_puzzles.find(puzzle);
    if (it == m_puzzles.end())
        return;
    const AskedPuzzle asked = it->second;
    m_puzzles.erase(it);
    if (Entry* e = find(asked.item); e && e->item.state == State::Puzzle)
        setState(*e, State::Downloading);
    if (!skipHoster || !text.isEmpty() || asked.site.isEmpty())
        return;
    m_skipSites.insert(asked.site);
    logInfo(QStringLiteral("puzzles of %1 are skipped until a retry").arg(asked.site));
    // the hoster's other puzzles open now go too
    QList<quint64> others;
    for (const auto& [p, a] : m_puzzles)
        if (a.site == asked.site)
            others << p;
    for (const quint64 p : std::as_const(others))
        answerPuzzle(p, {});
}

void Ed3kHosterJoin::checkAccount(const QString& host, const QString& user, const QString& password)
{
    if (!m_resolver) {
        ResolverEvent r;
        r.kind = ResolverEvent::Kind::Failed;
        r.code = QStringLiteral("unavailable");
        emit accountChecked(host, user, r);
        return;
    }
    // key-only solver services: the user is free text, the key goes as password
    const auto task = m_resolver->accountCheck(host, ResolverAccount{user.isEmpty() ? QStringLiteral("apikey") : user, password});
    m_accountChecks[task] = {host, user};
}

quint64 Ed3kHosterJoin::openContainer(const QByteArray& data, const QString& fileName)
{
    if (!m_resolver || !m_prefs.enabled || data.isEmpty() || data.size() > kMaxContainerBytes)
        return 0;
    // the plugin goes by the extension; JD reads a file
    const QString name = safeName(QFileInfo(fileName).fileName(), QStringLiteral("links.dlc"));
    const quint64 token = m_nextContainer++;
    const QString dir = QDir(m_tempDirs.isEmpty() ? joinDir(QStringLiteral("hoster")) : m_tempDirs.first())
                            .filePath(QStringLiteral("ed3k-hoster/containers"));
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("%1-%2").arg(token).arg(name));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size()) {
        logWarning(QStringLiteral("cannot write container %1").arg(path));
        f.remove();
        return 0;
    }
    f.close();
    m_containerAsks[m_resolver->openContainer(path)] = {token, path, name};
    logInfo(QStringLiteral("opening container %1").arg(name));
    return token;
}

quint64 Ed3kHosterJoin::openClickNLoad(const ClickNLoad& post)
{
    if (!m_resolver || !m_prefs.enabled || m_cnlAsks.size() >= kMaxCnlAsks)
        return 0;
    if (post.urls.size() + post.crypted.size() + post.jk.size() > kMaxCnlBytes)
        return 0;
    CnlAsk ask;
    ask.source = post.source.left(1024);
    ask.package = post.package.trimmed().left(255);
    for (const QString& pw : post.passwords)
        if (const QString t = pw.trimmed(); !t.isEmpty() && !ask.passwords.contains(t))
            ask.passwords << t;
    for (const QString& url : linksIn(post.urls)) {
        ResolverLink l{url, {}, {}, 0};
        l.package = ask.package;
        l.passwords = ask.passwords;
        ask.plain.append(l);
    }
    const QString dummy = dummyCnlUrl(post);
    if (dummy.isEmpty() && ask.plain.isEmpty())
        return 0;
    ask.token = m_nextContainer++;
    logInfo(QStringLiteral("Click'n'Load from %1: %2 link(s)%3")
                .arg(ask.source.isEmpty() ? QStringLiteral("?") : ask.source)
                .arg(ask.plain.size())
                .arg(dummy.isEmpty() ? QString() : QStringLiteral(" and an encrypted list")));
    const quint64 token = ask.token;
    if (dummy.isEmpty()) {
        // nothing to decrypt: the answer still comes after the caller has the token
        QMetaObject::invokeMethod(this, [this, ask] {
            emit clickNLoadOpened(ask.token, ask.source, ask.plain, {}, {});
        }, Qt::QueuedConnection);
        return token;
    }
    m_cnlAsks[m_resolver->expand(dummy)] = std::move(ask);
    return token;
}

QString Ed3kHosterJoin::dummyCnlUrl(const ClickNLoad& post)
{
    if (post.crypted.trimmed().isEmpty() || (post.jk.trimmed().isEmpty() && post.k.trimmed().isEmpty()))
        return {};
    // what JD's ExternInterfaceImpl.createDummyCNL builds; DummyCNL runs jk in Rhino
    QJsonObject o{{QStringLiteral("crypted"), post.crypted.trimmed()}};
    if (!post.jk.trimmed().isEmpty())
        o.insert(QStringLiteral("jk"), post.jk);
    if (!post.k.trimmed().isEmpty())
        o.insert(QStringLiteral("k"), post.k.trimmed());
    if (!post.source.isEmpty())
        o.insert(QStringLiteral("source"), post.source);
    return QStringLiteral("https://dummycnl.jdownloader.org/#")
           + QString::fromLatin1(QJsonDocument(o).toJson(QJsonDocument::Compact).toHex());
}

bool Ed3kHosterJoin::checkLinks(const QStringList& urls)
{
    // §3: no hoster is asked anything without the user's switch; the ed3k node may be off
    // (not connected): the resolver then checks on its own (pumpDirectChecks)
    if (!m_prefs.enabled || !m_resolver || !m_resolver->available())
        return false;
    refreshAccounts();
    const qint64 now = nowMs();
    for (const QString& u : urls) {
        const QStringList l = linksIn(u);
        if (l.size() != 1)
            continue;
        const QString& url = l.first();
        // an item's link is the item's: its state says what there is to say
        if (const auto c = itemCheck(url)) {
            setCheck(url, *c);
            continue;
        }
        if (const auto it = m_checks.find(url); it != m_checks.end()) {
            const LinkCheck& c = it->second;
            if (c.status == LinkStatus::Checking && now - c.at < kProbeQueueMs + kProbeTimeoutMs)
                continue;   // running; its result follows
            // an unknown one is asked again: a wait, a busy resolver or a timeout passes
            if (c.status != LinkStatus::Checking && c.status != LinkStatus::Unknown && now - c.at < kCheckTtlMs) {
                emit linkChecked(url, c);
                continue;
            }
        }
        dropProbe(url);
        LinkCheck c;
        c.status = LinkStatus::Checking;
        c.at = now;
        m_checks[url] = c;
        m_probeMatches[m_resolver->match(url)] = url;
    }
    return true;
}

int Ed3kHosterJoin::accountTier(const QString& host) const
{
    const Ed3kHosterAccount* a = accountEntry(host);
    if (!a)
        return 0;
    const auto it = m_accountStatus.find(a->host + QLatin1Char('|') + a->user);
    if (it == m_accountStatus.end() || !it->second.known)
        return 1;
    const AccountStatus& st = it->second;
    if (!st.valid)
        return 0;
    const bool expired = st.expires > 0 && st.expires * 1000 < nowMs();
    return st.premium && !expired && st.trafficLeft != 0 ? 2 : 1;
}

QStringList Ed3kHosterJoin::rankMirrors(const QStringList& urls) const
{
    struct Ranked {
        int offline;
        int tier;
        int unchecked;
        qsizetype index;
    };
    QList<std::pair<Ranked, QString>> ranked;
    for (qsizetype i = 0; i < urls.size(); ++i) {
        const QString& url = urls[i];
        const auto it = m_checks.find(url);
        const LinkCheck* c = it == m_checks.end() ? nullptr : &it->second;
        // the plugin's host once checked: an account is filed under it, not under the URL's
        const QString host = c && !c->host.isEmpty() ? c->host : QUrl(url).host();
        ranked.append({Ranked{c && c->status == LinkStatus::Offline ? 1 : 0, accountTier(host),
                              c && c->status == LinkStatus::Online ? 0 : 1, i},
                       url});
    }
    std::ranges::sort(ranked, [](const auto& a, const auto& b) {
        return std::tuple(a.first.offline, -a.first.tier, a.first.unchecked, a.first.index)
             < std::tuple(b.first.offline, -b.first.tier, b.first.unchecked, b.first.index);
    });
    QStringList out;
    for (const auto& [r, url] : ranked)
        out << url;
    return out;
}

QList<Ed3kHosterJoin::Item> Ed3kHosterJoin::items() const
{
    QList<Item> out;
    for (const auto& [id, e] : m_items)
        out << e->item;
    return out;
}

std::optional<Ed3kHosterJoin::Item> Ed3kHosterJoin::item(quint32 id) const
{
    if (const Entry* e = find(id))
        return e->item;
    return std::nullopt;
}

QList<Ed3kHosterJoin::SharedLink> Ed3kHosterJoin::sharedLinks(quint32 id) const
{
    if (const Entry* e = find(id))
        return e->shared;
    return {};
}

// -- Ed3kJoinBase -------------------------------------------------------------

std::vector<std::uint64_t> Ed3kHosterJoin::wantedBlocks(const Download& d) const
{
    std::vector<std::uint64_t> out;
    const Entry* e = find(idOf(d.owner));
    if (!e)
        return out;
    for (std::uint64_t off = 0; off < d.size; off += kBlock)
        if (!e->have.contains(off) && !d.outstanding.contains(off))
            out.push_back(off);
    return out;
}

void Ed3kHosterJoin::onFileBlock(const Download& d, const ev::Block& b)
{
    Entry* e = find(idOf(d.owner));
    if (!e || !e->root || e->have.contains(b.offset))
        return;
    if (!writeAt(*e, b.offset, std::span{b.data}))
        return;
    e->have.insert(b.offset);
    e->item.done += b.data.size();
    touch(*e);
    count(&HosterCounters::peerBytes, b.data.size());
    checkComplete(*e);
}

void Ed3kHosterJoin::onBlobFetched(const QString&, int, const ffi::Root&, const QByteArray&)
{
    // no blobs: hoster items are files
}

std::optional<Ed3kJoinBase::FileRead> Ed3kHosterJoin::sharedRead(const Shared&, std::uint64_t, std::uint32_t) const
{
    // a complete file is in the incoming folder: eMule's shared files serve it
    return std::nullopt;
}

void Ed3kHosterJoin::onOtherEvent(const ffi::Event& event)
{
    if (const auto* ck = std::get_if<ev::LinkChecked>(&event)) {
        if (const auto p = m_probes.find(ck->link); p != m_probes.end() && p->second.pending)
            probeDone(ck->link, LinkStatus::Online, QString::fromStdString(ck->name), ck->size.value_or(0));
        // §4.1: name and size known, the rootless link is opened
        if (Entry* e = byLink(ck->link); e && !e->root && e->item.state == State::Checking) {
            if (!ck->name.empty())
                e->item.name = safeName(QString::fromStdString(ck->name), kNoName);
            if (ck->size)
                e->item.size = *ck->size;
            askOpen(*e);
        }
    } else if (const auto* b = std::get_if<ev::LinkBlock>(&event))
        onLinkBlock(b->link, b->root, b->offset, b->data);
    else if (const auto* u = std::get_if<ev::LinkUnverified>(&event))
        onLinkUnverified(u->link, u->offset, u->data);
    else if (const auto* rl = std::get_if<ev::LinkRootless>(&event))
        onLinkRootless(rl->link, rl->root, rl->size);
    else if (const auto* en = std::get_if<ev::LinkEnded>(&event)) {
        if (const auto p = m_probes.find(en->link); p != m_probes.end() && p->second.pending) {
            const bool dead = en->code == ED3K_HTTP_GONE || en->code == ED3K_HTTP_SIZE_MISMATCH
                           || en->code == ED3K_HTTP_CORRUPT || en->code == ED3K_HTTP_BAD_SOURCE;
            probeDone(en->link, dead ? LinkStatus::Offline : LinkStatus::Unknown);
        }
        onLinkEnded(en->link, en->code, en->bytes);
    }
    else if (const auto* c = std::get_if<ev::LinkClaim>(&event))
        onLinkClaim(c->link, c->root, c->holds);
    else if (const auto* v = std::get_if<ev::LinkVouched>(&event))
        onLinkVouched(v->link, v->root);
    else if (const auto* l = std::get_if<ev::Links>(&event))
        onPeerLinks(*l);
    else if (const auto* hr = std::get_if<ev::HttpResults>(&event))
        onHttpResults(*hr);
    else if (const auto* af = std::get_if<ev::AliasFound>(&event); af && m_searches.contains(af->search))
        onAliasFound(*af);
    else if (const auto* d = std::get_if<ev::SetLinksDead>(&event))
        logInfo(QStringLiteral("links of a set member died: %1").arg(QString::fromStdString(d->links)));
}

bool Ed3kHosterJoin::hasOwner(const QString& owner) const
{
    return find(idOf(owner)) != nullptr;
}

void Ed3kHosterJoin::saveOwner(const QString&)
{
    save();
}

Ed3kJoinBase::LogCategory Ed3kHosterJoin::logCategory() const
{
    return &lcEmuleHoster;
}

// -- private ------------------------------------------------------------------

Ed3kHosterJoin::Entry* Ed3kHosterJoin::find(quint32 id) const
{
    const auto it = m_items.find(id);
    return it == m_items.end() ? nullptr : it->second.get();
}

Ed3kHosterJoin::Entry* Ed3kHosterJoin::byLink(std::uint64_t link) const
{
    if (link == 0)
        return nullptr;
    for (const auto& [id, e] : m_items)
        if (e->link == link)
            return e.get();
    if (const auto it = m_peerLinks.find(link); it != m_peerLinks.end())
        return find(it->second.item);
    return nullptr;
}

Ed3kHosterJoin::Entry* Ed3kHosterJoin::byRoot(const ffi::Root& root) const
{
    for (const auto& [id, e] : m_items)
        if (e->root && *e->root == root)
            return e.get();
    return nullptr;
}

QString Ed3kHosterJoin::ownerOf(quint32 id)
{
    return QStringLiteral("h%1").arg(id);
}

quint32 Ed3kHosterJoin::idOf(const QString& owner)
{
    return owner.startsWith(QLatin1Char('h')) ? owner.mid(1).toUInt() : 0;
}

void Ed3kHosterJoin::setState(Entry& e, State s, Reason why, const QString& arg)
{
    if (e.item.state == s && e.item.reason == why && e.item.reasonArg == arg)
        return;
    if (e.item.state != s || e.item.reason != why) {
        switch (s) {
        case State::Complete:
            if (why != Reason::Folder) {   // a folder's links are items of their own
                count(&HosterCounters::completed);
                count(&HosterCounters::completedBytes, e.item.size);
                if (e.item.verified)
                    count(&HosterCounters::completedVerified);
            }
            break;
        case State::Failed: count(&HosterCounters::failed); break;
        case State::Gone: count(&HosterCounters::gone); break;
        case State::Waiting:
            if (why == Reason::HosterTimer || why == Reason::HosterQuota || why == Reason::HosterBusy)
                count(&HosterCounters::hosterWaits);
            else if (why == Reason::EndedEarly)
                count(&HosterCounters::retries);
            break;
        default: break;
        }
    }
    e.item.state = s;
    e.item.reason = why;
    e.item.reasonArg = arg;
    if (s != State::Waiting)
        e.item.waitUntil = 0;
    if (s != State::Downloading)
        e.item.speed = 0;
    // a links dialog waits on this item's check
    if (const auto c = m_checks.find(e.item.url); c != m_checks.end() && c->second.status == LinkStatus::Checking)
        if (const auto ic = itemCheck(e.item.url); ic && ic->status != LinkStatus::Checking)
            setCheck(e.item.url, *ic);
    changed(e);
    touch(e);
}

void Ed3kHosterJoin::changed(const Entry& e)
{
    emit itemChanged(e.item.id);
}

std::optional<ResolverAccount> Ed3kHosterJoin::accountFor(const QString& host) const
{
    if (const Ed3kHosterAccount* a = accountEntry(host))
        return ResolverAccount{a->user, a->password};
    return std::nullopt;
}

const Ed3kHosterAccount* Ed3kHosterJoin::accountEntry(const QString& host) const
{
    if (host.isEmpty())
        return nullptr;
    // same domain, a subdomain, or another domain of the same hoster (its plugin's aliases)
    const QString site = siteOf(host);
    for (const Ed3kHosterAccount& a : m_prefs.accounts)
        if (host == a.host || host.endsWith(QLatin1Char('.') + a.host) || (!a.host.isEmpty() && siteOf(a.host) == site))
            return &a;
    return nullptr;
}

void Ed3kHosterJoin::begin(Entry& e)
{
    // verified blocks stay: the item goes on under its root from where it stood
    const bool keep = e.root && !e.have.empty() && QFileInfo(e.item.file).isFile();
    release(e, true);
    e.peerTried.clear();
    e.hashType.clear();
    e.hash.clear();
    if (!keep) {
        e.have.clear();
        e.item.done = 0;
        e.item.verified = false;
        e.item.rootHex.clear();
        e.root.reset();
    }
    if (!m_resolver) {
        setState(e, State::NoResolver, Reason::NoResolverBuilt);
        return;
    }
    if (!m_resolver->available()) {
        m_resolver->wantPlugins();   // none shipped: fetched on the first link
        setState(e, State::NoResolver, Reason::ResolverNotReady, m_resolver->unavailableReason());
        return;
    }
    if (!m_session.engine()) {
        setState(e, State::Queued, Reason::WaitingForNode);
        return;
    }
    setState(e, State::Matching);
    m_matches[m_resolver->match(e.item.url)] = e.item.id;
}

void Ed3kHosterJoin::onMatched(Entry& e, const QString& host)
{
    if (host.isEmpty()) {
        setState(e, State::NoResolver, Reason::NoPlugin, e.item.host);
        return;
    }
    e.item.host = host;
    if (!m_prefs.enabled) {
        // §3: no puzzle, timer or account without the user's switch
        setState(e, State::NeedsUser, Reason::HostersOff);
        return;
    }
    // blocks held under a root: no lookup, the item goes on under it
    if (e.root && !e.have.empty()) {
        startRooted(e, *e.root, e.item.size, {}, std::nullopt);
        return;
    }
    lookUp(e);
}

void Ed3kHosterJoin::lookUp(Entry& e)
{
    auto* eng = m_session.engine();
    const auto id = ffi::linkId(e.item.url.toStdString());
    // no contacts: nobody to ask, the lookup would only time out
    if (!eng || !id || m_session.contactCount() == 0) {
        startRootless(e);
        return;
    }
    const auto search = eng->aliasSearchStart(m_session.now(), "url", std::span<const std::uint8_t>{id->data(), id->size()},
                                              ED3K_REL_SAME);
    if (!search) {
        startRootless(e);
        return;
    }
    e.search = *search;
    m_searches[*search] = e.item.id;
    e.nextAskAt = nowMs() + kLookupMs;
    setState(e, State::Looking);
}

void Ed3kHosterJoin::onAliasFound(const ev::AliasFound& found)
{
    const quint32 id = m_searches[found.search];
    m_searches.erase(found.search);
    auto* eng = m_session.engine();
    Entry* e = find(id);
    if (!e || !eng) {
        if (eng)
            (void)eng->aliasSearchEnd(found.search);
        return;
    }
    e->search.reset();
    std::optional<::ed3k::Engine::AliasCandidate> cand;
    if (found.roots > 0)
        if (auto next = eng->aliasSearchNext(found.search); next && *next)
            cand = **next;
    (void)eng->aliasSearchEnd(found.search);
    if (!cand) {
        startRootless(*e);
        return;
    }
    std::optional<ffi::Key32> publisher;
    if (!cand->publishers.empty())
        publisher = cand->publishers.front();
    startRooted(*e, cand->root, cand->size, QString::fromStdString(cand->name), publisher);
}

void Ed3kHosterJoin::startRooted(Entry& e, const ffi::Root& root, std::uint64_t size, const QString& name,
                                 const std::optional<ffi::Key32>& publisher)
{
    auto* eng = m_session.engine();
    if (!eng)
        return;
    ffi::HttpSource src;
    src.url = e.item.url.toStdString();
    const auto link = eng->linkAddUser(m_session.now(), root, size, src);
    if (!link) {
        logDebug([&]() -> QString { return QStringLiteral("link not added under its root (error %1)").arg(errorName(link.error())); });
        startRootless(e);
        return;
    }
    e.link = *link;
    e.root = root;
    e.item.size = size;
    e.item.verified = true;
    e.item.rootHex = hexOf(root.bytes);
    if (!name.isEmpty())
        e.item.name = safeName(name, kNoName);
    if (publisher)
        (void)eng->linkClaim(e.link, *publisher, true);
    if (e.item.file.isEmpty())
        e.item.file = partPathFor(e.key);
    QDir().mkpath(QFileInfo(e.item.file).path());
    if (e.have.empty()) {
        e.item.done = 0;
        QFile::remove(e.item.file);
    }
    touch(e);
    logInfo(QStringLiteral("%1 is known in ed3k, downloading from peers and the link").arg(e.item.host));
    openDownload(root, size, ownerOf(e.item.id), 0);
    askOpen(e);
}

void Ed3kHosterJoin::startRootless(Entry& e)
{
    auto* eng = m_session.engine();
    if (!eng)
        return;
    const auto link = eng->linkAddRootless(e.item.url.toStdString());
    if (!link) {
        setState(e, State::Failed, Reason::LinkRefused, errorName(link.error()));
        return;
    }
    e.link = *link;
    if (e.item.file.isEmpty())
        e.item.file = partPathFor(e.key);
    QDir().mkpath(QFileInfo(e.item.file).path());
    QFile::remove(e.item.file);   // a rootless link starts over: the core's hash is not kept
    touch(e);
    // the core gave back a link a links dialog checks: the item goes on from it
    if (const auto p = m_probes.find(*link); p != m_probes.end()) {
        if (p->second.pending) {
            // its check runs: the answer is the item's now
            p->second.adopted = true;
            for (auto& [task, a] : m_asks)
                if (a.link == *link) {
                    a.item = e.item.id;
                    e.task = task;
                }
            setState(e, State::Checking);
            return;
        }
        m_probes.erase(p);
    }
    // name and size first; open follows LinkChecked
    if (auto ok = eng->linkAsk(m_session.now(), e.link, ED3K_LINK_CHECK); !ok) {
        // dead in the core since a links dialog's check, which told it once: no second check
        if (const auto c = m_checks.find(e.item.url);
            ok.error() == ffi::Error::Denied && c != m_checks.end() && c->second.status == LinkStatus::Offline) {
            release(e, false);
            setState(e, State::Gone, Reason::Removed);
            return;
        }
        retryLater(e, 15, Reason::Busy);
        return;
    }
    setState(e, State::Checking);
}

void Ed3kHosterJoin::askOpen(Entry& e)
{
    auto* eng = m_session.engine();
    if (!eng || !e.link)
        return;
    if (activeStreams() >= int(m_prefs.maxStreams)) {
        e.nextAskAt = nowMs() + 5000;
        setState(e, State::Queued);
        return;
    }
    // a rootless link resumes where its hash stands (the core picks the offset); a rooted
    // one after the blocks held from 0
    if (auto ok = eng->linkAsk(m_session.now(), e.link, ED3K_LINK_OPEN, resumeOffset(e)); !ok) {
        if (ok.error() == ffi::Error::Busy) {
            // one stream per hoster: a peer's link elsewhere may go now
            if (tryPeerLink(e))
                return;
            e.nextAskAt = nowMs() + 15000;
            setState(e, State::Queued);
        } else if (ok.error() == ffi::Error::Denied) {
            setState(e, State::NeedsUser, Reason::HostersOff);
        } else {
            retryLater(e, 60, Reason::LinkRefused, errorName(ok.error()));
        }
        return;
    }
    setState(e, State::Downloading);
}

int Ed3kHosterJoin::activeStreams() const
{
    return int(std::ranges::count_if(m_asks, [](const auto& a) { return a.second.kind == ED3K_LINK_OPEN && !a.second.ended; }));
}

void Ed3kHosterJoin::onResolve(const ev::LinkResolve& r)
{
    Entry* e = byLink(r.link);
    const QString url = QString::fromStdString(r.url);
    if (!m_resolver || !m_resolver->available()) {
        answer(r.link, ED3K_RESOLVER_NO_RESOLVER);
        return;
    }
    // a link a links dialog checks: the check only, with the account of its hoster
    if (const auto p = m_probes.find(r.link); !e && p != m_probes.end()) {
        if (r.ask != ED3K_LINK_CHECK) {
            answer(r.link, ED3K_RESOLVER_FAILED);
            return;
        }
        Ask a;
        a.link = r.link;
        a.kind = r.ask;
        a.url = url;
        m_asks[m_resolver->check(url, accountFor(p->second.host))] = a;
        return;
    }
    // a link of a peer for a root that is not ours: nobody here writes its bytes
    if (!e) {
        answer(r.link, ED3K_RESOLVER_NO_RESOLVER);
        return;
    }
    // a peer's link: its own host, for the account
    const bool peer = r.link != e->link;
    const std::optional<ResolverAccount> account = accountFor(peer ? QUrl(url).host() : e->item.host);
    Ask a;
    a.link = r.link;
    a.item = e->item.id;
    a.kind = r.ask;
    a.url = url;
    Ed3kResolver::TaskId task = 0;
    switch (r.ask) {
    case ED3K_LINK_CHECK:
        task = m_resolver->check(url, account);
        break;
    case ED3K_LINK_EXPAND:
        task = m_resolver->expand(url);
        break;
    case ED3K_LINK_OPEN: {
        a.dir = QDir(workDir(e->item.id)).filePath(QStringLiteral("jd-%1").arg(r.link));
        QDir(a.dir).removeRecursively();
        QDir().mkpath(a.dir);
        // resume: r.offset bytes are here already (rootless: what the core hashed; rooted: the
        // blocks held from 0); JD goes on from its part file when the hoster lets it
        const auto pl = m_peerLinks.find(r.link);
        const bool fromStart = pl == m_peerLinks.end() || pl->second.offset == 0;
        if (r.offset >= kMinResume && fromStart && QFileInfo(e->item.file).size() >= qint64(r.offset)) {
            prefill(a, e->item.file, r.offset);
            return;
        }
        task = m_resolver->open(url, account, a.dir, 0);
        break;
    }
    default:
        answer(r.link, ED3K_RESOLVER_FAILED);
        return;
    }
    e->task = task;
    m_asks[task] = a;
}

void Ed3kHosterJoin::onResolverEvent(Ed3kResolver::TaskId task, const ResolverEvent& ev)
{
    using K = ResolverEvent::Kind;
    if (ev.kind == K::Puzzle && m_directChecks.contains(task)) {
        // a links dialog's check never asks the user: skipped, it ends as unknown
        m_resolver->answerPuzzle(ev.puzzle, {});
        return;
    }
    if (ev.kind == K::Puzzle) {
        quint32 id = 0;
        if (const auto it = m_asks.find(task); it != m_asks.end()) {
            id = it->second.item;
            // a links dialog's check never asks the user: skipped, it ends as unknown
            if (id == 0 && m_probes.contains(it->second.link)) {
                if (m_resolver)
                    m_resolver->answerPuzzle(ev.puzzle, {});
                return;
            }
        }
        Entry* e = find(id);
        const QString site = siteOf(!ev.host.isEmpty() ? ev.host : e ? e->item.host : QString());
        // the user skips this hoster's puzzles: not shown, ends as HosterSkipped
        if (isSkippedSite(site)) {
            if (m_resolver)
                m_resolver->answerPuzzle(ev.puzzle, {});
            return;
        }
        if (e)
            setState(*e, State::Puzzle);
        m_puzzles[ev.puzzle] = {id, site};
        count(&HosterCounters::puzzlesAsked);
        emit puzzleAsked(id, ev.puzzle, ev.image, ev.host, ev.text, ev.timeoutS);
        return;
    }
    if (ev.kind == K::Matched)
        learnSites(ev.sites);
    if (const auto it = m_siteQueries.find(task); it != m_siteQueries.end()) {
        const QString host = siteOf(it->second);
        m_siteQueries.erase(it);
        if (ev.kind == K::Sites && !ev.sites.isEmpty())
            learnSites(ev.sites);
        else if (ev.kind == K::Sites || ev.code != QLatin1String("unavailable"))
            m_siteOf.insert(host, host);   // no hoster plugin (or an old resolver): not asked again
        return;
    }
    if (m_directChecks.contains(task)) {
        if (ev.isFinal())
            directCheckDone(task, &ev);
        return;
    }
    if (const auto it = m_probeMatches.find(task); it != m_probeMatches.end()) {
        const QString url = it->second;
        m_probeMatches.erase(it);
        if (ev.kind == K::Matched && !ev.host.isEmpty()) {
            m_probeQueue.append({url, ev.host, nowMs()});
            pumpProbes();
        } else {
            setCheck(url, LinkCheck{LinkStatus::Unknown, {}, {}, 0, nowMs()});
        }
        return;
    }
    if (const auto it = m_matches.find(task); it != m_matches.end()) {
        const quint32 id = it->second;
        m_matches.erase(it);
        Entry* e = find(id);
        if (!e)
            return;
        if (ev.kind == K::Matched)
            onMatched(*e, ev.host);
        else
            setState(*e, State::NoResolver, Reason::ResolverNotReady, m_resolver ? m_resolver->unavailableReason() : QString());
        pumpSession();
        return;
    }
    if (const auto it = m_containerAsks.find(task); it != m_containerAsks.end()) {
        if (!ev.isFinal())
            return;
        const ContainerAsk c = it->second;
        m_containerAsks.erase(it);
        QFile::remove(c.path);
        if (ev.kind == K::Links && !ev.links.isEmpty()) {
            logInfo(QStringLiteral("container %1: %2 link(s)").arg(c.fileName).arg(ev.links.size()));
            emit containerOpened(c.token, c.fileName, ev.links, {}, {});
        } else {
            const QString code = ev.kind == K::Links ? QStringLiteral("invalid")
                                 : ev.code.isEmpty()  ? QStringLiteral("failed")
                                                      : ev.code;
            logWarning(QStringLiteral("container %1: %2 %3").arg(c.fileName, code, ev.text));
            emit containerOpened(c.token, c.fileName, {}, code, ev.text);
        }
        return;
    }
    if (const auto it = m_cnlAsks.find(task); it != m_cnlAsks.end()) {
        if (!ev.isFinal())
            return;
        const CnlAsk a = std::move(it->second);
        m_cnlAsks.erase(it);
        QList<ResolverLink> links = a.plain;
        if (ev.kind == K::Links) {
            for (ResolverLink l : ev.links) {
                if (l.package.isEmpty())
                    l.package = a.package;
                if (l.passwords.isEmpty())
                    l.passwords = a.passwords;
                links.append(l);
            }
        }
        if (ev.kind == K::Links && !ev.links.isEmpty()) {
            logInfo(QStringLiteral("Click'n'Load: %1 link(s) decrypted").arg(ev.links.size()));
            emit clickNLoadOpened(a.token, a.source, links, {}, {});
            return;
        }
        // nothing decrypted: a wrong key, or links no plugin takes
        const QString code = ev.kind == K::Links || ev.kind == K::Gone ? QStringLiteral("invalid")
                             : ev.code.isEmpty()                         ? QStringLiteral("failed")
                                                                         : ev.code;
        logWarning(QStringLiteral("Click'n'Load: %1 %2").arg(code, ev.text));
        emit clickNLoadOpened(a.token, a.source, links, links.isEmpty() ? code : QString(), ev.text);
        return;
    }
    if (const auto it = m_accountChecks.find(task); it != m_accountChecks.end()) {
        const auto [host, user] = it->second;
        m_accountChecks.erase(it);
        AccountStatus& st = m_accountStatus[host + QLatin1Char('|') + user];
        st = AccountStatus{};
        st.at = nowMs();
        if (ev.kind == K::Account) {
            st.known = true;
            st.valid = ev.valid;
            st.premium = ev.premium;
            st.expires = ev.expires;
            st.trafficLeft = ev.trafficLeft;
        }
        emit accountChecked(host, user, ev);
        return;
    }
    const auto it = m_asks.find(task);
    if (it == m_asks.end())
        return;
    Ask& a = it->second;
    Entry* e = find(a.item);
    const auto final = [this, task] { m_asks.erase(task); };
    // what the core is told when no stream is open
    const auto fail = [&](std::uint32_t kind, std::optional<std::uint64_t> value = std::nullopt) {
        if (a.opened)
            endStream(task, true);
        else
            answer(a.link, kind, {}, value);
    };

    switch (ev.kind) {
    case K::Found:
        if (e) {
            if (!ev.name.isEmpty() && !e->item.verified)
                e->item.name = safeName(ev.name, kNoName);
            if (ev.size && !e->item.verified)
                e->item.size = ev.size;
            if (!ev.hash.isEmpty() && a.link == e->link) {
                e->hashType = ev.hashType;
                e->hash = ev.hash;
            }
            changed(*e);
        }
        answer(a.link, ED3K_RESOLVER_CHECKED, ev.name, ev.size ? std::optional<std::uint64_t>(ev.size) : std::nullopt);
        final();
        break;
    case K::Folder:
        // a folder link: its links become items of their own once the user picks them
        if (!e && m_probes.contains(a.link))
            probeDone(a.link, LinkStatus::Online);
        answer(a.link, ED3K_RESOLVER_FAILED);
        if (e && m_resolver) {
            e->expandAfterCheck = true;
            const auto t = m_resolver->expand(e->item.url);
            Ask x;
            x.item = e->item.id;
            x.kind = ED3K_LINK_EXPAND;
            m_asks[t] = x;
        }
        final();
        break;
    case K::Links: {
        QStringList urls;
        for (const ResolverLink& l : ev.links)
            urls << l.url;
        if (a.link)
            answer(a.link, ED3K_RESOLVER_EXPANDED, urls.join(QLatin1Char('\n')));
        if (e) {
            count(&HosterCounters::foldersExpanded);
            emit linksExpanded(e->item.id, ev.links);
            setState(*e, State::Complete, Reason::Folder, QString::number(ev.links.size()));
        }
        final();
        break;
    }
    case K::Status:
        // the step before bytes flow, for the status column; a peer's link keeps its own reason
        if (e && e->task == task && e->item.reason != Reason::PeerLink
            && (e->item.state == State::Downloading
                || (e->item.state == State::Waiting && e->item.reason == Reason::HosterTimer)
                || (e->item.state == State::Puzzle && ev.text != QLatin1StringView("puzzle")))) {
            if (ev.text == QLatin1StringView("transfer"))
                setState(*e, State::Downloading);
            else
                setState(*e, State::Downloading, Reason::HosterStep, ev.text);
        }
        break;
    case K::Progress:
        a.onDisk = std::max(a.onDisk, ev.bytes);
        if (!ev.path.isEmpty())
            a.path = ev.path;
        if (e) {
            if (ev.total && !e->item.verified && !e->item.size)
                e->item.size = ev.total;
            if (e->item.state != State::Downloading && e->item.state != State::Puzzle)
                setState(*e, State::Downloading);
        }
        if (!opened(a, ev))
            break;
        feed(task);
        break;
    case K::Wait:
        if (ev.text == QLatin1StringView("timer")) {
            // JD waits out the hoster's timer itself
            if (e) {
                e->item.waitUntil = nowMs() + qint64(ev.seconds) * 1000;
                setState(*e, State::Waiting, Reason::HosterTimer);
                changed(*e);
            }
            break;
        }
        fail(ED3K_RESOLVER_WAIT, ev.seconds);
        // the core holds the link until then and tells nothing more
        if (!e && m_probes.contains(a.link))
            probeDone(a.link, LinkStatus::Unknown);
        if (e && a.link == e->peerLink) {
            e->peerLink = 0;
            e->peerTried.insert(a.link);
            if (!tryPeerLink(*e) && e->link)
                retryLater(*e, 30, Reason::EndedEarly);
        } else if (e) {
            retryLater(*e, int(std::min<std::uint64_t>(ev.seconds, 24 * 3600)),
                       ev.text == QLatin1StringView("quota") ? Reason::HosterQuota : Reason::HosterBusy);
            if (ownLinkStuck(*e))
                tryPeerLink(*e);
        }
        final();
        break;
    case K::Done:
        a.done = true;
        a.onDisk = ev.size;
        // the plugin may learn the checksum only while downloading
        if (e && !ev.hash.isEmpty() && a.link == e->link) {
            e->hashType = ev.hashType;
            e->hash = ev.hash;
        }
        if (!ev.path.isEmpty())
            a.path = ev.path;
        if (!opened(a, ev)) {
            // an old resolver that tells no start: the bytes are the whole file
            ResolverEvent whole = ev;
            whole.from = 0;
            (void)opened(a, whole);
        }
        feed(task);
        break;
    case K::Gone:
        fail(ED3K_RESOLVER_GONE);
        final();
        break;
    case K::NeedsUser:
        fail(ED3K_RESOLVER_NEEDS_USER);
        if (e) {
            Reason why = Reason::NeedsBrowserPuzzle;
            QString arg;
            if (ev.text == QLatin1StringView("login")) {
                why = Reason::NeedsLogin;
            } else if (ev.text == QLatin1StringView("skipped")) {
                arg = siteOf(e->item.host);
                why = isSkippedSite(arg) ? Reason::HosterSkipped : Reason::PuzzleSkipped;
                if (why == Reason::PuzzleSkipped)
                    arg.clear();
            } else if (ev.text == QLatin1StringView("wrong")) {
                why = Reason::PuzzleWrong;
            } else if (!m_prefs.askPuzzles) {
                why = Reason::PuzzlesOff;   // no solver asked: not a browser puzzle for sure
            }
            setState(*e, State::NeedsUser, why, arg);
        }
        final();
        break;
    case K::Failed:
        fail(ev.code == QLatin1StringView("noPlugin") ? ED3K_RESOLVER_NO_RESOLVER : ED3K_RESOLVER_FAILED);
        // the resolver's own checksum check failed: a rootless file's bytes are bad (a rooted
        // one's blocks were verified under the root, only the stream is retried)
        if (e && ev.code == QLatin1StringView("hash") && !e->root)
            dropBadFile(*e, ev.text);
        else if (e && ev.code != QLatin1StringView("noPlugin"))
            retryLater(*e, 60, Reason::ResolverError, ev.text.isEmpty() ? ev.code : ev.text);
        final();
        break;
    case K::Cancelled:
        fail(ED3K_RESOLVER_FAILED);
        final();
        break;
    default:
        break;
    }
    pumpSession();
}

void Ed3kHosterJoin::answer(std::uint64_t link, std::uint32_t kind, const QString& text, std::optional<std::uint64_t> value)
{
    auto* eng = m_session.engine();
    if (!eng || !link)
        return;
    if (auto ok = eng->linkAnswer(m_session.now(), link, kind, text.toStdString(), value); !ok)
        logDebug([&]() -> QString { return QStringLiteral("link answer %1 not taken (error %2)").arg(kind).arg(errorName(ok.error())); });
}

void Ed3kHosterJoin::prefill(Ask a, const QString& from, std::uint64_t offset)
{
    const std::uint64_t link = a.link;
    const QString to = QDir(a.dir).filePath(QStringLiteral("data.part"));
    a.have = offset;
    m_preparing[link] = std::move(a);
    QPointer<Ed3kHosterJoin> self(this);
    QThreadPool::globalInstance()->start([self, link, from, to, offset] {
        const bool ok = copyPrefix(from, to, offset);
        if (!ok)
            QFile::remove(to);
        QMetaObject::invokeMethod(
            self.data(), [self, link, ok] {
                if (self)
                    self->prefilled(link, ok);
            },
            Qt::QueuedConnection);
    });
}

void Ed3kHosterJoin::prefilled(std::uint64_t link, bool ok)
{
    auto node = m_preparing.extract(link);
    if (node.empty())
        return;   // the item let go of it meanwhile
    Ask a = std::move(node.mapped());
    if (!ok) {
        logDebug([&]() -> QString { return QStringLiteral("cannot copy %1 bytes to resume, the link starts over").arg(a.have); });
        a.have = 0;
    }
    openTask(std::move(a));
}

void Ed3kHosterJoin::openTask(Ask a)
{
    Entry* e = find(a.item);
    if (!e || !m_resolver || !m_resolver->available()) {
        QDir(a.dir).removeRecursively();
        answer(a.link, ED3K_RESOLVER_NO_RESOLVER);
        return;
    }
    const bool peer = a.link != e->link;
    const auto account = accountFor(peer ? QUrl(a.url).host() : e->item.host);
    const auto task = m_resolver->open(a.url, account, a.dir, a.have);
    e->task = task;
    m_asks[task] = std::move(a);
}

bool Ed3kHosterJoin::opened(Ask& a, const ResolverEvent& ev)
{
    if (a.opened)
        return true;
    if (a.have > 0) {
        // the resolver tells once whether the hoster resumed
        if (!ev.from)
            return false;
        const std::uint64_t from = *ev.from >= a.have ? a.have : 0;
        if (from == 0)
            if (Entry* e = find(a.item); e && !e->root)
                e->item.done = 0;   // rootless: hashed again from 0
        a.fed = from;
    } else if (a.onDisk == 0 && ev.kind != ResolverEvent::Kind::Done) {
        return false;
    }
    a.opened = true;
    answer(a.link, ED3K_RESOLVER_OPENED, {}, a.fed);
    return true;
}

std::uint64_t Ed3kHosterJoin::resumeOffset(const Entry& e) const
{
    if (!e.root || e.item.size == 0)
        return 0;   // rootless: the core picks where its hash stands
    std::uint64_t off = 0;
    while (off < e.item.size && e.have.contains(off))
        off += kBlock;
    return off >= e.item.size || off < kMinResume ? 0 : off;
}

void Ed3kHosterJoin::feed(Ed3kResolver::TaskId task)
{
    const auto it = m_asks.find(task);
    if (it == m_asks.end())
        return;
    Ask& a = it->second;
    if (a.reading || a.ended || !a.opened)
        return;
    if (a.fed >= a.onDisk) {
        if (a.done)
            endStream(task, false);
        return;
    }
    a.reading = true;
    const auto len = std::uint32_t(std::min<std::uint64_t>(kFeedChunk, a.onDisk - a.fed));
    readInBackground(ownerOf(a.item) + QStringLiteral("-feed"), {FileRead{a.path, a.fed, len}},
                     [this, task](ReadResults r) { fed(task, r.empty() ? std::nullopt : std::move(r.front())); });
}

void Ed3kHosterJoin::fed(Ed3kResolver::TaskId task, std::optional<QByteArray> data)
{
    const auto it = m_asks.find(task);
    if (it == m_asks.end())
        return;
    Ask& a = it->second;
    a.reading = false;
    if (a.ended)
        return;
    if (!data) {
        // JD renames data.part to data at the end, maybe while this read ran: read from
        // there; else the file is behind the progress it told, try again shortly
        const QString done = a.path.endsWith(QLatin1StringView(".part")) ? a.path.chopped(5) : QString();
        if (!done.isEmpty() && QFileInfo(done).isFile())
            a.path = done;
        if (++a.readFailures > kMaxReadFailures) {
            logWarning(QStringLiteral("cannot read the resolver's file %1").arg(a.path));
            endStream(task, true);
            return;
        }
        QTimer::singleShot(kReadRetryMs, this, [this, task] { feed(task); });
        return;
    }
    a.readFailures = 0;
    auto* eng = m_session.engine();
    if (!eng) {
        endStream(task, true);
        return;
    }
    if (auto ok = eng->linkData(m_session.now(), a.link, spanOf(*data)); !ok) {
        logDebug([&]() -> QString { return QStringLiteral("link data not taken (error %1)").arg(errorName(ok.error())); });
        a.ended = true;
        if (m_resolver)
            m_resolver->cancel(task);
        pumpSession();
        return;
    }
    a.fed += std::uint64_t(data->size());
    pumpSession();
    feed(task);
}

void Ed3kHosterJoin::endStream(Ed3kResolver::TaskId task, bool broken)
{
    const auto it = m_asks.find(task);
    if (it == m_asks.end())
        return;
    Ask& a = it->second;
    if (a.ended)
        return;
    a.ended = true;
    if (auto* eng = m_session.engine(); eng && a.opened)
        (void)eng->linkEnd(m_session.now(), a.link, broken);
    if (broken && m_resolver)
        m_resolver->cancel(task);
    QDir(a.dir).removeRecursively();
    m_asks.erase(it);
    pumpSession();
}

void Ed3kHosterJoin::onLinkBlock(std::uint64_t link, const ffi::Root& root, std::uint64_t offset, const ffi::Bytes& data)
{
    Entry* e = byLink(link);
    if (!e)
        e = byRoot(root);
    if (!e || !e->root || *e->root != root || e->have.contains(offset))
        return;
    if (!writeAt(*e, offset, std::span{data}))
        return;
    e->have.insert(offset);
    e->item.done += data.size();
    touch(*e);
    count(&HosterCounters::hosterBytes, data.size());
    checkComplete(*e);
}

void Ed3kHosterJoin::onLinkUnverified(std::uint64_t link, std::uint64_t offset, const ffi::Bytes& data)
{
    Entry* e = byLink(link);
    if (!e || e->root)
        return;
    if (!writeAt(*e, offset, std::span{data}))
        return;
    if (offset + data.size() > e->item.done) {
        count(&HosterCounters::hosterBytes, offset + data.size() - e->item.done);
        e->item.done = offset + data.size();
    }
}

void Ed3kHosterJoin::onLinkRootless(std::uint64_t link, const ffi::Root& root, std::uint64_t size)
{
    Entry* e = byLink(link);
    if (!e)
        return;
    e->root = root;
    e->item.rootHex = hexOf(root.bytes);
    e->item.size = size;
    e->item.done = size;
    logInfo(QStringLiteral("%1: downloaded whole, root %2").arg(e->item.host, e->item.rootHex.left(16)));
    // its Vouched follows once finish() let go of the link
    m_rootlessDone[link] = e->item.id;
    if (!checkHash(*e))
        finish(*e);
}

void Ed3kHosterJoin::onLinkEnded(std::uint64_t link, std::uint32_t code, std::uint64_t bytes)
{
    Entry* e = byLink(link);
    if (const auto it = m_peerLinks.find(link); it != m_peerLinks.end()) {
        // a peer's link (the core tells the peer how it went): the next one, else the own
        if (!e || e->item.state == State::Complete)
            return;
        if (e->peerLink == link)
            e->peerLink = 0;
        if (code != ED3K_HTTP_OK)
            e->peerTried.insert(link);
        logDebug([&]() -> QString { return QStringLiteral("%1: a peer's link ended, code %2, %3 bytes").arg(e->item.host).arg(code).arg(bytes); });
        if (tryPeerLink(*e))
            return;
        if (e->link)
            retryLater(*e, 30, Reason::EndedEarly);
        else if (e->item.state == State::Downloading)
            setState(*e, State::Gone, Reason::Removed);
        pumpSession();
        return;
    }
    // complete, or whole and its checksum checked (checkHash)
    if (!e || e->item.state == State::Complete || e->hashing)
        return;
    logDebug([&]() -> QString { return QStringLiteral("%1: link ended, code %2, %3 bytes").arg(e->item.host).arg(code).arg(bytes); });
    switch (code) {
    case ED3K_HTTP_OK:
        // the stream ended before the file was complete: open it again
        if (e->item.state != State::Complete)
            retryLater(*e, 30, Reason::EndedEarly);
        break;
    case ED3K_HTTP_GONE:
        // under a root a peer's link can stand in for the own one, which is dropped
        if (e->root && tryPeerLink(*e)) {
            if (auto* eng = m_session.engine())
                (void)eng->linkRemove(e->link);
            e->link = 0;
            break;
        }
        release(*e, false);
        setState(*e, State::Gone, Reason::Removed);
        break;
    case ED3K_HTTP_NO_RESOLVER:
        if (tryPeerLink(*e))
            break;
        setState(*e, State::NoResolver, Reason::NoPlugin, e->item.host);
        break;
    case ED3K_HTTP_NEEDS_USER:
        if (tryPeerLink(*e))
            break;
        if (e->item.state != State::NeedsUser)
            setState(*e, State::NeedsUser,
                     m_prefs.enabled ? Reason::Blocked : Reason::HostersOff);
        break;
    case ED3K_HTTP_CORRUPT:
    case ED3K_HTTP_SIZE_MISMATCH:
        release(*e, false);
        setState(*e, State::Failed,
                 code == ED3K_HTTP_CORRUPT ? Reason::NotTheFile : Reason::OtherSize);
        break;
    default:
        // a wait the resolver told (quota, busy) is the better time
        if (e->item.state != State::Waiting)
            retryLater(*e, 60, Reason::TransferFailed);
    }
    pumpSession();
}

void Ed3kHosterJoin::onLinkClaim(std::uint64_t link, const ffi::Root& root, bool holds)
{
    Entry* e = byLink(link);
    if (!e)
        return;
    if (holds) {
        logInfo(QStringLiteral("%1: the first block from the link holds under the root").arg(e->item.host));
        return;
    }
    // §4: a false claim; the core goes on with the link as one without a root
    logWarning(QStringLiteral("%1: the ALIAS record's root %2 is false").arg(e->item.host, hexOf(root.bytes).left(16)));
    stopDownload(root);
    e->root.reset();
    e->have.clear();
    e->item.done = 0;
    e->item.verified = false;
    e->item.rootHex.clear();
    QFile::remove(e->item.file);
    touch(*e);
    changed(*e);
}

void Ed3kHosterJoin::onLinkVouched(std::uint64_t link, const ffi::Root& root)
{
    Entry* e = byLink(link);
    // a rootless link vouches once the file is whole, after the item let go of the link
    if (const auto d = m_rootlessDone.find(link); !e && d != m_rootlessDone.end())
        e = find(d->second);
    m_rootlessDone.erase(link);
    if (!e)
        e = byRoot(root);
    if (!e)
        return;
    // nothing vouched before the hoster's checksum matched
    if (e->hashing) {
        e->heldVouch = std::pair{link, root};
        return;
    }
    vouch(*e, link, root);
}

void Ed3kHosterJoin::vouch(Entry& ent, std::uint64_t link, const ffi::Root& root)
{
    Entry* e = &ent;
    // §4.1: the own link, or a peer's blocks from it verified under the root (§4 publisher)
    const auto peer = m_peerLinks.find(link);
    const QString url = peer != m_peerLinks.end() ? peer->second.url : e->item.url;
    if (url.isEmpty() || std::ranges::any_of(e->shared, [&url](const SharedLink& l) { return l.url == url; }))
        return;
    SharedLink l;
    l.url = url;
    l.checkedAt = nowMs();   // the vouch is the freshest check there is
    // own link first
    if (peer == m_peerLinks.end())
        e->shared.prepend(l);
    else
        e->shared.append(l);
    if (!e->root)
        e->root = root;
    touch(*e);
    publishShared(*e);
}

void Ed3kHosterJoin::onPeerLinks(const ev::Links& links)
{
    Entry* e = byRoot(links.root);
    if (!e || !m_prefs.fromPeers || e->item.state == State::Complete)
        return;
    for (const auto& [id, src] : links.links)
        m_peerLinks[id] = PeerLink{e->item.id, src.offset, QString::fromStdString(src.url)};
    logDebug([&]() -> QString { return QStringLiteral("%1 hoster link(s) from peers for %2").arg(links.links.size()).arg(e->item.name); });
    if (ownLinkStuck(*e))
        tryPeerLink(*e);
}

bool Ed3kHosterJoin::ownLinkStuck(const Entry& e)
{
    switch (e.item.state) {
    case State::NeedsUser:
    case State::NoResolver:
        return e.item.reason != Reason::HostersOff;
    case State::Queued:
        return e.item.reason == Reason::None;   // one stream per hoster
    case State::Waiting:
        return e.item.waitUntil - nowMs() > kLongWaitMs;
    default:
        return false;
    }
}

bool Ed3kHosterJoin::mirrorDue(const Item& it)
{
    switch (it.state) {
    case State::Gone:
        return true;
    case State::NoResolver:
        return it.reason == Reason::NoPlugin;
    case State::Failed:
        // the local disk: another hoster does not help
        return it.reason != Reason::WriteFailed && it.reason != Reason::MoveFailed;
    case State::NeedsUser:
        return it.reason == Reason::NeedsLogin || it.reason == Reason::NeedsBrowserPuzzle
            || it.reason == Reason::Blocked || it.reason == Reason::PuzzleSkipped || it.reason == Reason::PuzzleWrong
            || it.reason == Reason::HosterSkipped || it.reason == Reason::PuzzlesOff;
    case State::Waiting:
        return it.waitUntil - nowMs() > kLongWaitMs;
    default:
        return false;
    }
}

void Ed3kHosterJoin::nextMirror(Entry& e)
{
    release(e, true);
    e.item.tried << e.item.url;
    e.item.url = e.item.mirrors.takeFirst();
    e.item.host = QUrl(e.item.url).host();
    ++e.item.mirrorNo;
    e.tries = 0;
    e.nextAskAt = 0;
    logInfo(QStringLiteral("trying mirror %1 at %2").arg(e.item.mirrorNo + 1).arg(e.item.host));
    begin(e);
    touch(e);
}

bool Ed3kHosterJoin::tryPeerLink(Entry& e)
{
    auto* eng = m_session.engine();
    // §3: only with the user's switch; verified under the root, so only for a rooted item
    if (!eng || !m_prefs.enabled || !m_prefs.fromPeers || !e.root || e.peerLink || !m_resolver || !m_resolver->available())
        return false;
    if (activeStreams() >= int(m_prefs.maxStreams))
        return false;
    for (const auto& [link, p] : m_peerLinks) {
        if (p.item != e.item.id || e.peerTried.contains(link))
            continue;
        const std::uint64_t offset = p.offset == 0 ? resumeOffset(e) : 0;
        if (auto ok = eng->linkAsk(m_session.now(), link, ED3K_LINK_OPEN, offset); !ok) {
            if (ok.error() != ffi::Error::Busy)
                e.peerTried.insert(link);
            continue;
        }
        e.peerLink = link;
        e.nextAskAt = 0;
        count(&HosterCounters::peerLinksUsed);
        logInfo(QStringLiteral("%1: trying a link a peer named (%2)").arg(e.item.host, QUrl(p.url).host()));
        setState(e, State::Downloading, Reason::PeerLink, QUrl(p.url).host());
        return true;
    }
    return false;
}

bool Ed3kHosterJoin::writeAt(Entry& e, std::uint64_t offset, std::span<const std::byte> data)
{
    QFile f(e.item.file);
    if (!f.open(QIODevice::ReadWrite) || !f.seek(qint64(offset))
        || f.write(reinterpret_cast<const char*>(data.data()), qint64(data.size())) != qint64(data.size())) {
        logWarning(QStringLiteral("cannot write %1: %2").arg(e.item.file, f.errorString()));
        release(e, true);
        setState(e, State::Failed, Reason::WriteFailed, f.errorString());
        return false;
    }
    return true;
}

void Ed3kHosterJoin::checkComplete(Entry& e)
{
    if (!e.root || e.item.size == 0)
        return;
    const std::uint64_t blocks = (e.item.size + kBlock - 1) / kBlock;
    if (e.have.size() >= blocks)
        finish(e);
    else
        changed(e);
}

void Ed3kHosterJoin::finish(Entry& e)
{
    if (e.item.state == State::Complete)
        return;
    const QString from = e.item.file;
    release(e, true);
    // the incoming folder (a package's own folder in it), with a name that is free; an
    // archive to be unpacked waits in Temp instead: only what it holds goes to incoming
    const bool archive = m_prefs.extract && Ed3kHosterUnpack::isArchive(e.item.name);
    const QString dir = archive ? archiveDirOf(e.item) : destDirOf(e.item);
    QDir().mkpath(dir);
    const QString to = freePath(dir, e.item.name);
    if (!QFile::rename(from, to) && !(QFile::copy(from, to) && QFile::remove(from))) {
        setState(e, State::Failed, Reason::MoveFailed, dir);
        return;
    }
    QDir(workDir(e.item.id)).removeRecursively();
    QFile::remove(metPathOf(e));
    e.item.file = to;
    e.item.done = e.item.size;
    logInfo(QStringLiteral("completed %1 from %2").arg(QFileInfo(to).fileName(), e.item.host));
    setState(e, State::Complete, archive ? Reason::UnpackWaiting : Reason::None);
    save();
    if (archive)
        queueUnpacks();
    else
        published({to});
}

bool Ed3kHosterJoin::checkHash(Entry& e)
{
    if (e.hash.isEmpty() || !digestKnown(e.hashType))
        return false;
    e.hashing = true;
    setState(e, State::Downloading, Reason::HashChecking, e.hashType);
    QPointer<Ed3kHosterJoin> self(this);
    const quint32 id = e.item.id;
    const std::uint64_t link = e.link;
    QThreadPool::globalInstance()->start([self, id, link, path = e.item.file, type = e.hashType] {
        const QString digest = fileDigest(path, type);
        QMetaObject::invokeMethod(
            self.data(), [self, id, link, digest] {
                if (self)
                    self->hashChecked(id, link, digest);
            },
            Qt::QueuedConnection);
    });
    return true;
}

void Ed3kHosterJoin::hashChecked(quint32 id, std::uint64_t link, const QString& digest)
{
    Entry* e = find(id);
    // paused, removed or restarted meanwhile: that run's check is void
    if (!e || !e->hashing || e->link != link)
        return;
    e->hashing = false;
    const auto held = std::exchange(e->heldVouch, std::nullopt);
    // CRCs are told with or without leading zeros
    const auto norm = [this, e](QString h) {
        h = h.trimmed().toLower();
        if (e->hashType.startsWith(u"crc"))
            h = h.rightJustified(8, QLatin1Char('0'));
        return h;
    };
    if (digest.isEmpty()) {
        // unreadable here: the move to incoming tells why
        logWarning(QStringLiteral("%1: cannot read %2 to check its %3").arg(e->item.host, e->item.name, e->hashType));
    } else if (norm(digest) != norm(e->hash)) {
        dropBadFile(*e, e->hashType);
        return;
    } else {
        logInfo(QStringLiteral("%1: %2 matches the hoster's %3").arg(e->item.host, e->item.name, e->hashType));
    }
    finish(*e);
    if (held && e->item.state == State::Complete)
        vouch(*e, held->first, held->second);
}

void Ed3kHosterJoin::dropBadFile(Entry& e, const QString& type)
{
    logWarning(QStringLiteral("%1: %2 does not match the hoster's %3, dropped").arg(e.item.host, e.item.name, type));
    std::erase_if(m_rootlessDone, [&e](const auto& d) { return d.second == e.item.id; });
    release(e, false);
    e.root.reset();
    e.have.clear();
    e.item.rootHex.clear();
    e.item.verified = false;
    e.item.done = 0;
    // a mirror follows (mirrorDue); Retry starts the link over
    setState(e, State::Failed, Reason::HashMismatch, type);
    touch(e);
}

void Ed3kHosterJoin::release(Entry& e, bool keepFile)
{
    auto* eng = m_session.engine();
    if (e.search) {
        if (eng)
            (void)eng->aliasSearchEnd(*e.search);
        m_searches.erase(*e.search);
        e.search.reset();
    }
    for (auto it = m_asks.begin(); it != m_asks.end();) {
        if (it->second.item != e.item.id) {
            ++it;
            continue;
        }
        if (it->second.opened && !it->second.ended && eng)
            (void)eng->linkEnd(m_session.now(), it->second.link, true);
        if (m_resolver)
            m_resolver->cancel(it->first);
        QDir(it->second.dir).removeRecursively();
        it = m_asks.erase(it);
    }
    for (auto it = m_preparing.begin(); it != m_preparing.end();) {
        if (it->second.item == e.item.id) {
            QDir(it->second.dir).removeRecursively();
            it = m_preparing.erase(it);
        } else {
            ++it;
        }
    }
    std::erase_if(m_matches, [&e](const auto& m) { return m.second == e.item.id; });
    std::erase_if(m_peerLinks, [&e](const auto& m) { return m.second.item == e.item.id; });
    e.peerLink = 0;
    e.hashing = false;
    e.heldVouch.reset();
    if (e.root)
        stopDownload(*e.root);
    if (const auto p = m_probes.find(e.link); e.link && p != m_probes.end()) {
        if (p->second.pending)
            setCheck(p->second.url, LinkCheck{LinkStatus::Unknown, p->second.host, {}, 0, nowMs()});
        m_probes.erase(p);
    }
    if (e.link && eng)
        (void)eng->linkRemove(e.link);
    e.link = 0;
    e.task = 0;
    if (!keepFile && e.item.state != State::Complete)
        QFile::remove(e.item.file);
}

void Ed3kHosterJoin::retryLater(Entry& e, int seconds, Reason why, const QString& arg)
{
    if (++e.tries > kMaxTries) {
        release(e, true);
        setState(e, State::Failed, why, arg);
        return;
    }
    e.nextAskAt = nowMs() + qint64(std::max(1, seconds)) * 1000;
    setState(e, State::Waiting, why, arg);
    e.item.waitUntil = e.nextAskAt;
}

void Ed3kHosterJoin::onTick()
{
    const qint64 now = nowMs();
    std::uint64_t totalSpeed = 0;
    for (auto& [id, ep] : m_items) {
        Entry& e = *ep;
        if (e.item.state == State::Downloading) {
            const std::uint64_t speed = e.item.done > e.lastDone ? (e.item.done - e.lastDone) * 1000 / kTickMs : 0;
            totalSpeed += speed;
            if (speed != e.item.speed) {
                e.item.speed = speed;
                changed(e);
            }
        }
        e.lastDone = e.item.done;
        // one place for every way a link fails or sticks, restarts included
        if (!e.item.mirrors.isEmpty() && mirrorDue(e.item)) {
            nextMirror(e);
            continue;
        }
        if (e.item.state == State::Looking && e.search && e.nextAskAt && e.nextAskAt <= now) {
            logDebug([&]() -> QString { return QStringLiteral("%1: lookup timed out, using the link without a root").arg(e.item.host); });
            if (auto* eng = m_session.engine())
                (void)eng->aliasSearchEnd(*e.search);
            m_searches.erase(*e.search);
            e.search.reset();
            e.nextAskAt = 0;
            startRootless(e);
            continue;
        }
        if ((e.item.state == State::Queued || e.item.state == State::Waiting) && e.nextAskAt && e.nextAskAt <= now) {
            e.nextAskAt = 0;
            // nothing open for the item: a link without one starts over
            if (!e.link)
                begin(e);
            else if (e.root || e.item.size)
                askOpen(e);
            else
                begin(e);
        }
    }
    if (m_counters && totalSpeed > 0) {
        m_counters->downloadTimeMs += kTickMs;
        raiseCounter(m_counters->maxDownRate, totalSpeed);
    }
    expireProbes(now);
    queueUnpacks();
    // ALIAS records whose root was not shared yet
    for (auto it = m_aliasPending.begin(); it != m_aliasPending.end();) {
        Entry* e = find(it->first);
        if (!e || now - it->second > kAliasWaitMs) {
            if (e)
                logDebug([&]() -> QString { return QStringLiteral("%1: link not published, the file was never shared").arg(e->item.host); });
            it = m_aliasPending.erase(it);
            continue;
        }
        const quint32 id = it->first;
        publishShared(*e);   // erases it once every ALIAS is out
        it = m_aliasPending.upper_bound(id);
    }
    queueRechecks(now);
    pumpProbes();
    pumpSession();
}

void Ed3kHosterJoin::pumpProbes()
{
    auto* eng = m_session.engine();
    if (m_probeQueue.isEmpty())
        return;
    if (!eng) {
        pumpDirectChecks();
        return;
    }
    const int maxAll = std::max(1, m_prefs.checkMaxParallel);
    const int maxHost = std::max(1, m_prefs.checkMaxPerHost);
    int running = 0;
    QHash<QString, int> perHost;
    for (const auto& [link, p] : m_probes)
        if (p.pending && !p.adopted) {
            ++running;
            ++perHost[siteOf(p.host)];
        }
    for (const auto& [task, d] : m_directChecks) {
        ++running;
        ++perHost[siteOf(d.host)];
    }
    for (auto it = m_probeQueue.begin(); it != m_probeQueue.end() && running < maxAll;) {
        const QueuedProbe q = *it;
        const QString site = siteOf(q.host);
        if (perHost.value(site) >= maxHost) {
            ++it;
            continue;
        }
        const LinkCheck unknown{LinkStatus::Unknown, q.host, {}, 0, nowMs()};
        // an item took the link meanwhile: it says (a recheck asks the hoster all the same)
        if (const auto c = q.item ? std::nullopt : itemCheck(q.url)) {
            setCheck(q.url, *c);
            it = m_probeQueue.erase(it);
            continue;
        }
        const auto link = eng->linkAddRootless(q.url.toStdString());
        if (!link) {
            if (link.error() == ffi::Error::Busy) {
                ++it;
                continue;
            }
            setCheck(q.url, unknown);
            if (q.item)
                onRechecked(q.item, q.url, LinkStatus::Unknown);
            it = m_probeQueue.erase(it);
            continue;
        }
        if (auto ok = eng->linkAsk(m_session.now(), *link, ED3K_LINK_CHECK); !ok && ok.error() != ffi::Error::Denied) {
            if (ok.error() == ffi::Error::Busy) {
                // a hoster wait or too many failures there: the core says when
                ++it;
                continue;
            }
            setCheck(q.url, unknown);
            if (q.item)
                onRechecked(q.item, q.url, LinkStatus::Unknown);
            it = m_probeQueue.erase(it);
            continue;
        }
        // Denied: dead or not allowed; LinkEnded tells which
        m_probes[*link] = Probe{q.url, q.host, nowMs(), true, false, q.item};
        ++running;
        ++perHost[site];
        it = m_probeQueue.erase(it);
    }
    pumpSession();
}

void Ed3kHosterJoin::probeDone(std::uint64_t link, LinkStatus status, const QString& name, std::uint64_t size)
{
    const auto p = m_probes.find(link);
    if (p == m_probes.end() || !p->second.pending)
        return;
    p->second.pending = false;
    p->second.at = nowMs();
    setCheck(p->second.url, LinkCheck{status, p->second.host, name, size, p->second.at});
    if (p->second.item)
        onRechecked(p->second.item, p->second.url, status);
    // an item took it while it ran: the link is the item's from now on
    if (p->second.adopted)
        m_probes.erase(p);
}

void Ed3kHosterJoin::pumpDirectChecks()
{
    if (!m_resolver || !m_resolver->available())
        return;
    const int maxAll = std::max(1, m_prefs.checkMaxParallel);
    const int maxHost = std::max(1, m_prefs.checkMaxPerHost);
    int running = int(m_directChecks.size());
    QHash<QString, int> perHost;
    for (const auto& [task, d] : m_directChecks)
        ++perHost[siteOf(d.host)];
    for (auto it = m_probeQueue.begin(); it != m_probeQueue.end() && running < maxAll;) {
        const QueuedProbe q = *it;
        const QString site = siteOf(q.host);
        if (perHost.value(site) >= maxHost) {
            ++it;
            continue;
        }
        it = m_probeQueue.erase(it);
        // an item took the link meanwhile: it says (a recheck asks the hoster all the same)
        if (const auto c = q.item ? std::nullopt : itemCheck(q.url)) {
            setCheck(q.url, *c);
            continue;
        }
        const Ed3kResolver::TaskId task = m_resolver->check(q.url, accountFor(q.host));
        m_directChecks[task] = DirectCheck{q.url, q.host};
        ++running;
        ++perHost[site];
        // no tick runs without the node: each check times out on its own
        QTimer::singleShot(kProbeTimeoutMs, this, [this, task] {
            if (m_directChecks.contains(task))
                directCheckDone(task, nullptr);
        });
    }
}

void Ed3kHosterJoin::directCheckDone(Ed3kResolver::TaskId task, const ResolverEvent* ev)
{
    const auto it = m_directChecks.find(task);
    if (it == m_directChecks.end())
        return;
    const DirectCheck d = it->second;
    m_directChecks.erase(it);
    using K = ResolverEvent::Kind;
    LinkCheck c{LinkStatus::Unknown, d.host, {}, 0, nowMs()};
    if (!ev) {
        if (m_resolver)
            m_resolver->cancel(task);
    } else if (ev->kind == K::Found || ev->kind == K::Folder) {
        c.status = LinkStatus::Online;
        c.name = ev->name;
        c.size = ev->size;
    } else if (ev->kind == K::Gone) {
        c.status = LinkStatus::Offline;
    }
    setCheck(d.url, c);
    pumpProbes();
}

void Ed3kHosterJoin::setCheck(const QString& url, const LinkCheck& c)
{
    if (m_checks.size() >= kMaxChecks && !m_checks.contains(url)) {
        const qint64 now = nowMs();
        std::erase_if(m_checks, [now](const auto& x) {
            return x.second.status != LinkStatus::Checking && now - x.second.at >= kCheckTtlMs;
        });
        if (m_checks.size() >= kMaxChecks)
            m_checks.erase(std::ranges::min_element(m_checks, {}, [](const auto& x) { return x.second.at; }));
    }
    m_checks[url] = c;
    if (c.status != LinkStatus::Checking)
        emit linkChecked(url, c);
}

std::optional<Ed3kHosterJoin::LinkCheck> Ed3kHosterJoin::itemCheck(const QString& url) const
{
    for (const auto& [id, e] : m_items) {
        const Item& it = e->item;
        if (it.url != url || it.state == State::Failed)
            continue;
        LinkCheck c;
        c.host = it.host;
        c.name = it.name;
        c.size = it.size;
        c.at = nowMs();
        switch (it.state) {
        case State::Complete:
            c.status = it.reason == Reason::LinkOffline ? LinkStatus::Offline : LinkStatus::Online;
            break;
        case State::Downloading:
            c.status = LinkStatus::Online;
            break;
        case State::Gone:
            c.status = LinkStatus::Offline;
            break;
        case State::Matching:
        case State::Looking:
        case State::Checking:
            c.status = LinkStatus::Checking;   // setState() hands on the item's answer
            break;
        case State::NoResolver:
            c.status = LinkStatus::Unknown;
            break;
        default:
            // the hoster gave a size once: checked online
            if (it.size > 0)
                c.status = LinkStatus::Online;
            else if (!e->link)
                return std::nullopt;   // no core link held: a check of its own is free to run
            else
                c.status = LinkStatus::Unknown;
        }
        return c;
    }
    return std::nullopt;
}

void Ed3kHosterJoin::expireProbes(qint64 now)
{
    auto* eng = m_session.engine();
    for (auto it = m_probes.begin(); it != m_probes.end();) {
        Probe& p = it->second;
        const std::uint64_t link = it->first;
        if (p.pending && now - p.at > kProbeTimeoutMs) {
            setCheck(p.url, LinkCheck{LinkStatus::Unknown, p.host, {}, 0, now});
            p.pending = false;
            if (p.item)
                onRechecked(p.item, p.url, LinkStatus::Unknown);
            if (!p.adopted) {
                // the core still waits for the answer: the link goes, a later one starts clean
                for (auto a = m_asks.begin(); a != m_asks.end();) {
                    if (a->second.link == link && a->second.item == 0) {
                        if (m_resolver)
                            m_resolver->cancel(a->first);
                        a = m_asks.erase(a);
                    } else {
                        ++a;
                    }
                }
                if (eng)
                    (void)eng->linkRemove(link);
                it = m_probes.erase(it);
                continue;
            }
        }
        // the item's now, or kept long enough for a download to start from it
        if (!p.pending && (p.adopted || byLink(link))) {
            it = m_probes.erase(it);
            continue;
        }
        if (!p.pending && now - p.at > kCheckTtlMs) {
            if (eng)
                (void)eng->linkRemove(link);
            it = m_probes.erase(it);
            continue;
        }
        ++it;
    }
    for (auto it = m_probeQueue.begin(); it != m_probeQueue.end();) {
        if (now - it->since > kProbeQueueMs) {
            setCheck(it->url, LinkCheck{LinkStatus::Unknown, it->host, {}, 0, now});
            const quint32 item = it->item;
            const QString url = it->url;
            it = m_probeQueue.erase(it);
            if (item)
                onRechecked(item, url, LinkStatus::Unknown);
        } else {
            ++it;
        }
    }
}

void Ed3kHosterJoin::refreshAccounts()
{
    if (!m_prefs.enabled || !m_resolver || !m_resolver->available())
        return;
    const qint64 now = nowMs();
    for (const Ed3kHosterAccount& a : m_prefs.accounts) {
        if (a.host.isEmpty())
            continue;
        // the hoster's other domains, so a link on any of them finds the login
        if (const QString host = siteOf(a.host); !m_siteOf.contains(host)
            && !std::ranges::any_of(m_siteQueries, [&host, this](const auto& q) { return siteOf(q.second) == host; }))
            m_siteQueries[m_resolver->sites(a.host)] = a.host;
        if (a.password.isEmpty())
            continue;
        if (const auto it = m_accountStatus.find(a.host + QLatin1Char('|') + a.user);
            it != m_accountStatus.end() && now - it->second.at < kAccountTtlMs)
            continue;
        const std::pair<QString, QString> key{a.host, a.user};
        if (std::ranges::any_of(m_accountChecks, [&key](const auto& c) { return c.second == key; }))
            continue;
        m_accountChecks[m_resolver->accountCheck(a.host, ResolverAccount{a.user, a.password})] = key;
    }
}

QString Ed3kHosterJoin::siteOf(const QString& host) const
{
    QString h = host.trimmed().toLower();
    if (h.startsWith(QLatin1String("www.")))
        h.remove(0, 4);
    // a subdomain belongs to its parent's hoster
    for (QStringView d = h; d.contains(QLatin1Char('.')); d = d.sliced(d.indexOf(QLatin1Char('.')) + 1))
        if (const auto it = m_siteOf.constFind(d.toString()); it != m_siteOf.cend())
            return *it;
    return h;
}

bool Ed3kHosterJoin::isSkippedSite(const QString& site) const
{
    // a skip stored before the hoster's other domains were known still matches (JD: by plugin host)
    return !site.isEmpty()
        && (m_skipSites.contains(site)
            || std::ranges::any_of(m_skipSites, [&site, this](const QString& s) { return siteOf(s) == site; }));
}

void Ed3kHosterJoin::learnSites(const QStringList& sites)
{
    if (sites.isEmpty())
        return;
    const QString main = sites.first().toLower();
    for (const QString& d : sites)
        m_siteOf.insert(d.toLower(), main);
}

void Ed3kHosterJoin::publishShared(Entry& e)
{
    auto* eng = m_session.engine();
    if (!eng || !e.root)
        return;
    const qint64 now = nowMs();
    const auto t = m_session.now();
    QStringList urls;
    bool waiting = false;
    int aliases = 0;
    for (SharedLink& l : e.shared) {
        // §4.1: nothing while links are held back
        const bool live = m_prefs.publishLinks && linkLive(l, now);
        if (live && urls.size() < kMaxShareLinks)
            urls << l.url;
        const bool wantAlias = live && aliases < kMaxAliasLinks;
        if (wantAlias)
            ++aliases;
        if (wantAlias == l.aliasOut)
            continue;
        const auto alias = ffi::linkAliasSame(l.url.toStdString(), *e.root, e.item.size, e.item.name.toStdString());
        if (!alias) {
            logDebug([&]() -> QString { return QStringLiteral("%1: no ALIAS record of the link (error %2)").arg(e.item.host, errorName(alias.error())); });
            continue;
        }
        const std::vector<ffi::Bytes> list{*alias};
        const QString host = QUrl(l.url).host();
        if (wantAlias) {
            if (auto ok = eng->publishAlias(t, list); ok) {
                l.aliasOut = true;
                logInfo(QStringLiteral("%1: link published (ALIAS url)").arg(host));
            } else if (ok.error() == ffi::Error::State) {
                waiting = true;   // the file is not shared yet: the tick tries again
            } else {
                logDebug([&]() -> QString { return QStringLiteral("%1: link not published (error %2)").arg(host, errorName(ok.error())); });
            }
        } else {
            (void)eng->unpublishAlias(t, list);
            l.aliasOut = false;
            // Kad has no delete: the copies stored run out within 24 h
            logInfo(QStringLiteral("%1: link withdrawn (ALIAS url)").arg(host));
        }
    }
    if (waiting)
        m_aliasPending.try_emplace(e.item.id, now);
    else
        m_aliasPending.erase(e.item.id);
    if (urls == e.sharedOut)
        return;
    std::vector<std::string> list;
    for (const QString& u : std::as_const(urls))
        list.push_back(u.toStdString());
    if (auto ok = eng->linkShare(*e.root, list); !ok) {
        logDebug([&]() -> QString { return QStringLiteral("%1: links not shared (error %2)").arg(e.item.host, errorName(ok.error())); });
        return;
    }
    e.sharedOut = urls;
    if (!urls.isEmpty())
        logInfo(QStringLiteral("%1: %2 link(s) handed out with the file (SOURCE record, downloaders)").arg(e.item.host).arg(urls.size()));
    else
        logInfo(QStringLiteral("%1: links no longer handed out with the file").arg(e.item.host));
}

bool Ed3kHosterJoin::linkLive(const SharedLink& l, qint64 now) const
{
    if (l.offline)
        return false;
    const qint64 every = recheckMs();
    return every == 0 || l.unknown || now - l.checkedAt < every;
}

qint64 Ed3kHosterJoin::recheckMs() const
{
    return qint64(std::max(0, m_prefs.recheckHours)) * 3600 * 1000;
}

void Ed3kHosterJoin::queueRechecks(qint64 now)
{
    const qint64 every = recheckMs();
    if (!m_started)
        return;
    // §3: no hoster is asked anything without the user's switch; a link not checked stays as it is
    const bool canCheck = m_prefs.enabled && m_resolver && m_resolver->available() && m_session.engine();
    bool queued = false;
    for (auto& [id, ep] : m_items) {
        Entry& e = *ep;
        if (e.item.state != State::Complete || !e.root)
            continue;
        for (SharedLink& l : e.shared) {
            if (l.checking)
                continue;
            const bool reported = reportDue(l, now);
            if (!reported && (every == 0 || now - l.checkedAt < every || now < l.retryAt))
                continue;
            if (reported) {
                // checked now whether or not anyone can: the reports wait for the next turn
                l.reporters.clear();
                l.reportCheckAt = now;
                touch(e);
            }
            if (!canCheck) {
                // stale and nobody to ask: kept as unknown, asked again later
                if (!l.unknown) {
                    l.unknown = true;
                    publishShared(e);
                }
                l.retryAt = now + (every == 0 ? kRecheckRetryMs : std::min(kRecheckRetryMs, every));
                continue;
            }
            l.checking = true;
            l.reportCheck = reported;
            const QString host = l.url == e.item.url ? e.item.host : QUrl(l.url).host();
            if (reported)
                logInfo(QStringLiteral("%1: downloaders in %2 sites reported a published link dead, checking it").arg(host).arg(kReportsUntilCheck));
            else
                logDebug([&]() -> QString { return QStringLiteral("%1: rechecking a published link").arg(host); });
            m_probeQueue.append({l.url, host, now, e.item.id});
            queued = true;
        }
    }
    if (queued)
        pumpProbes();
}

void Ed3kHosterJoin::onRechecked(quint32 item, const QString& url, LinkStatus status)
{
    Entry* e = find(item);
    if (!e)
        return;
    const auto l = std::ranges::find(e->shared, url, &SharedLink::url);
    if (l == e->shared.end())
        return;
    const qint64 now = nowMs();
    l->checking = false;
    const bool byReports = std::exchange(l->reportCheck, false);
    const QString host = QUrl(url).host();
    switch (status) {
    case LinkStatus::Online:
        if (l->offline)
            logInfo(QStringLiteral("%1: link online again, published").arg(host));
        else if (byReports)
            logInfo(QStringLiteral("%1: reported link is online, kept").arg(host));
        // the core stopped handing it out after the reports: its count starts over
        if (byReports && !e->sharedOut.isEmpty() && e->root && m_session.engine()) {
            (void)m_session.engine()->linkShare(*e->root, {});
            e->sharedOut.clear();
        }
        l->checkedAt = now;
        l->offline = false;
        l->unknown = false;
        break;
    case LinkStatus::Offline:
        if (!l->offline)
            logInfo(QStringLiteral("%1: link offline, withdrawn").arg(host));
        l->checkedAt = now;
        l->offline = true;
        l->unknown = false;
        break;
    default:
        // not proven dead: kept, asked again later
        l->unknown = true;
        l->retryAt = now + (recheckMs() == 0 ? kRecheckRetryMs : std::min(kRecheckRetryMs, recheckMs()));
        logDebug([&]() -> QString { return QStringLiteral("%1: recheck of a published link could not tell").arg(host); });
        break;
    }
    // reports count from the last answer on
    if (status == LinkStatus::Online || status == LinkStatus::Offline)
        l->reporters.clear();
    // the item's own link says what the list shows
    if (url == e->item.url && e->item.state == State::Complete) {
        const Reason r = l->offline ? Reason::LinkOffline : Reason::None;
        if (e->item.reason != r && (e->item.reason == Reason::None || e->item.reason == Reason::LinkOffline)) {
            e->item.reason = r;   // not setState: Complete counted once
            changed(*e);
        }
    }
    touch(*e);
    publishShared(*e);
}

void Ed3kHosterJoin::onHttpResults(const ev::HttpResults& r)
{
    // no site (a virtual connection): the core does not count it either
    if (!m_started || m_prefs.reportRecheckHours <= 0 || std::ranges::all_of(r.site, [](std::uint8_t b) { return b == 0; }))
        return;
    // records of 20 bytes: url_id 8, code u32 LE, bytes u64 LE
    constexpr std::size_t kRecord = 20;
    std::vector<std::array<std::uint8_t, 8>> dead;
    for (std::size_t at = 0; at + kRecord <= r.results.size(); at += kRecord) {
        std::uint32_t code = 0;
        for (int i = 3; i >= 0; --i)
            code = (code << 8) | std::to_integer<std::uint8_t>(r.results[at + 8 + std::size_t(i)]);
        // hoster.md §2: gone or wrong counts; a failed fetch says nothing about the link
        if (code != ED3K_HTTP_GONE && code != ED3K_HTTP_SIZE_MISMATCH && code != ED3K_HTTP_CORRUPT)
            continue;
        std::array<std::uint8_t, 8> id{};
        for (std::size_t i = 0; i < id.size(); ++i)
            id[i] = std::to_integer<std::uint8_t>(r.results[at + i]);
        dead.push_back(id);
    }
    if (dead.empty())
        return;
    const qint64 now = nowMs();
    bool due = false;
    for (auto& [id, ep] : m_items) {
        Entry& e = *ep;
        if (e.item.state != State::Complete || !e.root || *e.root != r.root)
            continue;
        for (SharedLink& l : e.shared) {
            // only links handed out count: a withdrawn one is not this node's to answer for
            if (l.offline || !e.sharedOut.contains(l.url))
                continue;
            if (std::ranges::find(dead, ffi::linkUrlId(l.url.toStdString())) == dead.end())
                continue;
            if (std::ranges::find(l.reporters, r.site) == l.reporters.end() && l.reporters.size() < 16)
                l.reporters.push_back(r.site);
            logDebug([&]() -> QString { return QStringLiteral("%1: a downloader reported a published link dead (%2)")
                         .arg(QUrl(l.url).host()).arg(l.reporters.size()); });
            due = due || reportDue(l, now);
        }
    }
    if (due)
        queueRechecks(now);
}

bool Ed3kHosterJoin::reportDue(const SharedLink& l, qint64 now) const
{
    const qint64 floor = qint64(std::max(0, m_prefs.reportRecheckHours)) * 3600 * 1000;
    if (floor == 0 || l.offline || l.reporters.size() < kReportsUntilCheck)
        return false;
    // at most once a floor, counted from any check of the link
    return now - std::max(l.checkedAt, l.reportCheckAt) >= floor;
}

void Ed3kHosterJoin::load()
{
    // complete items; an unfinished one is in its .met in Temp (scanTempDirs)
    QFile f(itemsFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray list = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : list) {
        const QJsonObject o = v.toObject();
        auto e = std::make_unique<Entry>();
        fromJson(o, *e);
        Item& it = e->item;
        it.id = quint32(o.value(QStringLiteral("id")).toInteger());
        if (it.id == 0 || it.url.isEmpty() || it.state != State::Complete || m_items.contains(it.id))
            continue;
        it.file = o.value(QStringLiteral("file")).toString();
        it.packageId = it.package.isEmpty() ? 0 : quint32(o.value(QStringLiteral("pkg")).toInteger());
        if (it.packageId == 0)
            it.package.clear();
        m_nextPackageId = std::max(m_nextPackageId, it.packageId + 1);
        // before the links list: the item's own link, vouched for now
        if (e->shared.isEmpty() && o.value(QStringLiteral("vouched")).toBool())
            e->shared << SharedLink{it.url, nowMs()};
        if (it.reason == Reason::Unpacking) {   // cut short by a quit: again
            it.reason = Reason::UnpackWaiting;
            it.reasonArg.clear();
        }
        e->have.clear();
        it.done = it.size;
        e->root = rootFromHex(it.rootHex);
        it.verified = e->root.has_value();
        m_nextId = std::max(m_nextId, it.id + 1);
        m_items.emplace(it.id, std::move(e));
    }
}

void Ed3kHosterJoin::save()
{
    QJsonArray list;
    for (const auto& [id, e] : m_items) {
        const Item& it = e->item;
        if (it.state != State::Complete)
            continue;
        QJsonObject o = toJson(*e);
        o.insert(QStringLiteral("id"), qint64(it.id));
        o.insert(QStringLiteral("file"), it.file);
        if (it.packageId != 0)
            o.insert(QStringLiteral("pkg"), qint64(it.packageId));
        list.append(o);
    }
    // none kept and no file yet: create nothing under config/ed3k (#105)
    if (!list.isEmpty() || QFileInfo::exists(itemsFile())) {
        QDir().mkpath(QFileInfo(itemsFile()).absolutePath());
        QSaveFile f(itemsFile());
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(list).toJson(QJsonDocument::Indented));
            f.commit();
        }
    }
    for (auto& [id, e] : m_items)
        if (e->metDirty && e->item.state != State::Complete)
            writeMet(*e);
}

QString Ed3kHosterJoin::itemsFile() const
{
    return QDir(joinDir(QStringLiteral("hoster"))).filePath(QStringLiteral("items.json"));
}

QString Ed3kHosterJoin::workDir(quint32 id) const
{
    return QDir(m_tempDirs.isEmpty() ? joinDir(QStringLiteral("hoster")) : m_tempDirs.first())
        .filePath(QStringLiteral("ed3k-hoster/%1").arg(id));
}

void Ed3kHosterJoin::count(uint64 HosterCounters::* field, uint64 n)
{
    if (m_counters)
        m_counters->*field += n;
}

void Ed3kHosterJoin::dropProbe(const QString& url)
{
    // an ended check's core link, so the next one starts clean (an item's link stays)
    auto* eng = m_session.engine();
    for (auto it = m_probes.begin(); it != m_probes.end();) {
        if (it->second.url == url && !it->second.pending && !it->second.adopted && !byLink(it->first)) {
            if (eng)
                (void)eng->linkRemove(it->first);
            it = m_probes.erase(it);
        } else {
            ++it;
        }
    }
}

void Ed3kHosterJoin::touch(Entry& e)
{
    e.metDirty = true;
    scheduleSave(ownerOf(e.item.id));
}

void Ed3kHosterJoin::scanTempDirs()
{
    for (const QString& metPath : metFilesIn(m_tempDirs, kMetSuffix)) {
        const QString key = QFileInfo(metPath).fileName().chopped(kMetSuffix.size());
        if (std::ranges::any_of(m_items, [&key](const auto& p) { return p.second->key == key; }))
            continue;
        auto e = readMet(metPath);
        if (!e) {
            logWarning(QStringLiteral("%1 does not parse: left alone").arg(metPath));
            continue;
        }
        e->key = key;
        Item& it = e->item;
        if (const auto same = std::ranges::find_if(m_items, [&it](const auto& p) {
                return p.second->item.state != State::Failed && p.second->item.url == it.url;
            }); same != m_items.end()) {
            logWarning(QStringLiteral("%1: the link is an item already (%2)").arg(metPath, same->second->item.file));
            continue;
        }
        if (m_items.size() >= kMaxItems)
            break;
        it.id = m_nextId++;
        // a package by its name: the one of that name here, else a new one
        if (!it.package.isEmpty()) {
            const auto pkg = std::ranges::find_if(m_items, [&it](const auto& p) {
                return p.second->item.packageId != 0 && p.second->item.package == it.package;
            });
            it.packageId = pkg != m_items.end() ? pkg->second->item.packageId : m_nextPackageId++;
        }
        Entry& ref = *e;
        m_items.emplace(it.id, std::move(e));
        logInfo(QStringLiteral("%1 taken up from %2 (%3 of %4 B)")
                    .arg(ref.item.name, metPath).arg(ref.item.done).arg(ref.item.size));
        changed(ref);
        switch (ref.item.state) {
        case State::Paused:
        case State::Gone:
        case State::Failed:
        case State::NoResolver:
            break;
        default:
            if (m_started)
                begin(ref);
            else
                setState(ref, State::Queued, Reason::WaitingForNode);
        }
        if (m_started)
            pumpSession();
    }
}

QJsonObject Ed3kHosterJoin::toJson(const Entry& e) const
{
    const Item& it = e.item;
    QJsonArray have;
    for (auto b = e.have.begin(); b != e.have.end();) {
        const std::uint64_t first = *b;
        std::uint64_t count = 0;
        while (b != e.have.end() && *b == first + count * kBlock) {
            ++count;
            ++b;
        }
        have.append(QJsonArray{qint64(first), qint64(count)});
    }
    QJsonArray links;
    for (const SharedLink& l : e.shared)
        links.append(QJsonObject{{QStringLiteral("url"), l.url},
                                 {QStringLiteral("checkedAt"), l.checkedAt},
                                 {QStringLiteral("offline"), l.offline},
                                 {QStringLiteral("reportCheckAt"), l.reportCheckAt}});
    QJsonObject o{{QStringLiteral("links"), links},
                  {QStringLiteral("have"), have},
                  {QStringLiteral("url"), it.url},
                  {QStringLiteral("host"), it.host},
                  {QStringLiteral("name"), it.name},
                  {QStringLiteral("size"), qint64(it.size)},
                  {QStringLiteral("added"), it.added},
                  {QStringLiteral("root"), it.rootHex},
                  {QStringLiteral("state"), int(it.state)},
                  {QStringLiteral("reason"), int(it.reason)},
                  {QStringLiteral("reasonArg"), it.reasonArg},
                  {QStringLiteral("mirrors"), QJsonArray::fromStringList(it.mirrors)},
                  {QStringLiteral("mirrorNo"), it.mirrorNo},
                  {QStringLiteral("tried"), QJsonArray::fromStringList(it.tried)}};
    if (it.packageId != 0)
        o.insert(QStringLiteral("pkgName"), it.package);
    if (!it.passwords.isEmpty())
        o.insert(QStringLiteral("passwords"), QJsonArray::fromStringList(it.passwords));
    return o;
}

void Ed3kHosterJoin::fromJson(const QJsonObject& o, Entry& e) const
{
    Item& it = e.item;
    it.url = o.value(QStringLiteral("url")).toString();
    it.host = o.value(QStringLiteral("host")).toString();
    it.name = safeName(o.value(QStringLiteral("name")).toString(), kNoName);
    it.size = std::uint64_t(o.value(QStringLiteral("size")).toInteger());
    it.added = o.value(QStringLiteral("added")).toInteger();
    it.rootHex = o.value(QStringLiteral("root")).toString();
    it.state = State(std::clamp<qint64>(o.value(QStringLiteral("state")).toInteger(), 0, int(State::Failed)));
    it.reason = Reason(std::clamp<qint64>(o.value(QStringLiteral("reason")).toInteger(), 0, int(Reason::UnpackPassword)));
    it.reasonArg = o.value(QStringLiteral("reasonArg")).toString();
    for (const QJsonValue& m : o.value(QStringLiteral("mirrors")).toArray())
        if (const QString url = m.toString(); !url.isEmpty())
            it.mirrors << url;
    it.mirrorNo = int(o.value(QStringLiteral("mirrorNo")).toInteger());
    for (const QJsonValue& m : o.value(QStringLiteral("tried")).toArray())
        if (const QString url = m.toString(); !url.isEmpty())
            it.tried << url;
    if (const QString pkg = o.value(QStringLiteral("pkgName")).toString(); !pkg.isEmpty())
        it.package = safeName(pkg, kNoName);
    for (const QJsonValue& pw : o.value(QStringLiteral("passwords")).toArray())
        if (const QString t = pw.toString(); !t.isEmpty())
            it.passwords << t;
    for (const QJsonValue& lv : o.value(QStringLiteral("links")).toArray()) {
        const QJsonObject lo = lv.toObject();
        SharedLink l;
        l.url = lo.value(QStringLiteral("url")).toString();
        l.checkedAt = lo.value(QStringLiteral("checkedAt")).toInteger();
        l.offline = lo.value(QStringLiteral("offline")).toBool();
        l.reportCheckAt = lo.value(QStringLiteral("reportCheckAt")).toInteger();
        if (!l.url.isEmpty())
            e.shared << l;
    }
    // verified blocks: [first offset, count] runs
    for (const QJsonValue& run : o.value(QStringLiteral("have")).toArray()) {
        const QJsonArray r = run.toArray();
        const auto first = std::uint64_t(r.at(0).toInteger());
        const auto count = std::uint64_t(r.at(1).toInteger());
        for (std::uint64_t i = 0; i < count && first % kBlock == 0 && first + i * kBlock < it.size; ++i)
            e.have.insert(first + i * kBlock);
    }
}

std::unique_ptr<Ed3kHosterJoin::Entry> Ed3kHosterJoin::readMet(const QString& metPath) const
{
    const QByteArray raw = readBlob(metPath);
    const QJsonObject o = QJsonDocument::fromJson(raw).object();
    if (o.value(QStringLiteral("format")).toString() != kMetFormat || o.value(QStringLiteral("v")).toInt() != kMetVersion)
        return nullptr;
    auto e = std::make_unique<Entry>();
    fromJson(o, *e);
    Item& it = e->item;
    if (it.url.isEmpty() || it.state == State::Complete)
        return nullptr;
    it.file = metPath.chopped(4);   // the part file is the one beside it
    // a rooted item goes on from its verified blocks; else it starts over
    if (const auto root = rootFromHex(it.rootHex); root && it.size > 0 && QFileInfo(it.file).isFile()) {
        e->root = root;
        it.verified = true;
        for (const std::uint64_t off : e->have)
            it.done += std::min<std::uint64_t>(kBlock, it.size - off);
    } else {
        if (!e->have.empty())
            logWarning(QStringLiteral("%1: no part file beside it, the download starts over").arg(metPath));
        e->have.clear();
    }
    return e;
}

void Ed3kHosterJoin::writeMet(Entry& e)
{
    if (e.item.file.isEmpty())
        return;
    QJsonObject o = toJson(e);
    o.insert(QStringLiteral("format"), kMetFormat);
    o.insert(QStringLiteral("v"), kMetVersion);
    const QString path = metPathOf(e);
    QDir().mkpath(QFileInfo(path).path());
    if (writeBlob(path, spanOf(QJsonDocument(o).toJson(QJsonDocument::Indented))))
        e.metDirty = false;
    else
        logWarning(QStringLiteral("cannot write %1").arg(path));
}

QString Ed3kHosterJoin::partPathFor(const QString& key) const
{
    return QDir(m_tempDirs.isEmpty() ? joinDir(QStringLiteral("hoster")) : m_tempDirs.first()).filePath(key + kPartSuffix);
}

QString Ed3kHosterJoin::metPathOf(const Entry& e)
{
    return e.item.file + QStringLiteral(".met");
}

void Ed3kHosterJoin::retryNotReady()
{
    // only on the flip to ready: a sidecar that dies also signals, and must not respawn in a loop
    const bool ready = m_resolver && m_resolver->available();
    const bool flipped = ready && !m_resolverReady;
    m_resolverReady = ready;
    if (m_started && !ready && m_resolver)   // e.g. "Downloading resolver plugins" -> failed
        for (auto& [id, e] : m_items)
            if (e->item.state == State::NoResolver && e->item.reason == Reason::ResolverNotReady)
                setState(*e, State::NoResolver, Reason::ResolverNotReady, m_resolver->unavailableReason());
    if (!m_started || !flipped)
        return;
    bool any = false;
    for (auto& [id, e] : m_items)
        if (e->item.state == State::NoResolver && e->item.reason == Reason::ResolverNotReady) {
            begin(*e);
            any = true;
        }
    if (any)
        pumpSession();
}


QString Ed3kHosterJoin::freePath(const QString& dir, const QString& name)
{
    const QFileInfo base(name);
    QString to = QDir(dir).filePath(name);
    for (int n = 1; QFileInfo::exists(to) && n < 1000; ++n)
        to = QDir(dir).filePath(base.completeSuffix().isEmpty()
                                    ? QStringLiteral("%1 (%2)").arg(base.baseName()).arg(n)
                                    : QStringLiteral("%1 (%2).%3").arg(base.baseName()).arg(n).arg(base.completeSuffix()));
    return to;
}

QString Ed3kHosterJoin::destDirOf(const Item& it) const
{
    return it.package.isEmpty() ? m_incomingDir : QDir(m_incomingDir).filePath(it.package);
}

QString Ed3kHosterJoin::archiveDirOf(const Item& it) const
{
    const QString base = m_tempDirs.isEmpty() ? joinDir(QStringLiteral("hoster")) : m_tempDirs.first();
    return QDir(base).filePath(QStringLiteral("hoster-archives/%1")
                                   .arg(it.package.isEmpty() ? QStringLiteral("_") : it.package));
}

void Ed3kHosterJoin::queueUnpacks()
{
    if (m_unpack.busy() || !m_prefs.extract)
        return;
    for (auto& [id, ep] : m_items) {
        Entry& e = *ep;
        if (e.item.state != State::Complete || e.item.reason != Reason::UnpackWaiting)
            continue;
        const QString dir = QFileInfo(e.item.file).absolutePath();
        const QString key = Ed3kHosterUnpack::setKey(QFileInfo(e.item.file).fileName());
        // a volume of the set still on its way (same package): wait for it
        const bool pending = std::ranges::any_of(m_items, [&e, &key](const auto& p) {
            const Item& o = p.second->item;
            return o.packageId == e.item.packageId && o.state != State::Complete && o.state != State::Failed
                && o.state != State::Gone && Ed3kHosterUnpack::setKey(o.name) == key;
        });
        if (pending)
            continue;
        std::vector<Entry*> members;
        for (auto& [oid, op] : m_items)
            if (op->item.state == State::Complete && op->item.reason == Reason::UnpackWaiting
                && QFileInfo(op->item.file).absolutePath() == dir
                && Ed3kHosterUnpack::setKey(QFileInfo(op->item.file).fileName()) == key)
                members.push_back(op.get());
        const QString file = QFileInfo(e.item.file).absoluteFilePath();
        const auto sets = Ed3kHosterUnpack::setsIn(dir);
        const auto set = std::ranges::find_if(sets, [&file](const Ed3kHosterUnpack::Set& s) {
            return std::ranges::any_of(s.volumes, [&file](const QString& v) { return QFileInfo(v).absoluteFilePath() == file; });
        });
        if (set == sets.end() || set->headless) {
            for (Entry* m : members) {
                m->item.reason = Reason::UnpackFailed;   // not setState: Complete counted once
                m->item.reasonArg = set == sets.end() ? QStringLiteral("the archive is gone")
                                                      : QStringLiteral("the first volume is missing");
                changed(*m);
            }
            save();
            continue;
        }
        HosterUnpackJob job;
        job.firstVolume = set->firstVolume;
        job.archiveDir = dir;
        for (const Ed3kHosterUnpack::Set& s : sets)
            if (s.firstVolume != set->firstVolume)
                job.skip.insert(s.firstVolume);
        job.destDir = destDirOf(e.item);
        job.scratchDir = QDir(dir).filePath(QStringLiteral(".unpack"));
        job.externalTool = thePrefs.usenetExternalUnpacker();
        // the package's passwords (typed ones first, then a container's), of every member
        for (const Entry* m : members)
            for (const QString& pw : m->item.passwords)
                if (!job.passwords.contains(pw))
                    job.passwords << pw;
        // then the user's list from Options > Files (all networks)
        for (const QString& pw : thePrefs.archivePasswords())
            if (!job.passwords.contains(pw))
                job.passwords << pw;
        job.nestedDepth = thePrefs.archiveRecursiveExtract() ? usenet::UsenetUnpacker::kNestedDepth : 0;
        job.deleteNestedArchives = m_prefs.deleteArchives;
        if (!m_unpack.start(job))
            return;
        for (Entry* m : members) {
            m->item.reason = Reason::Unpacking;
            m->item.reasonArg = QStringLiteral("0");
            changed(*m);
        }
        logInfo(QStringLiteral("unpacking %1 into %2").arg(QFileInfo(set->firstVolume).fileName(), job.destDir));
        save();
        return;
    }
}

void Ed3kHosterJoin::onUnpacked(const HosterUnpackResult& r)
{
    const QString dir = QFileInfo(r.firstVolume).absolutePath();
    const QString key = Ed3kHosterUnpack::setKey(QFileInfo(r.firstVolume).fileName());
    const bool drop = r.ok && m_prefs.deleteArchives;
    for (auto& [id, ep] : m_items) {
        Entry& e = *ep;
        if (e.item.state != State::Complete || e.item.reason != Reason::Unpacking
            || QFileInfo(e.item.file).absolutePath() != dir
            || Ed3kHosterUnpack::setKey(QFileInfo(e.item.file).fileName()) != key)
            continue;
        if (r.ok) {
            e.item.reason = Reason::Unpacked;
            e.item.reasonArg = destDirOf(e.item);
        } else {
            e.item.reason = r.passwordRequired ? Reason::UnpackPassword : Reason::UnpackFailed;
            e.item.reasonArg = r.error;
        }
        changed(e);
    }
    if (r.ok) {
        logInfo(QStringLiteral("unpacked %1: %2 file(s)").arg(QFileInfo(r.firstVolume).fileName()).arg(r.files.size()));
        if (drop) {
            for (const QString& v : r.volumes)
                QFile::remove(v);
            QDir().rmdir(dir);
        }
    } else {
        logWarning(QStringLiteral("cannot unpack %1: %2").arg(QFileInfo(r.firstVolume).fileName(), r.error));
    }
    save();
    published(r.files);
    queueUnpacks();
}

void Ed3kHosterJoin::releaseArchives()
{
    for (auto& [id, ep] : m_items) {
        Entry& e = *ep;
        if (e.item.state != State::Complete
            || (e.item.reason != Reason::UnpackWaiting && e.item.reason != Reason::UnpackFailed
                && e.item.reason != Reason::UnpackPassword))
            continue;
        const QString dir = destDirOf(e.item);
        QDir().mkpath(dir);
        const QString from = e.item.file;
        const QString to = freePath(dir, QFileInfo(from).fileName());
        if (!QFile::rename(from, to) && !(QFile::copy(from, to) && QFile::remove(from)))
            continue;
        QDir().rmdir(QFileInfo(from).absolutePath());
        e.item.file = to;
        e.item.reason = Reason::None;
        e.item.reasonArg.clear();
        changed(e);
        published({to});
    }
    save();
}

void Ed3kHosterJoin::published(const QStringList& files)
{
    if (m_published && !files.isEmpty())
        m_published(files);
}

} // namespace eMule::ed3k
