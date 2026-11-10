/// @file Ed3kHidingGroup.cpp
/// @brief The "Hiding" group of the ed3k options — implementation.

#include "pch.h"
#include "dialogs/Ed3kHidingGroup.h"
#include "app/IpcClient.h"
#include "prefs/Preferences.h"
#include "IpcMessage.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

Ed3kHidingGroup::Ed3kHidingGroup(IpcClient* ipc, QWidget* parent)
    : QGroupBox(tr("Hiding"), parent)
    , m_ipc(ipc)
{
    auto* layout = new QVBoxLayout(this);

    m_hideCheck = new QCheckBox(tr("Hide my node"), this);
    m_hideCheck->setToolTip(tr("New ed3k searches, downloads and shared files use the anonymous key and reach "
                               "other nodes only through paths of hops. Files keep the key they are under: "
                               "move one in Shared Files or Transfers."));
    layout->addWidget(m_hideCheck);

    // one widget per row: the General page fits small screens (760 px)
    auto* hopsRow = new QHBoxLayout;
    hopsRow->addWidget(new QLabel(tr("Hops:"), this));
    m_hopsSpin = new QSpinBox(this);
    m_hopsSpin->setRange(1, 3);
    m_hopsSpin->setToolTip(tr("Nodes between you and the others, for every connection and search. More hops "
                              "hide better and are slower. 0 (only while forwarding for others): deniable, "
                              "not hidden - nobody stands in between."));
    hopsRow->addWidget(m_hopsSpin);
    // hops.md §2.6: the client tells the user what 0 gives
    m_zeroHint = new QLabel(tr("0 = deniable, not hidden"), this);
    m_zeroHint->setEnabled(false);
    hopsRow->addWidget(m_zeroHint);
    hopsRow->addStretch();
    layout->addLayout(hopsRow);
    m_hoppingCheck = new QCheckBox(tr("Forward traffic for others"), this);
    m_hoppingCheck->setToolTip(tr("Being a hop for other hiding nodes: their traffic covers your own."));
    layout->addWidget(m_hoppingCheck);
    auto* shareRow = new QHBoxLayout;
    shareRow->addWidget(new QLabel(tr("Share of the upload limit:"), this));
    m_shareSpin = new QSpinBox(this);
    m_shareSpin->setRange(1, 100);
    m_shareSpin->setSuffix(QStringLiteral(" %"));
    shareRow->addWidget(m_shareSpin);
    shareRow->addStretch();
    layout->addLayout(shareRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);
    m_newKeyBtn = new QPushButton(tr("New anonymous key..."), this);
    m_newKeyBtn->setToolTip(tr("A new anonymous identity. Queue places and what uploaders remember are lost."));
    connect(m_newKeyBtn, &QPushButton::clicked, this, &Ed3kHidingGroup::newAnonKey);
    layout->addWidget(m_newKeyBtn, 0, Qt::AlignLeft);

    auto* notes = new QLabel(notesText(), this);
    notes->setWordWrap(true);
    notes->setTextFormat(Qt::RichText);
    layout->addWidget(notes);

    // hops.md §1: forwarding for others is on by default while hiding is on
    connect(m_hideCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (on && !m_loading)
            m_hoppingCheck->setChecked(true);
    });
    for (QCheckBox* box : {m_hideCheck, m_hoppingCheck})
        connect(box, &QCheckBox::toggled, this, [this] {
            updateEnabled();
            if (!m_loading)
                emit changed();
        });
    for (QSpinBox* spin : {m_hopsSpin, m_shareSpin})
        connect(spin, &QSpinBox::valueChanged, this, [this] {
            if (!m_loading)
                emit changed();
        });
    applyStatus({});
    updateEnabled();
}

void Ed3kHidingGroup::load(const QCborMap& p)
{
    m_loading = true;
    m_hideCheck->setChecked(p.value(QStringLiteral("ed3kHide")).toBool(false));
    m_hoppingCheck->setChecked(p.value(QStringLiteral("ed3kHopping")).toBool(true));
    updateEnabled();   // the hops range follows hopping (0 only with it)
    m_hopsSpin->setValue(int(p.value(QStringLiteral("ed3kHops")).toInteger(1)));
    m_shareSpin->setValue(int(p.value(QStringLiteral("ed3kHopShare")).toInteger(25)));
    updateEnabled();
    m_loading = false;
}

