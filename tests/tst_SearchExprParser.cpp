/// @file tst_SearchExprParser.cpp
/// @brief Tests for search/SearchExprParser — search expression parsing.
///
/// Verifies the hand-written recursive descent parser that replaces the
/// original Bison/Yacc Parser.y + Flex Scanner.l implementation.

#include "TestHelpers.h"
#include "search/SearchExprParser.h"
#include "search/SearchExpr.h"

#include <QTest>

using namespace eMule;

class tst_SearchExprParser : public QObject {
    Q_OBJECT

private slots:
    // --- Basic keywords ---
    void empty_input();
    void single_keyword();
    void two_keywords_implicitAnd();
    void three_keywords_implicitAnd();
    void explicit_and();
    void explicit_or();
    void explicit_not();

    // --- Operator precedence ---
    void or_binds_tighter_than_and();
    void not_binds_tighter_than_or();
    void mixed_and_or_not();

    // --- Dash prefix NOT ---
    void dash_not();
    void dash_not_with_keyword_before();

    // --- Parentheses ---
    void simple_parens();
    void nested_parens();
    void parens_override_precedence();

    // --- Quoted strings ---
    void quoted_string();
    void quoted_string_with_escape();
    void quoted_string_keepQuoted();
    void empty_quoted_string_skipped();

    // --- Attribute filters: @size ---
    void size_greater();
    void size_equal_megabytes();
    void size_with_bytes_suffix();
    void size_with_kilobytes_suffix();
    void size_with_gigabytes_suffix();
    void size_default_megabytes();

    // --- Attribute filters: @type ---
    void type_audio();
    void type_video();
    void type_image();
    void type_document();
    void type_program();
    void type_archive();
    void type_iso();
    void type_abbreviation();

    // --- Attribute filters: other ---
    void ext_filter();
    void sources_filter();
    void complete_filter();
    void rating_filter();
    void bitrate_filter();
    void codec_filter();
    void title_filter();
    void album_filter();
    void artist_filter();

    // --- Attribute filter: @length ---
    void length_seconds();
    void length_minutes();
    void length_hours();
    void length_colon_ms();
    void length_colon_hms();

    // --- Attribute abbreviations ---
    void attr_abbreviation_3chars();

    // --- Comparison operators ---
    void all_comparison_operators();

    // --- Complex expressions ---
    void keyword_with_size_filter();
    void keyword_with_multiple_filters();

    // --- Error cases ---
    void error_unclosed_paren();
    void error_unknown_attribute();
    void error_missing_after_and();
    void error_missing_size_operator();
    void error_invalid_type_value();
    void error_missing_string_value();
};

// Helper to check the i-th element of the expression
static void verifyAttr(const SearchExpr& expr, std::size_t index,
                        const QByteArray& expectedStr, int expectedTag = FT_FILENAME)
{
    QVERIFY2(index < expr.m_expr.size(),
             qPrintable(QStringLiteral("Index %1 out of range (size=%2)")
                            .arg(index).arg(expr.m_expr.size())));
    QCOMPARE(expr.m_expr[index].m_str, expectedStr);
    QCOMPARE(expr.m_expr[index].m_tag, expectedTag);
}

static void verifyOp(const SearchExpr& expr, std::size_t index,
                      const char* expectedToken)
{
    QVERIFY2(index < expr.m_expr.size(),
             qPrintable(QStringLiteral("Index %1 out of range (size=%2)")
                            .arg(index).arg(expr.m_expr.size())));
    QCOMPARE(expr.m_expr[index].m_str, QByteArray(expectedToken));
}

static void verifyNumeric(const SearchExpr& expr, std::size_t index,
                           int expectedTag, uint32 expectedOp, uint64 expectedNum)
{
    QVERIFY2(index < expr.m_expr.size(),
             qPrintable(QStringLiteral("Index %1 out of range (size=%2)")
                            .arg(index).arg(expr.m_expr.size())));
    QCOMPARE(expr.m_expr[index].m_tag, expectedTag);
    QCOMPARE(expr.m_expr[index].m_integerOperator, expectedOp);
    QCOMPARE(expr.m_expr[index].m_num, expectedNum);
}

