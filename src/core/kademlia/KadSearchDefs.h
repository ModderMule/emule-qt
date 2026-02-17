#pragma once

/// @file KadSearchDefs.h
/// @brief Kademlia search type definitions: search term tree, type enum.

#include "protocol/Tag.h"

#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

namespace eMule::kad {

// ---------------------------------------------------------------------------
// Invalid keyword characters — delimiters for splitting search strings
// ---------------------------------------------------------------------------
inline constexpr const char* kInvKadKeywordChars = " ()[]{}<>,._-!?:;\\/\"";

// ---------------------------------------------------------------------------
// Search types
// ---------------------------------------------------------------------------
enum class SearchType : uint32_t {
    Node              = 0,
    NodeComplete      = 1,
    File              = 2,
    Keyword           = 3,
    Notes             = 4,
    StoreFile         = 5,
    StoreKeyword      = 6,
    StoreNotes        = 7,
    FindBuddy         = 8,
    FindSource        = 9,
    NodeSpecial       = 10,
    NodeFwCheckUDP    = 11
};

// ---------------------------------------------------------------------------
// Search expression tree
// ---------------------------------------------------------------------------
struct SearchTerm {
    enum class Type {
        AND, OR, NOT, String, MetaTag,
        OpGreaterEqual, OpLessEqual, OpGreater, OpLess, OpEqual, OpNotEqual
    };

    Type type = Type::AND;
    Tag tag;
    std::vector<QString> strings;
    std::unique_ptr<SearchTerm> left;
    std::unique_ptr<SearchTerm> right;

    SearchTerm()
        : tag(uint8(0), uint32(0))
    {
    }

    ~SearchTerm() = default;

    SearchTerm(const SearchTerm&) = delete;
    SearchTerm& operator=(const SearchTerm&) = delete;
    SearchTerm(SearchTerm&&) = default;
    SearchTerm& operator=(SearchTerm&&) = default;
};

} // namespace eMule::kad
