# Phase 2 scan — cross-file duplication and missing shared helpers

Scope: `patches/Accessibility/` (282 files), `tools/kdev/`,
`installer/KotorAccessibilityInstaller/`. Method: targeted grep sweeps for
the four known leads plus broader sweeps for repeated SEH-read helpers,
repeated debounce/settle-timer state, and repeated address/offset
constants; every candidate was opened and read in full before being
written up here. Namespaces were checked (not just filenames) per the
Phase-1 lesson about `probe_priority_groups.cpp` / `acc::probe::priority_groups`.

No behavior changes, hook byte/offset-value changes, calling-convention
changes, or file splits are proposed anywhere below — only where duplicate
*logic* could move into one already-stateless helper.

## Summary of what was checked but is NOT a finding

- `menus_listbox.cpp` / `.h` — this is already the product of a prior
  consolidation (spec-table dispatcher replacing three copy-pasted
  listbox-nav blocks; see the file's own header comment). No further
  listbox-walking duplication found elsewhere.
- The Pillar-1 "stability/debounce" idiom (`spatial_change_detector.cpp`,
  `camera_spin_guard.cpp`'s episode-quiet logging, `combat_log.cpp`) —
  each site tracks a different value type (wall/object identity, camera
  angle, combat-round id) with different quiet windows and no two sites
  are byte-for-byte identical. Not written up as a finding beyond D3
  below, where two sites *do* share a named constant and near-identical
  field shapes.
- `surfacemat`/walkable-face-type knowledge in the C++ patch
  (`room_topology.cpp`, `engine_area_walls.cpp`) — these only mention
  "surfacemat" in comments; neither hardcodes the walkable-type-id set
  that `tools/kdev`'s walkmesh commands do. No cross-language duplication
  found.
- `AnalyzeDumpCommand.cs`'s `TryReadU32(IDataReader, ...)` — reads live
  process memory through an abstraction, unrelated domain to the
  file-byte-array `ReadU32(byte[], int)` helpers in the walkmesh commands
  (D1). Not the same duplicate family despite the matching name.

---

## D1 — BWM walkmesh header parsing/knowledge, now tripled across three kdev files

**Sites**
- `tools/kdev/Core/WalkmeshGeometryAnalysis.cs:111-118` (`WalkableTypes`
  set) and `:126-131` (header offsets) and `:611-618` (`ReadU32`/`ReadF32`)
- `tools/kdev/Commands/WalkmeshStatsCommand.cs:36-53` (`WalkableTypes`
  set, same 21 values) and `:291-296` (header offsets) and `:474-481`
  (`ReadU32`/`ReadF32`)
- `tools/kdev/Commands/WalkmeshFaceTypesCommand.cs:82-85` and `:123-126`
  (same header offsets, no `WalkableTypes` set since it histograms every
  face type) and `:148-149` (`ReadU32`)

**Quoted evidence** — header-offset knowledge, identical across all three
files:
```
uint type        = ReadU32(bytes, 0x08);
uint vertexCount = ReadU32(bytes, 0x48);   // WalkmeshFaceTypesCommand omits vertex/face-vertex reads, keeps 0x50/0x58
uint faceCount   = ReadU32(bytes, 0x50);
uint faceTypeOff = ReadU32(bytes, 0x58);
```
`WalkmeshGeometryAnalysis.cs:126-131` and `WalkmeshStatsCommand.cs:291-296`
both additionally read `vertexOff` (0x4C) and `faceOff` (0x54) with
identical variable names. `WalkableTypes` is the identical 21-value set
`{1,3,4,5,6,9,10,11,12,13,14,18,21,22,...,30}` in both
`WalkmeshGeometryAnalysis.cs` and `WalkmeshStatsCommand.cs`.

`WalkmeshGeometryAnalysis.cs:111-113` carries this comment:
```
// Same set as WalkmeshStatsCommand. Kept local to keep this command
// self-contained; if a third walkmesh consumer appears, hoist into a
// shared helper at that point.
```
That third consumer already exists — `WalkmeshFaceTypesCommand.cs`
independently re-derives the BWM magic check, `type`/`faceCount`/
`faceTypeOff` header offsets, and its own private `ReadU32` (line
148-149), even though it doesn't need `WalkableTypes` (it histograms
every face type, walkable or not). The comment's trigger condition for
hoisting has been met.

**Shared helper shape**: a `BwmHeader` reader —
`static BwmHeader? ReadHeader(byte[] bytes)` returning
`{ uint Type, uint VertexCount, uint VertexOffset, uint FaceCount, uint FaceOffset, uint FaceTypeOffset }`
after validating length ≥ 0x88 and the `"BWM V1.0"` magic, plus the
byte-array `ReadU32`/`ReadF32` primitives. `WalkableTypes` would move next
to it since two of the three consumers need it.

**State needed**: none beyond the input `byte[]` — every one of these
functions is a pure function of its argument. `ReadU32`/`ReadF32` take
`(byte[], int offset)` and return a value; the header offsets are
compile-time constants, not runtime state. This is the safest kind of
extraction: no statics, no cross-call lifetime, no ordering dependency.

**Risk**: mechanical. Confidence: high.

**Evidence checked**: opened `WalkmeshGeometryAnalysis.cs` and
`WalkmeshStatsCommand.cs` in full; opened `WalkmeshFaceTypesCommand.cs` in
full; grepped `ReadU32|ReadF32` across `tools/kdev/` to confirm no other
consumer exists (`AnalyzeDumpCommand.cs`'s hit is the unrelated
`TryReadU32(IDataReader, ...)` live-memory reader, ruled out above).

---

## D2 — MGO-array walk + AsXxx downcast + safe-read primitives, tripled across the three minigame TUs

**Sites** — constants:
- `patches/Accessibility/minigame_turret.cpp:99-101`
  (`kClientInternalMgoArrayOffset = 0x0; kMgoArrayObjectsOffset = 0x4; kMgoArraySlotCount = 255;`)
  and `:105-106` (`kTrackFollowerModelsDataOffset = 0x68; kModelVtableSlotGetPosition = 0x64;`)
- `patches/Accessibility/minigame_swoop_audio.cpp:72-74` (identical three
  constants, identical values) and `:162-163` (identical
  `kTrackFollowerModelsDataOffset`/`kModelVtableSlotGetPosition`)

Both declaration sites carry near-identical comments: turret.cpp:639-641
says "MGO-walk helpers (mirrors swoop_spatial_audio.cpp — kept local so
that swoop TU isn't entangled with turret-specific cueing)"; swoop_audio's
own header comment at :95-98 spells out the same chain. Both are aware of
the duplication; neither has been hoisted.

**Sites — `ResolveMgoArray()`**, functionally identical (one calls a
`SafeReadPtr` helper per hop, the other inlines the same pointer
arithmetic — same three-hop chain, same offsets, same null-check-per-hop,
same SEH wrap):
- `minigame_turret.cpp:646-658`:
```cpp
void* ResolveMgoArray() {
    __try {
        void* appManager = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appManager) return nullptr;
        void* clientApp = SafeReadPtr(appManager, kAppManagerClientAppOffset);
        if (!clientApp) return nullptr;
        void* clientInternal = SafeReadPtr(clientApp, kClientExoAppInternalOffset);
        if (!clientInternal) return nullptr;
        return SafeReadPtr(clientInternal, kClientInternalMgoArrayOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}
```
- `minigame_swoop_audio.cpp:743-762`: same four-hop resolve
  (`appManager` → `clientApp` → `clientInternal` → the MGO array),
  same offsets `kAppManagerClientAppOffset`/`kClientExoAppInternalOffset`/
  `kClientInternalMgoArrayOffset`, same SEH wrap — written with inline
  `reinterpret_cast` arithmetic instead of calling `SafeReadPtr`.

**Sites — `CallAsCast`/`PFN_AsCast`** (vtable-slot thiscall downcast),
identical body:
- `minigame_turret.cpp:643,660-672`
- `minigame_swoop_audio.cpp:768,830-843`:
```cpp
void* CallAsCast(void* obj, size_t vtableSlotOffset) {
    if (!obj) return nullptr;
    __try {
        void* vtable = *reinterpret_cast<void**>(obj);
        if (!vtable) return nullptr;
        void* fn = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(vtable) + vtableSlotOffset);
        if (!fn) return nullptr;
        auto castFn = reinterpret_cast<PFN_AsCast>(fn);
        return castFn(obj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}
```
(turret.cpp's version returns `reinterpret_cast<PFN_AsCast>(fn)(obj)`
directly instead of naming the intermediate `castFn` — same generated
code.)

**Sites — CSWTrackFollower position read**, same logic under two names:
- `minigame_turret.cpp:674-693` — `ReadFollowerPosition(void* follower, Vector& out)`
- `minigame_swoop_audio.cpp:800-...` — `ReadTrackFollowerPosition(void* follower, Vector& out)`

Both walk `follower + kTrackFollowerModelsDataOffset` → `*modelsData` →
`*vtable` → `vtable[kModelVtableSlotGetPosition]` → call as
`PFN_GetPositionThunk`, both SEH-wrapped, both fall back to a zeroed
`Vector` buffer if the thunk returns null. Only the function name differs.

**Sites — safe-read primitives**, byte-identical `__try`/`__except`
bodies, defined three separate times:
- `minigame_turret.cpp:586-614` (`SafeReadPtr`, `SafeReadU32`, `SafeReadF32`)
- `minigame_swoop_race.cpp:245-285` (`SafeReadPtr`, `SafeReadU32`,
  `SafeReadFloat`, `SafeReadVector`)
- `minigame_swoop_audio.cpp:703-733` (`SafeReadPtr`, `SafeReadVector`,
  `SafeReadFloat`)

All three carry the comment "SEH-guarded primitive reads (same pattern as
engine_* / swoop_race)" or "Same pattern as the rest of engine_*." —
i.e. each author left a breadcrumb saying this is a known repeat, but
`engine_reads.h`'s `acc::engine::ReadU32` is a *different* shape (it
assumes the caller already holds a SEH frame; it is not itself
`__try`-wrapped — confirmed by reading `engine_reads.cpp:47-50`), so it is
not a drop-in replacement for these self-contained "safe" variants.
`probe_pathfind.cpp:89-99` has a fourth near-variant with an `ok`
out-param signature (`SafeReadU32(void* base, size_t offset, bool& ok)`)
— same __try/__except body, different call shape; noted but not folded
into the same helper without a signature decision.

