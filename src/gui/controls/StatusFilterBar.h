#pragma once

/// @file StatusFilterBar.h
/// @brief Row of exclusive "Name (count)" buttons filtering a list by state (Transfers, Torrents, Hoster tabs).
///
/// Filter 0 is "All". Turned off in Display options, the row hides and filters nothing,
/// keeping the checked button for when it comes back.

#include <QStringList>
#include <QWidget>

#include <span>

class QButtonGroup;

namespace eMule {

class StatusFilterBar : public QWidget {
    Q_OBJECT

public:
    /// One button per entry of @p names, the id its index; @p current starts checked (clamped).
    explicit StatusFilterBar(const QStringList& names, int current, QWidget* parent = nullptr);

    /// The checked button, shown or not: what the panel persists.
    [[nodiscard]] int current() const;
    /// The filter the list applies: current() while the row is on, else 0 ("All").
    [[nodiscard]] int activeFilter() const { return m_on ? current() : 0; }
    /// Shows or hides the row; emits filterChanged() when activeFilter() changes.
    void setRowOn(bool on);
    [[nodiscard]] int count() const { return int(m_names.size()); }
    /// Button texts become "Name (n)"; missing entries keep the plain name.
    void setCounts(std::span<const int> counts);

signals:
    /// activeFilter() changed (a click, or the row turned on or off).
    void filterChanged(int filter);

private:
    QButtonGroup* m_group = nullptr;
    QStringList m_names;
    bool m_on = true;
};

} // namespace eMule
