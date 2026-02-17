#pragma once

/// @file SearchExprParser.h
/// @brief Search expression parser — replaces MFC Bison/Yacc Parser.y + Scanner.l.
///
/// Hand-written recursive descent parser that converts user-entered search query
/// strings into SearchExpr (prefix-notation) expressions for ED2K/Kad searches.
///
/// Supports:
///   - Implicit AND: "a b" → AND(a, b)
///   - Explicit operators: AND, OR, NOT
///   - Parenthesized grouping: "(a OR b) AND c"
///   - Attribute filters: @size>10M, @type=audio, @ext=mp3, etc.
///   - Quoted strings: "exact phrase"
///   - Dash prefix for NOT: "a -b" → a NOT b
///   - ED2K file links
///
/// Operator precedence (lowest to highest): AND < OR < NOT
/// This matches the original eMule grammar where OR binds tighter than AND.

#include "search/SearchExpr.h"

#include <QStringList>

namespace eMule {

// ---------------------------------------------------------------------------
// ParseResult — output from the search expression parser
// ---------------------------------------------------------------------------

struct ParseResult {
    SearchExpr expr;
    QStringList errors;

    /// True if parsing succeeded without errors.
    [[nodiscard]] bool success() const { return errors.isEmpty(); }
};

// ---------------------------------------------------------------------------
// parseSearchExpression — parse a user-entered search string
// ---------------------------------------------------------------------------

/// Parse a user-entered search expression string into a SearchExpr (prefix notation).
///
/// The resulting SearchExpr stores elements in prefix (Polish) notation:
///   OPERATOR, LEFT_SUBTREE, RIGHT_SUBTREE
/// matching the original eMule Parser.y output format consumed by the
/// ED2K/Kad packet builder.
///
/// @param input The search expression entered by the user.
/// @param keepQuotedStrings If true, preserve literal quote marks around quoted
///                          strings (needed for Kad keyword searches).
/// @return ParseResult with the expression and any errors.
[[nodiscard]] ParseResult parseSearchExpression(const QString& input,
                                                 bool keepQuotedStrings = false);

} // namespace eMule
