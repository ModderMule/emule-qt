#include "pch.h"
/// @file CountryFlags.cpp
/// @brief Country flag icons, names and the Country column binding.

#include "utils/CountryFlags.h"


#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QHeaderView>
#include <QLocale>
#include <QPainter>
#include <QPixmap>

namespace eMule {

namespace {

constexpr int kIconSize = 16;
constexpr int kGap = 2;
constexpr int kFlagWidth = 18;   // MorphXT flags: 18x12 plus a 2 px border top/bottom

bool s_showFlags = true;
CountryFlags::NameMode s_nameMode = CountryFlags::NameMode::Hidden;

[[nodiscard]] qreal pixelRatio()
{
    return qApp ? qApp->devicePixelRatio() : 1.0;
}

} // namespace

namespace CountryFlags {

bool showFlags()
{
    return s_showFlags;
}

NameMode nameMode()
{
    return s_nameMode;
}

void setSettings(bool showFlags, NameMode mode)
{
    if (showFlags == s_showFlags && mode == s_nameMode)
        return;
    s_showFlags = showFlags;
    s_nameMode = mode;
    notifyChanged();
}

QIcon flag(const QString& cc)
{
    static QHash<QString, QIcon> cache;
    if (cc.size() != 2)
        return {};
    const QString key = cc.toLower();
    if (const auto it = cache.constFind(key); it != cache.cend())
        return *it;

    const QString path = QStringLiteral(":/flags/%1.ico").arg(key);
    const QIcon icon = QFile::exists(path) ? QIcon(path) : QIcon();
    cache.insert(key, icon);
    return icon;
}

QString countryName(const QString& cc)
{
    if (cc.isEmpty())
        return {};
    const QLocale::Territory t = QLocale::codeToTerritory(cc.toUpper());
    if (t == QLocale::AnyTerritory)
        return cc.toUpper();
    return QLocale::territoryToString(t);
}

QString columnText(const QString& cc)
{
    switch (nameMode()) {
    case NameMode::Short: return cc.toUpper();
    case NameMode::Long:  return countryName(cc);
    case NameMode::Hidden: break;
    }
    return {};
}

QString tooltip(const QString& cc)
{
    if (cc.isEmpty())
        return {};
    return QStringLiteral("%1 (%2)").arg(countryName(cc), cc.toUpper());
}

QIcon withFlag(const QIcon& base, const QString& cc)
{
    if (!showFlags())
        return base;

    static QHash<QString, QIcon> cache;
    const QString key = QStringLiteral("%1/%2").arg(base.isNull() ? 0 : base.cacheKey())
                            .arg(cc.toLower());
    if (const auto it = cache.constFind(key); it != cache.cend())
        return *it;

    const qreal dpr = pixelRatio();
    const int flagX = base.isNull() ? 0 : kIconSize + kGap;
    const int width = flagX + kFlagWidth;
    QPixmap pm(qRound(width * dpr), qRound(kIconSize * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        if (!base.isNull())
            base.paint(&p, QRect(0, 0, kIconSize, kIconSize));
        if (const QIcon f = flag(cc); !f.isNull())
            p.drawPixmap(QRect(flagX, 0, kFlagWidth, kIconSize),
                         f.pixmap(QSize(kFlagWidth, kIconSize), dpr));
    }
    QIcon icon(pm);
    if (cache.size() > 2000)
        cache.clear();
    cache.insert(key, icon);
    return icon;
}

void bindFlagColumn(QAbstractItemView* view)
{
    view->setItemDelegateForColumn(0, new FlagDecorationDelegate(view));
    QObject::connect(CountryFlagsNotifier::instance(), &CountryFlagsNotifier::changed, view,
                     [view] {
                         // Width changes with the flag, so re-query size hints too
                         view->doItemsLayout();
                         view->viewport()->update();
                     });
}

void notifyChanged()
{
    emit CountryFlagsNotifier::instance()->changed();
}

} // namespace CountryFlags

CountryFlagsNotifier* CountryFlagsNotifier::instance()
{
    static auto* s_instance = new CountryFlagsNotifier(qApp);
    return s_instance;
}

void FlagDecorationDelegate::initStyleOption(QStyleOptionViewItem* option,
                                             const QModelIndex& index) const
{
    // The base call fits the icon into the view's square icon size; keep that height
    const int h = option->decorationSize.isValid() ? option->decorationSize.height() : kIconSize;
    QStyledItemDelegate::initStyleOption(option, index);
    if (!(option->features & QStyleOptionViewItem::HasDecoration))
        return;
    const QList<QSize> sizes = option->icon.availableSizes();
    if (sizes.isEmpty())
        return;
    const QSize s = sizes.constFirst();
    if (s.height() <= 0 || s.width() <= s.height())
        return;
    option->decorationSize = QSize(h * s.width() / s.height(), h);
}

} // namespace eMule
