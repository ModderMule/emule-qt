#include "pch.h"
/// @file ClientIcons.cpp
/// @brief Client software icons shared by the client lists and download sources.

#include "utils/ClientIcons.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>

namespace eMule {

namespace {

// ClientSoftware enum values from ClientStateDefs.h
constexpr int SoftEMule          = 0;
constexpr int SoftCDonkey        = 1;
constexpr int SoftXMule          = 2;
constexpr int SoftAMule          = 3;
constexpr int SoftShareaza       = 4;
constexpr int SoftMLDonkey       = 10;
constexpr int SoftLphant         = 20;
constexpr int SoftEDonkeyHybrid  = 50;
// SoftEDonkey (51) and SoftOldEMule (52) fall through to default icon
constexpr int SoftURL            = 53;

} // namespace

QIcon clientSoftwareIcon(int softwareId, bool hasCredit, bool isFriend)
{
    static QHash<int, QIcon> cache;

    // URL source → Server icon (MFC index 15)
    if (softwareId == SoftURL) {
        static QIcon urlIcon(QStringLiteral(":/icons/Server.ico"));
        return urlIcon;
    }

    // Cache key: combine softwareId + hasCredit + isFriend
    const int key = softwareId * 4 + (hasCredit ? 2 : 0) + (isFriend ? 1 : 0);
    auto it = cache.find(key);
    if (it != cache.end())
        return it.value();

    QString path;
    switch (softwareId) {
    case SoftEMule:
    case SoftCDonkey:
    case SoftXMule:
        path = hasCredit ? QStringLiteral(":/icons/ClientCompatiblePlus.ico")
                         : QStringLiteral(":/icons/ClientCompatible.ico");
        break;
    case SoftAMule:
        path = hasCredit ? QStringLiteral(":/icons/ClientaMulePlus.ico")
                         : QStringLiteral(":/icons/ClientaMule.ico");
        break;
    case SoftShareaza:
        path = hasCredit ? QStringLiteral(":/icons/ClientShareazaPlus.ico")
                         : QStringLiteral(":/icons/ClientShareaza.ico");
        break;
    case SoftMLDonkey:
        path = hasCredit ? QStringLiteral(":/icons/ClientMLDonkeyPlus.ico")
                         : QStringLiteral(":/icons/ClientMLDonkey.ico");
        break;
    case SoftLphant:
        path = hasCredit ? QStringLiteral(":/icons/ClientlPhantPlus.ico")
                         : QStringLiteral(":/icons/ClientlPhant.ico");
        break;
    case SoftEDonkeyHybrid:
        path = hasCredit ? QStringLiteral(":/icons/ClienteDonkeyHybridPlus.ico")
                         : QStringLiteral(":/icons/ClienteDonkeyHybrid.ico");
        break;
    default:
        path = hasCredit ? QStringLiteral(":/icons/ClientDefaultPlus.ico")
                         : QStringLiteral(":/icons/ClientDefault.ico");
        break;
    }

    if (!isFriend) {
        QIcon icon(path);
        cache.insert(key, icon);
        return icon;
    }

    // Composite: software icon + friend badge at bottom-right
    QPixmap pixmap = QIcon(path).pixmap(16, 16);
    {
        QPainter painter(&pixmap);
        static const QPixmap friendBadge =
            QIcon(QStringLiteral(":/icons/Friend.ico")).pixmap(10, 10);
        painter.drawPixmap(6, 6, 10, 10, friendBadge);
    }
    QIcon composite(pixmap);
    cache.insert(key, composite);
    return composite;
}

} // namespace eMule
