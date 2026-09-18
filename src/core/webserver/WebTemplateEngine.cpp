#include "pch.h"
/// @file WebTemplateEngine.cpp
/// @brief Template engine implementation.

#include "webserver/WebTemplateEngine.h"

#include "utils/Log.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace eMule {

namespace {

/// `[Key]`, or a translation marker on one line. scripts/extract_web_strings.py
/// matches markers with the same pattern.
const QRegularExpression& tokenPattern()
{
    static const QRegularExpression rx(
        QStringLiteral("\\[([A-Za-z0-9_]+)\\]|\\{\\{(js:)?([^{}\\r\\n]+?)\\}\\}"));
    return rx;
}

} // namespace

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
    QString result;
    result.reserve(tmpl.size());
    qsizetype done = 0;

    auto it = tokenPattern().globalMatch(tmpl);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        result += QStringView(tmpl).mid(done, m.capturedStart() - done);
        done = m.capturedEnd();

        if (m.capturedLength(1) > 0) {
            const auto value = vars.constFind(m.captured(1));
            result += value != vars.cend() ? *value : m.captured(0);
            continue;
        }

        const QByteArray source = m.captured(3).toUtf8();
        const QString text = QCoreApplication::translate(kWebTranslationContext, source.constData());
        result += m.capturedLength(2) > 0 ? jsEscape(text) : htmlEscape(text);
    }
    result += QStringView(tmpl).mid(done);
    return result;
}

QString WebTemplateEngine::htmlEscape(const QString& s)
{
    // toHtmlEscaped() leaves the apostrophe alone.
    return s.toHtmlEscaped().replace(QLatin1Char('\''), QStringLiteral("&#39;"));
}

QString WebTemplateEngine::jsEscape(const QString& s)
{
    QString out;
    out.reserve(s.size() + 8);
    for (const QChar c : s) {
        switch (c.unicode()) {
        case u'\\': out += QLatin1String("\\\\"); break;
        case u'\n': out += QLatin1String("\\n"); break;
        case u'\r': out += QLatin1String("\\r"); break;
        // As \x escapes, so the literal is also safe inside an HTML attribute.
        case u'\'':
        case u'"':
        case u'<':
        case u'>':
        case u'&':
            out += QStringLiteral("\\x%1").arg(int(c.unicode()), 2, 16, QLatin1Char('0'));
            break;
        // Line terminators to a JS parser, even inside a string.
        case 0x2028:
        case 0x2029:
            out += QStringLiteral("\\u%1").arg(int(c.unicode()), 4, 16, QLatin1Char('0'));
            break;
        default:
            out += c;
        }
    }
    return out;
}

QStringList WebTemplateEngine::markerTexts(const QString& text)
{
    QStringList out;
    auto it = tokenPattern().globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedLength(3) > 0)
            out << m.captured(3);
    }
    return out;
}

void WebTemplateEngine::parseSections(const QString& rawContent)
{
    // Sections are delimited by <--TMPL_NAME--> and <--TMPL_NAME_END-->
    static const QRegularExpression sectionStart(QStringLiteral("<--TMPL_([A-Z_0-9]+)-->"));
    static const QRegularExpression sectionEnd(QStringLiteral("<--TMPL_([A-Z_0-9]+)_END-->"));

    // First pass: find all section boundaries
    struct SectionRange {
        QString name;
        qsizetype contentStart;
        qsizetype contentEnd;
    };
    QList<SectionRange> ranges;

    auto startIt = sectionStart.globalMatch(rawContent);
    while (startIt.hasNext()) {
        auto match = startIt.next();
        const QString name = match.captured(1);
        const qsizetype contentStart = match.capturedEnd();

        // Find the matching end tag
        const QString endTag = QStringLiteral("<--TMPL_") + name + QStringLiteral("_END-->");
        const qsizetype endPos = rawContent.indexOf(endTag, contentStart);
        if (endPos >= 0) {
            ranges.append({name, contentStart, endPos});
        }
    }

    for (const auto& r : ranges) {
        m_sections.insert(r.name, rawContent.mid(r.contentStart, r.contentEnd - r.contentStart));
    }
}

} // namespace eMule