// -----------------------------------------------------------------------
// Basic keywords
// -----------------------------------------------------------------------

void tst_SearchExprParser::empty_input()
{
    auto r = parseSearchExpression(QStringLiteral(""));
    QVERIFY(r.success());
    QVERIFY(r.expr.m_expr.empty());
}

void tst_SearchExprParser::single_keyword()
{
    auto r = parseSearchExpression(QStringLiteral("music"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{1});
    verifyAttr(r.expr, 0, "music");
}

void tst_SearchExprParser::two_keywords_implicitAnd()
{
    // "a b" → [AND, a, b]  (prefix notation)
    auto r = parseSearchExpression(QStringLiteral("a b"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{3});
    verifyOp(r.expr, 0, kSearchOpTokenAnd);
    verifyAttr(r.expr, 1, "a");
    verifyAttr(r.expr, 2, "b");
}

void tst_SearchExprParser::three_keywords_implicitAnd()
{
    // "a b c" → [AND, AND, a, b, c]  (left-associative)
    auto r = parseSearchExpression(QStringLiteral("a b c"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{5});
    verifyOp(r.expr, 0, kSearchOpTokenAnd);
    verifyOp(r.expr, 1, kSearchOpTokenAnd);
    verifyAttr(r.expr, 2, "a");
    verifyAttr(r.expr, 3, "b");
    verifyAttr(r.expr, 4, "c");
}

void tst_SearchExprParser::explicit_and()
{
    auto r = parseSearchExpression(QStringLiteral("a AND b"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{3});
    verifyOp(r.expr, 0, kSearchOpTokenAnd);
    verifyAttr(r.expr, 1, "a");
    verifyAttr(r.expr, 2, "b");
}

void tst_SearchExprParser::explicit_or()
{
    auto r = parseSearchExpression(QStringLiteral("a OR b"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{3});
    verifyOp(r.expr, 0, kSearchOpTokenOr);
    verifyAttr(r.expr, 1, "a");
    verifyAttr(r.expr, 2, "b");
}

void tst_SearchExprParser::explicit_not()
{
    auto r = parseSearchExpression(QStringLiteral("a NOT b"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{3});
    verifyOp(r.expr, 0, kSearchOpTokenNot);
    verifyAttr(r.expr, 1, "a");
    verifyAttr(r.expr, 2, "b");
}

// -----------------------------------------------------------------------
// Operator precedence
// -----------------------------------------------------------------------

void tst_SearchExprParser::or_binds_tighter_than_and()
{
    // "a AND b OR c" → AND(a, OR(b, c)) → [AND, a, OR, b, c]
    auto r = parseSearchExpression(QStringLiteral("a AND b OR c"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{5});
    verifyOp(r.expr, 0, kSearchOpTokenAnd);
    verifyAttr(r.expr, 1, "a");
    verifyOp(r.expr, 2, kSearchOpTokenOr);
    verifyAttr(r.expr, 3, "b");
    verifyAttr(r.expr, 4, "c");
}

void tst_SearchExprParser::not_binds_tighter_than_or()
{
    // "a OR b NOT c" → OR(a, NOT(b, c)) → [OR, a, NOT, b, c]
    auto r = parseSearchExpression(QStringLiteral("a OR b NOT c"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{5});
    verifyOp(r.expr, 0, kSearchOpTokenOr);
    verifyAttr(r.expr, 1, "a");
    verifyOp(r.expr, 2, kSearchOpTokenNot);
    verifyAttr(r.expr, 3, "b");
    verifyAttr(r.expr, 4, "c");
}

void tst_SearchExprParser::mixed_and_or_not()
{
    // "a b OR c AND d" = AND(AND(a, OR(b, c)), d) → [AND, AND, a, OR, b, c, d]
    auto r = parseSearchExpression(QStringLiteral("a b OR c AND d"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{7});
    verifyOp(r.expr, 0, kSearchOpTokenAnd);
    verifyOp(r.expr, 1, kSearchOpTokenAnd);
    verifyAttr(r.expr, 2, "a");
    verifyOp(r.expr, 3, kSearchOpTokenOr);
    verifyAttr(r.expr, 4, "b");
    verifyAttr(r.expr, 5, "c");
    verifyAttr(r.expr, 6, "d");
}

// -----------------------------------------------------------------------
// Dash prefix NOT
// -----------------------------------------------------------------------

void tst_SearchExprParser::dash_not()
{
    // "a -b" → NOT(a, b) → [NOT, a, b]
    auto r = parseSearchExpression(QStringLiteral("a -b"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{3});
    verifyOp(r.expr, 0, kSearchOpTokenNot);
    verifyAttr(r.expr, 1, "a");
    verifyAttr(r.expr, 2, "b");
}

void tst_SearchExprParser::dash_not_with_keyword_before()
{
    // "a b -c" → AND(a, NOT(b, c)) → [AND, a, NOT, b, c]
    auto r = parseSearchExpression(QStringLiteral("a b -c"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{5});
    verifyOp(r.expr, 0, kSearchOpTokenAnd);
    verifyAttr(r.expr, 1, "a");
    verifyOp(r.expr, 2, kSearchOpTokenNot);
    verifyAttr(r.expr, 3, "b");
    verifyAttr(r.expr, 4, "c");
}

// -----------------------------------------------------------------------
// Parentheses
// -----------------------------------------------------------------------

void tst_SearchExprParser::simple_parens()
{
    auto r = parseSearchExpression(QStringLiteral("(a)"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{1});
    verifyAttr(r.expr, 0, "a");
}

void tst_SearchExprParser::nested_parens()
{
    auto r = parseSearchExpression(QStringLiteral("((a))"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{1});
    verifyAttr(r.expr, 0, "a");
}

void tst_SearchExprParser::parens_override_precedence()
{
    // "(a OR b) AND c" → AND(OR(a, b), c) → [AND, OR, a, b, c]
    auto r = parseSearchExpression(QStringLiteral("(a OR b) AND c"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{5});
    verifyOp(r.expr, 0, kSearchOpTokenAnd);
    verifyOp(r.expr, 1, kSearchOpTokenOr);
    verifyAttr(r.expr, 2, "a");
    verifyAttr(r.expr, 3, "b");
    verifyAttr(r.expr, 4, "c");
}

// -----------------------------------------------------------------------
// Quoted strings
// -----------------------------------------------------------------------

void tst_SearchExprParser::quoted_string()
{
    auto r = parseSearchExpression(QStringLiteral("\"exact phrase\""));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{1});
    verifyAttr(r.expr, 0, "exact phrase");
}

void tst_SearchExprParser::quoted_string_with_escape()
{
    auto r = parseSearchExpression(QStringLiteral("\"hello\\tworld\""));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{1});
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray("hello\tworld"));
}

void tst_SearchExprParser::quoted_string_keepQuoted()
{
    auto r = parseSearchExpression(QStringLiteral("\"test\""), true);
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{1});
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray("\"test\""));
}

void tst_SearchExprParser::empty_quoted_string_skipped()
{
    // Empty quoted string followed by a keyword → just the keyword
    auto r = parseSearchExpression(QStringLiteral("\"\" music"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{1});
    verifyAttr(r.expr, 0, "music");
}

// -----------------------------------------------------------------------
// Attribute filters: @size
// -----------------------------------------------------------------------

void tst_SearchExprParser::size_greater()
{
    auto r = parseSearchExpression(QStringLiteral("@size>10M"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{1});
    verifyNumeric(r.expr, 0, FT_FILESIZE, ED2K_SEARCH_OP_GREATER, 10 * 1024 * 1024);
}

void tst_SearchExprParser::size_equal_megabytes()
{
    auto r = parseSearchExpression(QStringLiteral("@size=100M"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{1});
    verifyNumeric(r.expr, 0, FT_FILESIZE, ED2K_SEARCH_OP_EQUAL, 100ULL * 1024 * 1024);
}

void tst_SearchExprParser::size_with_bytes_suffix()
{
    auto r = parseSearchExpression(QStringLiteral("@size>1024B"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_FILESIZE, ED2K_SEARCH_OP_GREATER, 1024);
}

void tst_SearchExprParser::size_with_kilobytes_suffix()
{
    auto r = parseSearchExpression(QStringLiteral("@size>10K"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_FILESIZE, ED2K_SEARCH_OP_GREATER, 10 * 1024);
}

void tst_SearchExprParser::size_with_gigabytes_suffix()
{
    auto r = parseSearchExpression(QStringLiteral("@size>1G"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_FILESIZE, ED2K_SEARCH_OP_GREATER, 1ULL * 1024 * 1024 * 1024);
}

void tst_SearchExprParser::size_default_megabytes()
{
    // No suffix → default MB
    auto r = parseSearchExpression(QStringLiteral("@size>10"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_FILESIZE, ED2K_SEARCH_OP_GREATER, 10 * 1024 * 1024);
}

// -----------------------------------------------------------------------
// Attribute filters: @type
// -----------------------------------------------------------------------

void tst_SearchExprParser::type_audio()
{
    auto r = parseSearchExpression(QStringLiteral("@type=audio"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{1});
    QCOMPARE(r.expr.m_expr[0].m_tag, FT_FILETYPE);
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray(ED2KFTSTR_AUDIO));
}

void tst_SearchExprParser::type_video()
{
    auto r = parseSearchExpression(QStringLiteral("@type=video"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray(ED2KFTSTR_VIDEO));
}

void tst_SearchExprParser::type_image()
{
    auto r = parseSearchExpression(QStringLiteral("@type=image"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray(ED2KFTSTR_IMAGE));

    // Also accept "img"
    auto r2 = parseSearchExpression(QStringLiteral("@type=img"));
    QVERIFY(r2.success());
    QCOMPARE(r2.expr.m_expr[0].m_str, QByteArray(ED2KFTSTR_IMAGE));
}

void tst_SearchExprParser::type_document()
{
    auto r = parseSearchExpression(QStringLiteral("@type=document"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray(ED2KFTSTR_DOCUMENT));
}

void tst_SearchExprParser::type_program()
{
    auto r = parseSearchExpression(QStringLiteral("@type=program"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray(ED2KFTSTR_PROGRAM));
}

void tst_SearchExprParser::type_archive()
{
    auto r = parseSearchExpression(QStringLiteral("@type=archive"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray(ED2KFTSTR_ARCHIVE));
}

void tst_SearchExprParser::type_iso()
{
    auto r = parseSearchExpression(QStringLiteral("@type=iso"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray(ED2KFTSTR_CDIMAGE));

    // Also accept "cd"
    auto r2 = parseSearchExpression(QStringLiteral("@type=cd"));
    QVERIFY(r2.success());
    QCOMPARE(r2.expr.m_expr[0].m_str, QByteArray(ED2KFTSTR_CDIMAGE));
}

void tst_SearchExprParser::type_abbreviation()
{
    // Minimum 3-char abbreviation: "aud" → Audio
    auto r = parseSearchExpression(QStringLiteral("@type=aud"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray(ED2KFTSTR_AUDIO));
}

// -----------------------------------------------------------------------
// Attribute filters: other
// -----------------------------------------------------------------------

void tst_SearchExprParser::ext_filter()
{
    auto r = parseSearchExpression(QStringLiteral("@ext=mp3"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr[0].m_tag, FT_FILEFORMAT);
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray("mp3"));
}

void tst_SearchExprParser::sources_filter()
{
    auto r = parseSearchExpression(QStringLiteral("@sources>=5"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_SOURCES, ED2K_SEARCH_OP_GREATER_EQUAL, 5);
}

void tst_SearchExprParser::complete_filter()
{
    auto r = parseSearchExpression(QStringLiteral("@complete>3"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_COMPLETE_SOURCES, ED2K_SEARCH_OP_GREATER, 3);
}

void tst_SearchExprParser::rating_filter()
{
    auto r = parseSearchExpression(QStringLiteral("@rating>=3"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_FILERATING, ED2K_SEARCH_OP_GREATER_EQUAL, 3);
}

void tst_SearchExprParser::bitrate_filter()
{
    auto r = parseSearchExpression(QStringLiteral("@bitrate>128"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_MEDIA_BITRATE, ED2K_SEARCH_OP_GREATER, 128);
}

void tst_SearchExprParser::codec_filter()
{
    auto r = parseSearchExpression(QStringLiteral("@codec=mp3"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr[0].m_tag, FT_MEDIA_CODEC);
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray("mp3"));
}

void tst_SearchExprParser::title_filter()
{
    auto r = parseSearchExpression(QStringLiteral("@title=test"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr[0].m_tag, FT_MEDIA_TITLE);
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray("test"));
}

void tst_SearchExprParser::album_filter()
{
    auto r = parseSearchExpression(QStringLiteral("@album=greatest"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr[0].m_tag, FT_MEDIA_ALBUM);
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray("greatest"));
}

void tst_SearchExprParser::artist_filter()
{
    auto r = parseSearchExpression(QStringLiteral("@artist=beatles"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr[0].m_tag, FT_MEDIA_ARTIST);
    QCOMPARE(r.expr.m_expr[0].m_str, QByteArray("beatles"));
}

// -----------------------------------------------------------------------
// Attribute filter: @length
// -----------------------------------------------------------------------

void tst_SearchExprParser::length_seconds()
{
    auto r = parseSearchExpression(QStringLiteral("@length>90s"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_MEDIA_LENGTH, ED2K_SEARCH_OP_GREATER, 90);
}

void tst_SearchExprParser::length_minutes()
{
    auto r = parseSearchExpression(QStringLiteral("@length>3m"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_MEDIA_LENGTH, ED2K_SEARCH_OP_GREATER, 180);
}

void tst_SearchExprParser::length_hours()
{
    auto r = parseSearchExpression(QStringLiteral("@length>1h"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_MEDIA_LENGTH, ED2K_SEARCH_OP_GREATER, 3600);
}

void tst_SearchExprParser::length_colon_ms()
{
    // 1:30 = 90 seconds
    auto r = parseSearchExpression(QStringLiteral("@length>1:30"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_MEDIA_LENGTH, ED2K_SEARCH_OP_GREATER, 90);
}

void tst_SearchExprParser::length_colon_hms()
{
    // 1:02:30 = 3600 + 120 + 30 = 3750
    auto r = parseSearchExpression(QStringLiteral("@length>1:02:30"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_MEDIA_LENGTH, ED2K_SEARCH_OP_GREATER, 3750);
}

// -----------------------------------------------------------------------
// Attribute abbreviations
// -----------------------------------------------------------------------

void tst_SearchExprParser::attr_abbreviation_3chars()
{
    // @siz is valid (minimum 3 chars for "size")
    auto r = parseSearchExpression(QStringLiteral("@siz>10M"));
    QVERIFY(r.success());
    verifyNumeric(r.expr, 0, FT_FILESIZE, ED2K_SEARCH_OP_GREATER, 10 * 1024 * 1024);

    // @si is too short → error
    auto r2 = parseSearchExpression(QStringLiteral("@si>10M"));
    QVERIFY(!r2.success());
}

// -----------------------------------------------------------------------
// Comparison operators
// -----------------------------------------------------------------------

void tst_SearchExprParser::all_comparison_operators()
{
    auto test = [](const QString& op, uint32 expected) {
        auto r = parseSearchExpression(QStringLiteral("@sources%1100").arg(op));
        QVERIFY2(r.success(), qPrintable(r.errors.join(QStringLiteral("; "))));
        QCOMPARE(r.expr.m_expr[0].m_integerOperator, expected);
    };

    test(QStringLiteral("="),  ED2K_SEARCH_OP_EQUAL);
    test(QStringLiteral(">"),  ED2K_SEARCH_OP_GREATER);
    test(QStringLiteral("<"),  ED2K_SEARCH_OP_LESS);
    test(QStringLiteral(">="), ED2K_SEARCH_OP_GREATER_EQUAL);
    test(QStringLiteral("<="), ED2K_SEARCH_OP_LESS_EQUAL);
    test(QStringLiteral("<>"), ED2K_SEARCH_OP_NOTEQUAL);
}

// -----------------------------------------------------------------------
// Complex expressions
// -----------------------------------------------------------------------

void tst_SearchExprParser::keyword_with_size_filter()
{
    // "music @size>10M" → AND(music, @size>10M)
    auto r = parseSearchExpression(QStringLiteral("music @size>10M"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{3});
    verifyOp(r.expr, 0, kSearchOpTokenAnd);
    verifyAttr(r.expr, 1, "music");
    verifyNumeric(r.expr, 2, FT_FILESIZE, ED2K_SEARCH_OP_GREATER, 10 * 1024 * 1024);
}

void tst_SearchExprParser::keyword_with_multiple_filters()
{
    // "music @type=audio @size>5M"
    auto r = parseSearchExpression(QStringLiteral("music @type=audio @size>5M"));
    QVERIFY(r.success());
    QCOMPARE(r.expr.m_expr.size(), std::size_t{5});
    verifyOp(r.expr, 0, kSearchOpTokenAnd);
    verifyOp(r.expr, 1, kSearchOpTokenAnd);
    verifyAttr(r.expr, 2, "music");
    // index 3: @type=audio
    QCOMPARE(r.expr.m_expr[3].m_tag, FT_FILETYPE);
    QCOMPARE(r.expr.m_expr[3].m_str, QByteArray(ED2KFTSTR_AUDIO));
    // index 4: @size>5M
    verifyNumeric(r.expr, 4, FT_FILESIZE, ED2K_SEARCH_OP_GREATER, 5 * 1024 * 1024);
}

// -----------------------------------------------------------------------
// Error cases
// -----------------------------------------------------------------------

void tst_SearchExprParser::error_unclosed_paren()
{
    auto r = parseSearchExpression(QStringLiteral("(a OR b"));
    QVERIFY(!r.success());
    QVERIFY(r.errors[0].contains(QStringLiteral("parenthesis"), Qt::CaseInsensitive));
}

void tst_SearchExprParser::error_unknown_attribute()
{
    auto r = parseSearchExpression(QStringLiteral("@unknown=test"));
    QVERIFY(!r.success());
    QVERIFY(r.errors[0].contains(QStringLiteral("Unknown attribute")));
}

void tst_SearchExprParser::error_missing_after_and()
{
    auto r = parseSearchExpression(QStringLiteral("a AND"));
    QVERIFY(!r.success());
}

void tst_SearchExprParser::error_missing_size_operator()
{
    auto r = parseSearchExpression(QStringLiteral("@size 10M"));
    QVERIFY(!r.success());
}

void tst_SearchExprParser::error_invalid_type_value()
{
    auto r = parseSearchExpression(QStringLiteral("@type=foobar"));
    QVERIFY(!r.success());
}

void tst_SearchExprParser::error_missing_string_value()
{
    auto r = parseSearchExpression(QStringLiteral("@ext="));
    QVERIFY(!r.success());
}

QTEST_MAIN(tst_SearchExprParser)
#include "tst_SearchExprParser.moc"
