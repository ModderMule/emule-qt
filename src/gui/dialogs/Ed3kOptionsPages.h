#pragma once

/// @file Ed3kOptionsPages.h
/// @brief The "ed3k" group of the Options dialog: General, Connection, Credits, Names.
///
/// Kept out of OptionsDialog.cpp. The pages own their widgets; OptionsDialog adds them to
/// its stack, fills them from the daemon's preference map and collects them on OK/Apply.
/// No MFC original: laid out like the Usenet and BitTorrent pages.
/// - Credits (credit.md): off by default; switching them on shows the §9/§9.1/§10 notes and
///   asks; the key is made, imported or exported over local IPC only (1020-1022).
/// - Names (name.md): pins, petnames and followed name lists act at once over IPC
///   (1012-1019), not on OK.
/// - Credits → stake (Ed3kStakeGroup, credit.md §6.7) and Filters (Ed3kFilterPage,
///   filter-list.md) act at once over IPC; Connection → chaintube RPC endpoints on OK.
/// - Credits page is built but not in the sidebar for now (OptionsDialog::hasPage).
/// - Auto credits page (in the sidebar as "Credits"): autostart highspeed download, the
///   credit factor bid from a balance threshold down to ~20 credits (Ed3kCredit::AutoBid).
/// - Hoster (hoster.md): downloads from hosters are off by default; switching them on shows
///   what it means (§3, §8, §9). Vouched links are published by default (§4.1). Accounts are sent
///   with their password only when it was typed here; the daemon never sends one back.
///   Two tabs: General and Captcha solvers (JD's paid solver plugins, same password rule). Tabs:
///   General and Captcha solvers (JD's paid solver plugins, same password rule).
/// - Selling (Ed3kSellPage, subscription.md): paid subscriptions sold, passes issued; acts at
///   once over IPC (1033, 1034, 1040, 1041), not on OK.

#include <QCborMap>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QWidget;

namespace eMule {

class IpcClient;
struct Ed3kCreditPrefs;
struct Ed3kChainPrefs;
class Ed3kFilterPage;
class Ed3kSellPage;
class Ed3kHidingGroup;
class OptionsTree;
struct Ed3kHosterPrefs;

namespace Ipc {
class IpcMessage;
}

class Ed3kOptionsPages : public QObject {
    Q_OBJECT

public:
    /// Pages are parented to @p parent (the dialog).
    Ed3kOptionsPages(IpcClient* ipc, QWidget* parent);
    ~Ed3kOptionsPages() override;

    [[nodiscard]] QWidget* generalPage() const { return m_general; }
    [[nodiscard]] QWidget* connectionPage() const { return m_connection; }
    [[nodiscard]] QWidget* creditsPage() const { return m_credits; }
    [[nodiscard]] QWidget* autoCreditsPage() const { return m_autoCredits; }
    [[nodiscard]] QWidget* namesPage() const { return m_names; }
    [[nodiscard]] QWidget* filtersPage() const;
    /// Selling paid subscriptions (Ed3kSellPage): acts at once over IPC.
    [[nodiscard]] QWidget* sellingPage() const;
    [[nodiscard]] QWidget* hosterPage() const { return m_hoster; }
    /// Extended: an MFC-style settings tree (hashing, ...).
    [[nodiscard]] QWidget* extendedPage() const { return m_extended; }
    /// Hoster page "Open .dlc files with eMule Qt": GUI-only (uistate.yml), OptionsDialog
    /// loads and applies it with the other file types.
    [[nodiscard]] QCheckBox* associateDlcCheck() const { return m_associateDlcCheck; }
    /// GUI-only, like associateDlcCheck(): OptionsDialog loads and saves it
    [[nodiscard]] QCheckBox* clickNLoadCheck() const { return m_clickNLoadCheck; }

