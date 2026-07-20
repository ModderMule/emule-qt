/// @file tst_SearchExpr.cpp
/// @brief Tests for search/SearchExpr — expression builder, operators, RPN structure.

#include "TestHelpers.h"
#include "search/SearchExpr.h"

#include <QTest>

using namespace eMule;

class tst_SearchExpr : public QObject {
    Q_OBJECT

private slots:
    void construct_default_attr();
    void construct_fromString();
    void construct_numeric();
    void construct_attrString();
    void debugString_term();
    void debugString_numeric();
    void expr_default_empty();
    void expr_construct_fromAttr();
    void expr_addAttr();
    void expr_addOperator_and();
    void expr_addOperator_or();
    void expr_addOperator_not();
    void expr_addExpr();
    void expr_complex_rpn();

    // toBytes — wire format (MFC CSearchExprTarget, SearchResultsWnd.cpp:830-940)
    void toBytes_empty();
    void toBytes_stringTerm();
    void toBytes_metaTag();
    void toBytes_numeric32();
    void toBytes_numeric64();
    void toBytes_operatorsAreTwoBytes();
    void toBytes_notIsBinary();
    void toBytes_nestedTree();
};

void tst_SearchExpr::construct_default_attr()
{
    SearchAttr attr;
    QCOMPARE(attr.m_tag, FT_FILENAME);
    QCOMPARE(attr.m_integerOperator, static_cast<uint32>(ED2K_SEARCH_OP_EQUAL));
    QCOMPARE(attr.m_num, uint64{0});
    QVERIFY(attr.m_str.isEmpty());
}

void tst_SearchExpr::construct_fromString()
{
    SearchAttr attr(QByteArray("test query"));
    QCOMPARE(attr.m_str, QByteArray("test query"));
    QCOMPARE(attr.m_tag, FT_FILENAME);
}

void tst_SearchExpr::construct_numeric()
{
    SearchAttr attr(FT_FILESIZE, ED2K_SEARCH_OP_GREATER, 1024);
    QCOMPARE(attr.m_tag, FT_FILESIZE);
    QCOMPARE(attr.m_integerOperator, static_cast<uint32>(ED2K_SEARCH_OP_GREATER));
    QCOMPARE(attr.m_num, uint64{1024});
}

void tst_SearchExpr::construct_attrString()
{
    SearchAttr attr(FT_FILETYPE, QByteArray("Audio"));
    QCOMPARE(attr.m_tag, FT_FILETYPE);
    QCOMPARE(attr.m_str, QByteArray("Audio"));
}

void tst_SearchExpr::debugString_term()
{
    SearchAttr attr(QByteArray("music"));
    QString debug = attr.debugString();
    QVERIFY(debug.contains(QStringLiteral("music")));
    QVERIFY(debug.contains(QStringLiteral("term")));
}

void tst_SearchExpr::debugString_numeric()
{
    SearchAttr attr(FT_FILESIZE, ED2K_SEARCH_OP_GREATER, 9999);
    QString debug = attr.debugString();
    QVERIFY(debug.contains(QStringLiteral("9999")));
}

void tst_SearchExpr::expr_default_empty()
{
    SearchExpr expr;
    QVERIFY(expr.m_expr.empty());
}

void tst_SearchExpr::expr_construct_fromAttr()
{
    SearchAttr attr(QByteArray("hello"));
    SearchExpr expr(attr);
    QCOMPARE(expr.m_expr.size(), std::size_t{1});
    QCOMPARE(expr.m_expr[0].m_str, QByteArray("hello"));
}

void tst_SearchExpr::expr_addAttr()
{
    SearchExpr expr;
    expr.add(SearchAttr(QByteArray("word")));
    QCOMPARE(expr.m_expr.size(), std::size_t{1});
    QCOMPARE(expr.m_expr[0].m_str, QByteArray("word"));
}

void tst_SearchExpr::expr_addOperator_and()
{
    SearchExpr expr;
    expr.add(SearchOperator::And);
    QCOMPARE(expr.m_expr.size(), std::size_t{1});
    QCOMPARE(expr.m_expr[0].m_str, QByteArray(kSearchOpTokenAnd));
}

void tst_SearchExpr::expr_addOperator_or()
{
    SearchExpr expr;
    expr.add(SearchOperator::Or);
    QCOMPARE(expr.m_expr.size(), std::size_t{1});
    QCOMPARE(expr.m_expr[0].m_str, QByteArray(kSearchOpTokenOr));
}

void tst_SearchExpr::expr_addOperator_not()
{
    SearchExpr expr;
    expr.add(SearchOperator::Not);
    QCOMPARE(expr.m_expr.size(), std::size_t{1});
    QCOMPARE(expr.m_expr[0].m_str, QByteArray(kSearchOpTokenNot));
}

void tst_SearchExpr::expr_addExpr()
{
    SearchExpr sub;
    sub.add(SearchAttr(QByteArray("a")));
    sub.add(SearchAttr(QByteArray("b")));

    SearchExpr expr;
    expr.add(SearchAttr(QByteArray("c")));
    expr.add(sub);

    QCOMPARE(expr.m_expr.size(), std::size_t{3});
    QCOMPARE(expr.m_expr[0].m_str, QByteArray("c"));
    QCOMPARE(expr.m_expr[1].m_str, QByteArray("a"));
    QCOMPARE(expr.m_expr[2].m_str, QByteArray("b"));
}

