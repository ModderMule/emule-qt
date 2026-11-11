#include "AppIcon.h"

namespace eMule {

QIcon appIcon(bool lowId)
{
    // one file per size rather than Mule.ico: taskbar and Alt-Tab ask for 48 and up
    const QString pattern = lowId ? QStringLiteral(":/icons/app/emuleqt-lowid-%1.png")
                                  : QStringLiteral(":/icons/app/emuleqt-%1.png");
    QIcon icon;
    for (const int size : {16, 32, 48, 64, 128, 256})
        icon.addFile(pattern.arg(size), QSize(size, size));
    return icon;
}

} // namespace eMule
