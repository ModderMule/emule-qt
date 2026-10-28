/// @file Ed3kStakeGroup.cpp
/// @brief The stake group of the ed3k Credits page — implementation.

#include "pch.h"
#include "dialogs/Ed3kStakeGroup.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"

#include <QCborArray>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>
#include <vector>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

constexpr double kMicro = 1'000'000.0;   // µ per credit
// ED3K_CREDIT_TX_*, ED3K_CREDIT_STATE_* (the GUI does not include ed3k.h)
constexpr int kTxBurn = 3;
constexpr int kStateMined = 2;
constexpr int kStateReverted = 3;
constexpr int kStateFailed = 4;
// claim states (Ed3kCredit::ClaimState)
constexpr int kClaimWaiting = 0;
constexpr int kClaimClaiming = 1;

/// Big-endian hex → decimal digits (no leading zeros; "0" for zero).
QString decimalOf(const QString& hex)
{
    std::vector<int> digits{0};   // little-endian base 10
    for (const QChar c : hex) {
        bool ok = false;
        int carry = QString(c).toInt(&ok, 16);
        if (!ok)
            return {};
        for (int& d : digits) {
            const int v = d * 16 + carry;
            d = v % 10;
            carry = v / 10;
        }
        while (carry > 0) {
            digits.push_back(carry % 10);
            carry /= 10;
        }
    }
    QString out;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it)
        out += QChar(u'0' + *it);
    return out;
}

} // namespace

Ed3kStakeGroup::Ed3kStakeGroup(IpcClient* ipc, QWidget* parent)
    : QGroupBox(tr("Stake tokens (E3S)"), parent)
    , m_ipc(ipc)
{
    auto* layout = new QVBoxLayout(this);
    m_rateLabel = new QLabel(this);
    m_rateLabel->setWordWrap(true);
    layout->addWidget(m_rateLabel);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Claim"), tr("Amount (E3S)"), tr("To"), tr("Ready"), tr("State")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setFixedHeight(m_tree->fontMetrics().lineSpacing() * 6);
    layout->addWidget(m_tree);

    auto* row = new QHBoxLayout;
    m_burnBtn = new QPushButton(tr("Burn for stake…"), this);
    m_burnBtn->setToolTip(tr("Burns credits of this credit address for stake tokens. It cannot be undone and is public."));
    m_claimBtn = new QPushButton(tr("Claim"), this);
    m_claimBtn->setToolTip(tr("Mints a ready claim to its address."));
    m_claimIdBtn = new QPushButton(tr("Claim id…"), this);
    m_claimIdBtn->setToolTip(tr("Reads any claim by its id, shows it, and mints it to its address once ready."));
    m_refreshBtn = new QPushButton(tr("Refresh"), this);
    row->addWidget(m_burnBtn);
    row->addWidget(m_claimBtn);
    row->addWidget(m_claimIdBtn);
    row->addWidget(m_refreshBtn);
    row->addStretch();
    layout->addLayout(row);

    connect(m_burnBtn, &QPushButton::clicked, this, &Ed3kStakeGroup::burn);
    connect(m_claimBtn, &QPushButton::clicked, this, &Ed3kStakeGroup::claim);
    connect(m_claimIdBtn, &QPushButton::clicked, this, &Ed3kStakeGroup::claimId);
    connect(m_refreshBtn, &QPushButton::clicked, this, [this] {
        if (m_ipc && m_ipc->isConnected())
            m_ipc->sendRequest(IpcMessage(IpcMsgType::RefreshEd3kStakeClaims));
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &Ed3kStakeGroup::updateButtons);
    if (m_ipc) {
        connect(m_ipc, &IpcClient::ed3kStakeClaimsChanged, this, [this](const IpcMessage& m) { fill(m.fieldMap(0)); });
        connect(m_ipc, &IpcClient::ed3kStakeTx, this, [this](const IpcMessage& m) { onTx(m.fieldMap(0)); });
        connect(m_ipc, &IpcClient::ed3kStakeLookup, this, [this](const IpcMessage& m) { onLookup(m.fieldMap(0)); });
        connect(m_ipc, &IpcClient::ed3kStatusChanged, this,
                [this](const IpcMessage& m) { applyCredit(m.fieldMap(0).value(QStringLiteral("credit")).toMap()); });
    }
    refresh();
    updateButtons();
}

Ed3kStakeGroup::~Ed3kStakeGroup() = default;

void Ed3kStakeGroup::fill(const QCborMap& stake)
{
    if (const QString address = stake.value(QStringLiteral("address")).toString(); !address.isEmpty())
        m_address = address;
    m_configured = stake.value(QStringLiteral("configured")).toBool(m_configured);
    const QString rate = stake.value(QStringLiteral("rate")).toString();
    const qint64 delay = stake.value(QStringLiteral("delay")).toInteger();
    if (!m_configured)
        m_rateLabel->setText(tr("Stake tokens need credits in use with ledger endpoints."));
    else if (rate.isEmpty())
        m_rateLabel->setText(tr("The stake rate is not read yet."));
    else if (decimalOf(rate) == QLatin1String("0"))
        m_rateLabel->setText(tr("Burning for stake is off on this ledger."));
    else
        m_rateLabel->setText(tr("A credit buys %1 E3S, claimable %2 day(s) after the burn.")
                                 .arg(e3sText(rate))
                                 .arg(QLocale().toString(double(delay) / 86400.0, 'g', 3)));

    const qint64 selected = m_tree->currentItem() ? m_tree->currentItem()->data(0, Qt::UserRole).toLongLong() : -1;
    m_tree->clear();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const QCborValue& v : stake.value(QStringLiteral("claims")).toArray()) {
        const QCborMap c = v.toMap();
        const qint64 id = c.value(QStringLiteral("id")).toInteger();
        const qint64 readyAt = c.value(QStringLiteral("readyAt")).toInteger();
        const int state = int(c.value(QStringLiteral("state")).toInteger());
        auto* it = new QTreeWidgetItem(m_tree);
        it->setText(0, QString::number(id));
        it->setText(1, e3sText(c.value(QStringLiteral("amount")).toString()));
        it->setText(2, c.value(QStringLiteral("to")).toString());
        it->setText(3, QLocale().toString(QDateTime::fromSecsSinceEpoch(readyAt), QLocale::ShortFormat));
        const bool ready = readyAt <= now;
        it->setText(4, state == kClaimClaiming ? tr("Claiming…")
                       : state == kClaimWaiting ? (ready ? tr("Ready") : tr("Waiting"))
                                                : tr("Paid"));
        it->setData(0, Qt::UserRole, id);
        it->setData(4, Qt::UserRole, state == kClaimWaiting && ready);
        // the transactions (batch 12 #10)
        QStringList txs;
        if (const QString b = c.value(QStringLiteral("burnTx")).toString(); !b.isEmpty())
            txs << tr("Burn: 0x%1").arg(b);
        if (const QString t = c.value(QStringLiteral("claimTx")).toString(); !t.isEmpty())
            txs << tr("Claim: 0x%1").arg(t);
        for (int col = 0; col < 5 && !txs.isEmpty(); ++col)
            it->setToolTip(col, txs.join(QLatin1Char('\n')));
        if (id == selected)
            m_tree->setCurrentItem(it);
    }
    updateButtons();
}