**Shared helper shape**: a small header (e.g.
`minigame_safe_read.h`, or fold into `engine_offsets.h` since `Vector` is
already declared there) exporting
`void* SafeReadPtr(void* base, size_t off)`,
`uint32_t SafeReadU32(void* base, size_t off)`,
`float SafeReadFloat(void* base, size_t off)`,
`bool SafeReadVector(void* base, size_t off, Vector& out)`, plus
`void* ResolveMgoArray()`, `void* CallAsCast(void* obj, size_t vtableSlotOffset)`,
and `bool ReadTrackFollowerPosition(void* follower, Vector& out)` (keep
the swoop_audio name — turret's `ReadFollowerPosition` is the redundant
label). The five `kMgoArray*`/`kTrackFollower*`/`kModelVtableSlotGetPosition`
constants used only by `ResolveMgoArray`/`ReadTrackFollowerPosition` move
with them.

**State needed**: none for the primitives — pure functions of
`(base, offset)`. `ResolveMgoArray()` reads one process-global address
(`kAddrAppManagerPtr`, already centralized in `engine_player.h`) and holds
no state of its own between calls (nothing cached, nothing latched) — safe
to share. `CallAsCast`/`ReadTrackFollowerPosition` take their operand as a
parameter and hold no state either. The one thing a shared header would
need to preserve is that all three call sites currently tolerate a null
`AppManager`/chain link silently (return `nullptr`/`false`) — the merged
helper must keep exactly that null-propagation behavior since callers in
all three minigame TUs rely on "null means not in this minigame right
now," not on an exception.

