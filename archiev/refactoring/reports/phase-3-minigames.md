# Phase 3 scan — minigames batch

Scope:
- minigame_turret.cpp (2003 lines) + minigame_turret.h (37 lines)
- minigame_swoop_audio.cpp (1337 lines) + minigame_swoop_audio.h (23 lines)
- minigame_pazaak.cpp (883 lines) + minigame_pazaak.h (58 lines)
- minigame_swoop_race.cpp (852 lines) + minigame_swoop_race.h (35 lines)
- minigame_aim.cpp (157 lines) + minigame_aim.h (113 lines)

Method: full sequential read of all ten files (two paginated reads for the two
files over the single-page cap: minigame_turret.cpp in two parts,
minigame_swoop_audio.cpp in two parts). Followed up with targeted greps to
verify every "is this dead / is this the only copy" claim before writing it
down, listed inline with each finding. Cross-checked findings against
STATE.md's "Execution findings" and "Phase 2 status" sections; nothing below
re-raises a settled item (the B4 SEH/ResolveMgoArray/CallAsCast/
ReadFollowerPosition consolidation is treated as done — see the verification
note at the top of Section A instead of being re-proposed).

## Consolidation check (explicitly requested in the brief)

Verified that every minigame file uses the Phase-2 B4 consolidated primitives
and that no local duplicate copy survived.

Grep run: `^(void\*|uint32_t|float|bool)\s+(SafeRead|ResolveMgoArray|CallAsCast|ReadFollowerPosition)`
against `minigame_*.cpp` — the only definitions found are in
`minigame_aim.cpp:19-119`. `minigame_turret.cpp`, `minigame_swoop_race.cpp`
and `minigame_swoop_audio.cpp` each only `using acc::minigame::...;` the
qualified names (turret.cpp:64-70, swoop_race.cpp:44-49, swoop_audio.cpp:
39-45). Confirmed clean — the consolidation held.

One minor residue from it, though: `minigame_turret.cpp:67` still
`using`-imports `acc::minigame::SafeReadVector`, but nothing in the file
calls it (grep for `SafeReadVector` in that file returns only the
using-declaration). See A4.

`minigame_pazaak.cpp` was NOT expected to use these — it reads a completely
different struct family (a GUI panel model, not CSWMiniPlayer/CSWMiniGame)
through its own small `ReadIntAt`/`ReadPtrAt`/`WriteIntAt` primitives, which
have a different calling convention (bool-return + out-param) from the
minigame_aim primitives (value-return + sentinel). Not duplication, a
different data shape — no finding there.

## Section A — general low-level cleanup

### A1 — stale "SEH-guarded primitive reads" banner (minigame_turret.cpp:590-596)
What's there: a banner comment "SEH-guarded primitive reads (same pattern as
engine_* / swoop_race)" followed by two blank lines and then
`ReadMiniGameViaArea` — a turret-specific area-chain read, not a primitive.
Why it's a problem: this banner is pre-Phase-2 residue. The actual SEH
primitives it used to introduce (`SafeReadPtr`/`SafeReadU32`/etc.) were
consolidated into `minigame_aim.cpp` by B4; this file only `using`-imports
them now (lines 64-70). The banner now mis-describes what follows it.
Proposed change: delete the banner (or replace with a one-line note that the
primitives live in minigame_aim.h, if a section marker is still wanted).
Risk: mechanical (comment-only). Estimated delta: -4 lines.

### A2 — stale "MGO-walk helpers... kept local" banner (minigame_turret.cpp:617-625)
What's there: a banner claiming "MGO-walk helpers (mirrors
swoop_spatial_audio.cpp — kept local so that swoop TU isn't entangled with
turret-specific cueing)", followed by five blank lines and no code before the
next banner.
Why it's a problem: `ResolveMgoArray` is not kept local anymore — it is the
consolidated `acc::minigame::ResolveMgoArray` (imported at line 68). The
banner describes a design that Phase 2 changed and is now actively
misleading about where this logic lives.
Proposed change: delete the banner and the blank run.
Risk: mechanical (comment-only). Estimated delta: -9 lines.