void Ed3kStakeGroup::applyCredit(const QCborMap& credit)
{
    m_address = credit.value(QStringLiteral("address")).toString();
    const bool configured = credit.value(QStringLiteral("configured")).toBool() && credit.value(QStringLiteral("endpoints")).toInteger() > 0;
    if (configured != m_configured) {
        m_configured = configured;
        refresh();
    }
    updateButtons();
}

QString Ed3kStakeGroup::e3sText(const QString& hex)
{
    QString d = decimalOf(hex);
    if (d.isEmpty())
        return {};
    // 18 decimals (§6.7: the token has 18)
    if (d.size() <= 18)
        d = QString(19 - d.size(), u'0') + d;
    QString whole = d.left(d.size() - 18);
    QString frac = d.right(18);
    while (frac.endsWith(u'0'))
        frac.chop(1);
    whole = QLocale().toString(whole.toULongLong());   // whole E3S fits a u64 for any real amount
    return frac.isEmpty() ? whole : whole + QLocale().decimalPoint() + frac;
}

QString Ed3kStakeGroup::burnNotice()
{
    // credit.md §6.7 and §9 "A burn is public", paraphrased
    return tr("<p><b>A burn cannot be undone.</b> The credits are gone; nothing turns stake tokens back into "
              "credits. The claim can be minted to the address once its delay has passed.</p>"
              "<p><b>A burn is public.</b> The ledger shows it for good, and it ties your credit address to the "
              "address it pays, and through the bridge to an address on the C-Chain, where an exchange may know "
              "your name. To avoid that link, burn to a new address and move nothing back to the credit "
              "address.</p>"
              "<p>Only your node's own credit address burns, never an anonymous one.</p>");
}

