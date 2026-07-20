#include "pch.h"
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
// toBytes — serialize prefix-notation expr to the binary search payload
// ---------------------------------------------------------------------------
//
// Layout mirrors MFC CSearchExprTarget (srchybrid/SearchResultsWnd.cpp:830-940),
// which official eMule uses for BOTH the ED2K server search packet and the Kad
// KADEMLIA2_SEARCH_KEY_REQ search-terms blob — the two formats are identical:
//
//   boolean : 0x00 <op>                       op: 0=AND 1=OR 2=NOT (all binary)
//   string  : 0x01 <u16 len> <utf8>
//   metatag : 0x02 <u16 len> <utf8> <u16 namelen> <name…>
//   num32   : 0x03 <u32 value> <op> <u16 namelen> <name…>
//   num64   : 0x08 <u64 value> <op> <u16 namelen> <name…>
//
// Tag names are single bytes here, so namelen is always 1.

namespace {

void appendUInt16LE(QByteArray& out, uint16 v)
{
    out += char(v & 0xFF);
    out += char((v >> 8) & 0xFF);
}

void appendUInt32LE(QByteArray& out, uint32 v)
{
    for (int i = 0; i < 4; ++i)
        out += char((v >> (8 * i)) & 0xFF);
}

void appendUInt64LE(QByteArray& out, uint64 v)
{
    for (int i = 0; i < 8; ++i)
        out += char((v >> (8 * i)) & 0xFF);
}

/// Emit `<u16 len> <bytes>` — the WriteString form used by MFC's CSafeMemFile.
void appendString(QByteArray& out, const QByteArray& s)
{
    appendUInt16LE(out, static_cast<uint16>(s.size()));
    out += s;
}

/// Emit a single-byte meta tag name as `<u16 1> <tag>`.
void appendTagName(QByteArray& out, int tag)
{
    appendUInt16LE(out, 1);
    out += char(tag & 0xFF);
}

QByteArray serializeNode(const std::vector<SearchAttr>& expr, size_t& idx)
{
    if (idx >= expr.size())
        return {};

    const SearchAttr& a = expr[idx++];

    // Operator sentinel tokens (\255 prefix). All three are binary in eMule —
    // NOT is consumed as "left AND !right" by both the Kad decoder
    // (CreateSearchExpressionTree) and the entry matcher (SearchTermsMatch).
    auto boolNode = [&](char op) {
        QByteArray r;
        r += char(0x00);
        r += op;
        r += serializeNode(expr, idx);
        r += serializeNode(expr, idx);
        return r;
    };
    if (a.m_str == QByteArray(kSearchOpTokenAnd))
        return boolNode(char(0x00));
    if (a.m_str == QByteArray(kSearchOpTokenOr))
        return boolNode(char(0x01));
    if (a.m_str == QByteArray(kSearchOpTokenNot))
        return boolNode(char(0x02));

    // String term / meta tag with a string value
    if (!a.m_str.isEmpty()) {
        QByteArray r;
        if (a.m_tag == FT_FILENAME) {
            r += char(0x01);
            appendString(r, a.m_str);
        } else {
            r += char(0x02);
            appendString(r, a.m_str);
            appendTagName(r, a.m_tag);
        }
        return r;
    }

    // Numeric filter — 64-bit form only when the value needs it, so ordinary
    // filters stay readable to nodes that predate 64-bit support.
    QByteArray r;
    if (a.m_num > UINT32_MAX) {
        r += char(0x08);
        appendUInt64LE(r, a.m_num);
    } else {
        r += char(0x03);
        appendUInt32LE(r, static_cast<uint32>(a.m_num));
    }
    r += char(a.m_integerOperator & 0xFF);
    appendTagName(r, a.m_tag);
    return r;
}

} // namespace

QByteArray SearchExpr::toBytes() const
{
    size_t idx = 0;
    return serializeNode(m_expr, idx);
}

} // namespace eMule
