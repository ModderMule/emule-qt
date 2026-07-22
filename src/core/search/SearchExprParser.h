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

#include <memory>

namespace eMule {

class Packet;
class Server;

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

// ---------------------------------------------------------------------------
// buildGlobalSearchPacket — server-UDP global-search packet with opcode selection
// ---------------------------------------------------------------------------

/// Build the server-UDP global-search packet for @p server, selecting the opcode
/// from the server's UDP flags — port of MFC CSearchResultsWnd::OnTimer
/// (srchybrid/SearchResultsWnd.cpp:267-303):
///   - large-files + ext-get-files → OP_GLOBSEARCHREQ3 (prepends a
///     CT_SERVER_UDPSEARCH_FLAGS = SRVCAP_UDP_NEWTAGS_LARGEFILES tag)
///   - ext-get-files                → OP_GLOBSEARCHREQ2
///   - otherwise                    → legacy OP_GLOBSEARCHREQ
///
/// @param searchTerms The encoded search tree (e.g. from SearchExpr::toBytes()).
/// @param is64BitSearch True when the search carries a >4 GiB size condition; such
///        a request is skipped (returns nullptr) for servers without large-file
///        UDP support, matching the reference's b64BitSearchPacket guard.
/// @return The packet (prot = OP_EDONKEYPROT), or nullptr if the server must be
///         skipped. Obfuscation/obf-port are applied later by UDPSocket::sendPacket
///         when the server has a UDP key and advertises UDP obfuscation.
[[nodiscard]] std::unique_ptr<Packet> buildGlobalSearchPacket(const Server& server,
                                                              const QByteArray& searchTerms,
                                                              bool is64BitSearch);

} // namespace eMule