**Risk**: mechanical for the primitives, `CallAsCast`, and the position
reader (pure, parameterized, no state). Slightly higher — still
mechanical, but worth a smoke-test — for `ResolveMgoArray()` since it's on
the hot per-tick path for both turret and swoop cueing; behavior is
identical by inspection but an in-game check that turret enemy-approach
cues and swoop obstacle/pad audio still fire after the merge is cheap
insurance (per project convention: don't commit until in-game confirmed).

**Confidence**: high — bodies were read side by side, not diffed by name.

**Evidence checked**: read `minigame_turret.cpp` lines 90-106, 580-693,
860-870, 1640-1700; read `minigame_swoop_audio.cpp` lines 60-106, 695-830;
read `minigame_swoop_race.cpp` lines 235-285; read `engine_reads.h` in
full and `engine_reads.cpp:47-50`; read `probe_pathfind.cpp:75-104`;
grepped `kAddrAppManagerPtr`/`kAppManagerClientAppOffset`/
`kClientExoAppInternalOffset` to confirm both minigame files already
share those three constants via `#include "engine_player.h"` (i.e. that
part is *not* duplicated — only the MGO-specific offsets and the
`SafeRead*`/`ResolveMgoArray`/`CallAsCast`/position-reader functions are).

---

## D3 — `kHoverPauseMs` hover-settle-timer idiom, duplicated constant + duplicated shape

**Sites — the constant itself**, same name, same value, same purpose,
declared independently in two files:
- `patches/Accessibility/map_ui_cursor.cpp:61`: `constexpr DWORD kHoverPauseMs = 300;`
- `patches/Accessibility/view_mode.cpp:38`: `constexpr DWORD kHoverPauseMs = 300;          // settle time before speaking`

**Sites — the "arm on change, fire after quiet window" shape**, four
instances across the two files:

1. `view_mode.cpp:52-54` (fields) + `:467-477` (arm/fire):
```cpp
uint32_t hover_pending         = 0;
void*    hover_pending_obj     = nullptr;
DWORD    hover_pending_started = 0;
...
if (bestHandle != g_state.hover_pending) {
    g_state.hover_pending         = bestHandle;
    g_state.hover_pending_obj     = bestObj;
    g_state.hover_pending_started = now;
}
if (bestHandle == 0) return;
if (bestHandle == g_state.hover_last_spoken) return;
if (now - g_state.hover_pending_started < kHoverPauseMs) return;
```

2. `view_mode.cpp:58-59` (fields) + `:396-414` (arm/fire), same shape,
   tracking a `std::string` label instead of a handle:
```cpp
std::string region_pending_text;
DWORD region_pending_started_ms  = 0;
...
if (label == g_state.region_pending_text &&
    g_state.region_pending_started_ms != 0) {
    if (now - g_state.region_pending_started_ms >= kHoverPauseMs) {
        // ... speak, then clear
    }
}
// New pending region — arm the timer.
g_state.region_pending_text = label;
g_state.region_pending_started_ms = now;
```

3. `map_ui_cursor.cpp` waypoint-hover pending (`pending_note_waypoint`,
   `pending_note_started_ms`), fire check at `:981-983`:
```cpp
} else if (hit == g_state.pending_note_waypoint) {
    if (g_state.pending_note_started_ms != 0 &&
        now - g_state.pending_note_started_ms >= kHoverPauseMs) {
```

4. `map_ui_cursor.cpp` ambient-hover pending (`pending_ambient_kind`,
   `pending_ambient_room_idx`, `pending_ambient_started_ms`), fire check
   at `:1231-1232`:
```cpp
} else if (sameAsPending) {
    if (g_state.pending_ambient_started_ms != 0 &&
        now - g_state.pending_ambient_started_ms >= kHoverPauseMs) {
```

All four follow the identical shape: "if the tracked identity changed,
re-arm `*_started[_ms]` to `now`; once `now - *_started >= kHoverPauseMs`,
fire and clear." Only the tracked identity's type differs (object handle,
`std::string` label, waypoint pointer, ambient-kind+room-index pair).

