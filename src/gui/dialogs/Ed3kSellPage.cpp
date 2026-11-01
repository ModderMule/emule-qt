/// @file Ed3kSellPage.cpp
/// @brief Options → ed3k → Selling — implementation.

#include "pch.h"
#include "dialogs/Ed3kSellPage.h"

#include "app/IpcClient.h"
#include "dialogs/Ed3kSellDialogs.h"
#include "IpcMessage.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

QTreeWidget* makeTree(const QStringList& headers, QWidget* parent, int rows)
{
    auto* t = new QTreeWidget(parent);
    t->setHeaderLabels(headers);
    t->setRootIsDecorated(false);
    t->setUniformRowHeights(true);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->header()->setStretchLastSection(true);
    t->setFixedHeight(t->fontMetrics().lineSpacing() * rows);
    return t;
}

QString dateText(qint64 unixS)
{
    return unixS <= 0 ? QStringLiteral("-") : QLocale().toString(QDateTime::fromSecsSinceEpoch(unixS), QLocale::ShortFormat);
}

} // namespace

Ed3kSellPage::Ed3kSellPage(IpcClient* ipc, QWidget* parent)
    : QWidget(parent)
    , m_ipc(ipc)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    m_note = new QLabel(tr("Changes on this page take effect at once."), this);
    m_note->setWordWrap(true);
    layout->addWidget(m_note);

    // -- what is sold (§3): live channels from ed3k Sets → Live Streams → Sell…
    auto* saleGroup = new QGroupBox(tr("Paid subscriptions you sell"), this);
    auto* saleLayout = new QVBoxLayout(saleGroup);
    auto* saleNote = new QLabel(tr("A live channel you broadcast is sold from ed3k Sets → Live Streams → Sell…. Buyers pay "
                                   "in your own shop or by hand; you issue their passes here."),
                                saleGroup);
    saleNote->setWordWrap(true);
    saleLayout->addWidget(saleNote);
    m_sales = makeTree({tr("Title"), tr("Kind"), tr("Price"), tr("Season"), tr("Passes"), tr("Own pass until")}, saleGroup, 7);
    saleLayout->addWidget(m_sales);
    auto* saleRow = new QHBoxLayout;
    m_issue = new QPushButton(tr("Issue pass…"), saleGroup);
    m_terms = new QPushButton(tr("Change terms…"), saleGroup);
    m_season = new QPushButton(tr("New season after a leak…"), saleGroup);
    m_season->setToolTip(tr("A leaked key or a broken-into computer: from the next epoch on everything is sealed with a "
                            "new season (subscription.md §4.5)."));
    m_copyLink = new QPushButton(tr("Copy link"), saleGroup);
    saleRow->addWidget(m_issue);
    saleRow->addWidget(m_terms);
    saleRow->addWidget(m_season);
    saleRow->addStretch();
    saleRow->addWidget(m_copyLink);
    saleLayout->addLayout(saleRow);
    layout->addWidget(saleGroup);

    // -- passes issued by hand (§9.3) of the selected subscription
    auto* passGroup = new QGroupBox(tr("Passes issued"), this);
    auto* passLayout = new QVBoxLayout(passGroup);
    m_issued = makeTree({tr("Pass key"), tr("Valid until"), tr("Issued"), tr("Kind"), tr("Note")}, passGroup, 6);
    passLayout->addWidget(m_issued);
    auto* passRow = new QHBoxLayout;
    m_copyPass = new QPushButton(tr("Copy pass link"), passGroup);
    m_copyPass->setToolTip(tr("A voucher's link is not kept: it was shown once."));
    passRow->addStretch();
    passRow->addWidget(m_copyPass);
    passLayout->addLayout(passRow);
    layout->addWidget(passGroup);

    // -- defaults of a new offer
    auto* defGroup = new QGroupBox(tr("Defaults for new offers"), this);
    auto* defLayout = new QHBoxLayout(defGroup);
    m_defaults = new QLabel(defGroup);
    m_defaults->setWordWrap(true);
    auto* defBtn = new QPushButton(tr("Edit…"), defGroup);
    defLayout->addWidget(m_defaults, 1);
    defLayout->addWidget(defBtn);
    layout->addWidget(defGroup);
    layout->addStretch();

    connect(m_sales, &QTreeWidget::itemSelectionChanged, this, &Ed3kSellPage::onSaleChanged);
    connect(m_issued, &QTreeWidget::itemSelectionChanged, this, &Ed3kSellPage::updateButtons);
    connect(m_issue, &QPushButton::clicked, this, &Ed3kSellPage::issuePass);
    connect(m_terms, &QPushButton::clicked, this, &Ed3kSellPage::changeTerms);
    connect(m_season, &QPushButton::clicked, this, &Ed3kSellPage::newSeason);
    connect(defBtn, &QPushButton::clicked, this, &Ed3kSellPage::editDefaults);
    connect(m_copyLink, &QPushButton::clicked, this, [this] {
        if (const QString link = currentSale().value(QStringLiteral("link")).toString(); !link.isEmpty())
            QApplication::clipboard()->setText(link);
    });
    connect(m_copyPass, &QPushButton::clicked, this, [this] {
        const auto items = m_issued->selectedItems();
        if (!items.isEmpty() && !items.first()->data(0, Qt::UserRole).toString().isEmpty())
            QApplication::clipboard()->setText(items.first()->data(0, Qt::UserRole).toString());
    });
    if (m_ipc)
        connect(m_ipc, &IpcClient::ed3kSellingChanged, this, [this](const IpcMessage& m) { fill(m.fieldMap(0)); });
    refresh();
}

