/// @file Ed3kDeclineDialog.cpp
/// @brief Options → ed3k → General → Declined records — implementation.

#include "pch.h"
#include "dialogs/Ed3kDeclineDialog.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"

#include <QCborArray>
#include <QCborMap>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

Ed3kDeclineDialog::Ed3kDeclineDialog(IpcClient* ipc, QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
{
    setWindowTitle(tr("Declined records"));
    resize(560, 440);
    auto* layout = new QVBoxLayout(this);

    auto* help = new QLabel(tr("Records this node won't hold for others, won't publish for own files, and asks about "
                               "before downloading. One entry per line, # starts a comment:\n"
                               "  <64 hex digits>  a file root\n"
                               "  ed2k:<32 hex digits> or an ed2k:// file link  an eD2K hash\n"
                               "  key:<64 hex digits>  a publisher's key\n"
                               "  kind:<name or number>  a record kind (e.g. kind:NOTE)\n"
                               "  anything else  keywords, all of them in the file name"),
                            this);
    help->setWordWrap(true);
    layout->addWidget(help);

    m_edit = new QPlainTextEdit(this);
    m_edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(m_edit, 1);

    m_countsLabel = new QLabel(this);
    m_countsLabel->setWordWrap(true);
    layout->addWidget(m_countsLabel);
    m_invalidLabel = new QLabel(this);
    m_invalidLabel->setWordWrap(true);
    m_invalidLabel->setVisible(false);
    layout->addWidget(m_invalidLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    m_okBtn = buttons->button(QDialogButtonBox::Ok);
    m_applyBtn = buttons->button(QDialogButtonBox::Apply);
    connect(m_okBtn, &QPushButton::clicked, this, [this] { save(true); });
    connect(m_applyBtn, &QPushButton::clicked, this, [this] { save(false); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    const bool connected = m_ipc && m_ipc->isConnected();
    m_okBtn->setEnabled(connected);
    m_applyBtn->setEnabled(connected);
    m_countsLabel->setText(connected ? tr("Loading...") : tr("Not connected to the core."));
    refresh();
}

void Ed3kDeclineDialog::fill(const QCborMap& state, bool withText)
{
    if (withText)
        m_edit->setPlainText(state.value(QStringLiteral("text")).toString());
    QString counts = countsText(state.value(QStringLiteral("loaded")).toMap());
    if (!state.value(QStringLiteral("running")).toBool())
        counts += u' ' + tr("(the ed3k node is not running: read from the file)");
    else if (const qint64 dropped = state.value(QStringLiteral("dropped")).toInteger(); dropped > 0)
        counts += u' ' + tr("%n held record(s) dropped.", nullptr, int(dropped));
    m_countsLabel->setText(counts);
    QStringList invalid;
    for (const auto& v : state.value(QStringLiteral("invalid")).toArray())
        invalid << v.toString();
    m_invalidLabel->setVisible(!invalid.isEmpty());
    m_invalidLabel->setText(tr("Skipped, not understood: %1").arg(invalid.join(QStringLiteral("; "))));
}

QString Ed3kDeclineDialog::countsText(const QCborMap& loaded)
{
    const auto n = [&](const char* key) { return int(loaded.value(QLatin1String(key)).toInteger()); };
    return tr("Loaded: %1 roots, %2 eD2K hashes, %3 publisher keys, %4 kinds, %5 keywords.")
        .arg(n("roots"))
        .arg(n("ed2k"))
        .arg(n("keys"))
        .arg(n("kinds"))
        .arg(n("keywords"));
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kDeclineDialog::refresh()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    m_ipc->sendRequest(IpcMessage(IpcMsgType::GetEd3kDeclined), [self = QPointer<Ed3kDeclineDialog>(this)](const IpcMessage& r) {
        if (!self)
            return;
        if (r.type() == IpcMsgType::Result && r.fieldBool(0))
            self->fill(r.fieldMap(1));
        else
            self->m_countsLabel->setText(tr("The core has no ed3k node."));
    });
}

void Ed3kDeclineDialog::save(bool close)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    IpcMessage req(IpcMsgType::SetEd3kDeclined);
    req.append(m_edit->toPlainText());
    m_okBtn->setEnabled(false);
    m_applyBtn->setEnabled(false);
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kDeclineDialog>(this), close](const IpcMessage& r) {
        if (!self)
            return;
        self->m_okBtn->setEnabled(true);
        self->m_applyBtn->setEnabled(true);
        if (r.type() != IpcMsgType::Result || !r.fieldBool(0)) {
            QMessageBox::warning(self, tr("Declined records"),
                                 tr("The list was not saved: %1").arg(r.fieldMap(1).value(QStringLiteral("message")).toString()));
            return;
        }
        self->fill(r.fieldMap(1), false);
        if (close)
            self->accept();
    });
}

} // namespace eMule
