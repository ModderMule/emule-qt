/// @file Ed3kBootstrap.cpp
/// @brief Node lists for the first contact — implementation.

#include "Ed3kBootstrap.h"

#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDir>
#include <QTimer>
#include <QUrl>

#include <cstring>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

constexpr int kAskTimeoutMs = 5 * 60 * 1000;
const QString kLastFile = QStringLiteral("bootstrap.dat");

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
    if (auto ok = e->bootstrapConfigure(sources, true, m_last); !ok)
        logEd3kWarning(QStringLiteral("ed3k: node list sources not set (error %1)").arg(errorName(ok.error())));
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
    if (!writeBlob(lastFile(), {reinterpret_cast<const std::byte*>(b.constData()), std::size_t(b.size())}))
        logEd3kWarning(QStringLiteral("ed3k: cannot write %1").arg(lastFile()));
}

} // namespace eMule::ed3k
