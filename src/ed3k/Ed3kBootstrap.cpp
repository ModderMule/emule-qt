/// @file Ed3kBootstrap.cpp
/// @brief Node lists for the first contact — implementation.

#include "Ed3kBootstrap.h"

#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QTimer>
#include <QUrl>

#include <cstring>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

constexpr int kAskTimeoutMs = 5 * 60 * 1000;
const QString kLastFile = QStringLiteral("bootstrap.dat");
const QString kShippedName = QStringLiteral("nodes.ed3knod");
// constexpr qint64 kSeedAgainMs = 10 * 60 * 1000;  // each address once in 10 min (packmule decides now)
// constexpr std::uint64_t kSeedBelow = 10;         // contacts (packmule ED2K_SEED_BELOW; decides now)
constexpr qsizetype kSeedTriedMax = 4096;

QString errorName(ffi::Error e)
{
    return QString::number(static_cast<int>(e));
}

} // namespace

Ed3kBootstrap::Ed3kBootstrap(Ed3kSession& session, QObject* parent)
    : QObject(parent)
    , m_session(session)
{
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kBootstrap::onEventPolled, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kBootstrap::onStatus);
    m_shippedPath = QDir(thePrefs.configDir()).filePath(kShippedName);
    m_seedClock.start();
    onStatus();
}

Ed3kBootstrap::~Ed3kBootstrap() = default;

void Ed3kBootstrap::setSources(const QStringList& links)
{
    if (links == m_links)
        return;
    m_links = links;
    if (m_running)
        configure();
}

std::optional<std::uint64_t> Ed3kBootstrap::openLink(const QString& link)
{
    auto* e = m_session.engine();
    if (!e)
        return std::nullopt;
    const QByteArray text = link.trimmed().toUtf8();
    auto ask = e->bootstrapLink(m_session.now(), std::string_view(text.constData(), std::size_t(text.size())));
    m_session.pump();
    if (!ask) {
        logEd3kWarning(QStringLiteral("ed3k: node list link refused (error %1)").arg(errorName(ask.error())));
        return std::nullopt;
    }
    return *ask;
}

bool Ed3kBootstrap::answer(std::uint64_t ask, bool yes)
{
    auto* e = m_session.engine();
    if (!m_asks.erase(ask) || !e)
        return false;
    (void)e->bootstrapAnswer(m_session.now(), ask, yes);
    m_session.pump();
    return true;
}

bool Ed3kBootstrap::fetchNow()
{
    auto* e = m_session.engine();
    if (!e)
        return false;
    const auto ok = e->bootstrapFetch(m_session.now());
    m_session.pump();
    return ok.has_value();
}

void Ed3kBootstrap::setPolicy(bool autoFetch, int maxAgeDays)
{
    maxAgeDays = std::max(0, maxAgeDays);
    if (autoFetch == m_autoFetch && maxAgeDays == m_maxAgeDays)
        return;
    m_autoFetch = autoFetch;
    m_maxAgeDays = maxAgeDays;
    if (m_running)
        configure();
}

void Ed3kBootstrap::setShippedFile(const QString& path)
{
    m_shippedPath = path;
}

