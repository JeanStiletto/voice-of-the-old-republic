# Phase 3 scan — audio, speech backend and logging

Scope (14 files, 3346 lines total):
- audio_bus.cpp (432) / audio_bus.h (164)
- audio_cue_player.cpp (117) / audio_cue_player.h (28)
- audio_cues.h (94)
- audio_loop.cpp (270) / audio_loop.h (85)
- audio_pitch.cpp (78) / audio_pitch.h (28)
- audio_footstep_suppress.cpp (507) / audio_footstep_suppress.h (47)
- prism.cpp (664) / prism.h (73)
- log.cpp (637) / log.h (122)

Method: full read of every file (all 14, top to bottom), then targeted greps
to verify every "is this dead/duplicate?" hypothesis before writing it up —
the brief's traps (file-name-vs-namespace, function-name-only scans, exported/
hook-target functions, registered callbacks) were checked explicitly per
finding below, not assumed. Specific verification greps:
- `grep -rn "WideToUtf8"` (only the definition — 0 call sites)
- `grep -rn "std::is_\|type_traits"` in prism.cpp (only the include line)
- `grep -n "struct CResRef"` (only audio_bus.cpp and audio_loop.cpp)
- Per-address cross-check of every `acc::addr::R(0x...)` constant declared in
  this batch against `engine_rebase_table.inc`, `engine_rebase_rdata.inc` and
  the hand-resolved `kXrefTable` in engine_rebase.cpp (all 21 addresses
  resolved — see the note under "checked, not a finding" below)
- `grep -rn "kAddrCExoSoundSource<Name>"` for every CExoSoundSource address in
  audio_bus.h, excluding engine_rebase_table.inc, to separate genuinely
  unused constants from ones the file already documents as intentionally-kept
- `grep -n "OnPlayFootstep\|OnSetListenerPosition\|OnCalculatePitchVarianceFrequency"`
  against exports.def, hooks.toml and allard.hooks.toml (all three hook
  functions are wired on both builds)
- Per-NavCue-enum-value grep against the whole codebase to check for unused
  cue definitions (all 24 values in audio_cues.h have at least one live
  caller outside this batch)
- `grep -n "strings::Get("` across the batch (only one call site, in
  audio_footstep_suppress.cpp, already null-check-free per convention)

## Section A — general low-level cleanup

### A1 — dead `#include <type_traits>` (prism.cpp:22)

What's there: `#include <type_traits>` in the include block.
Why it's a problem: nothing in prism.cpp uses `std::is_*`, `enable_if`,
`decltype`-based trait queries, or any other type_traits facility — verified
by grepping the file for `std::is_` and `type_traits`; the only hit is the
include line itself.
Proposed change: delete the include line.
Risk: mechanical (compiler-checked — if anything did depend on it, removal
would fail to compile).
Estimated line delta: -1.

### A2 — dead function `WideToUtf8` (prism.cpp:207-215)

What's there:
```cpp
bool WideToUtf8(const wchar_t* in, char* outBuf, size_t outBufSize) {
    if (!in || !outBuf || outBufSize == 0) return false;
    int needed = WideCharToMultiByte(CP_UTF8, 0, in, -1, nullptr, 0, nullptr, nullptr);
    ...
}
```
Why it's a problem: defined in the anonymous namespace but has zero callers —
verified by grepping the whole codebase for `WideToUtf8`; the only match is
the definition itself. `Speak(const wchar_t*, bool)` (prism.cpp:504) does its
own inline wide-to-UTF-8 conversion with a stack/heap buffer instead of
calling this helper, so the two paths have quietly diverged (this function is
a plain "measure then convert, fail on overflow" version; the live `Speak`
path adds a heap fallback for long strings). Not a case of "looks unused but
is a registered callback" — it's a free function, not in exports.def, not
referenced from hooks.toml, not a function pointer stored anywhere.
Proposed change: delete the function.
Risk: mechanical.
Estimated line delta: -9.

### A3 — duplicated `CResRef` + `FillResRef` (audio_bus.cpp:21-35, audio_loop.cpp:18-32)

What's there: byte-identical 16-char resref struct and lowercasing/zero-fill
helper, defined independently in each file's anonymous namespace.
`audio_loop.cpp:17` even says so in a comment: `// Local mirror of the
16-byte tag from audio_bus.cpp.`
Why it's a problem: two independent copies of the same fixed-size-buffer
logic; a future change to the truncation or lowercasing rule (e.g. widening
past 16 chars, or fixing a case-mapping edge case) has to be remembered in
both places. Verified there is no third copy and no shared header already
providing this (`grep -n "struct CResRef"` returns exactly these two hits).
Proposed change: move `CResRef` and `FillResRef` into a small shared header
(or into audio_bus.h, which audio_loop.cpp already depends on via
`kAddrCExoSoundSource*`) and delete the local copy in audio_loop.cpp.
Risk: low — mechanical extraction, but touches two TUs and needs a rebuild
to confirm no ODR conflict from having both an extern declaration and the
old local `namespace { }` one during the edit.
Estimated line delta: -15 (one copy removed, one small shared declaration
added).

