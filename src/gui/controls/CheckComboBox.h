#pragma once

/// @file CheckComboBox.h
/// @brief Multi-select drop-down: a QComboBox whose items have check boxes. Clicking an
/// item toggles it and keeps the list open; the closed box shows a summary.

#include <QComboBox>
#include <QStringList>

class QStandardItemModel;

namespace eMule {

class CheckComboBox : public QComboBox {
    Q_OBJECT

public:
    explicit CheckComboBox(QWidget* parent = nullptr);

    /// @p value: what checkedValues() returns for the item, empty = @p text
    void addCheckItem(const QString& text, const QString& value = {}, const QString& toolTip = {});
    [[nodiscard]] QStringList checkedValues() const;
    /// Checks the items whose value is in @p values, unchecks the rest.
    void setCheckedValues(const QStringList& values);
    /// Closed-box text with nothing checked.
    void setEmptyText(const QString& text);
    /// Every item's value, in list order.
    [[nodiscard]] QStringList values() const;
    /// Del/Backspace on the highlighted item of the open list removes it (itemRemoved).
    void setRemovable(bool on) { m_removable = on; }
    /// One plain row after the items (e.g. "Reset to defaults"): click or Return emits actionTriggered.
    void setActionItem(const QString& text);
    /// Drops every check item; the action row stays.
    void clearCheckItems();

    void showPopup() override;

signals:
    void checkedChanged();
    void itemRemoved(const QString& value);
    void actionTriggered();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void toggleRow(int row);
    void removeRow(int row);
    [[nodiscard]] QString summary() const;
    [[nodiscard]] int itemCount() const;
    [[nodiscard]] bool isActionRow(int row) const { return m_hasAction && row == itemCount(); }

    QStandardItemModel* m_model;
    QString m_emptyText;
    bool m_removable = false;
    bool m_hasAction = false;
};

} // namespace eMule
