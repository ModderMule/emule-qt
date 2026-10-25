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
#include <QString>

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

} // namespace eMule::Clipboard
