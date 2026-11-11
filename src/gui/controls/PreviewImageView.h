#pragma once

/// @file PreviewImageView.h
/// @brief Picture viewer for search Details › Images: one large image with
///        back/forward, a thumbnail strip below, and a message while loading.

#include "controls/PreviewTabView.h"

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>

class QLabel;
class QListWidget;
class QToolButton;

namespace eMule {

class PreviewImageView : public PreviewTabView {
    Q_OBJECT

public:
    struct Image {
        QString name;
        QString kind;   ///< posted | frame | cover
        QByteArray data;
    };

    explicit PreviewImageView(QWidget* parent = nullptr);

    /// Undecodable entries are dropped; none left shows @p emptyText.
    void setImages(const QList<Image>& images, const QString& emptyText);

    [[nodiscard]] int imageCount() const { return int(m_images.size()); }
    [[nodiscard]] int currentIndex() const { return m_current; }
    void showImage(int index);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Shown {
        QString caption;
        QImage image;
    };
    void updateLarge();

    QLabel* m_large = nullptr;
    QLabel* m_caption = nullptr;
    QToolButton* m_prev = nullptr;
    QToolButton* m_next = nullptr;
    QListWidget* m_strip = nullptr;
    QList<Shown> m_images;
    int m_current = -1;
};

} // namespace eMule
