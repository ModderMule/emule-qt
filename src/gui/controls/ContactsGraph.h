#pragma once

/// @file ContactsGraph.h
/// @brief Small bar chart showing Kad contacts count over time.
///
/// Matches the "Contacts" histogram in the MFC eMule Kad tab right panel.

#include <QWidget>

#include <cstdint>
#include <deque>

namespace eMule {

/// Simple bar chart widget that plots contact counts over time.
class ContactsGraph : public QWidget {
    Q_OBJECT

public:
    explicit ContactsGraph(QWidget* parent = nullptr);

    /// Append a new sample (typically once per second or per Kad process tick).
    void addSample(int contacts);

    /// Clear all samples.
    void clearSamples();

    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int kMaxSamples = 120;
    std::deque<int> m_samples;
    int m_peakValue = 1;
};

} // namespace eMule