### A3 — comment references a removed constant (minigame_turret.cpp:627-628)
What's there: "Approach cue. Only the nearest kFighterMaxConcurrent in-range
fighters loop (the rest are stopped) — see kFighterMaxConcurrent for why
all-in-range clumped."
Why it's a problem: grepped `kFighterMaxConcurrent` across the whole
`patches/Accessibility` tree — the only two hits are this comment's own two
mentions. The constant does not exist. The design it describes (multiple
concurrent per-fighter loops, capped to N) is also not what the code does
today: `TickFighterCues`/`DriveSelectedPeg` drive exactly one `peg_cue` (the
Q/E-selected fighter's aim tone) and one `hum_cue` (the same fighter's
approach loop) — a single-target design, not a capped multi-loop one. This
is comment drift from an earlier iteration.
Proposed change: rewrite the comment to describe the current single-selected-
fighter design (a few lines below already do this correctly — e.g. lines
917-921 "Drive the continuous peg tone on the selected fighter..."), or
delete the stale paragraph since it's redundant with that correct one.
Risk: mechanical (comment-only). Estimated delta: -6/+2 lines.

### A4 — unused using-declaration (minigame_turret.cpp:67)
What's there: `using acc::minigame::SafeReadVector;` alongside the other five
consolidated-primitive imports.
Why it's a problem: grepped `SafeReadVector` in `minigame_turret.cpp` —
the only match is this using-declaration. The file never calls it (it uses
`ReadOffsetVector`/`WriteOffsetVector` and the other four primitives
instead).
Proposed change: remove the unused using-declaration.
Risk: mechanical (compiler-checked — an actual call would fail to resolve
if one existed). Estimated delta: -1 line.

### A5 — duplicated offset-vector-field constant (minigame_swoop_race.cpp:87, minigame_swoop_audio.cpp:200)
What's there: both files declare their own
`constexpr size_t kMiniPlayerOffsetVectorOffset = 0x1c4;` in their anonymous
namespace, even though `minigame_aim.h:80` already publishes
`acc::minigame::kMiniPlayerOffsetVectorOffset = 0x1c4` for exactly this
field, and both files already `using`-import several other `acc::minigame::*`
names.
Why it's a problem: this is the "magic number that already has a named
constant elsewhere" case the brief calls out — the same RE fact (the offset
field's byte offset) is now spelled out independently in three places. It is
not wrong today (all three agree), but it is a latent-drift risk: nothing
would fail to compile if one of the three copies were edited on its own.
Proposed change: delete the two local constants and add
`using acc::minigame::kMiniPlayerOffsetVectorOffset;` (or qualify the two
call sites directly) in both files.
Risk: mechanical (compiler-checked; both files already read the field only
through `SafeReadVector`/`WriteOffsetVector`, so this is a pure rename).
Estimated delta: -2 lines across the two files.

### A6 — unused `#include <cmath>` (minigame_swoop_race.cpp:17)
What's there: `#include <cmath>` in the include block.
Why it's a problem: grepped for every `std::` math call
(`sqrt|fabs|pow|sin|cos|atan|acos|asin`) and every bare call of the same
names in the file — zero matches. Nothing in `minigame_swoop_race.cpp` uses
`<cmath>` (the file only does integer/float arithmetic and calendar-clock
math, no transcendental functions).
Proposed change: remove the include.
Risk: mechanical (compiler-checked). Estimated delta: -1 line.

### A7 — stale "SEH-guarded primitive reads" banner (minigame_swoop_race.cpp:253-256)
Same pattern as A1: a banner "SEH-guarded primitive reads. Same pattern as
the rest of engine_*." immediately precedes `ReadMiniGameViaArea` (a
race-specific area-chain read, not a primitive) with a blank line where the
consolidated primitives used to be defined before B4.
Proposed change: delete the stale banner.
Risk: mechanical (comment-only). Estimated delta: -3 lines.

