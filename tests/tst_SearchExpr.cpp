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

QTEST_MAIN(tst_SearchExpr)
#include "tst_SearchExpr.moc"
