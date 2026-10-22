#pragma once

/// @file TorrentPieceGrid.h
/// @brief Square piece grid of the torrent inspector, as Transmission's PiecesView.
///
/// Up to 256 buckets in a square grid. Progress: each bucket's share done (0..255),
/// from the row's pieceMap. Available: each bucket's rarest piece in peers (0..255),
/// from GetTorrentDetails' availabilityMap.

#include <QByteArray>
#include <QWidget>

namespace eMule {

class TorrentPieceGrid : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Progress, Available };

    explicit TorrentPieceGrid(QWidget* parent = nullptr);

    void setProgressMap(const QByteArray& map);
    void setAvailabilityMap(const QByteArray& map);
    void setMode(Mode mode);
    [[nodiscard]] Mode mode() const { return m_mode; }

    /// Side of the square grid for @p buckets cells.
    [[nodiscard]] static int gridSide(int buckets);
    /// Colour of one cell. Public for tests.
    [[nodiscard]] QColor cellColor(quint8 value) const;

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int width) const override { return width; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    [[nodiscard]] const QByteArray& shown() const;

    QByteArray m_progress;
    QByteArray m_availability;
    Mode m_mode = Mode::Progress;
};

} // namespace eMule