Ed3kSellPage::~Ed3kSellPage() = default;

void Ed3kSellPage::fill(const QCborMap& selling)
{
    m_running = selling.value(QStringLiteral("running")).toBool();
    m_note->setText(m_running ? tr("Changes on this page take effect at once.")
                              : tr("The ed3k node is not running: what you sell can be changed while it runs."));
    m_defaultTerms = selling.value(QStringLiteral("defaults")).toMap();
    const QString price = m_defaultTerms.value(QStringLiteral("price")).toString();
    m_defaults->setText(tr("Price: %1; passes last %n day(s).", nullptr, int(m_defaultTerms.value(QStringLiteral("passDays")).toInteger(30)))
                            .arg(price.isEmpty() ? tr("none set") : price));
    const QString keep = currentSale().value(QStringLiteral("scope")).toString();
    m_rows = selling.value(QStringLiteral("sales")).toArray();
    m_sales->clear();
    for (const auto& v : m_rows) {
        const QCborMap s = v.toMap();
        auto* it = new QTreeWidgetItem(m_sales);
        const QString scope = s.value(QStringLiteral("scope")).toString();
        it->setText(0, s.value(QStringLiteral("title")).toString());
        it->setText(1, s.value(QStringLiteral("kind")).toInteger() == 1 ? tr("Live channel") : tr("Set"));
        it->setText(2, s.value(QStringLiteral("price")).toString());
        it->setText(3, seasonText(s));
        it->setText(4, QString::number(s.value(QStringLiteral("issued")).toArray().size()));
        it->setText(5, dateText(s.value(QStringLiteral("ownUntil")).toInteger()));
        it->setToolTip(0, s.value(QStringLiteral("link")).toString());
        it->setData(0, Qt::UserRole, scope);
        if (scope == keep)
            m_sales->setCurrentItem(it);
    }
    if (!m_sales->currentItem() && m_sales->topLevelItemCount() > 0)
        m_sales->setCurrentItem(m_sales->topLevelItem(0));
    onSaleChanged();
}