**Shared helper shape**: a small templated settle-timer, e.g.
```cpp
template <typename T>
struct HoverSettleTimer {
    T     value{};
    DWORD armedAtMs = 0;
    bool  Arm(const T& v, DWORD now) {           // true if re-armed (identity changed)
        if (v == value && armedAtMs != 0) return false;
        value = v; armedAtMs = now; return true;
    }
    bool Ready(DWORD now, DWORD quietMs) const {
        return armedAtMs != 0 && now - armedAtMs >= quietMs;
    }
    void Clear() { value = T{}; armedAtMs = 0; }
};
```
placed in a header both TUs already include indirectly, or a new small
`hover_settle_timer.h`. `kHoverPauseMs` becomes one definition, included
by both.

**State needed**: this is exactly the risk the task called out — the
"state" here is the per-site struct field(s) (`hover_pending`/
`hover_pending_obj`, `region_pending_text`, `pending_note_waypoint`,
`pending_ambient_kind`+`pending_ambient_room_idx`) that currently live
inline in each file's own state struct (`view_mode.cpp`'s `g_state`,
`map_ui_cursor.cpp`'s `g_state`). Converting them to
`HoverSettleTimer<T>` members changes the struct layout and every read
site that currently does `g_state.hover_pending_started` would need to
become `g_state.hoverTimer.armedAtMs` (or an accessor) — a mechanical but
non-trivial rename across each file's own read sites (not shared between
files, so no cross-file state ordering risk, but each file has several
internal read sites beyond the ones quoted above). The ambient case
additionally requires comparing a *pair* (`kind`, `roomIdx`), so `T` would
need to be a small comparable struct there, not a scalar — the templated
`Arm`'s `v == value` requires `operator==` on that pair.

