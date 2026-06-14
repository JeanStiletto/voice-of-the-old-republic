// Discovery-driven cycling — the middle tier of object cycling.
//
// Records which static/named objects the blind player has had narrated
// ORGANICALLY (passive ShowObject focus, Q/E line-of-sight, room-shape
// landmark callouts) and persists that index per-map IN THE SAVE, so the
// everyday in-world cycle (`,`/`.`) can resurface known objects without the
// spoilers/noise of the extended/map-side tier.
//
// Storage primitive: named string vars on the player creature's
// CSWSScriptVarTable (engine_scriptvar.*; one var per area keyed by area
// tag). The index holds locale-independent KEYS only — never names; spoken
// names are regenerated live at cycle time.
//
// Key model (stable across reload — positions/tags are fixed per area-load):
//   * Named NPCs   — "N~<objTag>", eligible only when the tag is UNIQUE among
//                    creatures in the area (generic/duplicate mobs excluded).
//   * Static (door / container / landmark / transition) — "S~<objTag>~<ord>",
//     where <ord> is the deterministic north-to-south ordinal among area
//     objects sharing the same category + tag (mirrors cycle_input's
//     PositionLess). Tags repeat for static objects, so the ordinal
//     disambiguates.
//   * World items + generic enemies are OUT of scope (mobile / respawn /
//     vanish) — DeriveKey returns ineligible, so they're never recorded or
//     surfaced in the discovery tier.
//
// Access: discovery is the DEFAULT filter for the in-world cycle. The
// existing "Extended cycling" mod setting, when ON, widens the same keys back
// to the full everything-on-map set (cycle_state consults the setting).

#pragma once

namespace acc::discovery {

// Area-change reconciliation hook. Called from transitions::Tick's existing
// area-change branch. Captures the new area's tag and clears the in-memory
// set; the actual read of the save var is DEFERRED to Tick() so it lands
// after the player creature's table has finished loading (a read on the exact
// save-load tick can race CSWSObject::LoadObjectState). Idempotent on the same
// area.
void OnAreaChanged(void* area);

// Per-tick driver (called from core_tick). Performs the deferred load of the
// current area's discovered set once the player creature has been stably
// present for a short settle window. Cheap no-op once loaded.
void Tick();

// Record `gameObject` as organically discovered. Called from the organic
// narration sites ONLY (never from extended/map-side cycling, or the tier
// would auto-discover everything). Derives the key; no-op when the object is
// ineligible (item / non-unique NPC / no tag) or already recorded. Persists
// the area's set into the save on a new discovery.
void Record(void* gameObject);

// True iff `gameObject` is in the current area's discovered set. Used by
// cycle_state to filter the in-world cycle down to the discovery tier.
// Returns false until the area's set has been reconciled from the save.
bool IsDiscovered(void* gameObject);

}  // namespace acc::discovery