bool Ed3kBootstrap::offerEd2kPeer(const std::array<std::uint8_t, 16>& userHash, const QHostAddress& ip, std::uint16_t tcpPort)
{
    auto* e = m_session.engine();
    if (!m_seedFromEd2k || !e || tcpPort == 0 || ip.isNull())
        return false;
    // cheap check first: most hashes are no hint
    const auto hint = ffi::userHashHint(userHash);
    // if (!hint || m_session.contactCount() >= kSeedBelow)
    if (!hint)
        return false;
    const qint64 now = m_seedClock.elapsed();
    const QString key = ip.toString();
    // every hint goes to packmule, also with Kad full or the address tried: it marks the host
    // as sent to, so a node seeding from our hello and dialling back is no unasked
    // connection (§9.2). packmule dials only below 10 contacts, each address once
    // if (const auto it = m_seedTried.constFind(key); it != m_seedTried.cend() && now - *it < kSeedAgainMs)
    //     return false;
    // no pacing here: packmule caps the seed dials (SEED_DIALS); a gate here dropped most
    // peers of a burst of hellos for good
    const auto dialled = e->kadSeedHint(m_session.now(), *hint, toAddress(ip, tcpPort));
    if (!dialled || !*dialled)
        return false;   // not marked: the next hello of this peer may try again
    m_session.pump();
    if (m_seedTried.size() >= kSeedTriedMax)
        m_seedTried.clear();
    m_seedTried.insert(key, now);
    logEd3kKadDebug([&]() -> QString { return QStringLiteral("ed3k: Kad seed from eD2K peer %1:%2").arg(key).arg(tcpPort); });
    return true;
}

std::expected<QByteArray, QString> Ed3kBootstrap::exportList(const QString& comment) const
{
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(QStringLiteral("ed3k is not running"));
    const QByteArray c = comment.toUtf8();
    auto file = e->bootstrapExport(m_session.now(), std::string_view(c.constData(), std::size_t(c.size())));
    if (!file) {
        if (file.error() == ffi::Error::NotFound)
            return std::unexpected(QStringLiteral("no usable contacts yet"));
        return std::unexpected(QStringLiteral("export failed (error %1)").arg(errorName(file.error())));
    }
    const auto bytes = file->bytes();
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
}

std::expected<QByteArray, QString> Ed3kBootstrap::exportSaved(const QString& configDir, const QString& comment)
{
    const QString dir = stateDir(configDir);
    auto seed = loadSecret(dir, QStringLiteral("node.key"));
    if (!seed)
        return std::unexpected(QStringLiteral("no ed3k node key in %1").arg(dir));
    const QByteArray state = readBlob(QDir(dir).filePath(QStringLiteral("kad.dat")));
    if (state.isEmpty()) {
        wipe(*seed);
        return std::unexpected(QStringLiteral("no saved ed3k contacts in %1 (run the node first)").arg(dir));
    }
    const QByteArray c = comment.toUtf8();
    auto file = ffi::nodeListFromState(*seed, {}, std::uint64_t(QDateTime::currentSecsSinceEpoch()),
                                       {reinterpret_cast<const std::byte*>(state.constData()), std::size_t(state.size())},
                                       std::string_view(c.constData(), std::size_t(c.size())));
    wipe(*seed);
    if (!file) {
        if (file.error() == ffi::Error::NotFound)
            return std::unexpected(QStringLiteral("the saved contacts hold no usable node"));
        return std::unexpected(QStringLiteral("export failed (error %1)").arg(errorName(file.error())));
    }
    const auto bytes = file->bytes();
    return QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
}

