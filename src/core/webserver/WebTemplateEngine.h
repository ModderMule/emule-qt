#pragma once

/// @file WebTemplateEngine.h
/// @brief Template engine for the eMule web interface.
///
/// Parses eMule .tmpl files which use `<--TMPL_NAME-->` / `<--TMPL_NAME_END-->`
/// delimiters for sections and `[Key]` syntax for variable substitution.

#include <QHash>
#include <QString>

namespace eMule {

class WebTemplateEngine {
public:
    /// Load and parse a template file. Returns true on success.
    bool loadTemplate(const QString& filePath);

    /// Reload the previously loaded template file.
    bool reload();

    /// Return a named section from the template.
    [[nodiscard]] QString section(const QString& name) const;

    /// Replace all `[Key]` occurrences in @p tmpl with values from @p vars.
    [[nodiscard]] static QString substitute(const QString& tmpl,
                                            const QHash<QString, QString>& vars);

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
