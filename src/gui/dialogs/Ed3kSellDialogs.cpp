/// @file Ed3kSellDialogs.cpp
/// @brief The dialogs of selling paid subscriptions — implementation.

#include "pch.h"
#include "dialogs/Ed3kSellDialogs.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

/// §3: the epoch lengths offered (s); 0 = the kind's own (1 h channel, 1 day set)
constexpr int kPeriods[] = {0, 3600, 86400, 7 * 86400};

QLineEdit* urlEdit(const QString& text, QWidget* parent)
{
    auto* e = new QLineEdit(text, parent);
    e->setPlaceholderText(QStringLiteral("https://"));
    return e;
}

/// §3: https with a host, no user name or password; empty = none
bool urlOk(const QString& u)
{
    if (u.isEmpty())
        return true;
    const QUrl url(u, QUrl::StrictMode);
    return url.isValid() && url.scheme() == QLatin1String("https") && !url.host().isEmpty() && url.userInfo().isEmpty();
}

} // namespace

// ---------------------------------------------------------------------------
// Ed3kSellTermsDialog
// ---------------------------------------------------------------------------

Ed3kSellTermsDialog::Ed3kSellTermsDialog(Mode mode, const QCborMap& t, QWidget* parent)
    : QDialog(parent)
    , m_mode(mode)
{
    auto* form = new QFormLayout(this);
    m_price = new QLineEdit(t.value(QStringLiteral("price")).toString(), this);
    m_price->setMaxLength(128);
    m_price->setPlaceholderText(tr("e.g. 4.99 EUR / month"));
    m_payUrl = urlEdit(t.value(QStringLiteral("payUrl")).toString(), this);
    m_payUrl->setToolTip(tr("The page of your web shop the buyer's browser opens (subscription.md §9.1). Leave both "
                            "addresses empty to sell by hand only."));
    m_passUrl = urlEdit(t.value(QStringLiteral("passUrl")).toString(), this);
    m_passUrl->setToolTip(tr("The address of your shop the buyer's client asks for the pass after payment."));
    m_reportUrl = urlEdit(t.value(QStringLiteral("reportUrl")).toString(), this);
    m_reportUrl->setToolTip(tr("Where nodes may report a pass key used on too many computers (§5.3); empty: no reports."));
    m_terms = new QPlainTextEdit(t.value(QStringLiteral("terms")).toString(), this);
    m_terms->setFixedHeight(m_terms->fontMetrics().lineSpacing() * 4 + 10);
    m_devices = new QSpinBox(this);
    m_devices->setRange(1, 63);
    const qint64 dev = t.value(QStringLiteral("devices")).toInteger();
    m_devices->setValue(dev == 0 ? 3 : int(dev));
    m_freeAfter = new QSpinBox(this);
    m_freeAfter->setRange(0, 1 << 20);
    m_freeAfter->setSpecialValueText(tr("never"));
    m_freeAfter->setValue(int(t.value(QStringLiteral("freeAfter")).toInteger()));
    m_freeAfter->setToolTip(tr("An epoch becomes free for everyone this many epochs after it began (§4.3)."));
    form->addRow(tr("Price:"), m_price);
    form->addRow(tr("Payment page:"), m_payUrl);
    form->addRow(tr("Pass address:"), m_passUrl);
    form->addRow(tr("Share reports to:"), m_reportUrl);
    form->addRow(tr("Terms:"), m_terms);
    form->addRow(tr("Computers per pass:"), m_devices);
    form->addRow(tr("Free after (epochs):"), m_freeAfter);
    // t0 and period never change for a scope (§3): set for a new sale only
    if (mode != Mode::Change) {
        m_period = new QComboBox(this);
        m_period->addItem(mode == Mode::NewSet ? tr("Default (1 day)") : mode == Mode::NewChannel ? tr("Default (1 hour)") : tr("Default"));
        m_period->addItem(tr("1 hour"));
        m_period->addItem(tr("1 day"));
        m_period->addItem(tr("1 week"));
        const qint64 period = t.value(QStringLiteral("period")).toInteger();
        for (int i = 0; i < int(std::size(kPeriods)); ++i)
            if (kPeriods[i] == period)
                m_period->setCurrentIndex(i);
        m_period->setToolTip(tr("Buyers get a key per epoch; a pass ends on an epoch boundary. Fixed once sold."));
        m_seasonLength = new QSpinBox(this);
        m_seasonLength->setRange(0, 1 << 20);
        m_seasonLength->setSpecialValueText(tr("default (about a month)"));
        m_seasonLength->setValue(int(t.value(QStringLiteral("seasonLength")).toInteger()));
        m_seasonLength->setToolTip(tr("A leaked key opens at most one season (§4.2)."));
        form->addRow(tr("Epoch:"), m_period);
        form->addRow(tr("Season (epochs):"), m_seasonLength);
    }
    if (mode == Mode::Defaults) {
        m_passDays = new QSpinBox(this);
        m_passDays->setRange(1, 3660);
        m_passDays->setValue(int(t.value(QStringLiteral("passDays")).toInteger(30)));
        form->addRow(tr("Passes issued last (days):"), m_passDays);
    }
    auto* note = new QLabel(tr("ed3k carries no payments: you sell through your own shop or by hand and issue the "
                               "passes. An automatic pass server for the shop is not part of eMule Qt."),
                            this);
    note->setWordWrap(true);
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (mode == Mode::NewChannel || mode == Mode::NewSet)
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Sell"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
    setMinimumWidth(460);
}

