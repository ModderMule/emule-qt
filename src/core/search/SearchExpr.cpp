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

// ---------------------------------------------------------------------------
// toBytes — serialize prefix-notation expr to binary ED2K packet payload
// ---------------------------------------------------------------------------

static QByteArray serializeNode(const std::vector<SearchAttr>& expr, size_t& idx)
{
    if (idx >= expr.size())
        return {};

    const SearchAttr& a = expr[idx++];

    // Check for operator sentinel tokens (\255 prefix)
    if (a.m_str == QByteArray(kSearchOpTokenAnd)) {
        QByteArray r;
        r += char(0x00);
        r += serializeNode(expr, idx);
        r += serializeNode(expr, idx);
        return r;
    }
    if (a.m_str == QByteArray(kSearchOpTokenOr)) {
        QByteArray r;
        r += char(0x01);
        r += serializeNode(expr, idx);
        r += serializeNode(expr, idx);
        return r;
    }
    if (a.m_str == QByteArray(kSearchOpTokenNot)) {
        QByteArray r;
        r += char(0x02);
        r += serializeNode(expr, idx);
        return r;
    }

    // String term
    if (!a.m_str.isEmpty()) {
        const auto len = static_cast<uint16>(a.m_str.size());
        QByteArray r;
        if (a.m_tag == FT_FILENAME) {
            r += char(0x01);
        } else {
            r += char(0x02);
        }
        r += char(len & 0xFF);
        r += char(len >> 8);
        r += a.m_str;
        if (a.m_tag != FT_FILENAME) {
            r += char(a.m_tag & 0xFF);
            r += char((a.m_tag >> 8) & 0xFF);
        }
        return r;
    }

    // Numeric filter
    const auto v = static_cast<uint32>(a.m_num);
    QByteArray r;
    r += char(0x03);
    r += char(v & 0xFF); r += char((v >> 8) & 0xFF);
    r += char((v >> 16) & 0xFF); r += char((v >> 24) & 0xFF);
    r += char(0); r += char(0); r += char(0); r += char(0); // min placeholder
    r += char(0); r += char(0); r += char(0); r += char(0); // max placeholder
    r += char(a.m_tag & 0xFF);
    r += char(a.m_integerOperator & 0xFF);
    return r;
}

QByteArray SearchExpr::toBytes() const
{
    size_t idx = 0;
    return serializeNode(m_expr, idx);
}

} // namespace eMule
