#include "net/ConnectionChecker.h"

#include "net/HttpFileDownload.h"
#include "utils/Log.h"

namespace eMule {

ConnectionChecker::ConnectionChecker(QObject* parent)
    : QObject(parent)
{
    m_probe = [this](const QUrl& url, ProbeDone done) { runHttpProbe(url, std::move(done)); };
    connect(&m_timer, &QTimer::timeout, this, &ConnectionChecker::startProbe);
}

ConnectionChecker::~ConnectionChecker() = default;

void ConnectionChecker::configure(bool enabled, const QString& url, uint32 intervalSecs)
{
    const QUrl probe = probeUrl(url);
    if (enabled == m_enabled && probe == m_url && intervalSecs == m_intervalSecs)
        return;

    const bool switched = enabled != m_enabled;
    m_enabled = enabled;
    m_url = probe;
    m_intervalSecs = std::max<uint32>(intervalSecs, 1);
    ++m_generation;
    m_timer.stop();
    if (m_job)
        m_job->abort();
    m_probeInFlight = false;
    m_failures = 0;
    m_lastProbeFailed = false;
    m_idleLogged = false;
    m_udpOnlyLogged = false;

    if (switched && enabled) {
        logInfo(m_url.isEmpty()
                    ? QStringLiteral("Connection checker on: Kad tells when the internet is down")
                    : m_kadRunning ? QStringLiteral("Connection checker on: Kad and %1").arg(host())
                                   : QStringLiteral("Connection checker on: %1 (Kad off)").arg(host()));
    } else if (switched) {
        logInfo(QStringLiteral("Connection checker off"));
    }
    evaluate();
}

void ConnectionChecker::setKadState(bool running, bool offline, bool evidence)
{
    if (!running) {
        offline = false;
        evidence = false;
    }
    if (running == m_kadRunning && offline == m_kadOffline && evidence == m_kadEvidence)
        return;

    const bool wasKadMode = isKadMode();
    const bool wentOffline = offline && !m_kadOffline;
    m_kadRunning = running;
    m_kadOffline = offline;
    m_kadEvidence = evidence;

    if (wasKadMode != isKadMode()) {
        // other mode, other probe rhythm: start it fresh
        ++m_generation;
        m_timer.stop();
        m_probeInFlight = false;
        m_failures = 0;
        m_lastProbeFailed = false;
    }
    if (wentOffline) {
        // a probe from before Kad's verdict must not confirm it
        m_lastProbeFailed = false;
        m_udpOnlyLogged = false;
    }
    if (running)
        m_idleLogged = false;
    evaluate();
}

QUrl ConnectionChecker::probeUrl(const QString& text)
{
    QString s = text.trimmed();
    if (s.isEmpty())
        return {};
    if (!s.contains(QStringLiteral("://")))
        s.prepend(QStringLiteral("http://"));
    const QUrl url(s, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != u"http" && url.scheme() != u"https"))
        return {};
    return url;
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

void ConnectionChecker::evaluate()
{
    const auto stopProbing = [this] {
        if (m_timer.isActive())
            m_timer.stop();
    };
    const auto keepProbing = [this] {
        if (!m_timer.isActive()) {
            m_timer.start(static_cast<int>(m_intervalSecs * 1000));
            startProbe();
        }
    };

    if (!m_enabled) {
        stopProbing();
        setOffline(false, {});
        return;
    }

    if (isKadMode()) {
        if (!m_kadOffline) {
            stopProbing();
            m_failures = 0;
            m_lastProbeFailed = false;
            setOffline(false, {});
            return;
        }
        if (m_url.isEmpty()) {
            setOffline(true, QStringLiteral("Kad gets no answers"));
            return;
        }
        // confirm, then keep asking for a quick way back
        keepProbing();
        if (m_lastProbeFailed)
            setOffline(true, QStringLiteral("Kad gets no answers and %1 is unreachable").arg(host()));
        return;
    }

    // URL-only: Kad off, or no Kad packet yet
    if (m_url.isEmpty()) {
        stopProbing();
        if (!m_kadRunning && !m_idleLogged) {
            m_idleLogged = true;
            logInfo(QStringLiteral("Connection checker: Kad is off and no check URL is set — "
                                   "nothing to check"));
        }
        setOffline(false, {});
        return;
    }
    keepProbing();
}

void ConnectionChecker::startProbe()
{
    if (m_probeInFlight || m_url.isEmpty() || !m_enabled)
        return;
    m_probeInFlight = true;
    const uint32 generation = m_generation;
    const QPointer<ConnectionChecker> self(this);
    m_probe(m_url, [self, generation](bool reached) {
        if (self)
            self->onProbeDone(generation, reached);
    });
}

void ConnectionChecker::onProbeDone(uint32 generation, bool reached)
{
    if (generation != m_generation)
        return;
    m_probeInFlight = false;
    if (!m_enabled)
        return;

    logDebug([&]() -> QString { return QStringLiteral("Connection checker: %1 %2")
                 .arg(host(), reached ? QStringLiteral("reached") : QStringLiteral("unreachable")); });

    if (reached) {
        m_failures = 0;
        m_lastProbeFailed = false;
        if (isKadMode() && m_kadOffline && !m_offline && !m_udpOnlyLogged) {
            m_udpOnlyLogged = true;
            logWarning(QStringLiteral("Connection checker: Kad gets no answers but %1 is reachable — "
                                      "staying online (UDP blocked?)").arg(host()));
        }
        setOffline(false, {});
        return;
    }

    ++m_failures;
    m_lastProbeFailed = true;
    if (isKadMode()) {
        if (m_kadOffline)
            setOffline(true, QStringLiteral("Kad gets no answers and %1 is unreachable").arg(host()));
    } else if (m_failures >= kUrlOnlyFailures) {
        setOffline(true, QStringLiteral("%1 is unreachable").arg(host()));
    }
}

void ConnectionChecker::setOffline(bool offline, const QString& reason)
{
    if (offline == m_offline)
        return;
    m_offline = offline;
    m_reason = reason;
    m_offlineSince = offline ? time(nullptr) : 0;
    if (offline)
        logStatusWarning(QStringLiteral("Internet connection lost (%1) — holding eD2K, BitTorrent "
                                        "and Usenet").arg(reason));
    else
        logStatusInfo(QStringLiteral("Internet connection back — resuming"));
    emit offlineChanged(offline);
}

void ConnectionChecker::runHttpProbe(const QUrl& url, ProbeDone done)
{
    HttpFileDownload::Options opts;
    opts.timeoutMs = kProbeTimeoutMs;
    opts.headOnly = true;   // status line is enough, no body
    opts.headers = {{"Cache-Control", "no-cache"}};
    // any answer from the far end proves the line, even an error page
    m_job = HttpFileDownload::fetch(this, url, opts, [done = std::move(done)](const auto& r) {
        done(r.ok || r.httpStatus != 0);
    });
}

QString ConnectionChecker::host() const
{
    return m_url.host();
}

} // namespace eMule
