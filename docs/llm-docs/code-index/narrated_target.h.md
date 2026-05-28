# narrated_target.h (48 lines)

Unified "last narrated target" slot header. Whichever narration channel spoke a bare target name most recently owns the activation target. Stamp sites: passive_narrate, cycle_input, view_mode hover-pause. Non-name narration does NOT stamp.

## Declarations (in source order)

- L24 — `namespace acc::narrated_target`
- L26 — `struct Slot`
  note: obj is CSWSObject* OR CSWCMapPin*; handle=0 for map pins; pos is frozen stamp-time position (only meaningful for map pins); isMapPin discriminates the two shapes
- L36 — `void Stamp(void* obj, uint32_t serverHandle);`
  note: both args must be server-side; no-ops on zero/sentinel handles
- L39 — `void StampMapPin(void* pin, const Vector& pos);`
  note: pos frozen at stamp time since map pins don't move
- L41 — `void Clear();`
- L46 — `bool TryGet(Slot& out);`
  note: re-validates on every read; clears stale slots (destroyed object, area-switch without Clear, pin removed from map_pins[])
