#include "pch.h"
/// @file TranslationRouter.cpp
/// @brief Per-request translation — implementation.

#include "app/TranslationRouter.h"

#include <QLocale>

#include <algorithm>

namespace eMule {

namespace {

/// Where this thread's lookups go; null outside any scope.
thread_local const QTranslator* tl_active = nullptr;

[[nodiscard]] bool isEnglish(const QString& code)
{
    return code.startsWith(QLatin1String("en"), Qt::CaseInsensitive);
}

} // namespace

TranslationRouter::TranslationRouter(QStringList searchDirs, QObject* parent)
    : QTranslator(parent)
    , m_dirs(std::move(searchDirs))
{
}

TranslationRouter::~TranslationRouter() = default;

QList<AppLanguage> TranslationRouter::availableLanguages() const
{
    if (!m_available) {
        m_available = AppConfig::availableLanguages(m_dirs);
        // An injected translator counts as installed.
        for (const auto& [code, translator] : m_translators) {
            const bool listed = std::ranges::any_of(
                *m_available, [&code](const AppLanguage& l) { return l.code == code; });
            if (translator && !listed && !isEnglish(code))
                m_available->append({code, code});
        }
    }
    return *m_available;
}

bool TranslationRouter::isUsable(const QString& code) const
{
    if (code.isEmpty())
        return false;
    if (isEnglish(code))
        return true;
    const QList<AppLanguage> all = availableLanguages();
    return std::ranges::any_of(all, [&code](const AppLanguage& l) { return l.code == code; });
}

QString TranslationRouter::resolve(const QString& requested, const QString& fallback) const
{
    if (isUsable(requested))
        return requested;
    if (!fallback.isEmpty())
        return fallback;
    return QLocale::system().name();
}

void TranslationRouter::setTranslator(const QString& code, std::unique_ptr<QTranslator> translator)
{
    m_translators[code] = std::move(translator);
    m_available.reset();
}

QString TranslationRouter::translate(const char* context, const char* sourceText,
                                     const char* disambiguation, int n) const
{
    // Null, not empty: on null QCoreApplication asks the next translator and then
    // falls back to the source text.
    if (!tl_active)
        return {};
    return tl_active->translate(context, sourceText, disambiguation, n);
}

TranslationRouter::Scope::Scope(TranslationRouter* router, const QString& code)
    : m_previous(tl_active)
{
    tl_active = router ? router->translatorFor(code) : nullptr;
}

TranslationRouter::Scope::~Scope()
{
    tl_active = m_previous;
}

const QTranslator* TranslationRouter::translatorFor(const QString& code)
{
    if (code.isEmpty() || isEnglish(code))
        return nullptr;
    if (const auto it = m_translators.find(code); it != m_translators.end())
        return it->second.get();

    // The GUI's own loader: QLocale matching, so "de" finds emuleqt_de_DE.qm.
    auto translator = std::make_unique<QTranslator>();
    const bool loaded = std::ranges::any_of(m_dirs, [&](const QString& dir) {
        return translator->load(QLocale(code), QStringLiteral("emuleqt"), QStringLiteral("_"), dir);
    });
    // Remembered either way, so a missing file is not looked for on every request.
    if (!loaded)
        translator.reset();
    const QTranslator* raw = translator.get();
    m_translators.emplace(code, std::move(translator));
    return raw;
}

} // namespace eMule