QString Ed3kBootstrap::lastFile() const
{
    return QDir(m_session.stateDir()).filePath(kLastFile);
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kBootstrap::onEventPolled(const ffi::Event& event, bool* handled)
{
    namespace ev = ffi::event;
    if (const auto* a = std::get_if<ev::BootstrapAsk>(&event)) {
        *handled = true;
        const QString host = QString::fromStdString(a->host);
        const QString url = QString::fromStdString(a->url);
        // §3: never added without the user; nobody to ask refuses it
        m_asks.insert(a->ask);   // before the asker: it may answer at once
        if (!m_asker || !m_asker(a->ask, host, url)) {
            logEd3k(QStringLiteral("ed3k: node list source at %1 refused: nobody to ask").arg(host));
            if (m_asks.erase(a->ask))
                if (auto* e = m_session.engine())
                    (void)e->bootstrapAnswer(m_session.now(), a->ask, false);
            return;
        }
        QTimer::singleShot(kAskTimeoutMs, this, [this, ask = a->ask] {
            if (answer(ask, false))
                logEd3k(QStringLiteral("ed3k: a node list source was not answered and is refused"));
        });
    } else if (const auto* s = std::get_if<ev::BootstrapStarted>(&event)) {
        m_last = s->unix;
        saveLast();
        logEd3kKad(QStringLiteral("ed3k: first contact from node lists"));
    } else if (const auto* l = std::get_if<ev::BootstrapLists>(&event)) {
        logEd3kKad(QStringLiteral("ed3k: node lists: %1 list(s), %2 node(s)").arg(l->lists).arg(l->nodes));
        emit listsReceived(int(l->lists), int(l->nodes));
    } else if (std::holds_alternative<ev::BootstrapFailed>(event)) {
        logEd3kKad(QStringLiteral("ed3k: node lists: no list could be used"));
    } else if (const auto* added = std::get_if<ev::BootstrapAdded>(&event)) {
        const QString link = nodesLink(QString::fromStdString(added->url), added->key);
        logEd3k(QStringLiteral("ed3k: node list source %1 added").arg(QUrl(QString::fromStdString(added->url)).host()));
        if (!m_links.contains(link))
            m_links.append(link);
        emit sourceAdded(link);
    }
}

void Ed3kBootstrap::onStatus()
{
    const bool running = m_session.isRunning();
    if (running == m_running)
        return;
    m_running = running;
    m_asks.clear();   // asks die with their engine
    if (running) {
        loadLast();
        configure();
        applyShipped();
    }
}

void Ed3kBootstrap::configure()
{
    auto* e = m_session.engine();
    if (!e)
        return;
    std::vector<ffi::Engine::NodesSource> sources;
    for (const QString& link : m_links) {
        const auto src = parseNodesLink(link);
        if (!src) {
            logEd3kWarning(QStringLiteral("ed3k: not a node list source, ignored: %1").arg(link));
            continue;
        }
        const QByteArray url = src->url.toUtf8();
        sources.push_back(ffi::Engine::NodesSource{std::string(url.constData(), std::size_t(url.size())), src->key});
    }
    if (auto ok = e->bootstrapConfigure(sources, m_autoFetch, m_last); !ok)
        logEd3kWarning(QStringLiteral("ed3k: node list sources not set (error %1)").arg(errorName(ok.error())));
    applyPolicy();
}

void Ed3kBootstrap::applyPolicy()
{
    auto* e = m_session.engine();
    if (!e)
        return;
    (void)e->bootstrapMaxAge(std::uint64_t(m_maxAgeDays) * 86'400);
}

void Ed3kBootstrap::applyShipped()
{
    auto* e = m_session.engine();
    if (!e || m_shippedPath.isEmpty())
        return;
    const QByteArray file = readBlob(m_shippedPath);
    if (file.isEmpty())
        return;
    if (auto ok = e->bootstrapShipped({reinterpret_cast<const std::byte*>(file.constData()), std::size_t(file.size())}); !ok)
        logEd3kWarning(QStringLiteral("ed3k: shipped node list not used (error %1)").arg(errorName(ok.error())));
    else
        logEd3kKadDebug([&]() -> QString { return QStringLiteral("ed3k: shipped node list %1 (%2 bytes)").arg(m_shippedPath).arg(file.size()); });
}

void Ed3kBootstrap::loadLast()
{
    m_last.reset();
    const QByteArray b = readBlob(lastFile());
    if (b.size() != 8)
        return;
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | std::uint8_t(b.at(i));   // little endian
    m_last = v;
}

void Ed3kBootstrap::saveLast()
{
    if (!m_last)
        return;
    QByteArray b(8, 0);
    for (int i = 0; i < 8; ++i)
        b[i] = char((*m_last >> (8 * i)) & 0xff);
    QDir().mkpath(m_session.stateDir());
    writeBlobLater(lastFile(), {reinterpret_cast<const std::byte*>(b.constData()), std::size_t(b.size())});
}

} // namespace eMule::ed3k
