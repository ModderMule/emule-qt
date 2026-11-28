// The one TU with ed3k.hpp's event conversion and poll bodies (ED3K_HPP_EXTERN_EVENTS is on
// for every emuleed3k user). Skips the PCH: it must read ed3k.hpp with the macro set.
#define ED3K_HPP_IMPLEMENTATION
#include "Ed3kFfi.h"
