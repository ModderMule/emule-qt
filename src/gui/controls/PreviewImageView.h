#pragma once

/// @file PreviewImageView.h
/// @brief Picture viewer for search Details › Images: one large image with
///        back/forward, a thumbnail strip below, and a message while loading.

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>
#include <QWidget>

class QLabel;
class QListWidget;
class QStackedWidget;
class QToolButton;

namespace eMule {

class PreviewImageView : public QWidget {
    Q_OBJECT

public:
    struct Image {
        QString name;
        QString kind;   ///< posted | frame | cover
        QByteArray data;
    };

    explicit PreviewImageView(QWidget* parent = nullptr);

    /// A centered note instead of the pictures (loading, none, error).
    void showMessage(const QString& text);
    /// Undecodable entries are dropped; none left shows @p emptyText.
    void setImages(const QList<Image>& images, const QString& emptyText);

    [[nodiscard]] int imageCount() const { return int(m_images.size()); }
    [[nodiscard]] int currentIndex() const { return m_current; }
    [[nodiscard]] QString message() const;
    void showImage(int index);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Shown {
        QString caption;
        QImage image;
    };
    void updateLarge();

    QStackedWidget* m_stack = nullptr;
    QLabel* m_message = nullptr;
    QLabel* m_large = nullptr;
    QLabel* m_caption = nullptr;
    QToolButton* m_prev = nullptr;
    QToolButton* m_next = nullptr;
    QListWidget* m_strip = nullptr;
    QList<Shown> m_images;
    int m_current = -1;
};

} // namespace eMule
