#include "pch.h"
/// @file WebTemplateEngine.cpp
/// @brief Template engine implementation.

#include "webserver/WebTemplateEngine.h"

#include "utils/Log.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace eMule {

bool WebTemplateEngine::loadTemplate(const QString& filePath)
{
    m_filePath = filePath;
    m_sections.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        logError(QStringLiteral("WebTemplateEngine: failed to open template: %1").arg(filePath));
        return false;
    }

    QTextStream stream(&file);
    const QString content = stream.readAll();
    parseSections(content);

    logInfo(QStringLiteral("WebTemplateEngine: loaded %1 sections from %2")
                .arg(m_sections.size()).arg(filePath));
    return !m_sections.isEmpty();
}

bool WebTemplateEngine::reload()
{
    if (m_filePath.isEmpty())
        return false;
    return loadTemplate(m_filePath);
}

QString WebTemplateEngine::section(const QString& name) const
{
    return m_sections.value(name);
}

QString WebTemplateEngine::substitute(const QString& tmpl,
                                      const QHash<QString, QString>& vars)
{
    QString result = tmpl;
    for (auto it = vars.cbegin(); it != vars.cend(); ++it)
        result.replace(QStringLiteral("[") + it.key() + QStringLiteral("]"), it.value());
    return result;
}

void WebTemplateEngine::parseSections(const QString& rawContent)
{
    // Sections are delimited by <--TMPL_NAME--> and <--TMPL_NAME_END-->
    static const QRegularExpression sectionStart(QStringLiteral("<--TMPL_([A-Z_0-9]+)-->"));
    static const QRegularExpression sectionEnd(QStringLiteral("<--TMPL_([A-Z_0-9]+)_END-->"));

    // First pass: find all section boundaries
    struct SectionRange {
        QString name;
        int contentStart;
        int contentEnd;
    };
    QList<SectionRange> ranges;

    auto startIt = sectionStart.globalMatch(rawContent);
    while (startIt.hasNext()) {
        auto match = startIt.next();
        const QString name = match.captured(1);
        const int contentStart = match.capturedEnd();

        // Find the matching end tag
        const QString endTag = QStringLiteral("<--TMPL_") + name + QStringLiteral("_END-->");
        const int endPos = rawContent.indexOf(endTag, contentStart);
        if (endPos >= 0) {
            ranges.append({name, contentStart, endPos});
        }
    }

    for (const auto& r : ranges) {
        m_sections.insert(r.name, rawContent.mid(r.contentStart, r.contentEnd - r.contentStart));
    }
}

} // namespace eMule
