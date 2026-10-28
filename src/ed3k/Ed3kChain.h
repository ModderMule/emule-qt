#pragma once

/// @file Ed3kChain.h
/// @brief The chaintube chain (chaintube.md §4.2, §4.3) from the prefs: chainConfigure on
/// the running node, at each start and when the prefs or the endpoints change.
///
/// - No defaults (§4.2): without a chain ID and the content, comment and vote contracts the
///   chain stays unconfigured (reads off). The forwarder is optional.
/// - Providers per read (§4.3): 0 without endpoints (ed3k.chaintubeRpc), 1 when every
///   endpoint is a local node, else 2 (two independent providers), held to the endpoints there are.

#include "prefs/Preferences.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace eMule::ed3k {

class Ed3kSession;

class Ed3kChain : public QObject {
    Q_OBJECT

public:
    explicit Ed3kChain(Ed3kSession& session, QObject* parent = nullptr);
    ~Ed3kChain() override;

    /// The prefs and the endpoint URLs (ed3k.chaintubeRpc); applied at once while the node
    /// runs, else at its start.
    void setConfig(const Ed3kChainPrefs& prefs, const QStringList& endpoints);
    [[nodiscard]] bool isConfigured() const { return m_configured; }
    /// Why it is not configured (English, for the log); empty when it is.
    [[nodiscard]] QString problem() const { return m_problem; }
    /// Providers a read asks for these endpoints (§4.3).
    [[nodiscard]] static std::uint32_t providersFor(const QStringList& endpoints);

private:
    void onSessionStatus();
    void apply();

    Ed3kSession& m_session;
    Ed3kChainPrefs m_prefs;
    QStringList m_endpoints;
    bool m_configured = false;
    QString m_configuredAs;
    QString m_problem;
};

} // namespace eMule::ed3k
