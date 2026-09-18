#pragma once

/// @file WebTemplateEngine.h
/// @brief Template engine for the eMule web interface.
///
/// Parses eMule .tmpl files which use `<--TMPL_NAME-->` / `<--TMPL_NAME_END-->`
/// delimiters for sections, `[Key]` for variables and `{{Text}}` / `{{js:Text}}`
/// for translatable text. See docs/web-interface.md.

#include <QHash>
#include <QString>
#include <QStringList>

namespace eMule {

/// Context every `{{…}}` marker is translated in. The web server's own tr()
/// uses it too, so a sentence the template and a page builder share is
/// translated once.
inline constexpr const char* kWebTranslationContext = "eMule::WebServer";

class WebTemplateEngine {
public:
    /// Load and parse a template file. Returns true on success.
    bool loadTemplate(const QString& filePath);

    /// Reload the previously loaded template file.
    bool reload();

    /// Return a named section from the template.
    [[nodiscard]] QString section(const QString& name) const;

    /// Fill @p tmpl in one left-to-right pass. `[Key]` becomes its value from
    /// @p vars; `{{Text}}` the translation of Text, HTML-escaped; `{{js:Text}}`
    /// the translation escaped for a JS string literal.
    ///
    /// Nothing inserted is scanned again, so a value holding "[Session]" stays
    /// exactly that. A key @p vars lacks stays as written: CSS `input[type=…]`
    /// and script `a[i]` are not keys.
    [[nodiscard]] static QString substitute(const QString& tmpl,
                                            const QHash<QString, QString>& vars = {});

    /// Text for element content or a quoted attribute value.
    [[nodiscard]] static QString htmlEscape(const QString& s);

    /// Text for inside a '…' or "…" JS string literal, in a <script> block or an
    /// on* attribute alike: it emits no HTML-special character at all.
    [[nodiscard]] static QString jsEscape(const QString& s);

    /// The text of every translation marker in @p text, in order.
    [[nodiscard]] static QStringList markerTexts(const QString& text);

    /// Returns true if a template has been successfully loaded.
    [[nodiscard]] bool isValid() const { return !m_sections.isEmpty(); }

    /// Return all available section names.
    [[nodiscard]] QStringList sectionNames() const { return m_sections.keys(); }

private:
    void parseSections(const QString& rawContent);

    QHash<QString, QString> m_sections;
    QString m_filePath;
};

} // namespace eMule