**Risk**: mechanical for the constant merge (trivial, zero state). For
the struct-level extraction: needs care (each file has multiple internal
consumers of the renamed fields) but no cross-file or cross-tick state
coupling — safe to test by reading the map cursor / cursor-hover-then-Tab
speech and 3D-cursor hover speech, per this project's screen-reader
verification channel ("does the label speak after ~300ms of holding
still on a waypoint/room/object").

**Confidence**: high on the duplicated constant and the four identical
arm/fire shapes (read line-by-line, not name-matched). Medium on whether
a generic `HoverSettleTimer<T>` is worth the touch-everywhere cost versus
just merging the constant — that trade-off is a judgment call, not
evidence.

**Evidence checked**: grepped for the `now - X >= kY` settle-check shape
across all of `patches/Accessibility`, found exactly these four hits;
read `map_ui_cursor.cpp` lines 55-65, 960-1000, 1200-1240; read
`view_mode.cpp` lines 30-90, 360-480; grepped `kHoverPauseMs\s*=` to
confirm only these two declarations exist.

---

## D4 — AppManager → ClientExoApp → ClientExoAppInternal chain constants, redeclared under near-identical names in three places

**Sites**:
- `patches/Accessibility/engine_player.h:195-196,209`:
```cpp
constexpr uintptr_t kAddrAppManagerPtr           = 0x007A39FC;
constexpr size_t    kAppManagerClientAppOffset   = 0x4;
...
constexpr size_t kClientExoAppInternalOffset = 0x4;
```
- `patches/Accessibility/engine_panels_internal.h:21-24`:
```cpp
inline constexpr uintptr_t kAddrAppManagerPtr        = 0x007A39FC;
inline constexpr size_t    kAppManagerClientOff      = 0x04;
inline constexpr size_t    kClientExoAppInternalOff  = 0x04;
inline constexpr size_t    kClientExoAppGuiInGameOff = 0x40;
```
- `patches/Accessibility/tutorial_popup.cpp:59`:
```cpp
constexpr uintptr_t kAddrAppManagerPtr  = 0x007A39FC;
```
(this one is explicitly self-contained by design — the block starting at
line 53 is commented "Pause (mirrors engine_subscreen; kept self-contained
here)", the same intentional-duplication pattern as D1's walkmesh comment,
so it is lower priority than the header/header overlap below.)

The first three constants are the *same three RE facts* (AppManager
address, AppManager→ClientApp offset, ClientApp→ClientExoAppInternal
offset) declared twice in header files under different names
(`kAppManagerClientAppOffset` vs `kAppManagerClientOff`;
`kClientExoAppInternalOffset` vs `kClientExoAppInternalOff` — note the
`Offset`-vs-`Off` naming near-collision, which is itself a readability
hazard for future greps). `engine_panels_internal.h`'s own header comment
(lines 1-9) says it was "moved verbatim from engine_panels.cpp" during
the Phase-1 split — i.e. the split preserved a pre-existing separate copy
rather than reusing `engine_player.h`'s copy of the same three facts.