void tst_SearchExpr::expr_complex_rpn()
{
    // Build (A AND B) OR C in RPN: A B AND C OR
    SearchExpr expr;
    expr.add(SearchAttr(QByteArray("A")));
    expr.add(SearchAttr(QByteArray("B")));
    expr.add(SearchOperator::And);
    expr.add(SearchAttr(QByteArray("C")));
    expr.add(SearchOperator::Or);

    QCOMPARE(expr.m_expr.size(), std::size_t{5});
    QCOMPARE(expr.m_expr[0].m_str, QByteArray("A"));
    QCOMPARE(expr.m_expr[1].m_str, QByteArray("B"));
    QCOMPARE(expr.m_expr[2].m_str, QByteArray(kSearchOpTokenAnd));
    QCOMPARE(expr.m_expr[3].m_str, QByteArray("C"));
    QCOMPARE(expr.m_expr[4].m_str, QByteArray(kSearchOpTokenOr));
}

// ---------------------------------------------------------------------------
// toBytes — byte-exact wire format
//
// The payload layout is shared by the ED2K server search packet and the Kad
// KADEMLIA2_SEARCH_KEY_REQ search-terms blob; official eMule emits both from the
// same encoder (CSearchExprTarget). Getting a single byte wrong here silently
// breaks every multi-term search, so these are exact-match vectors.
// ---------------------------------------------------------------------------

namespace {
QByteArray hex(const char* s) { return QByteArray::fromHex(s); }
} // namespace

void tst_SearchExpr::toBytes_empty()
{
    SearchExpr expr;
    QVERIFY(expr.toBytes().isEmpty());
}

void tst_SearchExpr::toBytes_stringTerm()
{
    // 01 <u16 len> <utf8>
    SearchExpr expr(SearchAttr(QByteArray("abc")));
    QCOMPARE(expr.toBytes(), hex("01") + hex("0300") + QByteArray("abc"));
}

void tst_SearchExpr::toBytes_metaTag()
{
    // 02 <u16 len> <utf8> <u16 namelen=1> <tag>
    SearchExpr expr(SearchAttr(FT_FILETYPE, QByteArray("Audio")));
    QCOMPARE(expr.toBytes(),
             hex("02") + hex("0500") + QByteArray("Audio")
                 + hex("0100") + QByteArray(1, char(FT_FILETYPE)));
}

void tst_SearchExpr::toBytes_numeric32()
{
    // 03 <u32 value LE> <op> <u16 namelen=1> <tag>
    SearchExpr expr(SearchAttr(FT_FILESIZE, ED2K_SEARCH_OP_GREATER_EQUAL, 0x12345678ULL));
    QCOMPARE(expr.toBytes(),
             hex("03") + hex("78563412")
                 + QByteArray(1, char(ED2K_SEARCH_OP_GREATER_EQUAL))
                 + hex("0100") + QByteArray(1, char(FT_FILESIZE)));
}

void tst_SearchExpr::toBytes_numeric64()
{
    // Values above UINT32_MAX switch to the 64-bit node (type 0x08); anything
    // that fits in 32 bits must stay on 0x03 so older nodes can still parse it.
    SearchExpr small(SearchAttr(FT_FILESIZE, ED2K_SEARCH_OP_LESS_EQUAL, 0xFFFFFFFFULL));
    QCOMPARE(small.toBytes().at(0), char(0x03));

    SearchExpr big(SearchAttr(FT_FILESIZE, ED2K_SEARCH_OP_LESS_EQUAL, 0x0000000100000000ULL));
    QCOMPARE(big.toBytes(),
             hex("08") + hex("0000000001000000")
                 + QByteArray(1, char(ED2K_SEARCH_OP_LESS_EQUAL))
                 + hex("0100") + QByteArray(1, char(FT_FILESIZE)));
}

void tst_SearchExpr::toBytes_operatorsAreTwoBytes()
{
    // Regression: operators used to be emitted as a single byte (00/01/02),
    // which desynchronised the whole stream for the receiver.
    const QByteArray a = hex("01") + hex("0100") + QByteArray("a");
    const QByteArray b = hex("01") + hex("0100") + QByteArray("b");

    SearchExpr andExpr;
    andExpr.add(SearchOperator::And);
    andExpr.add(SearchAttr(QByteArray("a")));
    andExpr.add(SearchAttr(QByteArray("b")));
    QCOMPARE(andExpr.toBytes(), hex("0000") + a + b);

    SearchExpr orExpr;
    orExpr.add(SearchOperator::Or);
    orExpr.add(SearchAttr(QByteArray("a")));
    orExpr.add(SearchAttr(QByteArray("b")));
    QCOMPARE(orExpr.toBytes(), hex("0001") + a + b);
}

void tst_SearchExpr::toBytes_notIsBinary()
{
    // NOT takes two operands in eMule ("a AND NOT b"); serialising it as unary
    // left the right-hand operand dangling as a sibling node.
    SearchExpr expr;
    expr.add(SearchOperator::Not);
    expr.add(SearchAttr(QByteArray("aaa")));
    expr.add(SearchAttr(QByteArray("bbb")));

    QCOMPARE(expr.toBytes(),
             hex("0002")
                 + hex("01") + hex("0300") + QByteArray("aaa")
                 + hex("01") + hex("0300") + QByteArray("bbb"));
}

void tst_SearchExpr::toBytes_nestedTree()
{
    // AND(a, OR(b, c)) in prefix notation.
    SearchExpr expr;
    expr.add(SearchOperator::And);
    expr.add(SearchAttr(QByteArray("aaa")));
    expr.add(SearchOperator::Or);
    expr.add(SearchAttr(QByteArray("bbb")));
    expr.add(SearchAttr(QByteArray("ccc")));

    QCOMPARE(expr.toBytes(),
             hex("0000")
                 + hex("01") + hex("0300") + QByteArray("aaa")
                 + hex("0001")
                 + hex("01") + hex("0300") + QByteArray("bbb")
                 + hex("01") + hex("0300") + QByteArray("ccc"));
}

QTEST_MAIN(tst_SearchExpr)
#include "tst_SearchExpr.moc"
