#pragma once

/// @file ConnectionChecker.h
/// @brief Is the own internet connection up? (#115, not MFC; eMuleAI ConChecker idea)
///
/// Two sources:
/// - Kad mode (Kad running and got a packet this run): Kad's NetworkWatch verdict.
///   A URL, if set, confirms it: offline only when Kad gets no answers and the URL
///   fails too. While Kad says offline the URL is probed for a quick way back.
/// - URL-only mode (Kad off, or no Kad packet yet): the URL every interval, offline
///   after kUrlOnlyFailures failed probes in a row. No URL = idle.
/// "Reached" = any HTTP answer, even an error status.
///
/// The checker only decides; CoreSession holds and resumes the networks.

#include "utils/Types.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <ctime>
#include <functional>

namespace eMule {

class HttpDownloadJob;

class ConnectionChecker : public QObject {
    Q_OBJECT

public:
    /// Failed probes in a row before URL-only mode calls it offline.
    static constexpr int kUrlOnlyFailures = 2;
    static constexpr int kProbeTimeoutMs = 5000;

    using ProbeDone = std::function<void(bool reached)>;
    /// Runs one probe; must call @p done once (tests swap it).
    using Probe = std::function<void(const QUrl& url, ProbeDone done)>;

    explicit ConnectionChecker(QObject* parent = nullptr);
    ~ConnectionChecker() override;

    void setProbe(Probe probe) { m_probe = std::move(probe); }

    /// From prefs; no-op when nothing changed. Disabling goes back online.
    void configure(bool enabled, const QString& url, uint32 intervalSecs);

    /// Kad changed: started/stopped, offline verdict, first packet.
    void setKadState(bool running, bool offline, bool evidence);

    [[nodiscard]] bool isEnabled() const { return m_enabled; }
    [[nodiscard]] bool isOffline() const { return m_offline; }
    /// 0 while online.
    [[nodiscard]] time_t offlineSince() const { return m_offlineSince; }
    /// Why offline; empty while online.
    [[nodiscard]] QString reason() const { return m_reason; }
    [[nodiscard]] bool isKadMode() const { return m_kadRunning && m_kadEvidence; }
    [[nodiscard]] bool isProbing() const { return m_timer.isActive(); }

    /// "example.com" -> "http://example.com"; invalid = empty QUrl.
    [[nodiscard]] static QUrl probeUrl(const QString& text);

signals:
    void offlineChanged(bool offline);

private:
    void evaluate();
    void startProbe();
    void onProbeDone(uint32 generation, bool reached);
    void setOffline(bool offline, const QString& reason);
    void runHttpProbe(const QUrl& url, ProbeDone done);
    [[nodiscard]] QString host() const;

    Probe m_probe;
    QTimer m_timer;
    QPointer<HttpDownloadJob> m_job;
    QUrl m_url;
    QString m_reason;
    time_t m_offlineSince = 0;
    uint32 m_intervalSecs = 30;
    uint32 m_generation = 0;   ///< bumped on every config/mode change: late answers drop
    int m_failures = 0;        ///< failed probes in a row
    bool m_enabled = false;
    bool m_offline = false;
    bool m_probeInFlight = false;
    bool m_lastProbeFailed = false;
    bool m_kadRunning = false;
    bool m_kadOffline = false;
    bool m_kadEvidence = false;
    bool m_idleLogged = false;
    bool m_udpOnlyLogged = false;
};

} // namespace eMule