QCborMap Ed3kSellTermsDialog::terms() const
{
    QCborMap m{
        {QStringLiteral("price"), m_price->text().trimmed()},
        {QStringLiteral("payUrl"), m_payUrl->text().trimmed()},
        {QStringLiteral("passUrl"), m_passUrl->text().trimmed()},
        {QStringLiteral("reportUrl"), m_reportUrl->text().trimmed()},
        {QStringLiteral("terms"), m_terms->toPlainText()},
        {QStringLiteral("devices"), m_devices->value()},
        {QStringLiteral("freeAfter"), m_freeAfter->value()},
    };
    if (m_period) {
        m.insert(QStringLiteral("period"), kPeriods[std::clamp(m_period->currentIndex(), 0, int(std::size(kPeriods)) - 1)]);
        m.insert(QStringLiteral("seasonLength"), m_seasonLength->value());
    }
    if (m_passDays)
        m.insert(QStringLiteral("passDays"), m_passDays->value());
    return m;
}

std::optional<QCborMap> Ed3kSellTermsDialog::ask(QWidget* parent, Mode mode, const QCborMap& terms, const QString& title)
{
    Ed3kSellTermsDialog d(mode, terms, parent);
    d.setWindowTitle(title);
    if (d.exec() != QDialog::Accepted)
        return std::nullopt;
    return d.terms();
}

void Ed3kSellTermsDialog::accept()
{
    QString problem;
    if (m_mode != Mode::Defaults && m_price->text().trimmed().isEmpty())
        problem = tr("The offer needs a price.");
    else if (!urlOk(m_payUrl->text().trimmed()) || !urlOk(m_passUrl->text().trimmed()) || !urlOk(m_reportUrl->text().trimmed()))
        problem = tr("The shop addresses must be https URLs with a host (no user name or password).");
    else if (m_payUrl->text().trimmed().isEmpty() != m_passUrl->text().trimmed().isEmpty())
        problem = tr("A web shop needs both the payment page and the pass address.");
    if (!problem.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), problem);
        return;
    }
    QDialog::accept();
}

// ---------------------------------------------------------------------------
// Ed3kIssuePassDialog
// ---------------------------------------------------------------------------