QString Ed3kSellPage::seasonText(const QCborMap& s)
{
    const qint64 base = s.value(QStringLiteral("seasonBase")).toInteger();
    const qint64 n = s.value(QStringLiteral("seasonEpochs")).toInteger();
    QString text = tr("epoch %1 of %2–%3").arg(s.value(QStringLiteral("epoch")).toInteger()).arg(base).arg(base + std::max<qint64>(n, 1) - 1);
    if (s.value(QStringLiteral("planned")).toBool())
        text += tr(", next one ready");
    return text;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kSellPage::refresh()
{
    if (!m_ipc)
        return;
    m_ipc->sendRequest(IpcMessage(IpcMsgType::GetEd3kSelling), [self = QPointer(this)](const IpcMessage& r) {
        if (self && r.fieldBool(0))
            self->fill(r.fieldMap(1));
    });
}

void Ed3kSellPage::onSaleChanged()
{
    const QCborMap s = currentSale();
    m_issued->clear();
    for (const auto& v : s.value(QStringLiteral("issued")).toArray()) {
        const QCborMap p = v.toMap();
        auto* it = new QTreeWidgetItem(m_issued);
        const bool voucher = p.value(QStringLiteral("voucher")).toBool();
        it->setText(0, p.value(QStringLiteral("holder")).toString().left(16));
        it->setToolTip(0, p.value(QStringLiteral("holder")).toString());
        it->setText(1, dateText(p.value(QStringLiteral("until")).toInteger()));
        it->setText(2, dateText(p.value(QStringLiteral("issued")).toInteger()));
        const int n = int(p.value(QStringLiteral("scopes")).toArray().size());
        it->setText(3, (voucher ? tr("Voucher") : tr("For a pass key")) + (n > 1 ? tr(" (%n subscriptions)", nullptr, n) : QString()));
        it->setText(4, p.value(QStringLiteral("note")).toString());
        it->setData(0, Qt::UserRole, p.value(QStringLiteral("link")).toString());
    }
    updateButtons();
}

void Ed3kSellPage::issuePass()
{
    const QCborMap s = currentSale();
    Ed3kIssuePassDialog::run(this, m_ipc, m_rows, s.value(QStringLiteral("scope")).toString(),
                             int(m_defaultTerms.value(QStringLiteral("passDays")).toInteger(30)));
}

void Ed3kSellPage::changeTerms()
{
    const QCborMap s = currentSale();
    if (s.isEmpty() || !m_ipc)
        return;
    const auto t = Ed3kSellTermsDialog::ask(this, Ed3kSellTermsDialog::Mode::Change, s,
                                            tr("Terms of \"%1\"").arg(s.value(QStringLiteral("title")).toString()));
    if (!t)
        return;
    IpcMessage req(IpcMsgType::Ed3kSellControl);
    req.append(qint64(2));
    req.append(s.value(QStringLiteral("scope")).toString());
    req.append(*t);
    m_ipc->sendRequest(std::move(req), [self = QPointer(this)](const IpcMessage& r) {
        if (self && !r.fieldBool(0))
            self->showFailure(tr("The new terms were not taken."), r.fieldMap(1));
    });
}

void Ed3kSellPage::newSeason()
{
    const QCborMap s = currentSale();
    if (s.isEmpty() || !m_ipc)
        return;
    if (QMessageBox::question(this, tr("New Season"),
                              tr("Start a new season of \"%1\" from the next epoch?\n\nUse it when a key leaked or a computer "
                                 "holding it was broken into: content from then on is sealed with keys the leak does not open. "
                                 "Buyers get the new keys as usual.")
                                  .arg(s.value(QStringLiteral("title")).toString()))
        != QMessageBox::Yes)
        return;
    IpcMessage req(IpcMsgType::Ed3kSellControl);
    req.append(qint64(1));
    req.append(s.value(QStringLiteral("scope")).toString());
    req.append(QCborMap{});
    m_ipc->sendRequest(std::move(req), [self = QPointer(this)](const IpcMessage& r) {
        if (self && !r.fieldBool(0))
            self->showFailure(tr("The new season could not start."), r.fieldMap(1));
    });
}

void Ed3kSellPage::editDefaults()
{
    if (!m_ipc)
        return;
    const auto t = Ed3kSellTermsDialog::ask(this, Ed3kSellTermsDialog::Mode::Defaults, m_defaultTerms, tr("Defaults for New Offers"));
    if (!t)
        return;
    IpcMessage req(IpcMsgType::Ed3kSellControl);
    req.append(qint64(3));
    req.append(QString());
    req.append(*t);
    m_ipc->sendRequest(std::move(req), [self = QPointer(this)](const IpcMessage& r) {
        if (!self)
            return;
        if (!r.fieldBool(0))
            self->showFailure(tr("The defaults were not kept."), r.fieldMap(1));
        self->refresh();
    });
}

void Ed3kSellPage::updateButtons()
{
    const bool sale = !currentSale().isEmpty();
    const bool connected = m_ipc && m_running;
    m_issue->setEnabled(connected && sale);
    m_terms->setEnabled(connected && sale);
    m_season->setEnabled(connected && sale);
    m_copyLink->setEnabled(sale);
    const auto items = m_issued->selectedItems();
    m_copyPass->setEnabled(!items.isEmpty() && !items.first()->data(0, Qt::UserRole).toString().isEmpty());
}

QCborMap Ed3kSellPage::currentSale() const
{
    const QTreeWidgetItem* it = m_sales ? m_sales->currentItem() : nullptr;
    if (!it)
        return {};
    const QString scope = it->data(0, Qt::UserRole).toString();
    for (const auto& v : m_rows)
        if (v.toMap().value(QStringLiteral("scope")).toString() == scope)
            return v.toMap();
    return {};
}

void Ed3kSellPage::showFailure(const QString& what, const QCborMap& reply)
{
    const QString why = reply.value(QStringLiteral("error")).toString();
    QMessageBox::warning(this, tr("Selling"), why.isEmpty() ? what : what + QLatin1Char('\n') + why);
}

} // namespace eMule