    /// From the GetPreferences map (keys ed3k*, nick). Signals stay quiet meanwhile.
    void load(const QCborMap& prefs);
    /// Appends key/value pairs of every ed3k preference to a SetPreferences request.
    void appendTo(Ipc::IpcMessage& req) const;
    /// Without a daemon: into thePrefs.
    void saveLocal() const;
    /// The contract address and chain ID are well-formed; else shows why and returns false.
    [[nodiscard]] bool validate();
    /// ed3k on the one eD2K TCP/UDP port (§14.1): no port of its own here; the Connection
    /// page sends ed3kUdpPort with the eD2K ports.
    [[nodiscard]] bool portJoined() const { return m_portJoined; }
    /// The eD2K TCP/UDP port the Connection page shows (note of a joined port).
    void setJoinedPort(int port);

signals:
    /// A setting was edited (OptionsDialog::markDirty).
    void changed();
    /// validate() failed on this page: OptionsDialog shows it.
    void showPage(QWidget* page);
    /// eD2K compat applied: TCP, UDP and ed3k on @p port (the Connection page joins them).
    void portsJoined(int port);

private:
    QWidget* buildGeneral(QWidget* parent);
    QWidget* buildConnection(QWidget* parent);
    QWidget* buildCredits(QWidget* parent);
    QWidget* buildNames(QWidget* parent);
    QWidget* buildHoster(QWidget* parent);
    /// GetEd3kStatus / PushEd3kStatus: running note, credit address, balance, stake.
    void applyStatus(const QCborMap& status);
    void requestStatus();
    /// MigrateEd2kCompat: the plan (apply false) or the switch, after a yes.
    void migrateEd2kCompat();
    void requestCompatPlan();
    void applyCompatPlan(const QCborMap& plan);
    /// The ed3k UDP spin or, joined, the note naming the shared port.
    void showPortJoined();
    /// The user ticked "Use credits": the notes of credit.md §9, §9.1, §10; false: not agreed.
    [[nodiscard]] bool confirmCredits();
    void newKey();
    void importKey();
    void exportKey();
    void refreshNames();
    void fillNames(const QCborMap& names);
    void addPetname();
    void followList();
    void moveList(int step);
    /// Shows a failed names/credit reply; true when it failed.
    bool failed(const Ipc::IpcMessage& reply, const QString& what);
    [[nodiscard]] QStringList extOff() const;
    [[nodiscard]] Ed3kCreditPrefs creditPrefs() const;
    // [[nodiscard]] Ed3kChainPrefs chainPrefs() const;   // ToDo: with the chaintube fields
    [[nodiscard]] bool keyActionsAllowed() const;
    [[nodiscard]] static QString notesText();
    [[nodiscard]] static QString creditsText(qint64 micro);
    /// A solver service's balance as an account status; @p balance -1: none given.
    [[nodiscard]] static QString creditText(double balance, const QString& currency);
    /// m_chainSingleNote shown for the endpoints typed.
    void updateChainNote();
    /// The "hoster" map of the ed3k status: Java runtime and resolver.
    void applyHosterStatus(const QCborMap& h);
    /// The Java buttons: only while hoster downloads are on, ticked and applied.
    void updateJavaButtons();
    /// "Hoster downloads need the ed3k node": shown while hosters are ticked and the node is not.
    void updateHosterNodeHint();
    /// Holding links back switched off: what sharing a link means (hoster.md §8); false: kept on.
    [[nodiscard]] bool confirmShareLinks();
    struct AccountList;
    /// The tree and Add/Edit/Remove/Check of @p list, into @p layout.
    /// @p stateKey: uistate.yml key of the list's column layout.
    void buildAccountList(AccountList& list, const QStringList& columns, const QString& stateKey, QVBoxLayout* layout,
                          QWidget* parent);
    /// The "Captcha solvers" tab of the Hoster page.
    void buildSolvers(QVBoxLayout* layout, QWidget* parent);
    /// Adds (@p row -1) or edits a login of @p list.
    void editAccount(AccountList& list, int row);
    void checkAccount(AccountList& list);
    void fillAccounts(AccountList& list);
    /// PushHosterAccount: the status of the rows it names.
    void applyAccountCheck(const QString& host, const QString& user, const QCborMap& result);
    [[nodiscard]] Ed3kHosterPrefs hosterPrefs() const;
    QWidget* buildAutoCredits(QWidget* parent);
    QWidget* buildExtended(QWidget* parent);
    /// m_autoNoteLabel and the group's enabled state: ed3k on, credits in use, running.
    void updateAutoCredits();

    QPointer<IpcClient> m_ipc;
    QWidget* m_parent = nullptr;
    bool m_loading = false;

    QWidget* m_general = nullptr;
    QWidget* m_connection = nullptr;
    QWidget* m_credits = nullptr;
    QWidget* m_autoCredits = nullptr;
    QWidget* m_names = nullptr;
    Ed3kFilterPage* m_filters = nullptr;
    Ed3kSellPage* m_selling = nullptr;
    QWidget* m_hoster = nullptr;
    QWidget* m_extended = nullptr;
    OptionsTree* m_extTree = nullptr;
    QTreeWidgetItem* m_hashRamItem = nullptr;
    QTreeWidgetItem* m_lowRamItem = nullptr;

    // General
    QLabel* m_runningLabel = nullptr;
    QCheckBox* m_enabledCheck = nullptr;
    QCheckBox* m_shareFilesCheck = nullptr;
    QCheckBox* m_jointDownloadsCheck = nullptr;
    QCheckBox* m_shareUsenetCheck = nullptr;
    QCheckBox* m_ownSetsWatchCheck = nullptr;   ///< ed3kOwnSetsWatch
    Ed3kHidingGroup* m_hiding = nullptr;   ///< batch 15: hops.md §1
    QList<QCheckBox*> m_extChecks;   ///< text = the extension name
    QLabel* m_compatLabel = nullptr;
    QPushButton* m_compatBtn = nullptr;

