#include "pch.h"
/// @file DetailDialog.cpp
/// @brief Prev/Next walker chrome for the detail dialogs — see DetailDialog.h.

#include "DetailDialog.h"

#include "app/IpcClient.h"
#include "prefs/Preferences.h"
#include "utils/IpcFeedback.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QKeySequence>
#include <QPointer>
#include <QShortcut>
#include <QStyle>
#include <QToolButton>

#include <memory>
#include <utility>

namespace eMule {

// ── construction ───────────────────────────────────────────────────────

DetailDialog::DetailDialog(QWidget* parent)
    : QDialog(parent)
{
    auto* mainLayout = new QVBoxLayout(this);

    // Subclasses fill this later, so it has to exist — and be above the button
    // row — before their constructor body runs.
    m_contentLayout = new QVBoxLayout;
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addLayout(m_contentLayout, 1);

    buildButtonRow();   // appends its row to mainLayout
}

void DetailDialog::setWalker(DetailWalker walker)
{
    m_walker = std::move(walker);

    const bool enabled = static_cast<bool>(m_walker.step);
    m_prevButton->setVisible(enabled);
    m_nextButton->setVisible(enabled);
    refreshStepButtons();
}

bool DetailDialog::event(QEvent* event)
{
    // The list keeps polling while the dialog is in the background, so the
    // "is there a row above/below" answer can go stale. Re-probe cheaply
    // whenever the user comes back to the window.
    if (event->type() == QEvent::WindowActivate)
        refreshStepButtons();
    return QDialog::event(event);
}

// ── shared navigation wiring ───────────────────────────────────────────

void connectDetailNavigation(DetailDialog* dialog, IpcClient* ipc,
                             DetailRequestFactory makeRequest)
{
    if (!dialog || !ipc || !makeRequest)
        return;

    // One counter per dialog: a reply overtaken by a later step must not
    // overwrite the newer content (same guard as connectEd2kLinkRequests).
    auto generation = std::make_shared<int>(0);
    QPointer<DetailDialog> dlgPtr(dialog);

    QObject::connect(dialog, &DetailDialog::navigated, dialog,
        [ipc, generation, dlgPtr, makeRequest = std::move(makeRequest)](const QString& key) {
            if (!dlgPtr || !ipc->isConnected())
                return;
            const int myGeneration = ++(*generation);

            ipc->sendRequest(makeRequest(key),
                [generation, myGeneration, dlgPtr](const Ipc::IpcMessage& resp) {
                    if (!dlgPtr || myGeneration != *generation)
                        return;
                    // Result(false) and Error(code) both read as false in field 0.
                    // The list has already moved; keep the previous content rather
                    // than blanking the dialog, and tell the user it went nowhere.
                    if (!resp.fieldBool(0)) {
                        QApplication::beep();
                        return;
                    }
                    dlgPtr->setDetails(resp.field(1).toMap());
                });
        });
}

void connectDetailNavigation(DetailDialog* dialog, IpcClient* ipc,
                             Ipc::IpcMsgType detailsRequest)
{
    connectDetailNavigation(dialog, ipc, [detailsRequest](const QString& key) {
        Ipc::IpcMessage req(detailsRequest);
        req.append(key);
        return req;
    });
}

// ── shared SearchKadNotes wiring ───────────────────────────────────────

void connectKadNotesSearch(DetailDialog* dialog, IpcClient* ipc,
                           DetailRequestFactory makeRequest)
{
    if (!dialog || !ipc || !makeRequest)
        return;

    QPointer<DetailDialog> dlgPtr(dialog);

    QObject::connect(dialog, &DetailDialog::searchKadNotes, dialog,
        [ipc, dlgPtr, makeRequest = std::move(makeRequest)](const QString& fileHash,
                                                            const QString& fileName) {
            if (!ipc->isConnected())
                return;

            Ipc::IpcMessage kadMsg(Ipc::IpcMsgType::SearchKadNotes);
            kadMsg.append(fileHash);
            kadMsg.append(fileName);
            ipc->sendRequest(std::move(kadMsg),
                [ipc, dlgPtr, makeRequest, fileHash](const Ipc::IpcMessage& resp) {
                    if (!dlgPtr)
                        return;
                    // Kad down, or a notes lookup for this file is already running.
                    if (!IpcFeedback::checkOrWarn(resp, dlgPtr,
                                                  DetailDialog::tr("Search Kad")))
                        return;

                    // Notes arrive asynchronously over UDP, so re-fetch a couple of
                    // times. Both the timer and its reply re-check the subject: the
                    // walker may have moved on, and these notes belong to the file
                    // the user actually asked about.
                    auto refresh = [ipc, dlgPtr, makeRequest, fileHash]() {
                        if (!dlgPtr || !ipc->isConnected() || dlgPtr->subjectKey() != fileHash)
                            return;
                        ipc->sendRequest(makeRequest(fileHash),
                            [dlgPtr, fileHash](const Ipc::IpcMessage& r) {
                                if (dlgPtr && dlgPtr->subjectKey() == fileHash && r.fieldBool(0))
                                    dlgPtr->applyDetails(r.field(1).toMap());
                            });
                    };
                    QTimer::singleShot(8000, dlgPtr, refresh);
                    QTimer::singleShot(20000, dlgPtr, refresh);
                });
        });
}

void connectKadNotesSearch(DetailDialog* dialog, IpcClient* ipc,
                           Ipc::IpcMsgType detailsRequest)
{
    connectKadNotesSearch(dialog, ipc, [detailsRequest](const QString& key) {
        Ipc::IpcMessage req(detailsRequest);
        req.append(key);
        return req;
    });
}

// ── shared comment spam-filter wiring ──────────────────────────────────

void connectCommentFilter(DetailDialog* dialog, IpcClient* ipc)
{
    if (!dialog || !ipc)
        return;

    QObject::connect(dialog, &DetailDialog::commentFilterChanged, dialog,
        [ipc](const QString& filter) {
            if (!ipc->isConnected())
                return;
            thePrefs.setCommentFilter(filter);   // keep the local mirror in step
            Ipc::IpcMessage req(Ipc::IpcMsgType::SetPreferences);
            req.append(QStringLiteral("commentFilter"));
            req.append(filter);
            ipc->sendRequest(std::move(req));
        });
}

// ── private helpers ────────────────────────────────────────────────────

void DetailDialog::buildButtonRow()
{
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);