Ed3kIssuePassDialog::Ed3kIssuePassDialog(const QCborArray& sales, const QString& scope, int passDays, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Issue ed3k Pass"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("The pass covers:"), this));
    m_scopes = new QTreeWidget(this);
    m_scopes->setHeaderLabels({tr("Subscription"), tr("Kind"), tr("Price")});
    m_scopes->setRootIsDecorated(false);
    m_scopes->setUniformRowHeights(true);
    m_scopes->header()->setStretchLastSection(true);
    m_scopes->setFixedHeight(m_scopes->fontMetrics().lineSpacing() * 6);
    for (const auto& v : sales) {
        const QCborMap s = v.toMap();
        auto* it = new QTreeWidgetItem(m_scopes);
        it->setText(0, s.value(QStringLiteral("title")).toString());
        it->setText(1, s.value(QStringLiteral("kind")).toInteger() == 1 ? tr("Live channel") : tr("Set"));
        it->setText(2, s.value(QStringLiteral("price")).toString());
        it->setData(0, Qt::UserRole, s.value(QStringLiteral("scope")).toString());
        it->setCheckState(0, s.value(QStringLiteral("scope")).toString() == scope ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(m_scopes);
    auto* form = new QFormLayout;
    m_holder = new QLineEdit(this);
    m_holder->setPlaceholderText(tr("the buyer's pass key (52 base32 characters)"));
    m_voucher = new QCheckBox(tr("Make a voucher: the link carries a new pass key (gifts, shop sales)"), this);
    m_voucher->setToolTip(tr("Whoever has a voucher link holds the pass. A voucher used twice is one pass for two people."));
    connect(m_voucher, &QCheckBox::toggled, m_holder, &QWidget::setDisabled);
    m_until = new QDateTimeEdit(QDateTime::currentDateTime().addDays(std::max(passDays, 1)), this);
    m_until->setCalendarPopup(true);
    m_until->setMinimumDateTime(QDateTime::currentDateTime().addSecs(60));
    m_note = new QLineEdit(this);
    m_note->setPlaceholderText(tr("for your list only, e.g. the order number"));
    form->addRow(tr("Pass key:"), m_holder);
    form->addRow(m_voucher);
    form->addRow(tr("Valid until:"), m_until);
    form->addRow(tr("Note:"), m_note);
    layout->addLayout(form);
    auto* hint = new QLabel(tr("Short passes renewed often take access away soon (subscription.md §5: at most 7 days "
                               "recommended for passes bought in a shop)."),
                            this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Issue"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    setMinimumWidth(480);
}

QStringList Ed3kIssuePassDialog::scopes() const
{
    QStringList out;
    for (int i = 0; i < m_scopes->topLevelItemCount(); ++i)
        if (m_scopes->topLevelItem(i)->checkState(0) == Qt::Checked)
            out << m_scopes->topLevelItem(i)->data(0, Qt::UserRole).toString();
    return out;
}

QString Ed3kIssuePassDialog::holder() const
{
    return m_voucher->isChecked() ? QString() : m_holder->text().trimmed();
}

qint64 Ed3kIssuePassDialog::until() const
{
    return m_until->dateTime().toSecsSinceEpoch();
}

QString Ed3kIssuePassDialog::note() const
{
    return m_note->text().trimmed();
}

void Ed3kIssuePassDialog::run(QWidget* parent, IpcClient* ipc, const QCborArray& sales, const QString& scope, int passDays)
{
    if (!ipc)
        return;
    Ed3kIssuePassDialog d(sales, scope, passDays, parent);
    if (d.exec() != QDialog::Accepted)
        return;
    IpcMessage req(IpcMsgType::IssueEd3kPass);
    QCborArray scopes;
    for (const QString& s : d.scopes())
        scopes.append(s);
    req.append(scopes);
    req.append(d.holder());
    req.append(d.until());
    req.append(d.note());
    ipc->sendRequest(std::move(req), [p = QPointer(parent)](const IpcMessage& r) {
        if (!p)
            return;
        if (!r.fieldBool(0)) {
            QMessageBox::warning(p, tr("Issue ed3k Pass"),
                                 tr("The pass could not be issued.") + QLatin1Char('\n') + r.fieldMap(1).value(QStringLiteral("error")).toString());
            return;
        }
        const QCborMap m = r.fieldMap(1);
        showLink(p, m.value(QStringLiteral("link")).toString(), m.value(QStringLiteral("voucher")).toBool());
    });
}

void Ed3kIssuePassDialog::showLink(QWidget* parent, const QString& link, bool voucher)
{
    QApplication::clipboard()->setText(link);
    QDialog d(parent);
    d.setWindowTitle(tr("ed3k Pass"));
    auto* layout = new QVBoxLayout(&d);
    auto* text = new QLabel(voucher ? tr("The voucher was copied to the clipboard. Whoever has this link holds the pass: "
                                         "send it to the buyer only. It is shown once and not kept.")
                                    : tr("The pass link was copied to the clipboard. Send it to the buyer; it works only "
                                         "with the buyer's own pass key."),
                            &d);
    text->setWordWrap(true);
    layout->addWidget(text);
    auto* edit = new QPlainTextEdit(link, &d);
    edit->setReadOnly(true);
    edit->setFixedHeight(edit->fontMetrics().lineSpacing() * 5 + 10);
    layout->addWidget(edit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &d);
    auto* copy = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
    QObject::connect(copy, &QPushButton::clicked, &d, [link] { QApplication::clipboard()->setText(link); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    layout->addWidget(buttons);
    d.setMinimumWidth(480);
    d.exec();
}

void Ed3kIssuePassDialog::accept()
{
    QString problem;
    if (scopes().isEmpty())
        problem = tr("Tick at least one subscription.");
    else if (scopes().size() > 16)
        problem = tr("A pass covers at most 16 subscriptions.");
    else if (!m_voucher->isChecked() && m_holder->text().trimmed().isEmpty())
        problem = tr("Enter the buyer's pass key, or make a voucher.");
    if (!problem.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), problem);
        return;
    }
    QDialog::accept();
}

} // namespace eMule
