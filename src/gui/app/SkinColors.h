#pragma once

/// @file SkinColors.h
/// @brief Skin profile [Colors] applied to lists, trees and logs.
///
/// Port of the LoadSkinColor/LoadSkinColorAlt callers in srchybrid: CMuleListCtrl::SetColors
/// (<key>Bk/Fg/Hl, else DefLv*, and the <key>BkImg watermark), CHTRichEditCtrl (<key>Fg/Bk),
/// the Statistics and shared dirs trees. Only skinned palette roles are set, the rest follow
/// the theme.

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <optional>

class QWidget;

namespace eMule {

class SkinColors : public QObject {
    Q_OBJECT

public:
    static SkinColors& instance();

    /// [Colors] of the active profile, keys lower-cased; empty = no skin.
    /// @p listImages: lower-cased <key>BkImg → image file (SkinEngine::listImages).
    void setColors(const QHash<QString, QColor>& colors,
                   const QHash<QString, QString>& listImages = {});

    [[nodiscard]] std::optional<QColor> color(const QString& key) const;
    /// First of @p keys that is set.
    [[nodiscard]] std::optional<QColor> color(const QStringList& keys) const;

    /// <prefix><suffix> of the first list prefix (listPrefixes) that has it.
    [[nodiscard]] std::optional<QColor> listColor(const QString& stateKey, QStringView suffix) const;

    /// Watermark of the first prefix with a <prefix>BkImg; null if none.
    [[nodiscard]] QPixmap watermark(const QStringList& prefixes) const;

    /// Colours @p widget from the first prefix in @p prefixes that has <prefix>Bk/Fg,
    /// now and on every profile change. @p list = an MFC CMuleListCtrl: also Hl and the
    /// BkImg watermark. Rebinding replaces.
    static void bind(QWidget* widget, const QStringList& prefixes, bool list = false);

    /// List: MFC skin key for an AbstractListView state key, falling back to DefLv.
    static void bindList(QWidget* view, const QString& stateKey);

    /// Prefixes for a list state key, e.g. "downloads" → DownloadsLv, DefLv.
    [[nodiscard]] static QStringList listPrefixes(const QString& stateKey);

signals:
    void changed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QHash<QString, QColor> m_colors;
    QHash<QString, QString> m_imagePaths;
    QHash<QString, QPixmap> m_images;   ///< same keys as m_imagePaths

    static void apply(QWidget* widget);
    static void watchWatermark(QWidget* widget);
};

} // namespace eMule
