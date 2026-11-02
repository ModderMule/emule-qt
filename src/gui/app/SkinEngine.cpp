#include "pch.h"
/// @file SkinEngine.cpp
/// @brief INI-based skin profile engine implementation.
///
/// Port of the skin loading logic from srchybrid/Emule.cpp (LoadIcon, LoadImage,
/// LoadSkinColor — lines 1218-1400).

#include "app/SkinEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QStringDecoder>

namespace eMule {

void SkinEngine::loadProfile(const QString& iniPath)
{
    clear();

    QFile file(iniPath);
    if (iniPath.isEmpty() || !file.open(QIODevice::ReadOnly))
        return;

    m_iniPath = iniPath;
    m_skinDir = QFileInfo(iniPath).absolutePath();

    const auto sections = parseIni(decodeIni(file.readAll()));
    m_icons = sections.value(QStringLiteral("icons"));
    m_bitmaps = sections.value(QStringLiteral("bitmaps"));

    // MFC reads "%i , %i , %i"; we also take #RRGGBB
    const auto colors = sections.value(QStringLiteral("colors"));
    for (const auto& [key, value] : colors.asKeyValueRange()) {
        // MFC CMuleListCtrl::SetColors keeps the list watermark path in [Colors]
        if (key.endsWith(QLatin1StringView("bkimg"))) {
            const QString path = resolveResourcePath(value);
            if (QImageReader(path).canRead())
                m_listImages.insert(key, path);
            continue;
        }
        QColor color;
        if (value.startsWith(QLatin1Char('#'))) {
            color = QColor::fromString(value);
        } else {
            const auto parts = value.split(QLatin1Char(','));
            // %i takes 0x.. and 0.. too; "09" is no octal, read it as decimal
            const auto channel = [](const QString& text, bool& ok) {
                const int v = text.trimmed().toInt(&ok, 0);
                return ok ? v : text.trimmed().toInt(&ok, 10);
            };
            if (parts.size() == 3) {
                bool okR = false, okG = false, okB = false;
                const int r = channel(parts[0], okR);
                const int g = channel(parts[1], okG);
                const int b = channel(parts[2], okB);
                if (okR && okG && okB)
                    color = QColor(r & 0xFF, g & 0xFF, b & 0xFF);
            }
        }
        if (color.isValid())
            m_colors.insert(key, color);
    }
}

void SkinEngine::clear()
{
    m_iniPath.clear();
    m_skinDir.clear();
    m_icons.clear();
    m_bitmaps.clear();
    m_colors.clear();
    m_listImages.clear();
}

QIcon SkinEngine::icon(const QString& resourceName, const QIcon& fallback) const
{
    const auto it = m_icons.constFind(resourceName.toLower());
    if (it == m_icons.cend())
        return fallback;

    // QIcon(path) is never null, even for a file it cannot read (a .dll, say)
    const QString path = resolveResourcePath(it.value());
    if (path.isEmpty() || !QImageReader(path).canRead())
        return fallback;
    return QIcon(path);
}

QHash<QString, QString> SkinEngine::iconFiles() const
{
    QHash<QString, QString> files;
    for (const auto& [name, spec] : m_icons.asKeyValueRange()) {
        if (const QString path = resolveResourcePath(spec); QImageReader(path).canRead())
            files.insert(name, path);
    }
    return files;
}

QPixmap SkinEngine::pixmap(const QString& resourceName) const
{
    const QString path = bitmapPath(resourceName);
    return path.isEmpty() ? QPixmap{} : QPixmap(path);
}

QString SkinEngine::bitmapPath(const QString& resourceName) const
{
    const auto it = m_bitmaps.constFind(resourceName.toLower());
    if (it == m_bitmaps.cend())
        return {};
    const QString path = resolveResourcePath(it.value());
    return QImageReader(path).canRead() ? path : QString{};
}

std::optional<QColor> SkinEngine::color(const QString& key) const
{
    const auto it = m_colors.constFind(key.toLower());
    if (it == m_colors.cend())
        return std::nullopt;
    return it.value();
}

QHash<QString, QHash<QString, QString>> SkinEngine::parseIni(const QString& text)
{
    // Not QSettings: it splits values at commas ("255,0,0", "file.dll,3") and treats
    // backslashes as escapes, both of which MFC profiles are full of. Sections and
    // keys are case-insensitive, as GetPrivateProfileString is.
    QHash<QString, QHash<QString, QString>> sections;
    QHash<QString, QString>* current = nullptr;
    for (QStringView line : QStringView(text).split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';')) || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            const auto close = line.indexOf(QLatin1Char(']'));
            const QStringView name = close > 0 ? line.mid(1, close - 1) : line.mid(1);
            current = &sections[name.trimmed().toString().toLower()];
            continue;
        }
        const auto eq = line.indexOf(QLatin1Char('='));
        if (!current || eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed().toString().toLower();
        QString value = line.mid(eq + 1).trimmed().toString();
        // GetPrivateProfileString drops one pair of enclosing quotes
        if (value.size() >= 2
            && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
                || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
            value = value.mid(1, value.size() - 2);
        }
        // first occurrence wins, as in Windows
        if (!value.isEmpty() && !current->contains(key))
            current->insert(key, value);
    }
    return sections;
}

QString SkinEngine::decodeIni(const QByteArray& raw)
{
    // Windows writes profiles as UTF-16 with BOM, UTF-8, or the ANSI code page
    QString text;
    if (const auto enc = QStringConverter::encodingForData(raw)) {
        QStringDecoder decoder(*enc);   // default flags drop the BOM
        text = decoder.decode(raw);
    } else {
        QStringDecoder utf8(QStringConverter::Utf8);
        text = utf8.decode(raw);
        if (utf8.hasError())
            text = QString::fromLatin1(raw);
    }
    text.remove(QLatin1Char('\r'));
    return text;
}

QString SkinEngine::resolveResourcePath(const QString& value) const
{
    if (value.isEmpty())
        return {};

    // %VAR% as ExpandEnvironmentStrings does; unknown names stay literal
    static const QRegularExpression envVar(QStringLiteral("%([^%]+)%"));
    QString path = value;
    for (qsizetype from = 0;;) {
        const auto m = envVar.match(path, from);
        if (!m.hasMatch())
            break;
        const QString name = m.captured(1);
        if (!qEnvironmentVariableIsSet(name.toLocal8Bit().constData())) {
            from = m.capturedEnd() - 1;
            continue;
        }
        const QString expanded = qEnvironmentVariable(name.toLocal8Bit().constData());
        path.replace(m.capturedStart(), m.capturedLength(), expanded);
        from = m.capturedStart() + expanded.size();
    }

    // Strip an icon index ("file.dll,3"); only image files are supported
    const auto commaIdx = path.lastIndexOf(QLatin1Char(','));
    if (commaIdx > 0) {
        bool isIndex = false;
        [[maybe_unused]] const int index = QStringView(path).mid(commaIdx + 1).trimmed().toInt(&isIndex);
        if (isIndex)
            path = path.left(commaIdx).trimmed();
    }

    // profiles are written on Windows
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));

    if (QDir::isAbsolutePath(path))
        return path;
    return m_skinDir + QLatin1Char('/') + path;
}

} // namespace eMule
