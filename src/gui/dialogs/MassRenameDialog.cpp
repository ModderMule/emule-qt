#include "pch.h"
/// @file MassRenameDialog.cpp
/// @brief Rename many files at once through one edit mask — implementation.

#include "dialogs/MassRenameDialog.h"

#include "dialogs/SimpleCleanupDialog.h"
#include "prefs/Preferences.h"
#include "utils/DialogSizing.h"
#include "utils/OtherFunctions.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextEdit>
#include <QVBoxLayout>

namespace eMule {

using namespace massrename;

// ---------------------------------------------------------------------------
// MassRenameMaskEdit
// ---------------------------------------------------------------------------

MaskEdit MassRenameMaskEdit::lastEdit() const
{
    MaskEdit e;
    e.before = m_before;
    e.start1 = m_selStart;
    e.end1 = m_selEnd;
    e.after = text();
    e.start2 = hasSelectedText() ? selectionStart() : cursorPosition();
    e.end2 = hasSelectedText() ? selectionEnd() : cursorPosition();
    return e;
}

bool MassRenameMaskEdit::event(QEvent* e)
{
    // Every way text can change: keys, IME, drops, middle-click paste
    switch (e->type()) {
    case QEvent::KeyPress:
    case QEvent::InputMethod:
    case QEvent::Drop:
    case QEvent::MouseButtonPress:
        snapshot();
        break;
    default:
        break;
    }
    return QLineEdit::event(e);
}

void MassRenameMaskEdit::keyPressEvent(QKeyEvent* e)
{
    if (e->matches(QKeySequence::Undo) || e->matches(QKeySequence::Redo)) {
        emit undoRequested();
        return;
    }
    QLineEdit::keyPressEvent(e);
}

void MassRenameMaskEdit::contextMenuEvent(QContextMenuEvent* e)
{
    snapshot();
    QMenu* menu = createStandardContextMenu();
    // The line's own undo would only undo the mask, not the filenames
    for (QAction* act : menu->actions()) {
        if (act->objectName() == u"edit-undo" || act->objectName() == u"edit-redo") {
            act->disconnect();
            act->setEnabled(true);
            connect(act, &QAction::triggered, this, &MassRenameMaskEdit::undoRequested);
        }
    }
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(e->globalPos());
}

void MassRenameMaskEdit::snapshot()
{
    m_before = text();
    m_selStart = hasSelectedText() ? selectionStart() : cursorPosition();
    m_selEnd = hasSelectedText() ? selectionEnd() : cursorPosition();
}

// ---------------------------------------------------------------------------
// MassRenameDialog
// ---------------------------------------------------------------------------

MassRenameDialog::MassRenameDialog(QList<Item> items, IpcClient* ipc, QWidget* parent)
    : QDialog(parent)
    , m_items(std::move(items))
    , m_ipc(ipc)
{
    setWindowTitle(tr("Mass-Rename"));
    setSizeGripEnabled(true);
    auto* root = new QVBoxLayout(this);

    root->addWidget(new QLabel(tr("Filename mask (changes here will be done to all filenames below):"), this));
    auto* maskRow = new QHBoxLayout;
    m_mask = new MassRenameMaskEdit(this);
    auto* resetBtn = new QPushButton(tr("&Reset"), this);
    resetBtn->setAutoDefault(false);
    maskRow->addWidget(m_mask, 1);
    maskRow->addWidget(resetBtn);
    root->addLayout(maskRow);

    auto* justifyRow = new QHBoxLayout;
    m_leftRadio = new QRadioButton(tr("Filenames left-justified"), this);
    m_rightRadio = new QRadioButton(tr("Filenames right-justified"), this);
    m_leftRadio->setChecked(true);
    justifyRow->addWidget(m_leftRadio);
    justifyRow->addWidget(m_rightRadio);
    justifyRow->addStretch();
    root->addLayout(justifyRow);

    auto* panes = new QGridLayout;
    panes->addWidget(new QLabel(tr("New filenames:"), this), 0, 0);
    panes->addWidget(new QLabel(tr("Original filenames:"), this), 0, 1);
    m_newNames = new QTextEdit(this);
    m_oldNames = new QTextEdit(this);
    for (QTextEdit* edit : {m_newNames, m_oldNames}) {
        edit->setAcceptRichText(false);
        edit->setLineWrapMode(QTextEdit::NoWrap);
        edit->setTabChangesFocus(true);
    }
    m_oldNames->setReadOnly(true);
    panes->addWidget(m_newNames, 1, 0);
    panes->addWidget(m_oldNames, 1, 1);
    panes->setColumnStretch(0, 1);
    panes->setColumnStretch(1, 1);
    root->addLayout(panes, 1);

    auto* bottom = new QHBoxLayout;
    auto* cleanupBtn = new QPushButton(tr("&Cleanup"), this);
    auto* simpleBtn = new QPushButton(tr("&Simple cleanup"), this);
    auto* insertBtn = new QPushButton(tr("&Insert text column from clipboard"), this);
    auto* okBtn = new QPushButton(tr("OK"), this);
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    for (QPushButton* b : {cleanupBtn, simpleBtn, insertBtn, cancelBtn})
        b->setAutoDefault(false);
    okBtn->setDefault(true);
    bottom->addWidget(cleanupBtn);
    bottom->addWidget(simpleBtn);
    bottom->addWidget(insertBtn);
    bottom->addStretch();
    bottom->addWidget(okBtn);
    bottom->addWidget(cancelBtn);
    root->addLayout(bottom);

    QStringList names;
    for (const Item& it : m_items)
        names << it.name;
    m_oldNames->setPlainText(names.join(u'\n'));
    setLines(names);
    m_undoBuffer = names.join(u'\n');

    // Old and new names scroll together, line for line
    const auto sync = [this](QScrollBar* to) {
        return [this, to](int v) {
            if (m_syncingScroll)
                return;
            m_syncingScroll = true;
            to->setValue(v);
            m_syncingScroll = false;
        };
    };
    connect(m_newNames->verticalScrollBar(), &QScrollBar::valueChanged, this,
            sync(m_oldNames->verticalScrollBar()));
    connect(m_oldNames->verticalScrollBar(), &QScrollBar::valueChanged, this,
            sync(m_newNames->verticalScrollBar()));

    connect(m_mask, &QLineEdit::textEdited, this, &MassRenameDialog::onMaskEdited);
    connect(m_mask, &MassRenameMaskEdit::undoRequested, this, &MassRenameDialog::undo);
    connect(resetBtn, &QPushButton::clicked, this, &MassRenameDialog::reset);
    connect(m_leftRadio, &QRadioButton::toggled, this, &MassRenameDialog::setJustify);
    connect(cleanupBtn, &QPushButton::clicked, this, &MassRenameDialog::cleanup);
    connect(simpleBtn, &QPushButton::clicked, this, &MassRenameDialog::simpleCleanup);
    connect(insertBtn, &QPushButton::clicked, this, &MassRenameDialog::insertColumn);
    connect(okBtn, &QPushButton::clicked, this, &MassRenameDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &MassRenameDialog::reject);
    m_mask->installEventFilter(this);
    m_newNames->installEventFilter(this);

    updateMask();
    m_mask->setFocus();
    m_lastFocused = m_mask;
    DialogSizing::applySize(this, {560, 400}, {820, 520}, DialogSizing::Fit::Layout);
}

void MassRenameDialog::accept()
{
    const QStringList all = lines();
    QStringList dirs;
    for (const Item& it : m_items)
        dirs << it.dir;
    const auto checked = validateNewNames(all, dirs);
    if (!checked) {
        QString text;
        switch (checked.error().kind) {
        case NameError::NotEnough:
            text = tr("Not enough filenames in the list of new filenames. Rename not possible.");
            break;
        case NameError::Invalid:
            text = tr("Invalid filename in line %1. Rename not possible.").arg(checked.error().line);
            break;
        case NameError::Duplicate:
            text = tr("Two or more equal filenames within the same directory are not allowed (Line %1). "
                      "Rename not possible.").arg(checked.error().line);
            break;
        }
        QMessageBox::warning(this, windowTitle(), text);
        return;
    }

    m_renames.clear();
    for (qsizetype i = 0; i < m_items.size(); ++i) {
        if (checked->at(i) != m_items[i].name)
            m_renames.append({m_items[i].hash, checked->at(i)});
    }
    QDialog::accept();
}

bool MassRenameDialog::eventFilter(QObject* watched, QEvent* e)
{
    // Insert column works where the user was last; a button click steals the focus
    if (e->type() == QEvent::FocusIn) {
        if (watched == m_mask) {
            m_lastFocused = m_mask;
            updateMask();
        } else if (watched == m_newNames) {
            m_lastFocused = m_newNames;
        }
    }
    return QDialog::eventFilter(watched, e);
}

// ---------------------------------------------------------------------------

QStringList MassRenameDialog::lines() const
{
    return m_newNames->toPlainText().split(u'\n');
}

void MassRenameDialog::setLines(const QStringList& lines)
{
    const int scroll = m_newNames->verticalScrollBar()->value();
    m_newNames->setPlainText(lines.join(u'\n'));
    setJustify();
    m_newNames->verticalScrollBar()->setValue(scroll);
}

bool MassRenameDialog::rightJustified() const
{
    return m_rightRadio->isChecked();
}

void MassRenameDialog::onMaskEdited()
{
    const MaskEdit e = m_mask->lastEdit();
    const QString all = m_newNames->toPlainText();
    if (m_lastEditWasUndo) {
        m_undoBuffer = all;
        m_lastEditWasUndo = false;
    }

    // Group runs of typing or deleting into one undo step
    if (isDeletion(e)) {
        if (!m_inDel) {
            m_undoBuffer = all;
            m_inDel = true;
        } else if (e.start2 != m_lastDelPos && e.start2 != m_lastDelPos - 1) {
            m_undoBuffer = all;
        }
        m_lastDelPos = e.start2;
    } else if (e.end2 <= e.start1) {
        // A selection replaced by nothing is a delete as well
        if (!m_inDel)
            m_undoBuffer = all;
        m_inDel = true;
        m_lastDelPos = e.start1;
    } else if (m_inDel) {
        if (e.start1 != m_lastDelPos)
            m_undoBuffer = all;
        m_lastEditPos = e.start1;
        m_inDel = false;
    } else {
        if (e.start1 != m_lastEditPos + 1)
            m_undoBuffer = all;
        m_lastEditPos = e.start1;
    }

    setLines(applyMaskEdit(lines(), e, rightJustified()));
}

void MassRenameDialog::undo()
{
    // One step; a second undo redoes (MorphXT)
    const QString current = m_newNames->toPlainText();
    setLines(m_undoBuffer.split(u'\n'));
    m_undoBuffer = current;
    m_lastEditWasUndo = true;
    updateMask();
}

void MassRenameDialog::reset()
{
    const QString orig = m_oldNames->toPlainText();
    setLines(orig.split(u'\n'));
    m_undoBuffer = orig;
    m_inDel = false;
    m_lastEditWasUndo = false;
    m_lastDelPos = -10;
    updateMask();
}

void MassRenameDialog::updateMask()
{
    // setText() is not a user edit: textEdited stays quiet
    m_mask->setText(lines().value(0));
}

void MassRenameDialog::setJustify()
{
    QTextOption opt = m_newNames->document()->defaultTextOption();
    opt.setAlignment(rightJustified() ? Qt::AlignRight : Qt::AlignLeft);
    m_newNames->document()->setDefaultTextOption(opt);
}

void MassRenameDialog::snapshotForUndo()
{
    m_undoBuffer = m_newNames->toPlainText();
    m_inDel = false;
    m_lastEditWasUndo = true;
}

void MassRenameDialog::cleanup()
{
    snapshotForUndo();
    QStringList all = lines();
    const QString words = thePrefs.filenameCleanups();
    for (QString& line : all)
        line = cleanupFilename(line, words);
    setLines(all);
    updateMask();
}

void MassRenameDialog::simpleCleanup()
{
    SimpleCleanupDialog dlg(SimpleCleanupDialog::storedOptions(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const SimpleCleanupOptions opt = dlg.options();
    SimpleCleanupDialog::storeOptions(opt, m_ipc);

    snapshotForUndo();
    QStringList all = lines();
    for (QString& line : all)
        line = simpleCleanupFilename(line, opt);
    setLines(all);
    updateMask();
}

void MassRenameDialog::insertColumn()
{
    const QString clip = QApplication::clipboard()->text();
    if (clip.isEmpty())
        return;

    int startLine = 0;
    qsizetype startX = 0;
    qsizetype endX = 0;
    qsizetype refLength = 0;
    if (m_lastFocused == m_mask) {
        startX = m_mask->hasSelectedText() ? m_mask->selectionStart() : m_mask->cursorPosition();
        endX = m_mask->hasSelectedText() ? m_mask->selectionEnd() : startX;
        refLength = m_mask->text().size();
    } else {
        // At the caret in the names; a selection over several lines collapses to its start
        const QTextCursor cur = m_newNames->textCursor();
        const QTextBlock block = m_newNames->document()->findBlock(cur.selectionStart());
        startLine = block.blockNumber();
        startX = cur.selectionStart() - block.position();
        endX = m_newNames->document()->findBlock(cur.selectionEnd()) == block
             ? cur.selectionEnd() - block.position() : startX;
        refLength = block.length() - 1;
    }

    snapshotForUndo();
    setLines(massrename::insertColumn(lines(), clip, startLine, startX, endX, refLength, rightJustified()));
    updateMask();
}

} // namespace eMule
