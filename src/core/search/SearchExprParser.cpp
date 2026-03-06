#include "pch.h"
/// @file SearchExprParser.cpp
/// @brief Search expression parser — port of MFC Parser.y + Scanner.l.
///
/// Hand-written recursive descent parser with integrated lexer,
/// replacing the original Bison/Yacc + Flex implementation.

#include "search/SearchExprParser.h"
#include "protocol/ED2KLink.h"

#include <QByteArray>
#include <QString>

#include <cctype>
#include <cstring>

namespace eMule {

namespace {

// ---------------------------------------------------------------------------
// Token types — internal to the parser
// ---------------------------------------------------------------------------

enum class TokenType {
    String,       // keyword or quoted string
    And, Or, Not, // boolean operators
    OpEq,         // =
    OpLt,         // <
    OpLe,         // <=
    OpGt,         // >
    OpGe,         // >=
    OpNe,         // <>
    AtSize, AtType, AtExt,
    AtSources, AtComplete,
    AtBitrate, AtLength,
    AtCodec, AtRating,
    AtTitle, AtAlbum, AtArtist,
    ED2KLink,     // ed2k:// file link (hash extracted)
    LParen,       // (
    RParen,       // )
    Eof,
    Error
};

struct Token {
    TokenType type = TokenType::Eof;
    QByteArray str;
    uint64 num = 0;
};

// ---------------------------------------------------------------------------
// Character classification
// ---------------------------------------------------------------------------

/// Keyword characters: everything except space, quote, parens, and comparison ops.
/// Matches the original flex rule: [^ \"()<>=]
bool isKeywordChar(char c)
{
    return c != ' ' && c != '"' && c != '(' && c != ')'
        && c != '<' && c != '>' && c != '=' && c != '\0';
}

/// Case-insensitive prefix match with minimum match length.
/// Returns true if @p input (length @p inputLen) is an abbreviation of @p match
/// with at least @p minMatch characters. Replicates MFC opt_strnicmp().
bool prefixMatch(const char* input, int inputLen, const char* match, int minMatch)
{
    if (inputLen < minMatch)
        return false;
    int matchLen = static_cast<int>(std::strlen(match));
    if (inputLen > matchLen)
        return false;
    for (int i = 0; i < inputLen; ++i) {
        if (std::tolower(static_cast<unsigned char>(input[i]))
            != std::tolower(static_cast<unsigned char>(match[i])))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Parser — recursive descent with integrated tokenizer
// ---------------------------------------------------------------------------

class Parser {
public:
    ParseResult parse(const QString& input, bool keepQuotedStrings)
    {
        m_utf8 = input.toUtf8();
        m_pos = m_utf8.constData();
        m_end = m_pos + m_utf8.size();
        m_keepQuotedStrings = keepQuotedStrings;
        m_errors.clear();

        ParseResult result;

        m_current = scanToken();

        if (m_current.type == TokenType::Eof)
            return result;

        if (m_current.type == TokenType::ED2KLink) {
            result.expr = SearchExpr(SearchAttr(m_current.str));
            m_current = scanToken();
        } else {
            result.expr = parseAndExpr();
        }

        if (m_current.type != TokenType::Eof && m_errors.isEmpty())
            addError(QStringLiteral("Unexpected input after search expression"));

        result.errors = m_errors;
        return result;
    }

private:
    // -----------------------------------------------------------------------
    // Tokenizer
    // -----------------------------------------------------------------------

    void skipSpaces()
    {
        while (m_pos < m_end && *m_pos == ' ')
            ++m_pos;
    }

    /// Scan the next token in INITIAL context.
    Token scanToken()
    {
        skipSpaces();
        if (m_pos >= m_end)
            return {TokenType::Eof, {}, 0};

        char c = *m_pos;

        if (c == '(') { ++m_pos; return {TokenType::LParen, {}, 0}; }
        if (c == ')') { ++m_pos; return {TokenType::RParen, {}, 0}; }

        // Comparison operators
        if (c == '=') { ++m_pos; return {TokenType::OpEq, {}, 0}; }
        if (c == '<') {
            ++m_pos;
            if (m_pos < m_end) {
                if (*m_pos == '=') { ++m_pos; return {TokenType::OpLe, {}, 0}; }
                if (*m_pos == '>') { ++m_pos; return {TokenType::OpNe, {}, 0}; }
            }
            return {TokenType::OpLt, {}, 0};
        }
        if (c == '>') {
            ++m_pos;
            if (m_pos < m_end && *m_pos == '=') { ++m_pos; return {TokenType::OpGe, {}, 0}; }
            return {TokenType::OpGt, {}, 0};
        }

        // Dash → NOT
        if (c == '-') {
            ++m_pos;
            return {TokenType::Not, {}, 0};
        }

        // Quoted string
        if (c == '"')
            return scanQuotedString(false);

        // @ attributes
        if (c == '@')
            return scanAttribute();

        // ED2K link detection
        if (m_end - m_pos >= 14
            && std::strncmp(m_pos, "ed2k://|file|", 13) == 0) {
            return scanED2KLink();
        }

        // Keyword
        return scanKeyword();
    }

    /// Scan a quoted string. Handles escape sequences matching the original Scanner.l.
    Token scanQuotedString(bool inStringContext)
    {
        ++m_pos; // skip opening quote

        QByteArray str;
        while (m_pos < m_end && *m_pos != '"') {
            char c = *m_pos;
            if (c == '\n') {
                addError(QStringLiteral("Unterminated string"));
                return {TokenType::Error, {}, 0};
            }
            if (c == '\\') {
                ++m_pos;
                if (m_pos >= m_end) {
                    addError(QStringLiteral("Unterminated escape sequence"));
                    return {TokenType::Error, {}, 0};
                }
                c = *m_pos;
                switch (c) {
                case '\n': ++m_pos; continue;
                case 't':  str += '\t'; break;
                case 'n':  str += '\n'; break;
                case 'f':  str += '\f'; break;
                case 'r':  str += '\r'; break;
                case '\\': str += '\\'; break;
                case '"':  str += '"'; break;
                case '\'': str += '\''; break;
                case '?':  str += '?'; break;
                case 'v':  str += '\v'; break;
                case 'a':  str += '\a'; break;
                case 'b':  str += '\b'; break;
                case 'x': {
                    ++m_pos;
                    int val = 0;
                    int n = 0;
                    while (n < 3 && m_pos < m_end) {
                        int digit = -1;
                        c = *m_pos;
                        if (c >= '0' && c <= '9') digit = c - '0';
                        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                        if (digit < 0) break;
                        val = val * 16 + digit;
                        ++m_pos;
                        ++n;
                    }
                    str += (n == 0) ? 'x' : static_cast<char>(val);
                    continue; // m_pos already advanced
                }
                default:
                    str += c;
                    break;
                }
                ++m_pos;
            } else {
                str += c;
                ++m_pos;
            }
        }

        if (m_pos < m_end && *m_pos == '"')
            ++m_pos; // skip closing quote

        // Skip empty strings and empty quoted strings (matching original behavior)
        if (str.isEmpty())
            return scanToken();

        Token tok;
        tok.type = TokenType::String;
        if (m_keepQuotedStrings && !inStringContext)
            tok.str = '"' + str + '"';
        else
            tok.str = str;
        return tok;
    }

    /// Scan an unquoted keyword. Recognizes AND/OR/NOT as operators.
    Token scanKeyword()
    {
        const char* start = m_pos;
        while (m_pos < m_end && isKeywordChar(*m_pos))
            ++m_pos;

        int len = static_cast<int>(m_pos - start);
        if (len == 0)
            return scanToken(); // skip and retry

        QByteArray word(start, len);

        if (word == "AND") return {TokenType::And, {}, 0};
        if (word == "OR")  return {TokenType::Or, {}, 0};
        if (word == "NOT") return {TokenType::Not, {}, 0};

        return {TokenType::String, word, 0};
    }

    /// Scan an @attribute name. Supports minimum 3-character abbreviations.
    Token scanAttribute()
    {
        ++m_pos; // skip @

        const char* start = m_pos;
        while (m_pos < m_end && std::isalpha(static_cast<unsigned char>(*m_pos)))
            ++m_pos;

        int len = static_cast<int>(m_pos - start);
        if (len == 0) {
            addError(QStringLiteral("Invalid attribute: @"));
            return {TokenType::Error, {}, 0};
        }

        if (prefixMatch(start, len, "size", 3))       return {TokenType::AtSize, {}, 0};
        if (prefixMatch(start, len, "type", 3))       return {TokenType::AtType, {}, 0};
        if (prefixMatch(start, len, "ext", 3))        return {TokenType::AtExt, {}, 0};
        if (prefixMatch(start, len, "availability", 3)
            || prefixMatch(start, len, "sources", 3))  return {TokenType::AtSources, {}, 0};
        if (prefixMatch(start, len, "complete", 3))    return {TokenType::AtComplete, {}, 0};
        if (prefixMatch(start, len, "bitrate", 3))     return {TokenType::AtBitrate, {}, 0};
        if (prefixMatch(start, len, "length", 3))      return {TokenType::AtLength, {}, 0};
        if (prefixMatch(start, len, "codec", 3))       return {TokenType::AtCodec, {}, 0};
        if (prefixMatch(start, len, "rating", 3))      return {TokenType::AtRating, {}, 0};
        if (prefixMatch(start, len, "title", 3))       return {TokenType::AtTitle, {}, 0};
        if (prefixMatch(start, len, "album", 3))       return {TokenType::AtAlbum, {}, 0};
        if (prefixMatch(start, len, "artist", 3))      return {TokenType::AtArtist, {}, 0};

        addError(QStringLiteral("Unknown attribute: @%1")
                     .arg(QString::fromUtf8(start, len)));
        return {TokenType::Error, {}, 0};
    }

    /// Scan an ed2k://|file|...|/ link, extract the hash.
    Token scanED2KLink()
    {
        const char* start = m_pos;

        // Scan until we find "|/" terminator or end of non-whitespace
        while (m_pos < m_end) {
            if (*m_pos == '|' && (m_pos + 1) < m_end && *(m_pos + 1) == '/') {
                m_pos += 2; // consume |/
                break;
            }
            if (*m_pos == ' ' || *m_pos == '\0')
                break;
            ++m_pos;
        }

        QString linkStr = QString::fromUtf8(start, static_cast<int>(m_pos - start));
        auto link = parseED2KLink(linkStr);
        if (!link || !std::holds_alternative<ED2KFileLink>(*link)) {
            addError(QStringLiteral("Invalid ED2K file link"));
            return {TokenType::Error, {}, 0};
        }

        const auto& fileLink = std::get<ED2KFileLink>(*link);
        // Build "ed2k::<hexhash>" string matching original format
        QByteArray hashHex;
        hashHex.reserve(38); // "ed2k::" + 32 hex chars
        hashHex.append("ed2k::");
        for (size_t i = 0; i < 16; ++i) {
            constexpr char hexDigits[] = "0123456789abcdef";
            hashHex += hexDigits[static_cast<size_t>((fileLink.hash[i] >> 4) & 0x0F)];
            hashHex += hexDigits[static_cast<size_t>(fileLink.hash[i] & 0x0F)];
        }

        return {TokenType::ED2KLink, hashHex, 0};
    }

    // -----------------------------------------------------------------------
    // Context-sensitive value scanners (called by parser, not by scanToken)
    // -----------------------------------------------------------------------

    /// Read a comparison operator from the current token.
    bool readComparisonOp(uint32& op)
    {
        switch (m_current.type) {
        case TokenType::OpEq: op = ED2K_SEARCH_OP_EQUAL; return true;
        case TokenType::OpGt: op = ED2K_SEARCH_OP_GREATER; return true;
        case TokenType::OpLt: op = ED2K_SEARCH_OP_LESS; return true;
        case TokenType::OpGe: op = ED2K_SEARCH_OP_GREATER_EQUAL; return true;
        case TokenType::OpLe: op = ED2K_SEARCH_OP_LESS_EQUAL; return true;
        case TokenType::OpNe: op = ED2K_SEARCH_OP_NOTEQUAL; return true;
        default: return false;
        }
    }

    /// Scan a number in SIZE context (B/K/M/G suffix, default MB).
    bool scanSizeNumber(uint64& result)
    {
        skipSpaces();
        if (m_pos >= m_end || !std::isdigit(static_cast<unsigned char>(*m_pos))) {
            addError(QStringLiteral("Expected number after @size operator"));
            return false;
        }

        char* endptr = nullptr;
        double val = std::strtod(m_pos, &endptr);
        m_pos = endptr;

        if (m_pos < m_end) {
            switch (*m_pos) {
            case 'B': case 'b': ++m_pos; break;
            case 'K': case 'k': val *= 1024; ++m_pos; break;
            case 'M': case 'm': val *= 1024 * 1024; ++m_pos; break;
            case 'G': case 'g': val *= 1024.0 * 1024.0 * 1024.0; ++m_pos; break;
            default: val *= 1024 * 1024; break; // default MB
            }
        } else {
            val *= 1024 * 1024; // default MB
        }

        result = static_cast<uint64>(val + 0.5);
        return true;
    }

    /// Scan a number in NUMBER context (k/m/g decimal multiplier).
    bool scanPlainNumber(uint64& result)
    {
        skipSpaces();
        if (m_pos >= m_end || !std::isdigit(static_cast<unsigned char>(*m_pos))) {
            addError(QStringLiteral("Expected number"));
            return false;
        }

        char* endptr = nullptr;
        double val = std::strtod(m_pos, &endptr);
        m_pos = endptr;

        if (m_pos < m_end) {
            switch (*m_pos) {
            case 'k': val *= 1000; ++m_pos; break;
            case 'm': val *= 1000000; ++m_pos; break;
            case 'g': val *= 1000000000; ++m_pos; break;
            default: break;
            }
        }

        result = static_cast<uint64>(val + 0.5);
        return true;
    }

    /// Scan a number in LENGTH context (s/m/h suffix, or m:s / h:m:s format).
    bool scanLengthNumber(uint64& result)
    {
        skipSpaces();
        if (m_pos >= m_end) {
            addError(QStringLiteral("Expected number or time value after @length operator"));
            return false;
        }

        // Try h:m:s or m:s colon-separated format
        const char* p = m_pos;
        while (p < m_end && std::isdigit(static_cast<unsigned char>(*p)))
            ++p;

        if (p > m_pos && p < m_end && *p == ':') {
            // Looks like a time format
            unsigned int v1 = 0;
            for (const char* d = m_pos; d < p; ++d)
                v1 = v1 * 10 + static_cast<unsigned>(*d - '0');

            const char* p2 = p + 1;
            const char* p2start = p2;
            while (p2 < m_end && std::isdigit(static_cast<unsigned char>(*p2)))
                ++p2;

            if (p2 > p2start) {
                unsigned int v2 = 0;
                for (const char* d = p2start; d < p2; ++d)
                    v2 = v2 * 10 + static_cast<unsigned>(*d - '0');

                if (p2 < m_end && *p2 == ':') {
                    // h:m:s
                    const char* p3 = p2 + 1;
                    const char* p3start = p3;
                    while (p3 < m_end && std::isdigit(static_cast<unsigned char>(*p3)))
                        ++p3;
                    if (p3 > p3start) {
                        unsigned int v3 = 0;
                        for (const char* d = p3start; d < p3; ++d)
                            v3 = v3 * 10 + static_cast<unsigned>(*d - '0');
                        m_pos = p3;
                        result = v3 + v2 * 60 + v1 * 3600;
                        return true;
                    }
                }

                // m:s
                m_pos = p2;
                result = v2 + v1 * 60;
                return true;
            }
        }

        // Regular number with optional s/m/h suffix
        if (!std::isdigit(static_cast<unsigned char>(*m_pos))) {
            addError(QStringLiteral("Expected number or time value after @length operator"));
            return false;
        }

        char* endptr = nullptr;
        double val = std::strtod(m_pos, &endptr);
        m_pos = endptr;

        if (m_pos < m_end) {
            switch (*m_pos) {
            case 's': ++m_pos; break;
            case 'm': val *= 60; ++m_pos; break;
            case 'h': val *= 3600; ++m_pos; break;
            default: break;
            }
        }

        result = static_cast<uint64>(val + 0.5);
        return true;
    }

    /// Scan a type value after @type= (audio, video, image, document, program, archive, iso/cd).
    bool scanTypeValue(QByteArray& result)
    {
        skipSpaces();
        const char* start = m_pos;
        while (m_pos < m_end && std::isalpha(static_cast<unsigned char>(*m_pos)))
            ++m_pos;

        int len = static_cast<int>(m_pos - start);
        if (len == 0) {
            addError(QStringLiteral("Expected type value after @type="));
            return false;
        }

        if (prefixMatch(start, len, "audio", 3))
            result = QByteArray(ED2KFTSTR_AUDIO);
        else if (prefixMatch(start, len, "video", 3))
            result = QByteArray(ED2KFTSTR_VIDEO);
        else if (prefixMatch(start, len, "image", 3)
                 || prefixMatch(start, len, "img", 3))
            result = QByteArray(ED2KFTSTR_IMAGE);
        else if (prefixMatch(start, len, "document", 3))
            result = QByteArray(ED2KFTSTR_DOCUMENT);
        else if (prefixMatch(start, len, "program", 3))
            result = QByteArray(ED2KFTSTR_PROGRAM);
        else if (prefixMatch(start, len, "archive", 3))
            result = QByteArray(ED2KFTSTR_ARCHIVE);
        else if (prefixMatch(start, len, "iso", 3)
                 || prefixMatch(start, len, "cd", 2))
            result = QByteArray(ED2KFTSTR_CDIMAGE);
        else {
            addError(QStringLiteral("Invalid @type value: %1")
                         .arg(QString::fromUtf8(start, len)));
            return false;
        }

        return true;
    }

    /// Scan a keyword/string value in STRING context (after @ext=, @codec=, etc.).
    bool scanStringValue(QByteArray& result)
    {
        skipSpaces();

        if (m_pos < m_end && *m_pos == '"') {
            Token tok = scanQuotedString(true);
            if (tok.type == TokenType::Error)
                return false;
            result = tok.str;
            return true;
        }

        const char* start = m_pos;
        while (m_pos < m_end && isKeywordChar(*m_pos))
            ++m_pos;

        int len = static_cast<int>(m_pos - start);
        if (len == 0) {
            addError(QStringLiteral("Expected value after attribute operator"));
            return false;
        }

        result = QByteArray(start, len);
        return true;
    }

    void advance() { m_current = scanToken(); }

    void addError(const QString& msg) { m_errors.append(msg); }

    // -----------------------------------------------------------------------
    // Parser — recursive descent
    //
    // Grammar (precedence lowest → highest):
    //   and_expr:  or_expr ((AND | implicit) or_expr)*
    //   or_expr:   not_expr (OR not_expr)*
    //   not_expr:  primary (NOT primary)*
    //   primary:   attribute | '(' and_expr ')'
    // -----------------------------------------------------------------------

    /// Check if the current token can start a primary expression.
    bool canStartPrimary() const
    {
        switch (m_current.type) {
        case TokenType::String:
        case TokenType::ED2KLink:
        case TokenType::AtSize:
        case TokenType::AtType:
        case TokenType::AtExt:
        case TokenType::AtSources:
        case TokenType::AtComplete:
        case TokenType::AtBitrate:
        case TokenType::AtLength:
        case TokenType::AtCodec:
        case TokenType::AtRating:
        case TokenType::AtTitle:
        case TokenType::AtAlbum:
        case TokenType::AtArtist:
        case TokenType::LParen:
            return true;
        default:
            return false;
        }
    }

    /// and_expr: or_expr ((AND | implicit) or_expr)*
    /// AND has lowest precedence. Implicit AND = juxtaposed terms.
    SearchExpr parseAndExpr()
    {
        SearchExpr result = parseOrExpr();
        if (!m_errors.isEmpty()) return result;

        while (true) {
            bool explicitAnd = (m_current.type == TokenType::And);
            if (explicitAnd) {
                advance();
                if (!canStartPrimary()) {
                    addError(QStringLiteral("Missing expression after AND"));
                    return result;
                }
            } else if (!canStartPrimary()) {
                break;
            }

            SearchExpr right = parseOrExpr();
            if (!m_errors.isEmpty()) return result;

            SearchExpr combined;
            combined.add(SearchOperator::And);
            combined.add(result);
            combined.add(right);
            result = std::move(combined);
        }

        return result;
    }

    /// or_expr: not_expr (OR not_expr)*
    SearchExpr parseOrExpr()
    {
        SearchExpr result = parseNotExpr();
        if (!m_errors.isEmpty()) return result;

        while (m_current.type == TokenType::Or) {
            advance();
            if (!canStartPrimary()) {
                addError(QStringLiteral("Missing expression after OR"));
                return result;
            }
            SearchExpr right = parseNotExpr();
            if (!m_errors.isEmpty()) return result;

            SearchExpr combined;
            combined.add(SearchOperator::Or);
            combined.add(result);
            combined.add(right);
            result = std::move(combined);
        }

        return result;
    }

    /// not_expr: primary (NOT primary)*
    SearchExpr parseNotExpr()
    {
        SearchExpr result = parsePrimary();
        if (!m_errors.isEmpty()) return result;

        while (m_current.type == TokenType::Not) {
            advance();
            if (!canStartPrimary()) {
                addError(QStringLiteral("Missing expression after NOT"));
                return result;
            }
            SearchExpr right = parsePrimary();
            if (!m_errors.isEmpty()) return result;

            SearchExpr combined;
            combined.add(SearchOperator::Not);
            combined.add(result);
            combined.add(right);
            result = std::move(combined);
        }

        return result;
    }

    /// primary: attribute | '(' and_expr ')'
    SearchExpr parsePrimary()
    {
        if (m_current.type == TokenType::LParen) {
            advance();
            SearchExpr result = parseAndExpr();
            if (!m_errors.isEmpty()) return result;

            if (m_current.type != TokenType::RParen) {
                addError(QStringLiteral("Missing closing parenthesis"));
                return result;
            }
            advance();
            return result;
        }

        return parseAttributeExpr();
    }

    /// Parse a single attribute: keyword, filter, or ed2k link.
    SearchExpr parseAttributeExpr()
    {
        switch (m_current.type) {
        case TokenType::String: {
            SearchExpr expr(SearchAttr(m_current.str));
            advance();
            return expr;
        }

        case TokenType::ED2KLink: {
            SearchExpr expr(SearchAttr(m_current.str));
            advance();
            return expr;
        }

        // -- Numeric attribute filters --

        case TokenType::AtSize:     return parseNumericAttr(FT_FILESIZE, QStringLiteral("@size"), true);
        case TokenType::AtSources:  return parseNumericAttr(FT_SOURCES, QStringLiteral("@sources"), false);
        case TokenType::AtComplete: return parseNumericAttr(FT_COMPLETE_SOURCES, QStringLiteral("@complete"), false);
        case TokenType::AtRating:   return parseNumericAttr(FT_FILERATING, QStringLiteral("@rating"), false);
        case TokenType::AtBitrate:  return parseNumericAttr(FT_MEDIA_BITRATE, QStringLiteral("@bitrate"), false);
        case TokenType::AtLength:   return parseLengthAttr();

        // -- Type filter --

        case TokenType::AtType: {
            advance(); // past @type
            if (m_current.type != TokenType::OpEq) {
                addError(QStringLiteral("Expected = after @type"));
                return {};
            }
            // m_pos is now past the = sign
            QByteArray typeVal;
            if (!scanTypeValue(typeVal)) return {};
            m_current = scanToken();
            return SearchExpr(SearchAttr(FT_FILETYPE, typeVal));
        }

        // -- String attribute filters --

        case TokenType::AtExt:    return parseStringAttr(FT_FILEFORMAT, QStringLiteral("@ext"));
        case TokenType::AtCodec:  return parseStringAttr(FT_MEDIA_CODEC, QStringLiteral("@codec"));
        case TokenType::AtTitle:  return parseStringAttr(FT_MEDIA_TITLE, QStringLiteral("@title"));
        case TokenType::AtAlbum:  return parseStringAttr(FT_MEDIA_ALBUM, QStringLiteral("@album"));
        case TokenType::AtArtist: return parseStringAttr(FT_MEDIA_ARTIST, QStringLiteral("@artist"));

        default:
            addError(QStringLiteral("Expected search term or attribute"));
            return {};
        }
    }

    /// Parse a numeric attribute: @attr OP NUMBER
    SearchExpr parseNumericAttr(int tag, const QString& attrName, bool isSizeContext)
    {
        advance(); // past attribute token
        uint32 op = 0;
        if (!readComparisonOp(op)) {
            addError(QStringLiteral("Expected comparison operator after %1").arg(attrName));
            return {};
        }
        // m_pos is past the operator (advance was already called by scanToken for the op)

        uint64 num = 0;
        bool ok = isSizeContext ? scanSizeNumber(num) : scanPlainNumber(num);
        if (!ok) return {};

        m_current = scanToken();
        return SearchExpr(SearchAttr(tag, op, num));
    }

    /// Parse @length OP VALUE (special: supports s/m/h suffix and h:m:s format)
    SearchExpr parseLengthAttr()
    {
        advance(); // past @length
        uint32 op = 0;
        if (!readComparisonOp(op)) {
            addError(QStringLiteral("Expected comparison operator after @length"));
            return {};
        }

        uint64 num = 0;
        if (!scanLengthNumber(num)) return {};

        m_current = scanToken();
        return SearchExpr(SearchAttr(FT_MEDIA_LENGTH, op, num));
    }

    /// Parse a string attribute: @attr = STRING
    SearchExpr parseStringAttr(int tag, const QString& attrName)
    {
        advance(); // past attribute token
        if (m_current.type != TokenType::OpEq) {
            addError(QStringLiteral("Expected = after %1").arg(attrName));
            return {};
        }
        // m_pos is past the = sign

        QByteArray strVal;
        if (!scanStringValue(strVal)) return {};

        m_current = scanToken();
        return SearchExpr(SearchAttr(tag, strVal));
    }

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    QByteArray m_utf8;
    const char* m_pos = nullptr;
    const char* m_end = nullptr;
    bool m_keepQuotedStrings = false;
    QStringList m_errors;
    Token m_current;
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ParseResult parseSearchExpression(const QString& input, bool keepQuotedStrings)
{
    Parser parser;
    return parser.parse(input, keepQuotedStrings);
}

} // namespace eMule
