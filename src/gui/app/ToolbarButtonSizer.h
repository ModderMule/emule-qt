#pragma once

/// @file ToolbarButtonSizer.h
/// @brief Even button widths for the main toolbar with labels under the icons.
///
/// MFC's toolbar (no BTNS_AUTOSIZE) gives every text-under-icon button the same
/// width. Qt sizes each to its label, so at large text scaling (175 % on a TV)
/// the row looked ragged. Every button gets the widest label's width when the
/// bar has room, else an even share of the bar; never less than its own label
/// needs, so no text is clipped (the » menu takes what still does not fit).

#include <QList>
#include <QObject>
#include <QPointer>

class QToolBar;

namespace eMule {

class ToolbarButtonSizer : public QObject {
    Q_OBJECT

public:
    explicit ToolbarButtonSizer(QToolBar* bar);

    /// Re-applies the widths (style, buttons, label or font changed).
    void update();

    /// Even width for buttons whose labels need @p naturals; a button never gets
    /// less than its own. Labels wider than an even share keep their width and the
    /// rest split what is left, so the row still fits @p available. 0: no room.
    [[nodiscard]] static int uniformWidth(int available, QList<int> naturals);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void scheduleUpdate();

    QPointer<QToolBar> m_bar;
    bool m_pending = false;
    bool m_applying = false;
};

} // namespace eMule