void Ed3kHidingGroup::appendTo(IpcMessage& req) const
{
    req.append(QStringLiteral("ed3kHide"));
    req.append(m_hideCheck->isChecked());
    req.append(QStringLiteral("ed3kHops"));
    req.append(qint64(m_hopsSpin->value()));
    req.append(QStringLiteral("ed3kHopping"));
    req.append(m_hoppingCheck->isChecked());
    req.append(QStringLiteral("ed3kHopShare"));
    req.append(qint64(m_shareSpin->value()));
}

void Ed3kHidingGroup::saveLocal() const
{
    thePrefs.setEd3kHide(m_hideCheck->isChecked());
    thePrefs.setEd3kHops(m_hopsSpin->value());
    thePrefs.setEd3kHopping(m_hoppingCheck->isChecked());
    thePrefs.setEd3kHopShare(m_shareSpin->value());
}

void Ed3kHidingGroup::applyStatus(const QCborMap& h)
{
    if (h.isEmpty() || !h.value(QStringLiteral("running")).toBool()) {
        const qint64 files = h.value(QStringLiteral("anonFiles")).toInteger(0);
        m_statusLabel->setText(files > 0 ? tr("Not hiding. %n file(s) under the anonymous key wait until hiding is on.",
                                              nullptr, int(files))
                                         : tr("Not hiding."));
        return;
    }
    const qint64 built = h.value(QStringLiteral("built")).toInteger();
    const qint64 paths = h.value(QStringLiteral("paths")).toInteger();
    QString text = tr("Hiding as anonymous node %1: %2 of %3 path(s) built.")
                       .arg(h.value(QStringLiteral("nodeId")).toString())
                       .arg(built)
                       .arg(paths);
    if (h.value(QStringLiteral("waitingForHops")).toBool())
        text += QLatin1Char(' ') + tr("Waiting for enough hops (20 known nodes that forward) before the first path.");
    m_statusLabel->setText(text);
}

bool Ed3kHidingGroup::hideChecked() const
{
    return m_hideCheck->isChecked();
}

int Ed3kHidingGroup::hops() const
{
    return m_hopsSpin->value();
}

QString Ed3kHidingGroup::notesText()
{
    // hops.md §6: what it does and does not give
    return tr("<small>Hiding is a draft: its cryptography has not been reviewed yet. "
              "With 1 hop that one node sees everything: it is a relay, not anonymity. "
              "Each hop uploads every byte again: hiding is slower. "
              "Only nodes that hide make up the crowd: a small network hides little. "
              "eD2K, HTTP, hoster and LAN transfers never go through hops.</small>");
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void Ed3kHidingGroup::newAnonKey()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    // hops.md §1: a new anonymous key loses the queue places and what uploaders remember
    if (QMessageBox::question(this, tr("Hiding"),
                              tr("A new anonymous key is a new anonymous identity: queue places and what uploaders "
                                 "remember of the old one are lost, and its running downloads start over at the "
                                 "sources.\n\nMake a new anonymous key?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    m_ipc->sendRequest(IpcMessage(IpcMsgType::NewEd3kAnonKey), [self = QPointer<Ed3kHidingGroup>(this)](const IpcMessage& r) {
        if (!self)
            return;
        if (r.type() == IpcMsgType::Result && r.fieldBool(0)) {
            emit self->keyChanged();
            return;
        }
        const QString why = r.type() == IpcMsgType::Result ? r.fieldMap(1).value(QStringLiteral("error")).toString()
                                                           : r.fieldString(1);
        QMessageBox::warning(self, tr("Hiding"), tr("No new anonymous key: %1").arg(why));
    });
}

void Ed3kHidingGroup::updateEnabled()
{
    const bool on = m_hideCheck->isChecked();
    const bool hopping = m_hoppingCheck->isChecked();
    m_hopsSpin->setEnabled(on);
    // hops.md §2.6: 0 only while forwarding for others; turning that off at 0 gives 1
    m_hopsSpin->setMinimum(hopping ? 0 : 1);
    m_zeroHint->setVisible(hopping);
    m_hoppingCheck->setEnabled(on);
    m_shareSpin->setEnabled(on && hopping);
}

} // namespace eMule
