#pragma once

/// @file Ed3kCredit.h
/// @brief ed3k credits (credit.md) in the daemon: the credit key file, the ledger
/// configuration from the prefs, bids, priority slots sold, and what the GUI shows.
///
/// - The credit key (§2) is Config/ed3k/credit.key: 32 bytes from the CSPRNG that
///   ed3k_credit_address takes, owner-only (Ed3kStore), never logged, wiped after use.
///   It is made on the first enable or by newKey(); import/export go over local IPC only.
/// - Off by default (§9); no default chain or contract (§7.1): creditConfigure only with
///   enabled + chain ID + contract. Without ledger endpoints receipts are still signed and
///   kept (§7.3 "A node that reaches no RPC endpoint ..."); nothing is sold then.
/// - creditConfigure drops the held receipts, so it is called again only when the key, the
///   chain, the contract or the endpoints change.
/// - Priority slots: the engine's queue policy (queueAuto, on from start) grants a
///   CreditSlotHint itself (ffi.md "The queue policy"); this class only counts them.
/// - Bids: bidMaxPrice for every root of an ed3k download (FileStatus seen), 0 = none.

#include "Ed3kFfi.h"

#include "prefs/Preferences.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace eMule::ed3k {

class Ed3kSession;

class Ed3kCredit : public QObject {
    Q_OBJECT

public:
    /// A stake claim of an own burn (§6.7), kept for claimStake.
    struct StakeClaim {
        std::uint64_t id = 0;
        ::ed3k::event::CreditAddress to{};
        ::ed3k::Key32 amount{};   ///< uint256 BE, 10^-18 E3S
        std::uint64_t readyAt = 0;
    };
    /// What the GUI shows (GetEd3kStatus "credit").
    struct Status {
        bool enabled = false;      ///< the pref
        bool configured = false;   ///< creditConfigure took it on the running node
        int endpoints = 0;         ///< ledger endpoint classes configured
        QString address;           ///< 0x + 40 hex of credit.key; empty: no key yet
        QString problem;           ///< why it is not configured (English, for the log/tooltip)
        std::optional<std::uint64_t> balance;   ///< µ, as last read
        bool mature = false;
        bool overdrawn = false;
        std::uint32_t lastTxWhat = 0;    ///< ED3K_CREDIT_TX_*
        std::uint32_t lastTxState = 0;   ///< ED3K_CREDIT_STATE_*
        std::uint32_t lastTxRevert = 0;  ///< ED3K_CREDIT_REVERT_*
        QString lastTx;                  ///< hash hex
        std::uint64_t receiptsSigned = 0;   ///< downloader: receipts out
        std::uint64_t kibSigned = 0;
        std::uint64_t receiptsTaken = 0;    ///< uploader: valid receipts in
        std::uint64_t kibTaken = 0;
        std::uint64_t slotHints = 0;        ///< priority turns the lane gave
        std::uint64_t lateBids = 0;         ///< turns lost to late payment
        std::vector<std::uint32_t> wrongChain;   ///< endpoint classes on another chain
        std::optional<::ed3k::Key32> stakeRate;  ///< stake units per credit (uint256 BE)
        std::uint64_t stakeDelay = 0;            ///< s
        std::vector<StakeClaim> claims;
    };

    Ed3kCredit(Ed3kSession& session, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kCredit() override;

    /// The prefs and the number of ledger endpoints (ed3k.creditRpc); applied at once
    /// while the node runs, else at its start.
    void setConfig(const Ed3kCreditPrefs& prefs, int endpoints);
    [[nodiscard]] const Ed3kCreditPrefs& config() const { return m_prefs; }

    [[nodiscard]] Status status() const;

    /// A new credit key replaces credit.key (a new address: no balance, immature, §2).
    /// The new address, or nullopt with @p error.
    std::optional<QString> newKey(QString* error = nullptr);
    /// Replaces credit.key with @p key (32 bytes, a secret; the caller wipes it).
    std::optional<QString> importKey(const QByteArray& key, QString* error = nullptr);
    /// The 32 bytes of credit.key (a secret: the caller wipes it); empty when there is none.
    [[nodiscard]] QByteArray exportKey() const;

    /// "0x" + 40 lowercase hex.
    [[nodiscard]] static QString addressText(const ::ed3k::event::CreditAddress& addr);
    /// "0x" + 40 hex (either case) → the address; nullopt otherwise.
    [[nodiscard]] static std::optional<::ed3k::event::CreditAddress> parseAddress(const QString& text);
    /// The key file's name in Config/ed3k.
    [[nodiscard]] static QString keyFileName();

signals:
    /// Status changed (at most once a second for the counters).
    void statusChanged();

private:
    void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onSessionStatus();
    /// Configures the running node when what it was configured with changed.
    void apply();
    void applyMarket();
    void bid(const ::ed3k::Root& root);
    void unconfigure(const QString& why);
    /// Reads credit.key, making it when @p create; nullopt when missing or not a key.
    [[nodiscard]] std::optional<::ed3k::Key32> loadKey(bool create, QString* error) const;
    void readAddress();
    void changed(bool now = false);

    Ed3kSession& m_session;
    QString m_dir;
    Ed3kCreditPrefs m_prefs;
    int m_endpoints = 0;
    bool m_configured = false;
    /// what the node was configured with: chain, contract, endpoints, quorum, address
    QString m_configuredAs;
    QString m_problem;
    QString m_address;
    Status m_status;
    std::set<::ed3k::Root> m_roots;   ///< ed3k downloads seen (bidMaxPrice)
    QTimer m_changed;
};

} // namespace eMule::ed3k