### A4 — duplicate offset constant for the same engine field (audio_loop.cpp:47, audio_pitch.cpp:31)

What's there: `audio_loop.cpp`'s `kInternalBaseFrequencyOffset = 0x48` and
`audio_pitch.cpp`'s `kOffsetBaseFrequency = 0x48` are two independently-named
constants for the identical field — `CExoSoundSourceInternal`'s cached
natural sample frequency, per both files' own comments (`// sample natural
Hz` vs the `base_frequency` field the pitch hook reads). Confirmed neither
value exists in `engine_offsets_fields.h` today (grepped for `0x48` and
`BaseFrequency`/`base_frequency` there — no match), so this is a live
duplicate-by-coincidence, not an existing-named-constant-bypassed case.
Why it's a problem: same fact recorded twice under different names in two
files that already reference each other's domain (audio_pitch neutralises
jitter on the exact source audio_loop plays through).
Proposed change: promote one of the two to a shared constant (candidate
location: `engine_offsets_fields.h`, next to the rest of the
CExoSoundSourceInternal-adjacent field offsets already documented in
audio_loop.cpp) and have both files reference it.
Risk: low — pure rename/relocate of a compile-time constant, no behaviour
change.
Estimated line delta: -1 net (one constant instead of two), +1 in the shared
header.

## Section B — AI-pattern findings

### B1 — near-identical resolve-and-cache pair (audio_bus.cpp:197-249)

What's there: `GetCuePriorityGroup()` and `GetSpatialCuePriorityGroup()` both
implement "static cached index, scan once for a sentinel FadeTime, cache the
result, log the outcome" with the same four-branch shape (already resolved /
found / not-ready-yet / absent-so-fall-back). They differ only in which
sentinel they scan for and what they do on absence (fixed fallback constant
vs. delegating to the other function).
Why it's a problem: not an accidental copy — the comments show the second
function was deliberately modelled on the first — but the ~40 lines of
resolve/cache/log control flow are now maintained twice, and any future
project decision about e.g. cache invalidation would need to change both.
Proposed change: factor the shared shape into a private helper taking the
sentinel value and a fallback strategy (constant vs. delegate), leaving the
two public functions as thin callers. Not urgent — the current duplication is
readable and the two functions really do have different fallback semantics,
so this is a nice-to-have rather than a defect.
Risk: low — pure refactor of already-covered logic (no behaviour change if
done carefully), but touches the resolve/cache pattern that Phase 2 already
scrutinised for a false alarm (C10) in the same file, so treat any change
here as needing the same care.
Estimated line delta: -20.

### B2 — belt-and-braces `g_sapiReady` recheck (prism.cpp:394-407, call site 600-602)

What's there: `SpeakUrgentImpl` only calls `EnsureSapiVoice(voiceId)` inside
`if (applyVoice && g_sapiReady)` (line 600), and `EnsureSapiVoice`'s own first
line re-checks `if (!g_sapiReady || !pPrism_backend_set_voice) return false;`
(line 395). Verified `EnsureSapiVoice` has exactly one call site in the file
(grepped `EnsureSapiVoice` in prism.cpp), so the `g_sapiReady` half of its
guard duplicates a check already made one frame up.
Why it's a problem: matches the brief's "belt-and-braces guards that
duplicate a check one frame up the call stack" pattern precisely. Harmless
today (single call site, no behaviour difference), but reads as if
`EnsureSapiVoice` were a public/multi-caller entry point when it is a private
single-purpose helper.
Proposed change: either drop the `g_sapiReady` half of the check inside
`EnsureSapiVoice` (relying on the call site's guard) or, if the intent is for
`EnsureSapiVoice` to stay safe as a standalone entry point for future
callers, leave it and add a one-line comment saying so. Low value either way
— flagging for completeness, not urging action.
Risk: mechanical if removed (compiler/tests would catch any new caller that
relied on the internal guard).
Estimated line delta: -1 (if simplified).

## Findings (possible bugs — user decides)

None. One candidate was investigated and ruled out: `audio_loop.cpp:54`
wraps `kIatAilSet3DPlaybackRate` (an IAT slot address, 0x0073D4E8) in
`acc::addr::R()`. At first read this looked like it might be a `.rdata`/
`.data` address wrongly treated as `.text` (`engine_rebase.h` says only
`.text` needs `R()`), and it has no entry in `engine_rebase_table.inc` — the
same shape of gap Phase 2's C8 fixed for 12 other addresses. Checking
`engine_rebase_rdata.inc` before writing this up found it: the address IS
mapped there (`{ 0x0073D4E8, 0x0073D4E8 }, // kIatAilSet3DPlaybackRate
(operand)`), consistent with `engine_rebase.cpp`'s documented
`.rdata`-supplement mechanism and its own comment naming "the import-table
entry" as one of the two operand-only resolutions from the 2026-07-25 pass.
Correctly wrapped, correctly tabled — not a bug. Recorded here so a future
scan doesn't re-spend time on the same address.

## Candidate 28 — narrow-header include opportunities

- `audio_bus.h:16` includes `engine_offsets.h` for `Vector` only —
  `engine_offsets_types.h` would suffice.
- `audio_loop.h:12` includes `engine_offsets.h` for `Vector` only — same.
- `audio_cue_player.h:18` includes `engine_offsets.h` for `Vector` only —
  same.
- `audio_footstep_suppress.cpp:10` includes `engine_offsets.h` for `Vector`
  only (the file's other engine dependencies — `GameObjectKind`,
  `AreaObjectIterator`, `WallEdge`, `SegmentCrossesWalkmesh` — come from the
  already-included `engine_area.h`) — same.
- Caveat: all four files also transitively pull the full `engine_offsets.h`
  through `engine_player.h` and/or `engine_area.h`, which are themselves
  single (non-split) headers that still `#include "engine_offsets.h"`
  wholesale. Narrowing just these four include lines buys nothing until
  `engine_player.h`/`engine_area.h` are themselves split — consistent with
  the STATE.md note that 10+28 are one job. Recording the four anyway since
  the brief asks for one line per file regardless of the transitive picture.

