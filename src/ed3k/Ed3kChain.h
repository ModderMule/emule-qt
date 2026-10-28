#pragma once

/// @file Ed3kChain.h
/// @brief The chaintube chain (chaintube.md §4.2, §4.3) from the prefs: chainConfigure on
/// the running node, at each start and when the prefs or the endpoints change.
///
/// - No defaults (§4.2): without a chain ID and the content, comment and vote contracts the
///   chain stays unconfigured (reads off). The forwarder is optional.
/// - Providers per read (§4.3): 0 without endpoints (ed3k.chaintubeRpc), 1 when every
///   endpoint is a local node, else 2 (two independent providers), held to the endpoints there are.
///   One remote endpoint only: the §4.3 SHOULD (two providers agree) can't be met; said once
///   in the log (batch 12 ruling #8), and the options page notes it.
/// - Refused (batch 12 #6): a chain read or write every provider answered with the same
///   JSON-RPC error (ED3K_CHAIN_REFUSED, e.g. "insufficient funds"), or a site's request the
///   forwarder's `verify` refused (ED3K_CHAIN_BAD_REQUEST): logged, kept as lastRefusal() for
///   the status (GetEd3kStatus "chain"); the node's text is plain text from strangers.

#include "Ed3kFfi.h"

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
    /// Exactly one usable endpoint and it is no local node (§4.3: no second provider to agree).
    [[nodiscard]] static bool singleRemote(const QStringList& endpoints);
    /// Whether the endpoints set are one remote endpoint only (singleRemote).
    [[nodiscard]] bool isSingleRemote() const { return singleRemote(m_endpoints); }
    /// The last refused read or write (for the user, shown as plain text); empty: none.
    [[nodiscard]] QString lastRefusal() const { return m_lastRefusal; }
    /// What a ChainFailed / ChainTxFailed tells the user; empty: not a refusal (tests).
    [[nodiscard]] static QString refusalText(std::uint32_t why, const QString& message);

signals:
    /// A read or write was refused (lastRefusal() changed).
    void refused(const QString& text);

private:
    void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onSessionStatus();
    void apply();

    Ed3kSession& m_session;
    Ed3kChainPrefs m_prefs;
    QStringList m_endpoints;
    bool m_configured = false;
    QString m_configuredAs;
    QString m_problem;
    /// the one-remote-endpoint notice was logged (once per run)
    bool m_toldSingle = false;
    QString m_lastRefusal;
};

} // namespace eMule::ed3k
