#pragma once

/// @file AccordionSidebar.h
/// @brief Outlook-bar sidebar: N groups of items, exactly one group expanded.
///
/// MorphXT's CSlideBar (srchybrid/SlideBar.cpp) in Qt terms — see
/// docs/eMuleScreensGUI/MorphXT-Options.png. Group headers at or before the expanded one
/// stack from the top; the headers after it are pinned to the bottom edge and the
/// expanded group's list fills the gap. CSlideBar computes those rectangles by hand
/// (GetGroupRect); here they fall out of a QVBoxLayout, because a hidden widget in a box
/// layout takes neither space nor spacing.
///
/// Items carry an opaque caller-chosen id rather than a row number, so the order shown
/// here is free to differ from the order the caller keeps its pages in. That is what lets
/// OptionsDialog regroup its sidebar without touching its Page enum, its stacked-page
/// order, or the page index already written to uistate.yml.

#include <QSize>
#include <QString>
#include <QWidget>

#include <vector>

class QIcon;
class QListWidget;
class QVBoxLayout;

namespace eMule {

class AccordionSidebar : public QWidget {
    Q_OBJECT

public:
    explicit AccordionSidebar(QWidget* parent = nullptr);

    /// Append a group. Returns its index, for passing back to addItem().
    int addGroup(const QString& title);

    /// Append an item to @p group. @p id is opaque and need not be unique across
    /// anything but this widget.
    void addItem(int group, const QIcon& icon, const QString& text, int id);

    [[nodiscard]] int     currentItemId() const;      ///< -1 when nothing is selected
    [[nodiscard]] QString currentItemText() const;
    [[nodiscard]] int     currentGroup() const;
    [[nodiscard]] QString groupTitle(int group) const;
    [[nodiscard]] QString currentGroupTitle() const;
    [[nodiscard]] int     itemCount() const;

    /// Ordinal of the current item counted across all groups in display order —
    /// CSlideBar::GetGlobalSelectedItem(). Nothing in this dialog needs it; it is here
    /// because a flat index is the obvious thing to reach for, and deriving it at a call
    /// site is how the two orders drift apart.
    [[nodiscard]] int currentOrdinal() const;

    /// Select the item carrying @p id, expanding its group. False for an unknown id.
    bool setCurrentItemId(int id);

    /// Expand @p group and make its remembered current item the current one — what
    /// CSlideBar::SelectGroup() does by posting WM_SBN_SELCHANGED. Anything less leaves
    /// the header stack and the visible page disagreeing.
    void setCurrentGroup(int group);

    [[nodiscard]] QSize sizeHint() const override;

signals:
    void currentItemChanged(int id);
    void currentGroupChanged(int group);

private:
    struct Group {
        QString      title;
        QWidget*     header = nullptr;
        QListWidget* list = nullptr;
    };

    void onListRowChanged(int group, int row);

    [[nodiscard]] const Group* groupAt(int index) const;

    QVBoxLayout*       m_layout = nullptr;
    std::vector<Group> m_groups;
    int                m_currentGroup = -1;
    /// Set while this widget is driving its own lists, so the currentRowChanged they
    /// emit does not re-enter as if the user had clicked. Same shape as
    /// OptionsDialog::m_loading.
    bool m_updating = false;
};

} // namespace eMule
