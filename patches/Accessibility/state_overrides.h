// Per-puzzle state-label registry.
//
// KOTOR puzzles often encode state as a small int on a placeable (the
// Sith-base "Lights Out" wall switches keep Aus/An at CSWSPlaceable+0x260;
// levers / pressure plates / consoles follow similar shapes). The engine
// never surfaces a user-facing label.
//
// Compile-time registry maps per-instance tags → (state offset, int→label
// map). GetSpokenName looks up CSWSObject+0x18's tag and appends
// ", <label>" if matched. Unmatched objects stay silent.
//
// New puzzles register here. When state lives in script local variables
// instead of a struct field, a future revision adds a local-vars reader
// behind the same API.

#pragma once

#include <cstddef>

namespace acc::state {

// True iff a suffix was appended.
bool AppendStateLabel(void* gameObject, char* outBuf, size_t bufSize);

}  // namespace acc::state
