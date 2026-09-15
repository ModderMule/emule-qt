#pragma once

/// @file Smileys.h
/// @brief Chat smileys for the Messages and IRC panes — MFC's table and matching rules.
///
/// Matching runs on the raw text, before any HTML escaping: a code such as ">_>" never
/// survives toHtmlEscaped() as itself. protect() swaps each code for a private-use
/// placeholder, the caller escapes (or renders mIRC codes) as usual — placeholders pass
/// through untouched — and expand() turns them into images.

#include <QString>

#include <span>

namespace eMule::Smileys {

struct Smiley {
    const char* code;
    const char* icon;   ///< basename under qrc:/smileys/
};

/// One code per icon, in the picker's order (MFC SmileySelector.cpp).
[[nodiscard]] std::span<const Smiley> picker();

/// @p raw with every smiley code replaced by a placeholder. A code counts only at the
/// start of the text or after a space or '.', and only when a space, a line break or
/// the end follows (MFC CHTRichEditCtrl::AddSmileys). Placeholder characters already
/// in @p raw are dropped, so a peer cannot inject an image.
[[nodiscard]] QString protect(const QString& raw);

/// @p html with each placeholder replaced by its 16x16 image.
[[nodiscard]] QString expand(const QString& html);

/// Plain text to HTML: escaped, with smileys as images.
[[nodiscard]] QString render(const QString& raw);

} // namespace eMule::Smileys
