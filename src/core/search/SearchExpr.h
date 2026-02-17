#pragma once

/// @file SearchExpr.h
/// @brief Search expression builder for ED2K/Kad search queries — replaces MFC SearchExpr.h.
///
/// Provides RPN (reverse Polish notation) expression building for constructing
/// boolean search queries sent to ED2K servers and Kademlia network.

#include "utils/Opcodes.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QString>

#include <vector>

namespace eMule {

// ---------------------------------------------------------------------------
// SearchOperator — boolean operator for combining search terms
// ---------------------------------------------------------------------------

enum class SearchOperator : uint8 { And, Or, Not };

// Sentinel token strings used in the RPN expression stack to mark operators.
// The \255 prefix ensures they cannot collide with real search terms.
inline constexpr const char* kSearchOpTokenAnd = "\255AND";
inline constexpr const char* kSearchOpTokenOr  = "\255OR";
inline constexpr const char* kSearchOpTokenNot = "\255NOT";

// ---------------------------------------------------------------------------
// SearchAttr — a single search term or filter
// ---------------------------------------------------------------------------

class SearchAttr {
public:
    SearchAttr() = default;

    /// Filename search term.
    explicit SearchAttr(const QByteArray& str);

    /// Numeric filter (e.g. min size, min sources).
    SearchAttr(int tag, uint32 integerOp, uint64 num);

    /// Attribute string filter (e.g. file type, codec).
    SearchAttr(int tag, const QByteArray& str);

    /// Debug representation for logging.
    [[nodiscard]] QString debugString() const;

    uint64     m_num = 0;
    QByteArray m_str;
    int        m_tag = FT_FILENAME;
    uint32     m_integerOperator = ED2K_SEARCH_OP_EQUAL;
};

// ---------------------------------------------------------------------------
// SearchExpr — RPN expression composed of SearchAttr entries
// ---------------------------------------------------------------------------

class SearchExpr {
public:
    SearchExpr() = default;

    explicit SearchExpr(const SearchAttr& attr);

    /// Push a boolean operator (encoded as sentinel SearchAttr).
    void add(SearchOperator op);

    /// Push an attribute/term.
    void add(const SearchAttr& attr);

    /// Concatenate another expression's terms.
    void add(const SearchExpr& expr);

    std::vector<SearchAttr> m_expr;
};

} // namespace eMule
