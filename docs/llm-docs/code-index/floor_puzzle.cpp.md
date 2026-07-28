# floor_puzzle.cpp (634 lines)

Rakatan temple floor-plate lights-out puzzle assist for module unk_m44ab.
Caches the 3x3 plate-trigger grid + reset trigger geometry from the area
object list (BuildCache), reads live lit/dark state off FloorPanel01..09
placeables' local boolean 10, and speaks: a one-shot orientation intro with
solo-mode hint, per-step toggle-delta announcements attributing flips to the
leader's own step vs. a follower, an on-demand board readout (R key,
unthrottled so the 150ms scan cadence never eats the press), continuous
nearest-plate cardinal-offset guidance while off-plate (view-mode cursor
aware), and a solved cue after which the module goes quiet. Pure poll driven
from core_tick; talks to engine_area (iterator, geometry, local booleans),
view_mode (cursor position while frozen), and prism (SpeakUrgent — all
feedback rides the urgent channel since it lands mid-movement-key-mashing).

## Declarations (in source order)

- L23 — `namespace acc::floor_puzzle`
- L34 — `constexpr DWORD kScanIntervalMs = 150`
- L37 — `constexpr char kPuzzleModule[] = "unk_m44ab"`
- L41 — `constexpr int kLitBooleanIndex = 10`
- L46-47 — `kSoloModeActionId`, `kSoloModeDefaultVk`
- L53 — `constexpr DWORD kNearestMinGapMs = 700`
- L57 — `constexpr float kIntroRadiusM = 12.0f`
- L62 — `constexpr DWORD kLeaderToggleWindowMs = 600`
- L64-67 — `kPlateCount=9`, `kResetIndex=9`, `kTrackCount=10`, `kMaxPolyVerts=8`
- L69 — `struct Plate` — trigHandle/plcHandle/poly/vertCount/center/minX-maxY span
- L84 — `constexpr Id kPlateWord[9]` — grid-position compass word per plate index
- L90-113 — `g_plates[10]`, `g_cacheReady`, `g_area`, `g_isPuzzleArea`, `g_lit[9]`, board/entry-merge state
- L115 — `void ResetAreaState()`
- L133 — `const char* ShortName(int idx)`
- L140 — `std::string FullName(int idx)`
- L147 — `bool PointInPoly(const Plate&, float x, float y)`
- L161 — `float DistToSegment(float, float, const Vector&, const Vector&)`
- L175 — `float DistToPlate(const Plate&, float x, float y)`
- L197 — `std::string AxisPhrase(const Vector& playerPos, const Plate&)`
  note: speaks per-axis offset to the plate's AREA span, not its centre point (2026-07-16 fix — centre-based version spoke false diagonals).
- L226 — `int TrigSlotForTag(const char* tag)`
- L235 — `int PlcSlotForTag(const char* tag)`
- L243 — `bool BuildCache(void* area)` — populates g_plates from the area object list
  note: defends against local-space trigger polygons (GIT authoring quirk) by translating when vertex coords look local (<30 units).
- L326 — `void SpeakIntro(const Vector& playerPos)`
- L358 — `std::string DeltaText(const bool* flipped, const bool* lit, int litCount)`
- L379 — `std::string BoardStateText(const bool* lit, int litCount)`
- L398 — `bool GetNavPosition(Vector& out)` — view-mode cursor when active, else leader
- L409 — `int ReadLiveBoard(bool* lit)`
- L423 — `bool IsActive()` — public
- L425 — `bool IsPuzzlePlateTrigger(uint32_t handle)` — public
- L433 — `void Tick()` — public; on-demand R read runs unthrottled above the 150ms scan gate
