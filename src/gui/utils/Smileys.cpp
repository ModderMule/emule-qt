#include "pch.h"
/// @file Smileys.cpp
/// @brief Chat smileys — implementation.

#include "utils/Smileys.h"

#include <QStringView>

#include <iterator>

namespace eMule::Smileys {

namespace {

constexpr Smiley kPicker[] = {
    { ":-)",     "Smiley_Smile"    },
    { ":-))",    "Smiley_Happy"    },
    { ":-D",     "Smiley_Laugh"    },
    { ";-)",     "Smiley_Wink"     },
    { ":-P",     "Smiley_Tongue"   },
    { "=-)",     "Smiley_Interest" },
    { ":-(",     "Smiley_Sad"      },
    { ":'(",     "Smiley_Cry"      },
    { ":-|",     "Smiley_Disgust"  },
    { ":-O",     "Smiley_omg"      },
    { ":-/",     "Smiley_Skeptic"  },
    { ":-*",     "Smiley_Love"     },
    { ":-]",     "Smiley_smileq"   },
    { ":-[",     "Smiley_sadq"     },
    { ":ph34r:", "Smiley_Ph34r"    },
    { ">_>",     "Smiley_lookside" },
    { ":-X",     "Smiley_Sealed"   },
};

/// Every code the renderer knows, in MFC's order (HTRichEditCtrl.cpp:1019-1103). The
/// first match wins; the boundary rule makes ":)" and ":))" unambiguous.
constexpr Smiley kAliases[] = {
    { ":)", "Smiley_Smile" }, { ":-)", "Smiley_Smile" },
    { ":]", "Smiley_smileq" }, { ":-]", "Smiley_smileq" },
    { ":))", "Smiley_Happy" }, { ":)))", "Smiley_Happy" }, { ":-))", "Smiley_Happy" },
    { ":-)))", "Smiley_Happy" }, { "^_^", "Smiley_Happy" }, { "(^.^)", "Smiley_Happy" },
    { ";)", "Smiley_Wink" }, { ";-)", "Smiley_Wink" },
    { ":D", "Smiley_Laugh" }, { ":-D", "Smiley_Laugh" }, { "lol", "Smiley_Laugh" },
    { "LOL", "Smiley_Laugh" }, { ":lol:", "Smiley_Laugh" }, { ":LOL:", "Smiley_Laugh" },
    { "*lol*", "Smiley_Laugh" }, { "*LOL*", "Smiley_Laugh" },
    { "=)", "Smiley_Interest" }, { "=-)", "Smiley_Interest" },
    { ":/", "Smiley_Skeptic" }, { ":-/", "Smiley_Skeptic" }, { ":\\", "Smiley_Skeptic" },
    { ":-\\", "Smiley_Skeptic" },
    { "<.<", "Smiley_lookside" }, { "<_<", "Smiley_lookside" }, { ">.>", "Smiley_lookside" },
    { ">_>", "Smiley_lookside" },
    { ":P", "Smiley_Tongue" }, { ":-P", "Smiley_Tongue" }, { ":p", "Smiley_Tongue" },
    { ":-p", "Smiley_Tongue" },
    { ":-x", "Smiley_Sealed" }, { ":-X", "Smiley_Sealed" },
    { ":-|", "Smiley_Disgust" },
    { ":(", "Smiley_Sad" }, { ":-(", "Smiley_Sad" },
    { ":[", "Smiley_sadq" }, { ":-[", "Smiley_sadq" },
    { ":cry:", "Smiley_Cry" }, { ":'-(", "Smiley_Cry" }, { ":~-(", "Smiley_Cry" },
    { ":~(~~~", "Smiley_Cry" }, { ":~(~~", "Smiley_Cry" }, { ":~(~", "Smiley_Cry" },
    { ":,(", "Smiley_Cry" }, { ";(", "Smiley_Cry" }, { ";-(", "Smiley_Cry" },
    { "&.(..", "Smiley_Cry" }, { ":'(", "Smiley_Cry" }, { ":,-(", "Smiley_Cry" },
    { ":o", "Smiley_omg" }, { ":O", "Smiley_omg" }, { ":-o", "Smiley_omg" },
    { ":-O", "Smiley_omg" },
    { ":love:", "Smiley_Love" }, { "(*_*)", "Smiley_Love" }, { "<*_*>", "Smiley_Love" },
    { ":kiss:", "Smiley_Love" }, { ":-*", "Smiley_Love" },
    { "-_-", "Smiley_Ph34r" }, { ":ph34r:", "Smiley_Ph34r" },
};

constexpr char16_t kPlaceholderBase = 0xE000;   // Unicode private use area
constexpr auto kAliasCount = static_cast<char16_t>(std::size(kAliases));

bool isPlaceholder(QChar c)
{
    return c.unicode() >= kPlaceholderBase && c.unicode() < kPlaceholderBase + kAliasCount;
}

bool endsCode(const QString& text, qsizetype pos)
{
    return pos == text.size() || text[pos] == u' ' || text[pos] == u'\r' || text[pos] == u'\n';
}

} // namespace

std::span<const Smiley> picker()
{
    return kPicker;
}

QString protect(const QString& raw)
{
    QString text = raw;
    text.removeIf(isPlaceholder);

    QString out;
    out.reserve(text.size());
    qsizetype i = 0;
    while (i < text.size()) {
        const bool startsCode = i == 0 || text[i - 1] == u' ' || text[i - 1] == u'.';
        bool matched = false;
        for (std::size_t a = 0; startsCode && a < std::size(kAliases); ++a) {
            const QLatin1StringView code(kAliases[a].code);
            const qsizetype end = i + code.size();
            if (end <= text.size() && QStringView(text).sliced(i, code.size()) == code
                && endsCode(text, end))
            {
                out += QChar(static_cast<char16_t>(kPlaceholderBase + a));
                i = end;
                matched = true;
            }
        }
        if (!matched)
            out += text[i++];
    }
    return out;
}

QString expand(const QString& html)
{
    QString out;
    out.reserve(html.size());
    for (const QChar c : html) {
        if (!isPlaceholder(c)) {
            out += c;
            continue;
        }
        out += QStringLiteral("<img src=\"qrc:/smileys/%1.ico\" width=\"16\" height=\"16\">")
                   .arg(QLatin1StringView(kAliases[c.unicode() - kPlaceholderBase].icon));
    }
    return out;
}

QString render(const QString& raw)
{
    return expand(protect(raw).toHtmlEscaped());
}

} // namespace eMule::Smileys
