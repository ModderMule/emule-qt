#include "pch.h"
/// @file WebServices.cpp
/// @brief Web services menu integration — port of MFC CWebServices.

#include "utils/WebServices.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

namespace eMule {

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

WebServices& WebServices::instance()
{
    static WebServices s;
    return s;
}

// ---------------------------------------------------------------------------
// Config file path
// ---------------------------------------------------------------------------

QString WebServices::servicesFilePath() const
{
    // Look for webservices.dat in the application config directory,
    // falling back to the data directory shipped with the install.
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString configPath = configDir + QStringLiteral("/webservices.dat");
    if (QFile::exists(configPath))
        return configPath;

    // Shipped default next to the executable
    const QString appDir = QCoreApplication::applicationDirPath();
    return appDir + QStringLiteral("/data/config/webservices.dat");
}

// ---------------------------------------------------------------------------
// reload — parse webservices.dat
// ---------------------------------------------------------------------------

void WebServices::reload()
{
    const QString path = servicesFilePath();
    const QFileInfo fi(path);
    if (!fi.exists())
        return;

    // Only re-read if the file has been modified
    if (fi.lastModified() == m_lastModified && !m_services.empty())
        return;

    if (loadFromFile(path))
        m_lastModified = fi.lastModified();
}

// ---------------------------------------------------------------------------
// loadFromFile — parse a webservices.dat at an arbitrary path
// ---------------------------------------------------------------------------

bool WebServices::loadFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    m_services.clear();

    static constexpr std::array kFileMacros = {
        QStringView(u"#hashid"),
        QStringView(u"#filesize"),
        QStringView(u"#filename"),
        QStringView(u"#name"),
        QStringView(u"#cleanfilename"),
        QStringView(u"#cleanname"),
    };

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();

        // Skip comments and short lines
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char('/')) || line.size() < 5)
            continue;

        const auto comma = line.indexOf(QLatin1Char(','));
        if (comma <= 0)
            continue;

        const QString label = line.left(comma).trimmed();
        const QString urlTemplate = line.mid(comma + 1).trimmed();
        if (urlTemplate.isEmpty())
            continue;

        bool hasFileMacros = false;
        for (const auto& macro : kFileMacros) {
            if (urlTemplate.contains(macro)) {
                hasFileMacros = true;
                break;
            }
        }

        m_services.push_back({label, urlTemplate, hasFileMacros});
    }

    return true;
}

// ---------------------------------------------------------------------------
// populateFileMenu
// ---------------------------------------------------------------------------

void WebServices::populateFileMenu(QMenu* menu, const QString& hash,
                                    const QString& fileName, uint64_t fileSize)
{
    reload();

    for (const auto& svc : m_services) {
        if (!svc.hasFileMacros)
            continue;

        menu->addAction(svc.label, menu, [=]() {
            const QString url = expandMacros(svc.urlTemplate, hash, fileName, fileSize);
            QDesktopServices::openUrl(QUrl(url));
        });
    }
}

// ---------------------------------------------------------------------------
// expandMacros
// ---------------------------------------------------------------------------

QString WebServices::expandMacros(const QString& urlTemplate, const QString& hash,
                                   const QString& fileName, uint64_t fileSize)
{
    QString url = urlTemplate;

    url.replace(QStringLiteral("#hashid"), hash);
    url.replace(QStringLiteral("#filesize"), QString::number(fileSize));

    const QString encodedName = QString::fromUtf8(QUrl::toPercentEncoding(fileName));
    url.replace(QStringLiteral("#filename"), encodedName);

    // Basename without extension
    const QString baseName = QFileInfo(fileName).completeBaseName();
    url.replace(QStringLiteral("#name"), QString::fromUtf8(QUrl::toPercentEncoding(baseName)));

    // Cleaned variants
    url.replace(QStringLiteral("#cleanfilename"),
                QString::fromUtf8(QUrl::toPercentEncoding(cleanupFilename(fileName, true))));
    url.replace(QStringLiteral("#cleanname"),
                QString::fromUtf8(QUrl::toPercentEncoding(cleanupFilename(baseName, false))));

    return url;
}

// ---------------------------------------------------------------------------
// cleanupFilename — strip common bracket content and junk chars
// ---------------------------------------------------------------------------

QString WebServices::cleanupFilename(const QString& name, bool keepExtension)
{
    QString result = name;

    // Strip [xxx], (xxx), {xxx}
    static const QRegularExpression brackets(QStringLiteral(R"(\[[^\]]*\]|\([^\)]*\)|\{[^\}]*\})"));
    result.replace(brackets, QString());

    // Replace underscores, dots (except last if keepExtension), dashes with spaces
    if (!keepExtension) {
        result.replace(QLatin1Char('.'), QLatin1Char(' '));
    } else {
        // Preserve the file extension
        const auto lastDot = result.lastIndexOf(QLatin1Char('.'));
        if (lastDot > 0) {
            QString base = result.left(lastDot);
            base.replace(QLatin1Char('.'), QLatin1Char(' '));
            result = base + result.mid(lastDot);
        }
    }
    result.replace(QLatin1Char('_'), QLatin1Char(' '));

    // Collapse multiple spaces
    static const QRegularExpression multiSpace(QStringLiteral(R"(\s{2,})"));
    result.replace(multiSpace, QStringLiteral(" "));

    return result.trimmed();
}

} // namespace eMule
