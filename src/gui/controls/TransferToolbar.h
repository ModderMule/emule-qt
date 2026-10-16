#pragma once

/// @file TransferToolbar.h
/// @brief Section header toolbar widget for the Transfer panel.
///
/// Replicates the MFC m_btnWnd1 / m_btnWnd2 header bars that show a bold
/// label with a row of toggled icon buttons for view switching.

#include <QStringList>
#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QMouseEvent;
class QToolButton;

namespace eMule {

/// A thin header toolbar with an icon, bold text label, and a row of
/// exclusive toggle buttons — matching the MFC Transfer window section headers.
class TransferToolbar : public QWidget {
    Q_OBJECT

public:
    explicit TransferToolbar(QWidget* parent = nullptr);

    /// Add a toggle button with the given icon and tooltip. Returns the button ID.
    int addButton(const QIcon& icon, const QString& tooltip);

    /// Programmatically check a button by ID.
    void checkButton(int id);

    /// Enable or disable a button by ID. A list the other pane is showing is greyed
    /// out here, matching the MFC toolbar's `fsState = 0` for unavailable lists.
    void setButtonEnabled(int id, bool on);

    /// Show or hide the row of buttons ("Show additional toolbar on Transfers
    /// window"). Without them a click on the icon or the label asks for the next
    /// list, as MFC's header does (srchybrid/TransferWnd.cpp:1345-1360).
    void setButtonsVisible(bool on);

    /// Update the bold text label (e.g. "Downloads (5)").
    void setLabelText(const QString& text);

    /// Keep the label at least as wide as the widest of @p texts (and MFC's
    /// 170 px), so the buttons after it don't shift when the view changes.
    void reserveLabelWidth(const QStringList& texts);

    /// Set the leading icon shown before the label (e.g. current view icon).
    void setLeadingIcon(const QIcon& icon);

signals:
    /// Emitted when a toolbar button is toggled on.
    void buttonClicked(int id);
    /// The header was clicked while its buttons are hidden.
    void cycleRequested();

protected:
    void changeEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void applyLabelWidth();

    QStringList m_reservedTexts;
    QHBoxLayout* m_layout = nullptr;
    QLabel* m_iconLabel = nullptr;
    QLabel* m_label = nullptr;
    QButtonGroup* m_group = nullptr;
    int m_nextId = 0;
    bool m_buttonsVisible = true;
};

} // namespace eMule