    // Connection
    QSpinBox* m_udpPortSpin = nullptr;
    QLabel* m_udpPortJoinedLabel = nullptr;
    bool m_portJoined = false;
    int m_joinedPort = 0;
    QCheckBox* m_lanCheck = nullptr;
    QSpinBox* m_sitePortSpin = nullptr;
    QPlainTextEdit* m_nodeListsEdit = nullptr;
    QCheckBox* m_nodeListAutoCheck = nullptr;
    QSpinBox* m_nodeListAgeSpin = nullptr;
    QCheckBox* m_seedEd2kCheck = nullptr;
    QPlainTextEdit* m_chaintubeRpcEdit = nullptr;
    /// one remote endpoint: no second provider (chaintube.md §4.3, batch 12 #8)
    QLabel* m_chainSingleNote = nullptr;
    /// the last chain read/write the node refused (GetEd3kStatus "chain", batch 12 #6)
    QLabel* m_chainRefusedLabel = nullptr;
    // chaintube chain (chaintube.md §4.2). ToDo: decide whether the UI edits it
    // QLineEdit* m_ctChainIdEdit = nullptr;
    // QLineEdit* m_ctContentEdit = nullptr;
    // QLineEdit* m_ctCommentEdit = nullptr;
    // QLineEdit* m_ctVoteEdit = nullptr;
    // QLineEdit* m_ctForwarderEdit = nullptr;

    // Credits
    QCheckBox* m_creditEnabledCheck = nullptr;
    QGroupBox* m_ledgerGroup = nullptr;
    QLineEdit* m_chainIdEdit = nullptr;
    QLineEdit* m_contractEdit = nullptr;
    QPlainTextEdit* m_creditRpcEdit = nullptr;
    QSpinBox* m_quorumSpin = nullptr;
    QDoubleSpinBox* m_bidSpin = nullptr;
    QCheckBox* m_sellCheck = nullptr;
    QDoubleSpinBox* m_sellFloorSpin = nullptr;
    QSpinBox* m_sellSlotsSpin = nullptr;
    QLabel* m_addressLabel = nullptr;
    QPushButton* m_newKeyBtn = nullptr;
    QPushButton* m_importKeyBtn = nullptr;
    QPushButton* m_exportKeyBtn = nullptr;
    QLabel* m_accountLabel = nullptr;

    // Auto credits
    QCheckBox* m_autoBidCheck = nullptr;
    QGroupBox* m_autoBidGroup = nullptr;   ///< enabled by m_autoBidCheck
    QDoubleSpinBox* m_autoFactorSpin = nullptr;
    QSpinBox* m_autoThresholdSpin = nullptr;
    QLabel* m_autoNoteLabel = nullptr;
    QCborMap m_creditStatus;   ///< the "credit" map of the last ed3k status

    // Names
    QLabel* m_namesNote = nullptr;
    QTreeWidget* m_pinsTree = nullptr;
    QTreeWidget* m_petnamesTree = nullptr;
    QTreeWidget* m_listsTree = nullptr;

    // Hoster
    struct HosterAccountRow {
        QString host;
        QString user;
        QString password;      ///< typed here; empty: the daemon keeps the stored one
        bool passwordSet = false;
        QString status;
    };
    QCheckBox* m_hosterEnabledCheck = nullptr;
    QLabel* m_hosterNodeLabel = nullptr;
    QLabel* m_resolverLabel = nullptr;
    QSpinBox* m_hosterStreamsSpin = nullptr;
    QCheckBox* m_hosterPuzzlesCheck = nullptr;
    QCheckBox* m_hosterPeersCheck = nullptr;
    QCheckBox* m_hosterCheckLinksCheck = nullptr;
    QCheckBox* m_hosterExtractCheck = nullptr;
    QCheckBox* m_hosterDeleteArchivesCheck = nullptr;   ///< greyed out while extraction is off
    QCheckBox* m_hosterShareCheck = nullptr;            ///< ed3kHosterShareFiles
    QCheckBox* m_associateDlcCheck = nullptr;
    QCheckBox* m_clickNLoadCheck = nullptr;
    QCheckBox* m_hosterClipboardCheck = nullptr;
    QCheckBox* m_linkGrabberCheck = nullptr;
    QCheckBox* m_grabAllCheck = nullptr;      ///< greyed out while the Link Grabber is off
    QCheckBox* m_publishLinksCheck = nullptr;
    QLabel* m_javaLabel = nullptr;
    QLabel* m_jdVersionLabel = nullptr;   ///< the resolver engine's JD revision
    QCheckBox* m_resolverAutoUpdateCheck = nullptr;
    QLineEdit* m_javaEdit = nullptr;
    QCheckBox* m_autoJavaCheck = nullptr;
    QPushButton* m_javaDownloadBtn = nullptr;
    QPushButton* m_javaLocateBtn = nullptr;
    int m_javaState = 0;          ///< Ed3kJavaRuntime::State from the status
    bool m_hosterOnInCore = false; ///< the saved switch, as the core runs it
    /// a login list: hoster accounts or captcha solver services
    struct AccountList {
        QTreeWidget* tree = nullptr;
        QList<HosterAccountRow> rows;
        bool solvers = false;   ///< solver services: user optional, credit as status
    };
    AccountList m_hosterAccounts;
    AccountList m_solverAccounts{.solvers = true};
    QCheckBox* m_solversCheck = nullptr;   ///< ed3kHosterSolversEnabled
    QGroupBox* m_solversGroup = nullptr;   ///< enabled by m_solversCheck
};

} // namespace eMule
