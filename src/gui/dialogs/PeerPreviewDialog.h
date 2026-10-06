#pragma once

/// @file PeerPreviewDialog.h
/// @brief Shows the preview frames a peer sent for one of its shared files.
///
/// The Qt counterpart of MFC's PreviewDlg: one frame at a time, Prior / Next
/// wrapping around, "Image n of m" underneath.

#include <QDialog>
#include <QImage>

#include <vector>

class QLabel;
class QPushButton;

namespace eMule {

class PeerPreviewDialog : public QDialog {
    Q_OBJECT

public:
    PeerPreviewDialog(const QString& fileName, std::vector<QImage> frames,
                      QWidget* parent = nullptr);

    [[nodiscard]] int currentFrame() const { return m_current; }
    [[nodiscard]] int frameCount() const { return static_cast<int>(m_frames.size()); }

public slots:
    /// Show frame @p index, wrapping at both ends.
    void showFrame(int index);

private:
    std::vector<QImage> m_frames;
    int m_current = 0;
    QLabel* m_image = nullptr;
    QLabel* m_counter = nullptr;
};

} // namespace eMule
