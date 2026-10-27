#pragma once

/// @file MassRenameDialog.h
/// @brief Rename many files at once through one edit mask (MorphXT CMassRenameDialog,
///        IDD_MASSRENAME, by Mighty Knife).

#include "utils/MassRename.h"

#include <QDialog>
#include <QLineEdit>
#include <QList>

#include <utility>

class QPushButton;
class QRadioButton;
class QTextEdit;

namespace eMule {

class IpcClient;

/// The mask line. Remembers text and selection before each user change, so the
/// change can be repeated on every filename; undo goes to the dialog.
class MassRenameMaskEdit : public QLineEdit {
    Q_OBJECT

public:
    using QLineEdit::QLineEdit;

    [[nodiscard]] massrename::MaskEdit lastEdit() const;

signals:
    void undoRequested();

protected:
    bool event(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    void snapshot();

    QString m_before;
    qsizetype m_selStart = 0;
    qsizetype m_selEnd = 0;
};

class MassRenameDialog : public QDialog {
    Q_OBJECT

public:
    struct Item {
        QString hash;
        QString name;
        QString dir;    ///< duplicate check scope; empty for downloads
    };

    MassRenameDialog(QList<Item> items, IpcClient* ipc, QWidget* parent = nullptr);

    /// [hash, new name] for every file whose name changed; valid after accept().
    [[nodiscard]] QList<std::pair<QString, QString>> renames() const { return m_renames; }

    void accept() override;

protected:
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    [[nodiscard]] QStringList lines() const;
    void setLines(const QStringList& lines);
    [[nodiscard]] bool rightJustified() const;
    void onMaskEdited();
    void undo();
    void reset();
    void updateMask();
    void setJustify();
    void snapshotForUndo();
    void cleanup();
    void simpleCleanup();
    void insertColumn();

    QList<Item> m_items;
    IpcClient* m_ipc = nullptr;
    QList<std::pair<QString, QString>> m_renames;

    MassRenameMaskEdit* m_mask = nullptr;
    QRadioButton* m_leftRadio = nullptr;
    QRadioButton* m_rightRadio = nullptr;
    QTextEdit* m_newNames = nullptr;
    QTextEdit* m_oldNames = nullptr;
    QWidget* m_lastFocused = nullptr;

    // One-step undo grouped by runs of typing/deleting (MorphXT UndoBuffer)
    QString m_undoBuffer;
    bool m_inDel = false;
    bool m_lastEditWasUndo = false;
    qsizetype m_lastDelPos = -10;
    qsizetype m_lastEditPos = -10;
    bool m_syncingScroll = false;
};

} // namespace eMule