QString Ed3kStakeGroup::lookupText(const QCborMap& lookup)
{
    const qint64 id = lookup.value(QStringLiteral("id")).toInteger();
    if (lookup.value(QStringLiteral("failed")).toBool())
        return tr("Claim %1 could not be read from the ledger. Try again later.").arg(id);
    if (!lookup.value(QStringLiteral("found")).toBool())
        return lookup.value(QStringLiteral("paid")).toBool() ? tr("Claim %1 is paid already.").arg(id)
                                                             : tr("There is no claim %1 to mint: it was never made, or it is paid already.").arg(id);
    const qint64 readyAt = lookup.value(QStringLiteral("readyAt")).toInteger();
    const QString when = QLocale().toString(QDateTime::fromSecsSinceEpoch(readyAt), QLocale::ShortFormat);
    const QString what = tr("Claim %1: %2 E3S to %3.")
                             .arg(id)
                             .arg(e3sText(lookup.value(QStringLiteral("amount")).toString()), lookup.value(QStringLiteral("to")).toString());
    return readyAt <= QDateTime::currentSecsSinceEpoch() ? what + QLatin1Char(' ') + tr("Ready since %1.").arg(when)
                                                         : what + QLatin1Char(' ') + tr("Ready at %1.").arg(when);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kStakeGroup::refresh()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    m_ipc->sendRequest(IpcMessage(IpcMsgType::GetEd3kStakeClaims), [self = QPointer<Ed3kStakeGroup>(this)](const IpcMessage& r) {
        if (self && r.type() == IpcMsgType::Result && r.fieldBool(0))
            self->fill(r.fieldMap(1));
    });
}

void Ed3kStakeGroup::burn()
{
    if (!m_ipc || !m_configured)
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Burn for stake"));
    auto* form = new QFormLayout;
    auto* amount = new QDoubleSpinBox(&dlg);
    amount->setDecimals(6);
    amount->setRange(0.000001, 1'000'000'000);
    amount->setValue(1);
    amount->setSuffix(tr(" credits"));
    form->addRow(tr("Burn:"), amount);
    auto* own = new QRadioButton(tr("To my credit address (%1)").arg(m_address), &dlg);
    auto* other = new QRadioButton(tr("To another address:"), &dlg);
    auto* address = new QLineEdit(&dlg);
    address->setPlaceholderText(QStringLiteral("0x…"));
    address->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("(0[xX][0-9a-fA-F]{0,40})?")), address));
    address->setEnabled(false);
    own->setChecked(!m_address.isEmpty());
    other->setChecked(m_address.isEmpty());
    own->setEnabled(!m_address.isEmpty());
    address->setEnabled(m_address.isEmpty());
    connect(other, &QRadioButton::toggled, address, &QLineEdit::setEnabled);
    auto* notice = new QLabel(burnNotice(), &dlg);
    notice->setWordWrap(true);
    notice->setTextFormat(Qt::RichText);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Burn…"));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(own);
    auto* otherRow = new QHBoxLayout;
    otherRow->addWidget(other);
    otherRow->addWidget(address, 1);
    layout->addLayout(otherRow);
    layout->addWidget(notice);
    layout->addWidget(buttons);
    dlg.setMinimumWidth(dlg.fontMetrics().horizontalAdvance(QLatin1Char('0')) * 70);
    if (dlg.exec() != QDialog::Accepted)
        return;

    static const QRegularExpression kAddress(QStringLiteral("^0[xX][0-9a-fA-F]{40}$"));
    const QString to = own->isChecked() ? m_address : address->text().trimmed();
    if (!kAddress.match(to).hasMatch() || QStringView(to).mid(2).count(u'0') == 40) {
        QMessageBox::warning(this, tr("Burn for stake"), tr("The address is 0x and 40 hexadecimal digits, not zero."));
        return;
    }
    const qint64 micro = qint64(std::llround(amount->value() * kMicro));
    // the second, last question: §6.7 "A burn cannot be undone"
    QMessageBox box(QMessageBox::Warning, tr("Burn for stake"),
                    tr("Burn %1 credits for stake tokens to %2?").arg(QLocale().toString(amount->value(), 'f', 6), to),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setInformativeText(tr("This cannot be undone, and it is public."));
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes)
        return;
    IpcMessage req(IpcMsgType::BurnEd3kStake);
    req.append(micro);
    req.append(to);
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kStakeGroup>(this)](const IpcMessage& r) {
        if (!self)
            return;
        if (r.type() == IpcMsgType::Result && r.fieldBool(0)) {
            self->m_rateLabel->setText(tr("The burn was sent; its claim shows here once it is mined."));
            return;
        }
        QMessageBox::warning(self, tr("Burn for stake"), tr("The burn was refused (error %1).")
                                                             .arg(r.fieldMap(1).value(QStringLiteral("error")).toInteger()));
    });
}

