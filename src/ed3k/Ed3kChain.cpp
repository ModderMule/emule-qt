/// @file Ed3kChain.cpp
/// @brief The chaintube chain from the prefs — implementation.

#include "Ed3kChain.h"

#include "Ed3kCredit.h"
#include "Ed3kHttp.h"
#include "Ed3kSession.h"

#include "utils/Log.h"

#include <QHostAddress>
#include <QUrl>

#include <algorithm>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

Ed3kChain::Ed3kChain(Ed3kSession& session, QObject* parent)
    : QObject(parent)
    , m_session(session)
{
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kChain::onSessionStatus);
}

Ed3kChain::~Ed3kChain() = default;

void Ed3kChain::setConfig(const Ed3kChainPrefs& prefs, const QStringList& endpoints)
{
    m_prefs = prefs;
    m_endpoints = endpoints;
    apply();
}

std::uint32_t Ed3kChain::providersFor(const QStringList& endpoints)
{
    QStringList usable;
    for (const QString& u : endpoints)
        if (Ed3kHttp::rpcUrlUsable(u))
            usable << u;
    if (usable.isEmpty())
        return 0;
    const bool allLocal = std::ranges::all_of(usable, [](const QString& u) {
        const QString host = QUrl(u).host();
        const QHostAddress ip(host);
        return host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0 || (!ip.isNull() && ip.isLoopback());
    });
    // §4.3: a local node needs nothing more; remote: two independent providers SHOULD agree.
    // One remote endpoint only: 1 (literal reading: asking two of one is no read at all)
    return allLocal ? 1 : std::min<std::uint32_t>(2, std::uint32_t(usable.size()));
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kChain::onSessionStatus()
{
    if (!m_session.isRunning()) {
        m_configured = false;
        m_configuredAs.clear();
        return;
    }
    if (!m_configured)
        apply();
}

void Ed3kChain::apply()
{
    auto* engine = m_session.engine();
    if (!engine)
        return;
    const auto content = Ed3kCredit::parseAddress(m_prefs.content);
    const auto comment = Ed3kCredit::parseAddress(m_prefs.comment);
    const auto vote = Ed3kCredit::parseAddress(m_prefs.vote);
    const auto forwarder = Ed3kCredit::parseAddress(m_prefs.forwarder);
    const std::uint32_t providers = providersFor(m_endpoints);
    // §4.2: the network's parameters, no defaults; nothing set: chain reads off
    if (m_prefs.chainId == 0 || !content || !comment || !vote) {
        m_problem = QStringLiteral("no chain ID or contracts set");
        if (m_configured) {
            (void)engine->chainConfigure(0, {}, {}, {}, 0);   // providers 0: reads off
            m_configured = false;
            m_configuredAs.clear();
            logEd3k(QStringLiteral("ed3k: chaintube chain off (%1)").arg(m_problem));
        }
        return;
    }
    if (!m_prefs.forwarder.isEmpty() && !forwarder)
        logEd3kWarning(QStringLiteral("ed3k: the chaintube forwarder is no address; none used"));
    const QString as = QStringLiteral("%1/%2/%3/%4/%5/%6").arg(m_prefs.chainId).arg(m_prefs.content.toLower(),
        m_prefs.comment.toLower(), m_prefs.vote.toLower(), forwarder ? m_prefs.forwarder.toLower() : QString()).arg(providers);
    if (m_configured && as == m_configuredAs)
        return;
    const auto ok = engine->chainConfigure(m_prefs.chainId, *content, *comment, *vote, providers,
                                           forwarder ? std::optional<ffi::Engine::ChainAddress>(*forwarder) : std::nullopt);
    if (!ok) {
        m_configured = false;
        m_configuredAs.clear();
        m_problem = ok.error() == ffi::Error::Unsupported ? QStringLiteral("the chaintube extension is turned off")
                                                          : QStringLiteral("not configured (error %1)").arg(int(ok.error()));
        logEd3kWarning(QStringLiteral("ed3k: chaintube: %1").arg(m_problem));
        return;
    }
    m_configured = true;
    m_configuredAs = as;
    m_problem = providers == 0 ? QStringLiteral("no chaintube RPC endpoints: reads off") : QString();
    logEd3k(QStringLiteral("ed3k: chaintube chain %1 configured (%2 provider(s) per read)").arg(m_prefs.chainId).arg(providers));
    m_session.pump();
}

} // namespace eMule::ed3k
