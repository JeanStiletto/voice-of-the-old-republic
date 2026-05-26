// Stable per-object disambiguator for same-LocName game objects.
//
// Multiple placeables / creatures in an area often share a localized
// display name (5x "Wandverkleidung" wall panels in one corridor, 3x
// "Sith-Soldat" in a combat encounter). Sighted players tell them
// apart by on-screen position and target reticle; blind players hear
// the same name spoken five times in a row with no way to refer back
// to "the second one from the left".
//
// This module wraps `engine::GetObjectName` and tacks on a stable
// numeric suffix when, and only when, two or more live objects in the
// current area share the same resolved name. The suffix is assigned
// on first narration of each instance, keyed by the server-side
// handle, and persists for the rest of the area's lifetime — so
// "Sith-Soldat C" keeps meaning C even after A drops, and the same
// wall panel always speaks the same number every time the player
// passes it.
//
// Layered above `engine_area`: this is poll-time enrichment of the
// raw engine read, not engine state.
//
// Call sites swap `engine::GetObjectName(obj, buf, size)` for
// `narration::GetSpokenName(obj, buf, size)` one at a time. Initial
// roll-out covers `passive_narrate` and the combat-narration TUs;
// other narrators (`cycle_input`, `interact_hotkey`, `examine_view`,
// `view_mode`) still call `GetObjectName` directly until we've shaken
// out behaviour on the first batch.

#pragma once

#include <cstddef>

namespace acc::narration {

// Wraps `acc::engine::GetObjectName(gameObject, outBuf, bufSize)` and
// appends ` <serial>` when this object shares its LocName with at
// least one other live object in the current area.
//
// The serial is 1-based per LocName bucket, assigned on first
// observation, and follows the object until the next `Reset()`
// (area transition). Surviving members keep their serial when others
// die — slots are never recycled within an area lifetime.
//
// Returns true on a non-empty resolved name. Empty/fault returns
// false and leaves `outBuf` empty, mirroring `GetObjectName`'s
// contract so call-site error handling stays unchanged.
bool GetSpokenName(void* gameObject, char* outBuf, size_t bufSize);

// Companion to `GetSpokenName` for call sites whose name resolution
// runs through the engine's universal `GetObjectDisplayNameByHandle`
// accessor (combat narration paths). Given an already-resolved name
// in `outBuf`, append the disambiguator suffix in place.
//
// Uses the current contents of `outBuf` as the LocName bucket key,
// so the suffix lands on whatever string the caller already chose to
// speak — staying consistent across the engine-handle path and the
// offset-based `GetObjectName` path.
//
// No-op when `gameObject` is null, the resolved server handle is
// invalid, the name is empty, or the bucket has only one member.
void AppendSuffix(void* gameObject, char* outBuf, size_t bufSize);

// Clear all per-area state. Hooked into the area-transition reset
// chain in `transitions.cpp` alongside `region::Reset` and
// `wall_topology::Reset`.
void Reset();

}  // namespace acc::narration