## Checked, not a finding (for the record)

- **Unused `NavCue` cue definitions** — none. All 24 enum values in
  `audio_cues.h` have a live caller somewhere in the codebase (grepped each
  by name); the eleven "world" cues route through `audio_cue_player.cpp`,
  the four guidance/beacon cues are called directly from
  `guidance_beacon.cpp`/`map_ui_cursor.cpp`, and the seven swoop cues are
  called directly from `minigame_swoop_audio.cpp`/`minigame_swoop_race.cpp`
  (by design — the header documents that the on-demand/minigame cues bypass
  the shared gate-and-range funnel).
- **Four `kAddrCExoSoundSource*` constants with no call site**
  (`kAddrCExoSoundSourceCtorWithResRef`, `kAddrCExoSoundSourceSetPitchVariance`,
  `kAddrCExoSoundSourceSetFixedVariance`, `kAddrCExoSoundSourceGetLooping` —
  audio_bus.h:151,157,161,162) — grepped, genuinely zero callers. Not
  flagging as dead: audio_bus.h documents this exact situation in place for
  a sibling constant (`kAddrCExoSoundSourceInternalCalculatePitchVarianceFrequency`,
  lines 127-131 — "Nothing reads this constant... kept because the address
  belongs next to the rest of the CExoSound surface... so that the file has
  one rule rather than an exception someone has to explain"). These four are
  the same situation: part of the documented full CExoSoundSource lifecycle
  surface (lines 135-164), most of which audio_loop.cpp does use. Removing
  four of fourteen sibling constants while explicitly keeping a fifth for
  the stated reason would contradict the file's own documented policy, not
  execute it.
- **`acc::strings::Get()` null-checks** — none present in this batch to be
  dead. The only caller (`audio_footstep_suppress.cpp:219-234`) already
  follows the no-null-check convention.
- **Hardcoded user-facing strings bypassing `Get(Id)`** — none. The only
  spoken/`snprintf`-built string in the batch (`RunStuckProbe`'s direction
  list, audio_footstep_suppress.cpp:214-235) is built entirely from
  `acc::strings::Get()` calls.
- **Raw hex literals with an existing named constant in
  `engine_offsets_fields.h`** — none beyond A4 above (which is a
  cross-file duplicate, not a bypassed existing constant).
- **Address-rebase coverage** — every `acc::addr::R(0x...)` constant declared
  in this batch (21 total, all in audio_bus.h/.cpp and audio_loop.cpp) has an
  entry in `engine_rebase_table.inc`, `engine_rebase_rdata.inc`, or the
  hand-resolved `kXrefTable`. No C8-style gap in this batch.
- **Oversized functions for Phase-3 decomposition** — none in this batch
  clear the bar. The largest, `RunStuckProbe` (audio_footstep_suppress.cpp,
  ~118 lines) and `LoopSource::Start` (audio_loop.cpp, ~85 lines), are each a
  single cohesive lifecycle/sequence with the sub-steps already named and
  commented; nothing like `ClassifyCluster`/`BuildForArea`'s 500+ line,
  multi-responsibility shape shows up here.
- **Logging rate limits / fidelity** — not proposing any. Noted for context
  only: `OnSetListenerPosition` (audio_bus.cpp:410-429) and
  `OnCalculatePitchVarianceFrequency` (audio_pitch.cpp:69-75) each already
  use a pre-existing edge+heartbeat pattern (log on state change or every
  30s/1s) to keep a per-frame hook's log volume sane. This is the project's
  documented stability-debounce pattern applied to two hot hooks, not
  something this scan is proposing — leaving as-is.

## Files scanned with nothing to report

- audio_cues.h — clean; data-only header, every entry live.
- audio_pitch.h — clean.
- audio_footstep_suppress.h — clean.
- log.h — clean.
- log.cpp — clean; no dead helpers (`WriteHex`, `RegisterPtr`, `FmtPtr`,
  `BringupMark`, `Once` all have live callers elsewhere in the codebase),
  no rate-limiting to flag either way, no oversized functions.
- prism.h — clean.
