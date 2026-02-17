#include <QApplication>

#include "utils/Types.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("eMule Qt"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setOrganizationName(QStringLiteral("eMule"));

    // ToDo: Create main window (Module 21)
    // ToDo: Load preferences (Module 16)
    // ToDo: Initialize core session (Module 31)

    return QApplication::exec();
}