### A8 — stale "SEH-guarded primitive reads" banner (minigame_swoop_audio.cpp:707-711)
Same pattern again: banner "SEH-guarded primitive reads. Same pattern as the
rest of engine_*." followed by a blank line and then the
"MGO array + AsXxx vtable downcasts..." banner — no primitives actually
follow it (they live in minigame_aim.cpp now). Also note the immediately
following block at lines 712-725 ("MGO array + AsXxx vtable downcasts...")
has its own now-empty explanatory paragraph (the "Resolve the global
CSWMiniGameObjectArray via..." comment at 716-719 and the "Call an MGO
object's vtable..." comment at 721-724) with no code directly beneath either
— both describe `ResolveMgoArray`/`CallAsCast`, which also moved to
minigame_aim.cpp.
Proposed change: delete or fold these three orphaned banners into the single
real block that follows (`ReadAurObjectName`), or replace them with a short
pointer to minigame_aim.h.
Risk: mechanical (comment-only). Estimated delta: about -20 lines.

### A9 — dead constants from two retired steering features (minigame_swoop_audio.cpp)
What's there: a cluster of `constexpr` values left over from the "discrete
left/right co-pilot ticks" feature (retired 2026-06-23 per the file's own
comment at line 948) and the "next-gate preview" feature (designed, per the
comment block at 608-621, but apparently never wired into
`TickAccelpadCues`):
- `kSteerLeadTicks` (line 369) — referenced only in its own explanatory
  comment (356), never in code.
- `kSteerPanSmooth` (377), `kSteerDeadzoneUnits` (381), `kSteerSettleVel`
  (382), `kSteerMaxErr` (387) — same, declared and commented, never read.
- `kSteerTickPanM` (502), `kSteerTickFwdM` (503), `kSteerTickMs` (512),
  `kSteerLeftResref` (516), `kSteerRightResref` (518) — the discrete-tick
  geometry/cadence/resrefs; the file's own comment at 948 says the discrete
  ticks were "retired 2026-06-23" in favour of the continuous panned guide.
  (`kSteerAlignedResref` at 520 is NOT in this list — it is still used, at
  line 1260, for the "on track" confirmation blip.)
- `kPreviewLeadU` (618), `kPreviewVolume` (620) — the next-gate preview
  design's tuning constants; no code reads either.
Why it's a problem: grepped every one of these names across
`minigame_swoop_audio.cpp` — each has exactly the declaration line plus (for
some) a mention inside its own descriptive comment, and nothing else. They
are inert: not called, not read into any log line, not part of any log
format string.
Proposed change: delete the eleven constants above. The surrounding design
commentary (which is valuable RE/design history) can stay or be trimmed to a
short "retired 2026-06-23, superseded by the continuous panned guide below"
note — that's a judgement call for whoever executes this, not part of the
finding.
Risk: mechanical (compiler-checked — MSVC does not warn on unused
namespace-scope `constexpr`, which is presumably how these survived a
0-warning build undetected). Estimated delta: about -11 lines of code plus
whatever commentary trimming is chosen.

### A10 — dead state fields from the same two retired features (minigame_swoop_audio.cpp)
What's there: three `SpatialAudioState` fields — `smoothed_steer_err`
(line 655), `last_steer_tick_ms` (669), `previewed_slot` (678) — each
initialised at declaration and reset in `ResetSpatialAudio()` (1315, 1321,
1323), but never read or written anywhere in `TickObstacleCues` or
`TickAccelpadCues`.
Why it's a problem: same residue as A9 — `last_steer_tick_ms`'s own comment
(666) says it "paced the (now retired) directional tick"; `previewed_slot`
belongs to the never-wired preview feature. Grepped all three names in the
file — only the declaration and the reset touch them.
Proposed change: delete the three fields and their reset lines.
Risk: mechanical (compiler-checked, pure dead-store removal). Estimated
delta: -6 lines.

