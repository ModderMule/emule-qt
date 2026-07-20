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
#include "search/SearchParams.h"

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

// ---------------------------------------------------------------------------
// buildSearchTermsPayload — full search payload (expression + filters)
// ---------------------------------------------------------------------------

/// Build the binary search-terms payload for @p params: the parsed boolean
/// expression AND-combined with every active filter (type, size, availability,
/// extension, complete sources, media tags).
///
/// This is the port of MFC `GetSearchPacket` (srchybrid/SearchResultsWnd.cpp:965),
/// which official eMule uses for both the ED2K server search packet and the Kad
/// KADEMLIA2_SEARCH_KEY_REQ search-terms blob — the wire format is the same.
///
/// @param kadKeyword When non-empty, the search is a Kad search indexed under
///        this keyword. The keyword is then dropped from the filename terms
///        (the target hash already encodes it) and, when the expression is a
///        plain AND-chain of filename terms, the remaining terms are collapsed
///        into a single space-joined string term — matching official's layout,
///        which the receiving node re-tokenizes and ANDs.
/// @return Encoded payload, empty if the expression fails to parse or there is
///         nothing left to send.
[[nodiscard]] QByteArray buildSearchTermsPayload(const SearchParams& params,
                                                  const QString& kadKeyword = QString());

} // namespace eMule
