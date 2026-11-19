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

    void showPopup() override;

signals:
    void checkedChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void toggleRow(int row);
    [[nodiscard]] QString summary() const;

    QStandardItemModel* m_model;
    QString m_emptyText;
};

} // namespace eMule
