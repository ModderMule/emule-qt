#include "pch.h"
/// @file SkinColors.cpp
/// @brief Skin profile [Colors] applied to widgets.

#include "app/SkinColors.h"

#include "utils/ColorUtils.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QPalette>
#include <QScrollBar>
#include <QWidget>

namespace eMule {

namespace {

constexpr const char* kPrefixesProp = "emuleSkinPrefixes";
constexpr const char* kListProp = "emuleSkinList";
constexpr const char* kSkinnedProp = "emuleSkinned";
constexpr const char* kWatchedProp = "emuleSkinWatermarkWatched";

/// Alternate rows over a watermark: see-through, MFC lists have no stripes to hide it.
constexpr int kAlternateAlphaOverImage = 102;

/// AbstractListView state key → MFC SetSkinKey() names. Ours fall back to a close MFC list.
const QHash<QString, QStringList>& listKeys()
{
    static const QHash<QString, QStringList> keys{
        {QStringLiteral("downloads"),            {QStringLiteral("DownloadsLv")}},
        {QStringLiteral("clientsDownloading3"),  {QStringLiteral("DownloadingLv")}},
        {QStringLiteral("clientsUploading3"),    {QStringLiteral("UploadsLv")}},
        {QStringLiteral("clientsOnQueue3"),      {QStringLiteral("QueuedLv")}},
        {QStringLiteral("clientsKnown3"),        {QStringLiteral("ClientsLv")}},
        {QStringLiteral("serverList"),           {QStringLiteral("ServersLv")}},
        {QStringLiteral("sharedfiles"),          {QStringLiteral("SharedFilesLv")}},
        {QStringLiteral("searchResults"),        {QStringLiteral("SearchResultsLv")}},
        {QStringLiteral("searchResultsIndexer"), {QStringLiteral("SearchResultsLv")}},
        {QStringLiteral("kadContacts"),          {QStringLiteral("KadContactsLv")}},
        {QStringLiteral("kadSearches"),          {QStringLiteral("KadActionsLv")}},
        {QStringLiteral("ircChannelList"),       {QStringLiteral("IRCChannelsLv")}},
        // eMule Qt only
        {QStringLiteral("torrentList"),  {QStringLiteral("TorrentsLv"), QStringLiteral("DownloadsLv")}},
        {QStringLiteral("usenetQueue"),  {QStringLiteral("UsenetLv"), QStringLiteral("DownloadsLv")}},
    };
    return keys;
}

/// Alternate rows: a step off the skinned background, darker on light, lighter on dark.
QColor alternateOf(const QColor& base)
{
    return base.lightness() > 128 ? base.darker(106) : base.lighter(125);
}

} // namespace

SkinColors& SkinColors::instance()
{
    static SkinColors skin;
    return skin;
}

void SkinColors::setColors(const QHash<QString, QColor>& colors,
                           const QHash<QString, QString>& listImages)
{
    if (colors == m_colors && listImages == m_imagePaths)
        return;
    m_colors = colors;
    if (listImages != m_imagePaths) {
        m_imagePaths = listImages;
        m_images.clear();
        for (const auto& [key, path] : listImages.asKeyValueRange()) {
            // MFC LoadImageAsPARGB: alpha kept, a plain bitmap is drawn opaque
            if (QPixmap pm(path); !pm.isNull())
                m_images.insert(key, pm);
        }
    }
    emit changed();
}

std::optional<QColor> SkinColors::color(const QString& key) const
{
    const auto it = m_colors.constFind(key.toLower());
    if (it == m_colors.cend())
        return std::nullopt;
    return it.value();
}

std::optional<QColor> SkinColors::color(const QStringList& keys) const
{
    for (const QString& key : keys) {
        if (auto c = color(key))
            return c;
    }
    return std::nullopt;
}

std::optional<QColor> SkinColors::listColor(const QString& stateKey, QStringView suffix) const
{
    QStringList keys = listPrefixes(stateKey);
    for (QString& key : keys)
        key += suffix;
    return color(keys);
}

QPixmap SkinColors::watermark(const QStringList& prefixes) const
{
    for (const QString& prefix : prefixes) {
        const auto it = m_images.constFind(prefix.toLower() + QLatin1StringView("bkimg"));
        if (it != m_images.cend())
            return it.value();
    }
    return {};
}

void SkinColors::bind(QWidget* widget, const QStringList& prefixes, bool list)
{
    if (!widget)
        return;
    const bool bound = widget->property(kPrefixesProp).isValid();
    widget->setProperty(kPrefixesProp, prefixes);
    widget->setProperty(kListProp, list);
    if (list)
        watchWatermark(widget);
    if (!bound)
        connect(&instance(), &SkinColors::changed, widget, [widget] {
            apply(widget);
            // models paint skin colours too (search results)
            if (auto* view = qobject_cast<QAbstractItemView*>(widget))
                view->viewport()->update();
        });
    apply(widget);
}

void SkinColors::bindList(QWidget* view, const QString& stateKey)
{
    bind(view, listPrefixes(stateKey), true);
}

QStringList SkinColors::listPrefixes(const QString& stateKey)
{
    QStringList prefixes = listKeys().value(stateKey);
    prefixes.append(QStringLiteral("DefLv"));
    return prefixes;
}

void SkinColors::apply(QWidget* widget)
{
    const QStringList prefixes = widget->property(kPrefixesProp).toStringList();
    const auto find = [&](QLatin1StringView suffix) {
        QStringList keys;
        for (const QString& prefix : prefixes)
            keys.append(prefix + suffix);
        return instance().color(keys);
    };
    const auto bk = find(QLatin1StringView("Bk"));
    const auto fg = find(QLatin1StringView("Fg"));
    const bool list = widget->property(kListProp).toBool();
    const auto hl = list ? find(QLatin1StringView("Hl")) : std::nullopt;
    const bool image = list && !instance().watermark(prefixes).isNull();

    if (!bk && !fg && !hl && !image) {
        // only undo our own palette, never one the widget set itself
        if (widget->property(kSkinnedProp).toBool()) {
            widget->setPalette(QPalette());
            widget->setProperty(kSkinnedProp, false);
        }
        return;
    }

    // An empty palette resolves nothing: unset roles keep following the theme.
    // Text and Highlight leave the Disabled group to the theme.
    QPalette pal;
    constexpr QPalette::ColorGroup kEnabled[] = {QPalette::Active, QPalette::Inactive};
    if (bk) {
        pal.setColor(QPalette::Base, *bk);
        pal.setColor(QPalette::AlternateBase, alternateOf(*bk));
    }
    if (image) {
        QColor alt = bk ? alternateOf(*bk) : QApplication::palette(widget).color(QPalette::AlternateBase);
        alt.setAlpha(kAlternateAlphaOverImage);
        pal.setColor(QPalette::AlternateBase, alt);
    }
    if (fg) {
        for (const auto group : kEnabled)
            pal.setColor(group, QPalette::Text, *fg);
    }
    if (hl) {
        const QColor text = readableTextOn(*hl, QGuiApplication::palette().color(QPalette::HighlightedText));
        for (const auto group : kEnabled) {
            pal.setColor(group, QPalette::Highlight, *hl);
            pal.setColor(group, QPalette::HighlightedText, text);
        }
    }
    widget->setPalette(pal);
    widget->setProperty(kSkinnedProp, true);
}

bool SkinColors::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::Paint: {
        // Win32 LVBKIF_TYPE_WATERMARK: bottom-right of the client area, fixed while
        // scrolling. Drawn after the background fill, before the rows.
        auto* viewport = qobject_cast<QWidget*>(watched);
        auto* area = viewport ? qobject_cast<QAbstractScrollArea*>(viewport->parentWidget()) : nullptr;
        if (!area || area->viewport() != viewport || !area->property(kListProp).toBool())
            break;
        const QPixmap pm = watermark(area->property(kPrefixesProp).toStringList());
        if (pm.isNull())
            break;
        const QSize size = pm.deviceIndependentSize().toSize();
        const QRect r = viewport->rect();
        QPainter painter(viewport);
        painter.drawPixmap(QPoint(r.right() - size.width() + 1, r.bottom() - size.height() + 1), pm);
        break;
    }
    case QEvent::Resize:
        // the corner moves
        if (auto* viewport = qobject_cast<QWidget*>(watched))
            viewport->update();
        break;
    case QEvent::ApplicationPaletteChange:
        // theme switch: the see-through alternate colour comes from the theme
        if (auto* area = qobject_cast<QWidget*>(watched); area && area->property(kSkinnedProp).toBool())
            apply(area);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void SkinColors::watchWatermark(QWidget* widget)
{
    auto* area = qobject_cast<QAbstractScrollArea*>(widget);
    if (!area || area->property(kWatchedProp).toBool())
        return;
    area->setProperty(kWatchedProp, true);
    area->installEventFilter(&instance());
    area->viewport()->installEventFilter(&instance());
    // Scrolling blits the rows, image included; repaint all to keep it in its corner
    const auto repaint = [area] {
        if (area->property(kListProp).toBool()
            && !instance().watermark(area->property(kPrefixesProp).toStringList()).isNull())
            area->viewport()->update();
    };
    connect(area->verticalScrollBar(), &QScrollBar::valueChanged, area, repaint);
    connect(area->horizontalScrollBar(), &QScrollBar::valueChanged, area, repaint);
}

} // namespace eMule
