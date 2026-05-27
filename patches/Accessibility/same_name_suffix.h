// Per-object disambiguator for same-LocName objects.
//
// Multiple objects in an area often share a localized display name
// (5x "Wandverkleidung" in one corridor; 3x "Sith-Soldat" in a fight).
// Sighted players tell them apart by position + reticle; blind players
// hear the same name in a row with no way to refer back.
//
// Wraps engine::GetObjectName and appends a stable numeric suffix when
// two or more live objects in the current area share the resolved name.
// Suffix is keyed by server handle, assigned on first narration, and
// persists for the rest of the area lifetime — surviving members keep
// their slot even after others die.

#pragma once

#include <cstddef>

namespace acc::narration {

// Mirrors engine::GetObjectName's contract: true on non-empty resolved
// name; false leaves outBuf empty.
bool GetSpokenName(void* gameObject, char* outBuf, size_t bufSize);

// Append suffix to an already-resolved name in outBuf (combat narration
// paths use the engine's universal GetObjectDisplayNameByHandle). Uses
// the current outBuf contents as the LocName bucket key.
void AppendSuffix(void* gameObject, char* outBuf, size_t bufSize);

// Hooked into transitions.cpp's area-reset chain.
void Reset();

}  // namespace acc::narration
