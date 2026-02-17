/// @file SearchExpr.cpp
/// @brief Search expression builder — port of MFC SearchExpr.

#include "search/SearchExpr.h"

namespace eMule {

// ---------------------------------------------------------------------------
// SearchAttr
// ---------------------------------------------------------------------------

SearchAttr::SearchAttr(const QByteArray& str)
    : m_str(str)
    , m_tag(FT_FILENAME)
{
}

SearchAttr::SearchAttr(int tag, uint32 integerOp, uint64 num)
    : m_num(num)
    , m_tag(tag)
    , m_integerOperator(integerOp)
{
}

SearchAttr::SearchAttr(int tag, const QByteArray& str)
    : m_str(str)
    , m_tag(tag)
{
}

QString SearchAttr::debugString() const
{
    if (!m_str.isEmpty()) {
        switch (m_tag) {
        case FT_FILENAME:
            return QStringLiteral("term:\"%1\"").arg(QString::fromUtf8(m_str));
        case FT_FILETYPE:
            return QStringLiteral("type:\"%1\"").arg(QString::fromUtf8(m_str));
        default:
            return QStringLiteral("tag(%1):\"%2\"")
                .arg(m_tag)
                .arg(QString::fromUtf8(m_str));
        }
    }
    return QStringLiteral("tag(%1) op(%2) val(%3)")
        .arg(m_tag)
        .arg(m_integerOperator)
        .arg(m_num);
}

// ---------------------------------------------------------------------------
// SearchExpr
// ---------------------------------------------------------------------------

SearchExpr::SearchExpr(const SearchAttr& attr)
{
    m_expr.push_back(attr);
}

void SearchExpr::add(SearchOperator op)
{
    SearchAttr sentinel;
    switch (op) {
    case SearchOperator::And:
        sentinel.m_str = QByteArray(kSearchOpTokenAnd);
        break;
    case SearchOperator::Or:
        sentinel.m_str = QByteArray(kSearchOpTokenOr);
        break;
    case SearchOperator::Not:
        sentinel.m_str = QByteArray(kSearchOpTokenNot);
        break;
    }
    m_expr.push_back(sentinel);
}

void SearchExpr::add(const SearchAttr& attr)
{
    m_expr.push_back(attr);
}

void SearchExpr::add(const SearchExpr& expr)
{
    m_expr.insert(m_expr.end(), expr.m_expr.begin(), expr.m_expr.end());
}

} // namespace eMule
