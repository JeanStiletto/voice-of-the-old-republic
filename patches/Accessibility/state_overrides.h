// Per-puzzle state-label registry for game objects.
//
// Many KOTOR puzzles encode their state as a small integer field on
// a placeable (the Sith-base "Lights Out" wall switches keep their
// Aus/An position at CSWSPlaceable+0x260; we expect levers, pressure
// plates, console terminals etc. to follow similar shapes). The
// engine never surfaces a user-facing label for these — sighted
// players read the visual pose, blind players need spoken text.
//
// This module owns a compile-time registry mapping each known
// per-instance tag to (state offset, integer→label map). On every
// `GetSpokenName` call we look up the object's tag (CSWSObject+0x18)
// and, if matched, append ", <label>" to the spoken name. Unmatched
// objects stay silent — no noisy "Position 0" spam for ordinary
// containers/doors.
//
// New puzzles register here. When the state lives in a script local
// variable instead of a struct field, a future revision will plug
// in a "local-vars iteration" reader behind the same API.

#pragma once

#include <cstddef>

namespace acc::state {

// Append ", <label>" in place when `gameObject`'s tag matches a
// registered override and the override's integer field decodes to a
// known label for the current locale. No-op on null, empty `outBuf`,
// missing tag, unknown tag, or fault during the field read.
//
// Returns true when a suffix was appended (logging hook for the
// caller; not currently consumed).
bool AppendStateLabel(void* gameObject, char* outBuf, size_t bufSize);

}  // namespace acc::state
