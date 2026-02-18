#pragma once

/// @file KadTypes.h
/// @brief Container typedefs for Kademlia (ported from kademlia/routing/Maps.h).

#include "kademlia/KadUInt128.h"
#include "protocol/Tag.h"
#include "utils/MapKey.h"

#include <cstdint>
#include <ctime>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

namespace eMule::kad {

// Forward declarations
class Contact;
class Search;
class RoutingZone;
class Entry;

// ---------------------------------------------------------------------------
// Container aliases
// ---------------------------------------------------------------------------
using ContactMap   = std::map<UInt128, Contact*>;
using ContactList  = std::list<Contact*>;
using ContactArray = std::vector<Contact*>;
using UIntList     = std::vector<UInt128>;
using TagList      = std::vector<Tag>;
using WordList     = std::vector<QString>;
using SearchMap    = std::map<UInt128, Search*>;

// ---------------------------------------------------------------------------
// Indexed data structures (used by KadIndexed.h)
// ---------------------------------------------------------------------------
struct Source {
    UInt128 sourceID;
    std::list<Entry*> entryList;
};

struct KeyHash {
    UInt128 keyID;
    std::unordered_map<HashKeyOwn, Source*> mapSource;
};

struct SrcHash {
    UInt128 keyID;
    std::list<Source*> sourceList;
};

struct Load {
    UInt128 keyID;
    time_t  time;
};

using KeyHashMap = std::unordered_map<HashKeyOwn, KeyHash*>;
using SrcHashMap = std::unordered_map<HashKeyOwn, SrcHash*>;
using LoadMap    = std::unordered_map<HashKeyOwn, Load*>;

} // namespace eMule::kad
