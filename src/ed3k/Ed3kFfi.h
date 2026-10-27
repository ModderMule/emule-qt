#pragma once

/// @file Ed3kFfi.h
/// @brief ed3k.hpp (packmule-ffi) for Qt code: include this, never ed3k.hpp directly.
///
/// ed3k.hpp names fields `slots` (CreditOffer, creditSell), which Qt's keyword macros
/// turn into nothing when a Qt header came first. Hidden here while the header is read.

#pragma push_macro("slots")
#pragma push_macro("signals")
#pragma push_macro("emit")
#undef slots
#undef signals
#undef emit
#include "ed3k.hpp"
#pragma pop_macro("emit")
#pragma pop_macro("signals")
#pragma pop_macro("slots")
