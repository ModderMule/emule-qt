#pragma once

/// @file Clipboard.h
/// @brief Copies text to the clipboard and tells the clipboard watcher to skip it.
///
/// MainWindow watches the clipboard for ed2k links (MFC SearchClipboard). Without this,
/// "Copy eD2K Link" feeds our own link straight back into the importer, which then
/// reports "You already have the file". MFC does the same through
/// CemuleApp::IgnoreClipboardLinks() in CopyTextToClipboard (srchybrid/Emule.h).
/// Header-only so the tests that compile single panel sources need no extra .cpp.

#include <QClipboard>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <initializer_list>

namespace eMule::Clipboard {

namespace detail {
inline QString& lastOwnText()
{
    static QString text;
    return text;
}
} // namespace detail

/// Puts @p text on the clipboard. Every copy the app makes goes through here.
inline void setText(const QString& text)
{
    // Record first: dataChanged may fire synchronously inside setText()
    detail::lastOwnText() = text.trimmed();
    QGuiApplication::clipboard()->setText(text);
}

/// True when @p trimmedText is what the app itself copied last.
[[nodiscard]] inline bool isOwnText(const QString& trimmedText)
{
    return !trimmedText.isEmpty() && trimmedText == detail::lastOwnText();
}

/// The clipboard's text for a link box; empty when it is too big to be a link list.
[[nodiscard]] inline QString text()
{
    QString clip = QGuiApplication::clipboard()->text();
    return clip.size() <= 1024 * 1024 ? clip : QString();
}

/// Every token in @p text starting with one of @p prefixes, in order, duplicates dropped.
[[nodiscard]] inline QStringList linksIn(const QString& text, std::initializer_list<QLatin1StringView> prefixes)
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    QStringList links;
    for (const QString& token : text.split(ws, Qt::SkipEmptyParts)) {
        const bool match = std::ranges::any_of(prefixes, [&token](QLatin1StringView prefix) {
            return token.size() > prefix.size() && token.startsWith(prefix, Qt::CaseInsensitive);
        });
        if (match && !links.contains(token))
            links << token;
    }
    return links;
}

/// The first token in the clipboard starting with @p prefix (e.g. "ed3k:room?"), or empty.
[[nodiscard]] inline QString firstLink(QLatin1StringView prefix)
{
    return linksIn(text(), {prefix}).value(0);
}

/// Every http(s):// token in @p text.
[[nodiscard]] inline QStringList httpLinksIn(const QString& text)
{
    return linksIn(text, {QLatin1StringView("http://"), QLatin1StringView("https://")});
}

/// Every tracker announce URL in @p text: http(s), udp, ws(s).
[[nodiscard]] inline QStringList trackerLinksIn(const QString& text)
{
    return linksIn(text, {QLatin1StringView("http://"), QLatin1StringView("https://"),
                          QLatin1StringView("udp://"), QLatin1StringView("ws://"),
                          QLatin1StringView("wss://")});
}

} // namespace eMule::Clipboard
