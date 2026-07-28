# audio_cues.h (94 lines)

Data-only navigation-cue vocabulary: `NavCue` enum plus `GetNavCueResref` mapping each to a ≤16-char engine resref. Covers Pillar-4 per-kind cues (doors split by open-state + material, NPC, container, item, landmark [silent], transition, wall, hazard), guidance/view-mode signals (collision, beacon variants), and the swoop-race minigame's steering/warning cues (shared between the live race and the glossary preview). CResRef truncates silently past 16 chars — a resolution miss is a silent cue, so resrefs must be checked against `build/sounds-extracted-full/`.

## Declarations (in source order)

- L19 — `enum class NavCue` — DoorOpen/ClosedMetal/ClosedWood/ClosedStone, NpcCreature, ContainerPlaceable, Item, Landmark, TransitionExit, Wall, HazardLedge, Collision, BeaconActive/WaypointReached/DestinationReached, SwoopAccelpadBoost/ObstacleWarn/WallImpact, SwoopSteerLeft/Right/Aligned, SwoopShiftReady
  note: Landmark maps to empty resref ("") — TTS-only, short-circuits PlayCue3D
- L64 — `constexpr const char* GetNavCueResref(NavCue cue)` — switch mapping enum → resref string, e.g. DoorOpen→"gui_close", Wall→"as_nt_wtrdrip_09", SwoopSteerLeft→"acc_steer_l" (custom Override WAV)
