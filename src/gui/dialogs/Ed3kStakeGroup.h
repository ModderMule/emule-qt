#pragma once

/// @file Ed3kStakeGroup.h
/// @brief The "Stake" group of Options → ed3k → Credits (credit.md §6.7): the rate, "Burn for
/// stake…" and the claims of own burns with "Claim" once ready.
///
/// Acts at once over IPC (BurnEd3kStake 1023, ClaimEd3kStake 1024, GetEd3kStakeClaims 1025,
/// RefreshEd3kStakeClaims 1026; pushes PushEd3kStakeClaims 1076, PushEd3kStakeTx 1077).
/// A burn is asked first: it cannot be undone and is public (§6.7, §9). The core burns with
/// the main credit key only, never the anonymous one (§9.1).

#include <QCborMap>
#include <QGroupBox>
#include <QPointer>
#include <QString>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace eMule {

class IpcClient;

namespace Ipc {
class IpcMessage;
}

class Ed3kStakeGroup : public QGroupBox {
    Q_OBJECT

public:
    Ed3kStakeGroup(IpcClient* ipc, QWidget* parent);
    ~Ed3kStakeGroup() override;

    /// The GetEd3kStakeClaims / PushEd3kStakeClaims map (tests and the pushes).
    void fill(const QCborMap& stake);
    /// The credit map of GetEd3kStatus: the own address and whether credits are in use.
    void applyCredit(const QCborMap& credit);

    /// uint256 big-endian hex of stake units (10^-18 E3S) as E3S text.
    [[nodiscard]] static QString e3sText(const QString& hex);
    /// The notice shown before a burn (§6.7, §9).
    [[nodiscard]] static QString burnNotice();

private:
    void refresh();
    void burn();
    void claim();
    void onTx(const QCborMap& tx);
    void updateButtons();

    QPointer<IpcClient> m_ipc;
    QLabel* m_rateLabel = nullptr;
    QPushButton* m_burnBtn = nullptr;
    QPushButton* m_claimBtn = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QTreeWidget* m_tree = nullptr;
    QString m_address;          ///< own credit address: the default `to`
    bool m_configured = false;  ///< credits in use on the running node
};

} // namespace eMule