### A11 — leftover full-fidelity per-tick diagnostic left enabled (minigame_swoop_audio.cpp:606, used at 1182-1189)
What's there: `constexpr bool kSwoopLateralProbe = true;`, gating an
unthrottled per-tick `acclog::Write("SwoopProbe", ...)` in
`TickAccelpadCues`. The constant's own comment (600-605) calls it a
"temporary diagnostic... used to size the magnet's hold/finish tuning and
the rock-repel step... Flip to false (or delete the probe block...) once the
authority number is captured — it is a measurement aid, not a feature."
Why it's flagged, not proposed as a fix: this is exactly the
self-documented-temporary-diagnostic case, but STATE.md records
rock-avoidance as the *next planned* tuning lever (not yet done), and this
file is under the same active-tuning umbrella as swoop_race.cpp per the
brief. It is plausible the authority number this probe measures is still
in active use for that upcoming work.
Question for the user, not a candidate: has the lateral-authority number
already been captured and used? If yes, this is a safe, mechanical flip to
`false` (pure logging-volume change, doesn't touch any heuristic). If the
rock-avoidance work still needs it, leave it as is.

### A12 — stale doc comment: function renamed, header comment wasn't (minigame_pazaak.h:53, minigame_pazaak.cpp:597)
What's there: `minigame_pazaak.h`'s comment on `DispatchWagerInput` says
"the per-tick wager observer (ObserveWager) announces the resulting amount."
The actual function is `ServiceWagerPopup` (anonymous namespace,
`minigame_pazaak.cpp:597`) — there is no `ObserveWager` anywhere in the
codebase (grepped).
Proposed change: update the comment to say `ServiceWagerPopup`.
Risk: mechanical (comment-only). Estimated delta: 0 (one word changed).

### A13 — DriveSelectedPeg is doing at least eight separable jobs (minigame_turret.cpp:922-1575, ~650 lines)
What's there: one function that in sequence (1) resolves lock-loss/kill vs.
out-of-view and re-picks a target, (2) fires the range-window enter/leave
cue, (3) estimates relative velocity and centripetal acceleration (EMA),
(4) solves the linear + curve-aware intercept lead, (5) computes the
hitbox subtend and on-target/makeability tests, (6) fires the one-shot
"fire now" cue and drives the continuous peg loudness/behind-gate/tick-rate
logic, (7) runs the passive offset→world sign calibration and the
magnetism/full-autoaim steering write, (8) drives the elevation-pitch
channel, (9) logs four distinct diagnostic lines (`TurretHit`, `TurretVel`,
`TurretAim`, plus the throttled assist/calibration line), and (10) runs the
full-autoaim switch-on-hit / unmakeable-retarget state machine.
Why it's a problem: every other per-tick driver function in this batch
(`TickObstacleCues`, `TickAccelpadCues`, `TickRaceTimer`, pazaak's
`AnnounceDeltas`) is well under 150 lines and does one identifiable job.
This function is a clear outlier for the codebase's own conventions, and
its size makes the four A1/A2-style stale-banner and A3/A4 dead-reference
findings above more likely to keep recurring here as the function evolves.
Proposed change: NOT proposed as a concrete split in this report — Phase 3
is scan-only, and per the execution lessons already logged for candidates
13/24 (STATE.md), a naive function-boundary read here would likely miss
threaded local state (at minimum `angle`, `aimRel`, `sd`, `sp`, `leadT`,
`radius`, `onTargetCue`, `steer`/`gain` cross several of the ten jobs above)
and need real design work, not a mechanical extraction. Flagging this as a
Phase-3 candidate for the user to decide whether it's worth that work, with
the explicit caveat that it is the single largest/riskiest item in this
report.
Risk: needs-design-pass (not mechanical) — and if approved,
needs-in-game-test (a full turret round exercising kills, misses, range
transitions, and both magnetism and full-autoaim). Estimated delta: 0 net
lines (pure reorganisation), touches the file's most performance/precision-
sensitive code path.

## Section B — AI-pattern findings

### B1 — repeated "compress a target position toward the listener" arithmetic (minigame_turret.cpp)
What's there: the same three-line shape —
`kk = <near-distance> / <real-distance>; pos.x = listener.x + (target.x -
listener.x) * kk;` (and the .y/.z equivalents) — appears near-identical four
times:
- `PlayRangeCueAt` (810-820), ratio `kPegMinDist / dist`
- `AnnounceSelectedTarget`'s locator ping (859-866), ratio `kPegMinDist / sd`
- the "fire now" one-shot position (1237-1243), ratio `kPegMinDist / sd`
- the continuous peg's `cuePos` (1262-1267), ratio `srcDist / aimTgtDist`
  (same shape, loudness-ramped distance instead of the fixed `kPegMinDist`)
Why it's a problem: this is the "copy-paste block an abstraction should
own" case from the brief's Section B checklist. All four exist to solve the
same problem (KOTOR's 3D audio engine has a steep near-field attenuation
curve, so cues are played at a compressed, always-audible distance while
keeping the true bearing) and would read more clearly as one named helper
call at each site.
Proposed change: a small helper, e.g.
`Vector CompressToward(const Vector& target, const Vector& listener, float
compressedDist, float realDist)` returning `listener + (target-listener) *
(compressedDist/realDist)`, guarded for `realDist <= 0`. Replace all four
call sites.
Risk: low (pure refactor of already-tested arithmetic; each site's constants
are unchanged, only the shape is factored out) — still worth a short in-game
turret check since it touches every positional cue in the file.
Estimated delta: -12/+6 lines net.

No other Section B items met the evidence bar. The rest of this batch reads
as a deliberately-kept RE/tuning investigation log (dated, measured,
"superseded" call-outs) rather than AI-generated filler — comments explain
*why*, not restate the adjacent line, and the guard clauses I checked (e.g.
the multi-condition gates in `TickAccelpadCues`) each correspond to a
genuinely distinct failure mode rather than duplicated validation.

## Findings (possible bugs — user decides)

None found with enough confidence to report. I looked specifically at the
makeability/lead-gate interaction, the calibration sign fallback, and the
kill-vs-despawn detection in `minigame_turret.cpp`, and the finish-vs-coast-
fallback detection in `TickRaceTimer` (`minigame_swoop_race.cpp`) — all read
as intentional and internally consistent with their own comments. Nothing
else stood out on a full read of the other three files.

## Candidate 28 — narrow-header include opportunities

- `minigame_turret.cpp` includes `engine_offsets.h` for `Vector` only,
  `engine_area.h` for `GetCurrentArea`/`GetClientArea` only, and
  `engine_player.h` for `GetCameraPosition`/`GetPlayerPosition` +
  `kAddrAppManagerPtr` only (out of a much larger surface in each header).
- `minigame_swoop_race.cpp` — same three, same narrow usage (`Vector`;
  `GetCurrentArea`/`GetClientArea`; `GetCameraPosition`/`kAddrAppManagerPtr`
  for the race-timer's server-app chain).
- `minigame_swoop_audio.cpp` includes `engine_offsets.h` for `Vector` only
  and `engine_player.h` for `GetCameraPosition`/`GetPlayerPosition` only.
- `minigame_aim.h` includes `engine_offsets.h` for `Vector` only.
- `minigame_pazaak.cpp`/`.h` include none of the four aggregators named in
  the brief — clean already.

Of these, only the `engine_offsets.h` → `engine_offsets_types.h` narrowing is
actually actionable today: `Vector` lives in `engine_offsets_types.h` (the
Phase-2 C8 split already exists). `engine_player.h` and `engine_area.h` have
no narrower sibling headers yet (confirmed via `Glob` — only
`engine_player.h`/`engine_player_internal.h` and a single `engine_area.h`
exist), so per STATE.md's candidate-28 deferral note these four files are
just recording the future opportunity, not something to act on now.

## Files scanned with nothing to report

- `minigame_turret.h` — clean.
- `minigame_swoop_race.h` — clean.
- `minigame_swoop_audio.h` — clean.
- `minigame_aim.cpp` — clean; this is the Phase-2 B4 consolidation target and
  reads as intended, verbatim-migrated primitive bodies plus the two new
  magnetism functions.
- `minigame_aim.h` — clean apart from the candidate-28 note above.
- `minigame_pazaak.cpp` — clean apart from A12 (its header's stale
  function-name reference); the rest of the file (structural panel
  identification, arrow-zone navigator, wager stepper) is tight and each
  helper does one job.