`kAppManagerClientAppOffset` (the `engine_player.h` name) is already the
one widely reused — confirmed via grep, it's referenced from
`camera_orient.cpp`, `combat.cpp`, `combat_diag.cpp`, `engine_actionbar.cpp`,
`engine_area.cpp` (twice), `engine_options.cpp`, `engine_radial.cpp`,
`engine_player_party.cpp` (three times), `engine_player_inputlock.cpp`,
`examine_view.cpp`, `engine_player.cpp` (twice), `engine_picker.cpp`,
`minigame_turret.cpp`, `minigame_swoop_audio.cpp`, `passive_narrate.cpp`,
`probe_camera_distance.cpp`, `probe_camera_state.cpp` — i.e. ~19 use
sites across the codebase already standardized on the `engine_player.h`
names. `engine_panels_internal.h`'s parallel `kAppManagerClientOff`/
`kClientExoAppInternalOff` have exactly two consumers today
(`engine_panels.cpp:515,518`, `engine_panels_state.cpp:163,200,222`).

**No current ODR conflict**: `engine_player.h`'s trio sits at *global*
scope (outside any namespace — confirmed by reading lines 190-196, the
`}  // namespace acc::engine` closes before the declarations);
`engine_panels_internal.h`'s trio sits inside `namespace acc::engine`.
No file currently includes both headers (checked: neither
`engine_panels.cpp` nor `engine_panels_state.cpp` includes
`engine_player.h`), so there is no live ambiguity today — but the two
copies mean the "one address, one offset, one name" invariant this
codebase otherwise follows for this exact chain (see the 19-site reuse
above) has a silent exception in the panels module.

**Shared helper shape**: no function to extract — this is pure constant
consolidation. `engine_panels_internal.h` would `#include "engine_player.h"`
and drop its own `kAddrAppManagerPtr`/`kAppManagerClientOff`/
`kClientExoAppInternalOff`, rewriting its two consumer call sites
(`engine_panels.cpp:515,518`, `engine_panels_state.cpp:163,200,222`) to
the `engine_player.h` names (fully-qualified `::kAddrAppManagerPtr`,
`kAppManagerClientAppOffset`, `kClientExoAppInternalOffset` — note
`engine_panels_internal.h`'s consumers are inside `namespace acc::engine`,
so the global-scope `kAddrAppManagerPtr` needs `::` or relies on ordinary
unqualified lookup falling through to the enclosing global namespace,
which it does). `kClientExoAppGuiInGameOff` (the fourth constant, the
CGuiInGame hop) has no counterpart in `engine_player.h` and stays where
it is — that hop is genuinely unique to the panels module.

**State needed**: none — these are compile-time address/offset constants,
not runtime state. Zero lifetime or ordering concerns.

**Risk**: mechanical. The only thing to double check post-merge is that
`engine_panels.cpp`/`engine_panels_state.cpp` don't pick up an unwanted
transitive include from pulling in `engine_player.h` (it's a fairly large
header) — a build-only concern, not a behavior concern.

**Confidence**: high on the value/name overlap (read both headers in
full, confirmed identical `0x007A39FC`/`0x4`/`0x4` values). Medium-high on
"worth merging" — it's a real duplicate-knowledge hazard (two names for
the same RE fact, one letter apart) but the blast radius of *not* fixing
it is low since both copies are read-only constants that can't drift
silently (a value change would need a person to edit two files with two
different names, which is exactly the failure mode consolidation
prevents).

**Evidence checked**: read `engine_player.h` lines 185-215 and
`engine_panels_internal.h` in full; read `tutorial_popup.cpp` lines 40-70;
grepped `kAppManagerClientAppOffset` and `kAppManagerClientOff` /
`kClientExoAppInternalOff` across `patches/Accessibility` to enumerate
every consumer and confirm no file includes both headers.