void Ed3kStakeGroup::claim()
{
    auto* it = m_tree->currentItem();
    if (!m_ipc || !it || !it->data(4, Qt::UserRole).toBool())
        return;
    // IpcMessage req(IpcMsgType::ClaimEd3kStake);
    // req.append(it->data(0, Qt::UserRole).toLongLong());
    // m_ipc->sendRequest(...): refused → warning (now sendClaim, shared with "Claim id…")
    sendClaim(it->data(0, Qt::UserRole).toLongLong());
}

void Ed3kStakeGroup::claimId()
{
    if (!m_ipc || !m_configured)
        return;
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Claim id"), tr("Id of the stake claim (anyone may mint it to its address):"),
                                               QLineEdit::Normal, {}, &ok)
                             .trimmed();
    if (!ok)
        return;
    const qint64 id = text.toLongLong(&ok);
    if (!ok || id <= 0) {
        QMessageBox::warning(this, tr("Claim id"), tr("A claim id is a whole number above 0."));
        return;
    }
    m_lookingFor = id;
    m_rateLabel->setText(tr("Reading claim %1…").arg(id));
    IpcMessage req(IpcMsgType::ReadEd3kStakeClaim);
    req.append(id);
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kStakeGroup>(this)](const IpcMessage& r) {
        if (!self || (r.type() == IpcMsgType::Result && r.fieldBool(0)))
            return;
        self->m_lookingFor = 0;
        QMessageBox::warning(self, tr("Claim id"), tr("The claim could not be read (error %1).")
                                                       .arg(r.fieldMap(1).value(QStringLiteral("error")).toInteger()));
    });
}

void Ed3kStakeGroup::onLookup(const QCborMap& lookup)
{
    const qint64 id = lookup.value(QStringLiteral("id")).toInteger();
    if (id == 0 || id != m_lookingFor)
        return;
    m_lookingFor = 0;
    const QString text = lookupText(lookup);
    m_rateLabel->setText(text);
    const bool ready = lookup.value(QStringLiteral("found")).toBool() && !lookup.value(QStringLiteral("failed")).toBool()
                       && lookup.value(QStringLiteral("readyAt")).toInteger() <= QDateTime::currentSecsSinceEpoch();
    if (!ready) {
        QMessageBox::information(this, tr("Claim id"), text);
        return;
    }
    QMessageBox box(QMessageBox::Question, tr("Claim id"), text, QMessageBox::Yes | QMessageBox::No, this);
    box.setInformativeText(tr("Mint it now? Your credit address pays the gas; the stake tokens go to the claim's address."));
    box.setDefaultButton(QMessageBox::Yes);
    if (box.exec() == QMessageBox::Yes)
        sendClaim(id);
}

void Ed3kStakeGroup::sendClaim(qint64 id)
{
    if (!m_ipc)
        return;
    IpcMessage req(IpcMsgType::ClaimEd3kStake);
    req.append(id);
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kStakeGroup>(this)](const IpcMessage& r) {
        if (self && !(r.type() == IpcMsgType::Result && r.fieldBool(0)))
            QMessageBox::warning(self, tr("Claim"), tr("The claim was refused (error %1).")
                                                        .arg(r.fieldMap(1).value(QStringLiteral("error")).toInteger()));
    });
}

void Ed3kStakeGroup::onTx(const QCborMap& tx)
{
    const int what = int(tx.value(QStringLiteral("what")).toInteger());
    const int state = int(tx.value(QStringLiteral("state")).toInteger());
    if (state == kStateReverted || state == kStateFailed)
        m_rateLabel->setText(what == kTxBurn ? tr("The burn did not go through (revert %1).").arg(tx.value(QStringLiteral("revert")).toInteger())
                                             : tr("The claim did not go through (revert %1).").arg(tx.value(QStringLiteral("revert")).toInteger()));
    else if (state == kStateMined)
        refresh();
}

void Ed3kStakeGroup::updateButtons()
{
    const bool connected = m_ipc && m_ipc->isConnected();
    m_burnBtn->setEnabled(connected && m_configured);
    auto* it = m_tree->currentItem();
    m_claimBtn->setEnabled(connected && m_configured && it && it->data(4, Qt::UserRole).toBool());
    m_claimIdBtn->setEnabled(connected && m_configured);
    m_refreshBtn->setEnabled(connected && m_configured);
}

} // namespace eMule
