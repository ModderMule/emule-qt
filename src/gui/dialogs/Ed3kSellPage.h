#pragma once

/// @file Ed3kSellPage.h
/// @brief Options → ed3k → Selling (subscription.md §3, §4, §5, §9.3): what this node sells
/// (live channels, sets), their seasons and own pass, the passes issued by hand, and the
/// defaults of a new offer. Acts at once over IPC (1033, 1034, 1040, 1041; push 1067), not
/// on OK. A channel is sold from "ed3k Sets" → Live Streams → Sell….

#include <QCborArray>
#include <QCborMap>
#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace eMule {

class IpcClient;

class Ed3kSellPage : public QWidget {
    Q_OBJECT

public:
    Ed3kSellPage(IpcClient* ipc, QWidget* parent);
    ~Ed3kSellPage() override;

    /// The GetEd3kSelling map (tests and refreshes).
    void fill(const QCborMap& selling);

    /// "epoch 3 of 0..719" style text of a sale row
    [[nodiscard]] static QString seasonText(const QCborMap& sale);

private:
    void refresh();
    void onSaleChanged();
    void issuePass();
    void changeTerms();
    void newSeason();
    void editDefaults();
    void updateButtons();
    [[nodiscard]] QCborMap currentSale() const;
    void showFailure(const QString& what, const QCborMap& reply);

    QPointer<IpcClient> m_ipc;
    QLabel* m_note = nullptr;
    QTreeWidget* m_sales = nullptr;
    QTreeWidget* m_issued = nullptr;
    QLabel* m_defaults = nullptr;
    QPushButton* m_issue = nullptr;
    QPushButton* m_terms = nullptr;
    QPushButton* m_season = nullptr;
    QPushButton* m_copyLink = nullptr;
    QPushButton* m_copyPass = nullptr;
    QCborArray m_rows;
    QCborMap m_defaultTerms;
    bool m_running = false;
};

} // namespace eMule
