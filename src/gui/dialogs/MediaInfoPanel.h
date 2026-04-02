#pragma once

/// @file MediaInfoPanel.h
/// @brief Compact media info display panel for the Shared Files Content tab.
///
/// Shows General (Format, Length), Video (Codec, Bitrate, Resolution,
/// Aspect Ratio, FPS) and Audio (Codec, Bitrate, Channels, Sample Rate,
/// Language) — matching the MFC CFileInfoDialog reduced layout.

#include <QWidget>

class QLabel;

namespace eMule {

class MediaInfoPanel : public QWidget {
    Q_OBJECT
public:
    explicit MediaInfoPanel(QWidget* parent = nullptr);

    /// Set the file to analyze.  Triggers background extraction.
    void setFile(const QString& filePath, int64_t fileSize);

    /// Clear all displayed values.
    void clear();

private:
    void buildUi();
    void clearLabels(const QString& text = QStringLiteral("-"));

    // General
    QLabel* m_formatValue     = nullptr;
    QLabel* m_lengthValue     = nullptr;

    // Video
    QLabel* m_vCodecValue     = nullptr;
    QLabel* m_vBitrateValue   = nullptr;
    QLabel* m_vResValue       = nullptr;
    QLabel* m_vAspectValue    = nullptr;
    QLabel* m_vFpsValue       = nullptr;

    // Audio
    QLabel* m_aCodecValue     = nullptr;
    QLabel* m_aBitrateValue   = nullptr;
    QLabel* m_aChannelsValue  = nullptr;
    QLabel* m_aSampleValue    = nullptr;
    QLabel* m_aLanguageValue  = nullptr;

    // No-info fallback
    QLabel* m_noInfoLabel     = nullptr;

    // Widget containers for show/hide
    QWidget* m_infoWidget     = nullptr;

    QString m_currentFilePath;
};

} // namespace eMule
