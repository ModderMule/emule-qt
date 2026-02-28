#pragma once

/// @file KadContactHistogram.h
/// @brief Kad contact distance histogram matching MFC's CKadContactHistogramCtrl.
///
/// Displays 4096 buckets computed from the top 12 bits of 128-bit Kad contact IDs.
/// The visual style faithfully replicates the original MFC eMule histogram.

#include <QWidget>

#include <array>
#include <cstdint>
#include <vector>

namespace eMule {

struct KadContactRow;

/// Distance histogram widget — Qt port of CKadContactHistogramCtrl.
class KadContactHistogram : public QWidget {
    Q_OBJECT

public:
    explicit KadContactHistogram(QWidget* parent = nullptr);

    /// Recompute histogram from the full contacts list.
    void setContacts(const std::vector<KadContactRow>& contacts);

    /// Clear all buckets.
    void clear();

    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int kHistSize = 1 << 12;  // 4096 buckets
    std::array<uint32_t, kHistSize> m_buckets{};
    uint32_t m_contactCount = 0;
};

} // namespace eMule
