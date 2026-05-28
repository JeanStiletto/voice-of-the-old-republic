# audio_cues.h (64 lines)

Data-only header. Maps NavCue enum values to engine resref strings via
GetNavCueResref. Swapping a resref requires changing the string literal in the
switch. CResRef has a 16-char hard limit (silent truncation on miss).

## Declarations (in source order)

- L17 — `namespace acc::audio`
- L19 — `enum class NavCue`
  note: per-kind cues (DoorOpen/DoorClosedMetal/Wood/Stone, NpcCreature, ContainerPlaceable, Item, Landmark, TransitionExit, Wall, HazardLedge) plus guidance/view-mode signals (Collision, BeaconActive, BeaconWaypointReached, BeaconDestinationReached).
- L41 — `constexpr const char* GetNavCueResref(NavCue cue)`
  note: Landmark returns "" to short-circuit PlayCue3D without an engine call (announces via TTS only).
