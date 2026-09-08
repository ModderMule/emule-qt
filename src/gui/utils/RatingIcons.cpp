#include "pch.h"

/// @file RatingIcons.cpp
/// @brief Composing the column-0 marks into one cached icon.

#include "utils/RatingIcons.h"

#include "utils/FileTypeIcons.h"

#include "media/ContainerSniffer.h"

#include "prefs/Preferences.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>

namespace eMule {

namespace {

/// MFC's metrics: 16 px marks, 2 px before each one
/// (RATING_ICON_WIDTH / rcDraw.left + 2, srchybrid/DownloadListCtrl.cpp:60,407-411).
constexpr int kMarkPx = 16;
constexpr int kGapPx = 2;

/// The rating art, indexed by FileMark. Same six icons MFC loads into every one
/// of its image lists, plus the two substitutes.
QString markIconPath(FileMark mark)
{
    switch (mark) {
    case FileMark::NotRated:     return QStringLiteral(":/icons/FileRating0.ico");
    case FileMark::Fake:         return QStringLiteral(":/icons/FileRating1.ico");
    case FileMark::Poor:         return QStringLiteral(":/icons/FileRating2.ico");
    case FileMark::Fair:         return QStringLiteral(":/icons/FileRating3.ico");
    case FileMark::Good:         return QStringLiteral(":/icons/FileRating4.ico");
    case FileMark::Excellent:    return QStringLiteral(":/icons/FileRating5.ico");
    case FileMark::KadSearching: return QStringLiteral(":/icons/emuleCollSearch.ico");
    case FileMark::Spam:         return QStringLiteral(":/icons/Spam.ico");
    case FileMark::None:         break;
    }
    return {};
}

void drawInto(QPainter& painter, const QIcon& icon, int x, int dpr)
{
    const QPixmap pm = icon.pixmap(QSize(kMarkPx, kMarkPx), dpr);
    if (!pm.isNull())
        painter.drawPixmap(QRect(x, 0, kMarkPx, kMarkPx), pm);
}

} // namespace

QString ratingText(int userRating)
{
    switch (userRating) {
    case 1: return QObject::tr("Fake");
    case 2: return QObject::tr("Poor");
    case 3: return QObject::tr("Fair");
    case 4: return QObject::tr("Good");
    case 5: return QObject::tr("Excellent");
    default: return {};
    }
}


QIcon ratingIcon(int rating)
{
    if (rating < 0 || rating > 5 || !thePrefs.useOriginalIcons())
        return {};
    return QIcon(QStringLiteral(":/icons/FileRating%1.ico").arg(rating));
}

FileMark ratingMark(bool hasComment, int userRating)
{
    // The preference only ever governs other people's opinions. It is titled
    // "Indicate downloads with comments/rating by icon", and the fake mark is
    // neither -- that one is drawn regardless, by fileMarksIcon().
    if (!thePrefs.indicateRatings())
        return FileMark::None;

    if (userRating == 6)
        return FileMark::KadSearching;
    if (userRating > 0 && userRating <= 5)
        return static_cast<FileMark>(userRating);
    // Rated 0 but commented: MFC still shows the mark, using the "not rated"
    // art, because the point is that there is something to read.
    return hasComment ? FileMark::NotRated : FileMark::None;
}

QIcon fileMarksIcon(const QString& fileType, bool containerSuspect, bool ownComment,
                    FileMark mark)
{
    // A row with nothing to add is the common case; hand back the plain type
    // icon rather than a one-cell composite of it.
    if (!containerSuspect && !ownComment && mark == FileMark::None)
        return fileTypeIcon(fileType);

    const QString key = fileType + QLatin1Char('|')
                      + QLatin1Char(containerSuspect ? '1' : '0')
                      + QLatin1Char(ownComment ? '1' : '0')
                      + QString::number(static_cast<int>(mark));

    static QHash<QString, QIcon> cache;
    if (const auto it = cache.constFind(key); it != cache.constEnd())
        return *it;

    QStringList marks;
    if (containerSuspect)
        marks << QStringLiteral(":/icons/RatingBad.ico");   // the red exclamation
    if (const QString ratingPath = markIconPath(mark); !ratingPath.isEmpty())
        marks << ratingPath;

    // Device pixel ratio is baked in rather than read off a widget: a model has
    // no widget, and Qt scales the finished icon per screen anyway.
    constexpr int kDpr = 2;
    const int width = kMarkPx + static_cast<int>(marks.size()) * (kGapPx + kMarkPx);

    QPixmap canvas(QSize(width, kMarkPx) * kDpr);
    canvas.setDevicePixelRatio(kDpr);
    canvas.fill(Qt::transparent);
    {
        QPainter painter(&canvas);
        drawInto(painter, fileTypeIcon(fileType), 0, kDpr);
        if (ownComment) {
            // Over the type icon, not beside it: MFC draws this one as an image-list
            // overlay (INDEXTOOVERLAYMASK(1)), so it never costs the row any width.
            static const QPixmap overlay =
                QIcon(QStringLiteral(":/icons/FileCommentsOvl.ico")).pixmap(kMarkPx, kMarkPx);
            painter.drawPixmap(QRect(0, 0, kMarkPx, kMarkPx), overlay);
        }
        int x = kMarkPx;
        for (const QString& path : marks) {
            x += kGapPx;
            drawInto(painter, QIcon(path), x, kDpr);
            x += kMarkPx;
        }
    }

    QIcon icon(canvas);
    cache.insert(key, icon);
    return icon;
}

QString fileMarksTooltip(const QString& fileName, bool containerSuspect,
                         const QString& containerActual, bool hasComment, int userRating)
{
    QString extra;

    if (containerSuspect) {
        ContainerCheck check;
        check.verdict = containerActual.isEmpty() ? ContainerVerdict::NoKnownContainer
                                                  : ContainerVerdict::WrongContainer;
        check.actual = containerActual;
        extra += QStringLiteral("\n\n") + containerWarningText(check, fileName);
    }

    // The rating when there is one, otherwise just that there is something to read —
    // the same either/or MFC's indicator makes. Not gated on indicateRatings: the
    // preference is about drawing an icon, not about answering a hover.
    if (userRating > 0 && userRating <= 5)
        extra += QObject::tr("\nRating:\t%1").arg(ratingText(userRating));
    else if (hasComment)
        extra += QObject::tr("\nHas comments");

    return extra;
}

} // namespace eMule
