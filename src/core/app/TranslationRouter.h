#pragma once

/// @file TranslationRouter.h
/// @brief Per-request translation for a process that answers in several languages.

#include "app/AppConfig.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QTranslator>

#include <map>
#include <memory>
#include <optional>

namespace eMule {

/// A QTranslator installed once, process-wide, that translates nothing by itself.
///
/// Inside a Scope every QCoreApplication::translate() on that thread -- tr(), the
/// core's unit and rating labels -- answers in the scope's language. Outside one
/// it stays silent, so the log, the IPC and the REST API are untouched. That is
/// what lets the web UI answer each session in its own language while the daemon
/// installs no language of its own.
class TranslationRouter : public QTranslator {    // no Q_OBJECT: adds no signals
public:
    /// @p searchDirs as AppConfig::langCandidates() returns them.
    explicit TranslationRouter(QStringList searchDirs, QObject* parent = nullptr);
    ~TranslationRouter() override;

    /// Every installed translation, English excluded, by native name.
    [[nodiscard]] QList<AppLanguage> availableLanguages() const;

    /// English (the source language) or an installed translation.
    [[nodiscard]] bool isUsable(const QString& code) const;

    /// @p requested when usable, else @p fallback, else the system locale --
    /// general.language's own "empty means the system's" rule.
    [[nodiscard]] QString resolve(const QString& requested, const QString& fallback) const;

    /// Use @p translator for @p code instead of a .qm. Tests only: it frees the
    /// translator it replaces.
    void setTranslator(const QString& code, std::unique_ptr<QTranslator> translator);

    [[nodiscard]] QString translate(const char* context, const char* sourceText,
                                    const char* disambiguation = nullptr,
                                    int n = -1) const override;

    /// Never empty: installTranslator() reports failure for an empty translator.
    [[nodiscard]] bool isEmpty() const override { return false; }

    /// Routes this thread's lookups to one language until destroyed. Nests.
    class Scope {
    public:
        Scope(TranslationRouter* router, const QString& code);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        const QTranslator* m_previous;
    };

private:
    /// Loaded on first use and kept for good, so a scope's pointer never dangles.
    /// Null for English and for a code nothing could be loaded for.
    [[nodiscard]] const QTranslator* translatorFor(const QString& code);

    QStringList m_dirs;
    std::map<QString, std::unique_ptr<QTranslator>> m_translators;
    mutable std::optional<QList<AppLanguage>> m_available;
};

} // namespace eMule
