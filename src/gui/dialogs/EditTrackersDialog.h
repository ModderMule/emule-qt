#pragma once

/// @file EditTrackersDialog.h
/// @brief Edit Trackers… of the Torrents list: one tracker edit for all selected
///        torrents (#143), as the merged Comments sheet does for eD2K files.
///
/// The box holds the trackers every selected torrent has. Lines added go to all of
/// them, lines removed come off all of them; trackers only some have stay, unless
/// "Remove all other trackers" makes every list the box's.

#include "dialogs/RememberedSizeDialog.h"

#include <QList>
#include <QStringList>

class QCheckBox;
class QLabel;
class QPlainTextEdit;

namespace eMule {

class EditTrackersDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    /// What EditTorrentTrackers sends. exact: each list becomes add.
    struct Edit {
        QStringList add;
        QStringList remove;
        bool exact = false;

        [[nodiscard]] bool isEmpty() const { return add.isEmpty() && remove.isEmpty() && !exact; }
    };

    /// @p lists: the tracker URLs of each selected torrent, tier order.
    explicit EditTrackersDialog(const QList<QStringList>& lists, QWidget* parent = nullptr);

    [[nodiscard]] Edit edit() const;

    /// URLs on every list, in the first list's order. Public for tests.
    [[nodiscard]] static QStringList commonTrackers(const QList<QStringList>& lists);
    /// The edit turning @p common into @p lines (trimmed, deduped). Public for tests.
    [[nodiscard]] static Edit editFor(const QStringList& common, const QStringList& lines, bool exact);

    void accept() override;

private:
    void updateNote();

    QStringList m_common;
    int m_torrents = 0;
    int m_others = 0;   ///< trackers only some torrents have
    QPlainTextEdit* m_edit = nullptr;
    QCheckBox* m_exact = nullptr;
    QLabel* m_note = nullptr;
};

} // namespace eMule
