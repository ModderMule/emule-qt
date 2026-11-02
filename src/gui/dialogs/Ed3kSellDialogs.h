#pragma once

/// @file Ed3kSellDialogs.h
/// @brief The dialogs of selling paid subscriptions (subscription.md §3, §5, §9.3; IPC 1033,
/// 1034, 1040, 1041): the terms of an offer (Sell…, Change terms…, the defaults), Issue
/// pass… and the link it gives. No MFC original: laid out like eMule's small form dialogs.

#include <QCborArray>
#include <QCborMap>
#include <QDialog>
#include <QString>

#include <optional>

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTreeWidget;

namespace eMule {

class IpcClient;

/// ToDo: selling hidden in the GUI until it is decided if/how it is used. Gates Sell… /
/// Stop Selling (My ed3k Sets), the Paid kind + Terms… (new set), Sell…, Buy Pass… and
/// Enter Pass… (sets, channels).
/// The Options page is hidden in OptionsDialog (hasPage).
inline constexpr bool kEd3kSellingShown = false;

/// The terms of an offer: price, shop addresses, terms text, devices, released epochs; for a
/// new sale also the epoch and season length, for the defaults the days of a pass. A terms
/// map with `private` true (a private channel): the note that the shop's pass address must
/// send the offer (subscription.md §9.1 Ed3k-Offer: there is no channel file).
class Ed3kSellTermsDialog : public QDialog {
    Q_OBJECT

public:
    enum class Mode { NewChannel, NewSet, Change, Defaults };

    Ed3kSellTermsDialog(Mode mode, const QCborMap& terms, QWidget* parent = nullptr);
    /// The terms map of IpcProtocol (SellEd3k, Ed3kSellControl).
    [[nodiscard]] QCborMap terms() const;

    /// nullopt: cancelled.
    [[nodiscard]] static std::optional<QCborMap> ask(QWidget* parent, Mode mode, const QCborMap& terms, const QString& title);

private:
    void accept() override;

    Mode m_mode;
    QLineEdit* m_price = nullptr;
    QLineEdit* m_payUrl = nullptr;
    QLineEdit* m_passUrl = nullptr;
    QLineEdit* m_reportUrl = nullptr;
    QPlainTextEdit* m_terms = nullptr;
    QSpinBox* m_devices = nullptr;
    QSpinBox* m_freeAfter = nullptr;
    QComboBox* m_period = nullptr;
    QSpinBox* m_seasonLength = nullptr;
    QSpinBox* m_passDays = nullptr;
};

/// Issue pass…: the subscriptions (scopes) sold, the buyer's pass key or a voucher, the end.
class Ed3kIssuePassDialog : public QDialog {
    Q_OBJECT

public:
    /// @p sales: GetEd3kSelling rows; @p scope ticked first; @p passDays the default length.
    Ed3kIssuePassDialog(const QCborArray& sales, const QString& scope, int passDays, QWidget* parent = nullptr);

    [[nodiscard]] QStringList scopes() const;
    /// empty: a voucher
    [[nodiscard]] QString holder() const;
    [[nodiscard]] qint64 until() const;
    [[nodiscard]] QString note() const;

    /// Asks, sends IssueEd3kPass and shows the link (copied to the clipboard).
    static void run(QWidget* parent, IpcClient* ipc, const QCborArray& sales, const QString& scope, int passDays);
    /// The ed3k:pass link given, copied; a voucher's is a credential, shown once. @p channelLinks:
    /// the private channels' links of the pass (live.md §10: credentials too), for the buyer.
    static void showLink(QWidget* parent, const QString& link, bool voucher, const QStringList& channelLinks = {});

private:
    void accept() override;

    QTreeWidget* m_scopes = nullptr;
    QLineEdit* m_holder = nullptr;
    QCheckBox* m_voucher = nullptr;
    QDateTimeEdit* m_until = nullptr;
    QLineEdit* m_note = nullptr;
};

} // namespace eMule
