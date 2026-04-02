#include "pch.h"
/// @file MediaInfoPanel.cpp
/// @brief Compact media info display panel implementation.

#include "dialogs/MediaInfoPanel.h"
#include "media/MediaInfo.h"

#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

#include <QtConcurrent/QtConcurrent>

#include <cmath>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MediaInfoPanel::MediaInfoPanel(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void MediaInfoPanel::setFile(const QString& filePath, int64_t /*fileSize*/)
{
    if (filePath.isEmpty()) {
        clear();
        return;
    }

    m_currentFilePath = filePath;
    clearLabels(tr("Scanning..."));
    m_noInfoLabel->hide();
    m_infoWidget->show();

    auto* watcher = new QFutureWatcher<std::optional<MediaInfo>>(this);

    connect(watcher, &QFutureWatcher<std::optional<MediaInfo>>::finished, this,
            [this, watcher, expectedPath = filePath]() {
                auto result = watcher->result();
                watcher->deleteLater();

                // Discard stale result if user selected another file
                if (m_currentFilePath != expectedPath)
                    return;

                if (!result) {
                    m_infoWidget->hide();
                    m_noInfoLabel->setText(tr("No media information available."));
                    m_noInfoLabel->show();
                    return;
                }

                const MediaInfo& mi = *result;

                // --- General ---
                QString fmt = mi.fileFormat;
                if (!mi.mimeType.isEmpty()) {
                    if (!fmt.isEmpty())
                        fmt += QStringLiteral("; MIME type=");
                    fmt += mi.mimeType;
                }
                m_formatValue->setText(fmt.isEmpty() ? QStringLiteral("-") : fmt);

                if (mi.lengthSec > 0.0) {
                    auto totalSec = static_cast<int>(mi.lengthSec + 0.5);
                    int h = totalSec / 3600;
                    int m = (totalSec % 3600) / 60;
                    int s = totalSec % 60;
                    QString len;
                    if (h > 0)
                        len = QStringLiteral("%1:%2:%3")
                                  .arg(h)
                                  .arg(m, 2, 10, QLatin1Char('0'))
                                  .arg(s, 2, 10, QLatin1Char('0'));
                    else
                        len = QStringLiteral("%1:%2")
                                  .arg(m)
                                  .arg(s, 2, 10, QLatin1Char('0'));
                    if (mi.lengthEstimated)
                        len += QStringLiteral(" (%1)").arg(tr("estimated"));
                    m_lengthValue->setText(len);
                } else {
                    m_lengthValue->setText(QStringLiteral("-"));
                }

                // --- Video ---
                if (mi.videoStreamCount > 0) {
                    // Codec
                    QString vc = mi.video.codecName;
                    if (vc.isEmpty() && mi.video.codecTag != 0)
                        vc = videoFormatName(mi.video.codecTag);
                    m_vCodecValue->setText(vc.isEmpty() ? QStringLiteral("-") : vc);

                    // Bitrate
                    if (mi.video.bitRate > 0) {
                        uint32_t kbps = (mi.video.bitRate + 500) / 1000;
                        m_vBitrateValue->setText(QStringLiteral("%1 kbit/s").arg(kbps));
                    } else {
                        m_vBitrateValue->setText(QStringLiteral("-"));
                    }

                    // Resolution
                    if (mi.video.width > 0 && mi.video.height > 0)
                        m_vResValue->setText(QStringLiteral("%1 x %2").arg(mi.video.width).arg(mi.video.height));
                    else
                        m_vResValue->setText(QStringLiteral("-"));

                    // Aspect Ratio
                    if (mi.video.aspectRatio > 0.0) {
                        QString ar = QStringLiteral("%1").arg(mi.video.aspectRatio, 0, 'f', 3);
                        const QString known = knownAspectRatioString(mi.video.aspectRatio);
                        if (!known.isEmpty())
                            ar += QStringLiteral("  (%1)").arg(known);
                        m_vAspectValue->setText(ar);
                    } else {
                        m_vAspectValue->setText(QStringLiteral("-"));
                    }

                    // FPS
                    if (mi.video.frameRate > 0.0)
                        m_vFpsValue->setText(QStringLiteral("%1").arg(mi.video.frameRate, 0, 'f', 2));
                    else
                        m_vFpsValue->setText(QStringLiteral("-"));
                } else {
                    m_vCodecValue->setText(QStringLiteral("-"));
                    m_vBitrateValue->setText(QStringLiteral("-"));
                    m_vResValue->setText(QStringLiteral("-"));
                    m_vAspectValue->setText(QStringLiteral("-"));
                    m_vFpsValue->setText(QStringLiteral("-"));
                }

                // --- Audio ---
                if (mi.audioStreamCount > 0) {
                    // Codec
                    QString ac = mi.audio.codecName;
                    if (ac.isEmpty() && mi.audio.formatTag != 0)
                        ac = audioFormatName(mi.audio.formatTag);
                    m_aCodecValue->setText(ac.isEmpty() ? QStringLiteral("-") : ac);

                    // Bitrate
                    if (mi.audio.avgBytesPerSec > 0) {
                        uint32_t kbps = static_cast<uint32_t>((static_cast<uint64_t>(mi.audio.avgBytesPerSec) * 16 + 1000) / 2000);
                        m_aBitrateValue->setText(QStringLiteral("%1 kbit/s").arg(kbps));
                    } else {
                        m_aBitrateValue->setText(QStringLiteral("-"));
                    }

                    // Channels
                    if (mi.audio.channels > 0) {
                        switch (mi.audio.channels) {
                        case 1: m_aChannelsValue->setText(QStringLiteral("1 (Mono)")); break;
                        case 2: m_aChannelsValue->setText(QStringLiteral("2 (Stereo)")); break;
                        case 5: m_aChannelsValue->setText(QStringLiteral("5.1 (Surround)")); break;
                        default: m_aChannelsValue->setText(QString::number(mi.audio.channels)); break;
                        }
                    } else {
                        m_aChannelsValue->setText(QStringLiteral("-"));
                    }

                    // Sample Rate
                    if (mi.audio.sampleRate > 0)
                        m_aSampleValue->setText(QStringLiteral("%1 kHz").arg(mi.audio.sampleRate / 1000.0, 0, 'f', 3));
                    else
                        m_aSampleValue->setText(QStringLiteral("-"));

                    // Language
                    m_aLanguageValue->setText(mi.audio.language.isEmpty() ? QStringLiteral("-") : mi.audio.language);
                } else {
                    m_aCodecValue->setText(QStringLiteral("-"));
                    m_aBitrateValue->setText(QStringLiteral("-"));
                    m_aChannelsValue->setText(QStringLiteral("-"));
                    m_aSampleValue->setText(QStringLiteral("-"));
                    m_aLanguageValue->setText(QStringLiteral("-"));
                }

                m_noInfoLabel->hide();
                m_infoWidget->show();
            });

    watcher->setFuture(QtConcurrent::run([filePath]() -> std::optional<MediaInfo> {
        MediaInfo info;
        if (!extractMediaInfo(filePath, info))
            return std::nullopt;
        return info;
    }));
}