    // Not QDialogButtonBox::ActionRole: Qt's macOS layout table banishes action
    // buttons to the far left of the row, while MFC puts them right next to OK.
    auto* row = new QHBoxLayout;
    row->addStretch(1);

    const auto* closeBtn = m_buttonBox->button(QDialogButtonBox::Close);
    const int navWidth = std::max(24, closeBtn->sizeHint().width() / 2);  // MFC: rcOk.Width()/2

    const auto makeButton = [this, navWidth](QStyle::StandardPixmap pixmap,
                                             const QString& tip, int delta) {
        auto* button = new QToolButton(this);
        button->setIcon(style()->standardIcon(pixmap));
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        button->setFixedWidth(navWidth);
        button->setVisible(false);          // shown once a walker is installed
        connect(button, &QToolButton::clicked, this, [this, delta] { walk(delta); });
        return button;
    };

    m_prevButton = makeButton(QStyle::SP_ArrowUp,   tr("Previous"), -1);
    m_nextButton = makeButton(QStyle::SP_ArrowDown, tr("Next"),     +1);
    row->addWidget(m_prevButton);
    row->addWidget(m_nextButton);
    row->addSpacing(8);
    row->addWidget(m_buttonBox);

    // Window-context shortcuts, so the read-only QTextEdit on the ED2K Link tab
    // cannot swallow them. MFC has no equivalent — its Alt+P/Alt+N mnemonics are
    // destroyed the moment the arrow glyphs are set as the button text.
    connect(new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Up), this),
            &QShortcut::activated, this, [this] { walk(-1); });
    connect(new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Down), this),
            &QShortcut::activated, this, [this] { walk(+1); });

    static_cast<QVBoxLayout*>(layout())->addLayout(row);
}

void DetailDialog::walk(int delta)
{
    if (!m_walker.step)
        return;

    const QString key = m_walker.step(delta);
    if (key.isEmpty()) {
        QApplication::beep();               // MFC: MessageBeep(MB_OK) at the ends
        refreshStepButtons();               // the list moved under us — re-probe
        return;
    }

    emit navigated(key);
    refreshStepButtons();
}

void DetailDialog::refreshStepButtons()
{
    if (!m_prevButton || !m_nextButton)
        return;   // an activation event can reach us before the row is built

    if (!m_walker.canStep) {
        m_prevButton->setEnabled(static_cast<bool>(m_walker.step));
        m_nextButton->setEnabled(static_cast<bool>(m_walker.step));
        return;
    }
    m_prevButton->setEnabled(m_walker.canStep(-1));
    m_nextButton->setEnabled(m_walker.canStep(+1));
}

} // namespace eMule