void MediaInfoPanel::clear()
{
    m_currentFilePath.clear();
    clearLabels();
    m_noInfoLabel->hide();
    m_infoWidget->show();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void MediaInfoPanel::clearLabels(const QString& text)
{
    m_formatValue->setText(text);
    m_lengthValue->setText(text);
    m_vCodecValue->setText(text);
    m_vBitrateValue->setText(text);
    m_vResValue->setText(text);
    m_vAspectValue->setText(text);
    m_vFpsValue->setText(text);
    m_aCodecValue->setText(text);
    m_aBitrateValue->setText(text);
    m_aChannelsValue->setText(text);
    m_aSampleValue->setText(text);
    m_aLanguageValue->setText(text);
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void MediaInfoPanel::buildUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // "No info" label (hidden by default)
    m_noInfoLabel = new QLabel(tr("No media information available."), this);
    m_noInfoLabel->setAlignment(Qt::AlignCenter);
    m_noInfoLabel->hide();
    mainLayout->addWidget(m_noInfoLabel);

    // Info container
    m_infoWidget = new QWidget(this);
    auto* infoLayout = new QVBoxLayout(m_infoWidget);
    infoLayout->setContentsMargins(4, 2, 4, 2);
    infoLayout->setSpacing(2);

    auto makeValueLabel = [this]() {
        auto* lbl = new QLabel(QStringLiteral("-"), this);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return lbl;
    };

    // --- General group ---
    auto* generalBox = new QGroupBox(tr("General"), m_infoWidget);
    auto* generalForm = new QFormLayout(generalBox);
    generalForm->setContentsMargins(6, 4, 6, 4);
    generalForm->setSpacing(2);
    m_formatValue = makeValueLabel();
    m_lengthValue = makeValueLabel();
    generalForm->addRow(tr("Format:"), m_formatValue);
    generalForm->addRow(tr("Length:"), m_lengthValue);
    infoLayout->addWidget(generalBox);

    // --- Video + Audio side by side ---
    auto* avRow = new QHBoxLayout;
    avRow->setSpacing(4);

    // Video group
    auto* videoBox = new QGroupBox(tr("Video"), m_infoWidget);
    auto* videoForm = new QFormLayout(videoBox);
    videoForm->setContentsMargins(6, 4, 6, 4);
    videoForm->setSpacing(2);
    m_vCodecValue   = makeValueLabel();
    m_vBitrateValue = makeValueLabel();
    m_vResValue     = makeValueLabel();
    m_vAspectValue  = makeValueLabel();
    m_vFpsValue     = makeValueLabel();
    videoForm->addRow(tr("Codec:"),        m_vCodecValue);
    videoForm->addRow(tr("Bitrate:"),      m_vBitrateValue);
    videoForm->addRow(tr("Resolution:"),   m_vResValue);
    videoForm->addRow(tr("Aspect Ratio:"), m_vAspectValue);
    videoForm->addRow(tr("FPS:"),          m_vFpsValue);
    avRow->addWidget(videoBox);

    // Audio group
    auto* audioBox = new QGroupBox(tr("Audio"), m_infoWidget);
    auto* audioForm = new QFormLayout(audioBox);
    audioForm->setContentsMargins(6, 4, 6, 4);
    audioForm->setSpacing(2);
    m_aCodecValue    = makeValueLabel();
    m_aBitrateValue  = makeValueLabel();
    m_aChannelsValue = makeValueLabel();
    m_aSampleValue   = makeValueLabel();
    m_aLanguageValue = makeValueLabel();
    audioForm->addRow(tr("Codec:"),       m_aCodecValue);
    audioForm->addRow(tr("Bitrate:"),     m_aBitrateValue);
    audioForm->addRow(tr("Channels:"),    m_aChannelsValue);
    audioForm->addRow(tr("Sample Rate:"), m_aSampleValue);
    audioForm->addRow(tr("Language:"),    m_aLanguageValue);
    avRow->addWidget(audioBox);

    infoLayout->addLayout(avRow);
    infoLayout->addStretch();
    mainLayout->addWidget(m_infoWidget);
}

} // namespace eMule
