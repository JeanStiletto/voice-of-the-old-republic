# Phase 3 scan — engine player, panels and picker

Scope: engine_player.cpp (171), engine_player.h (246), engine_player_internal.h
(35), engine_player_party.cpp (494), engine_player_inputlock.cpp (236),
engine_panels.cpp (838), engine_panels.h (314), engine_panels_internal.h (41),
engine_panels_state.cpp (410), engine_picker.cpp (571), engine_picker.h (144).

Method: full read of all eleven files (no truncation). Then targeted greps to
verify every "unused include" and "unused constant" claim codebase-wide before
reporting it — never trusted a file-name-only impression. Specific greps run:

- `#include "engine_player` / `#include "engine_panels` / `#include "engine_picker`
  across the whole patch tree, to size the aggregator fan-out (75 / 46 / 4
  includers respectively) for the candidate-28 section.
- Per candidate unused-include: grepped the .cpp file itself for every symbol
  the include comment claims to provide (e.g. `GetObjectHandle`,
  `ReadCExoString`, `acclog::`, `acc::addr::`) — zero matches outside the
  `#include` line itself is what "unused" means below.
- `kServerExoAppPartyTableOffset`, `kVtableCSWGuiPowersLevelUp`,
  `TickActionQueueDiag`, `ReadDialogReplyCount` — whole-tree greps to confirm
  dead vs. live before writing any removal candidate (per the probe_priority_
  groups / candidate-22 trap in the brief).
- `GetForegroundPanel` definition in engine_manager.cpp, read directly, to
  check whether the SEH-guard gap found in engine_panels_state.cpp is unique
  to this batch or part of a wider pattern (it's wider — noted in the finding).
- `0x9c`, `kClientExoAppInternalOffset`, `kInputClassOffset` across the tree,
  to confirm the magic-number claim against the sibling named constant in
  engine_area.cpp rather than assuming.

Context verified per the brief's specific asks:
- **B5 fold (Phase 2)**: confirmed no duplicate AppManager-constant definition
  survived. `kAddrAppManagerPtr` / `kAppManagerClientAppOffset` are defined
  exactly once, in engine_player.h:195-196; engine_panels_internal.h now only
  *documents* that history and reaches them via `#include "engine_player.h"`.
  `kVtableCSWGuiPowersLevelUp` (used in engine_panels.cpp:346) resolves to the
  single definition in engine_offsets_addresses.h:252 — no shadow copy.
- **R()/Ok() guards in engine_panels_state.cpp**: read every guarded call
  (`CallPrevSWInGameGui`, `CallHideSWInGameGui`, `SetGlobalDialogState`,
  `CloseInGameMenuToWorld`). Each checks `Ok()` before dispatch and logs+bails
  otherwise; `CloseInGameMenuToWorld` requires *both* its addresses per the
  comment (`HideSWInGameGui` and `SetInputClass`), matching the execution
  finding recorded in STATE.md. `is_active` is not part of this file (it
  belongs to a different subsystem) so that specific carried-forward warning
  doesn't apply here — no `is_active` write exists in this batch. No wrapping
  changes proposed.

## Section A — general low-level cleanup

### A1 — Four unused post-split includes in engine_player.cpp (engine_player.cpp:16-21)

What's there: the file includes `engine_area.h`, `engine_reads.h`, `log.h`
and `engine_rebase.h`, each with an inline comment explaining what it's for
— `GetObjectHandle` / `GetObjectDisplayNameByHandle` / `kCreatureStatsPtrOffset`
"used by GetActiveLeaderName", `ReadCExoString, ExtractTextOrStrRef`,
`acclog::Write`, and the `SetPlayerInputEnabled` auto-restore tick.

Why it's a problem: none of those four symbols, or anything else from those
four headers, appears anywhere in engine_player.cpp's actual code (grep
confirmed zero matches outside the include block itself). This is exactly the
post-split residue the brief called out: `GetActiveLeaderName` and
`SetPlayerInputEnabled` are real functions, but they were split out to
engine_player_party.cpp and engine_player_inputlock.cpp respectively — the
comments (and the includes they justify) are leftovers from when all three
lived in one file. `Vector`/SEH/etc. that this file *does* need already come
through `engine_player.h` (which itself includes `engine_offsets.h`) and
`<windows.h>`/`<cmath>`/`<cstdint>`.

Proposed change: delete the four includes and their comments (lines 16-21).

Risk: mechanical (compiler-checked — `kdev build` will fail immediately if
anything was actually needed).

Estimated line delta: -6.

### A2 — Four unused includes in engine_player_inputlock.cpp (engine_player_inputlock.cpp:20-27)

Same pattern as A1, same root cause (candidate-5 split). `engine_area.h`,
`engine_reads.h` and `engine_rebase.h` are included but nothing from them is
used (grepped for `GetObjectHandle`, `ReadCExoString`, `ExtractTextOrStrRef`,
`acc::addr::` — zero matches). `<cmath>` is also unused: this file's
watchdog does arithmetic (`dx*dx + dy*dy`) but never calls a `std::` math
function; the `std::atan2` calls that need `<cmath>` stayed behind in
engine_player.cpp. `log.h` IS used (`acclog::Write` appears repeatedly) —
keep it.

Proposed change: delete `engine_area.h`, `engine_reads.h`, `engine_rebase.h`
(lines 24-27) and `<cmath>` (line 21).

Risk: mechanical (compiler-checked).

Estimated line delta: -5.

### A3 — Unused `<cmath>` in engine_player_party.cpp (engine_player_party.cpp:20)

Same reasoning as the `<cmath>` half of A2: no `std::` math call anywhere in
this file. Everything else this file includes (`engine_area.h`,
`engine_reads.h`, `log.h`, `engine_rebase.h`) IS used — `GetObjectHandle`,
`GetObjectDisplayNameByHandle`, `ExtractTextOrStrRef`, `ReadCExoString`,
`ResolveServerObjectHandle`, `acclog::Trace`, `acc::addr::R` all appear in
this file's body, so those stay.

Proposed change: delete line 20.

Risk: mechanical (compiler-checked).

Estimated line delta: -1.

### A4 — Dead legacy alias `kServerExoAppPartyTableOffset` (engine_player.h:231-233)

What's there:
```
// Legacy alias kept for source compatibility while old (incorrect) path
// callers are audited.
constexpr size_t    kServerExoAppPartyTableOffset  = kServerInternalPartyTableOffset;
```

Why it's a problem: a whole-tree grep for `kServerExoAppPartyTableOffset`
finds exactly one hit — its own definition. The audit the comment describes
appears to be complete; every caller already reads
`kServerInternalPartyTableOffset` (or the correct-chain accessor
`GetServerPartyTable()`) directly. The alias is now pure dead weight, and its
own comment is the best evidence it's safe to remove — it was explicitly
scaffolding for a since-finished migration, not a value anyone still depends on.

Proposed change: delete the alias and its comment.

Risk: mechanical (compiler-checked; zero callers).

Estimated line delta: -3.

### A5 — engine_panels_state.cpp `GetInputClass` uses raw offsets instead of the in-scope named constant (engine_panels_state.cpp:206-221)

What's there:
```cpp
int GetInputClass() {
    __try {
        void* appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr);
        if (!appMgr) return -1;
        void* client = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appMgr) + kAppManagerClientAppOffset);
        if (!client) return -1;
        void* internal = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(client) + 0x04);
        if (!internal) return -1;
        return *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(internal) + 0x9c);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
```

Why it's a problem: the `+ 0x04` step is `kClientExoAppInternalOffset`
(engine_player.h:209), which is already in scope in this exact file — every
other AppManager-chain function in engine_panels_state.cpp and
engine_player_*.cpp names it. Using the raw literal here is inconsistent
with the rest of the file (and the rest of the module) for no reason; the
value is identical. Separately, `+ 0x9c` (the input_class field itself) has
no shared named constant anywhere — the only precedent is a function-local
`constexpr size_t kInputClassOffset = 0x9c;` inside
`MaybeDrivePassiveSelection()` in engine_area.cpp (not exported, so not
directly reusable here without a small header change). Flagging the
`0x9c` half as a secondary, lower-confidence note only — promoting it to a
shared constant is a slightly bigger call than the mechanical `0x04` fix.

Proposed change: replace `+ 0x04` with `+ kClientExoAppInternalOffset`.
Optionally (separate, smaller decision): publish a shared `kInputClassOffset`
next to `kClientExoAppGuiInGameOff` in engine_panels_internal.h and point both
engine_area.cpp's local copy and this `0x9c` at it — left to the user, since
it touches a second file outside this batch.

Risk: mechanical for the `0x04` fix (same value, compiler-checked). The
`0x9c` consolidation (if taken) is `low` — same value, no behavior change,
but touches engine_area.cpp.

Estimated line delta: 0 (rename only) to +2 if the shared constant is added.

### A6 — Same AppManager→CClientExoApp chain duplicated three times inside engine_panels_state.cpp (engine_panels_state.cpp:171-174, 208-212, 231-233)

What's there: `CloseInGameMenuToWorld`, `GetInputClass` and
`SetGuiInputClass` each re-derive `client` from `appMgr` with the identical
two-line shape:
```cpp
void* appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr);
void* client = appMgr ? *reinterpret_cast<void**>(
    reinterpret_cast<unsigned char*>(appMgr) + kAppManagerClientAppOffset) : nullptr;
```
(`GetInputClass` writes the same two derefs with early-returns instead of the
ternary, but it's the same read.)

Why it's a problem: this is a same-file, same-shape repetition — the
category the brief calls out explicitly ("small repetitions inside a file").
`ResolveGuiInGame()` (declared in engine_panels_internal.h, defined in
engine_panels.cpp) already walks one step further along the identical chain
(appMgr → client → internal → gui), so a `GetClientApp()`-style local helper
in this file (or promoting the first two steps into
engine_panels_internal.h next to `kClientExoAppGuiInGameOff`) would let all
three functions — and `ResolveGuiInGame` itself — share one read.

Proposed change: extract a small file-local (or `engine_panels_internal.h`)
helper `void* GetClientApp()` doing the two-step read, and call it from
`CloseInGameMenuToWorld`, `GetInputClass` and `SetGuiInputClass`.

Risk: low (pure refactor, output identical for every input including the
null/fault paths — each call site already treats a null result the same way
it does today).

Estimated line delta: -6 to -8.

### A7 — Target-handle validation and client-bit conversion duplicated three times in engine_picker.cpp (engine_picker.cpp:258-262, 268-270, 483-489, 541-547)

What's there: `Drive()`, `ReanchorRadial()` and `InitiateDialog()` each open
with the identical sentinel check:
```cpp
if (targetServerHandle == 0u || targetServerHandle == 0xFFFFFFFFu ||
    targetServerHandle == kInvalidObjectId) {
    return false;   // (Drive also clears *outSnapshot first)
}
```
and the identical server→client bit-OR:
```cpp
uint32_t targetClient =
    (targetServerHandle & 0x80000000u) ? targetServerHandle
                                       : (targetServerHandle | 0x80000000u);
```
(`InitiateDialog` names its local `clientHandle` instead of `targetClient`,
otherwise byte-identical.)

Why it's a problem: three verbatim copies of the same two checks in one
571-line file — a textbook small-repetition case, and cheap to fix since all
three are pure, stateless, single-expression checks with no coupling to
per-function state (unlike the candidate-13/24 traps, there is no shared
anonymous-namespace variable hiding in this one).

Proposed change: add two small helpers in the file's anonymous namespace:
```cpp
bool IsValidTargetHandle(uint32_t h) {
    return h != 0u && h != 0xFFFFFFFFu && h != kInvalidObjectId;
}
uint32_t ToClientHandle(uint32_t h) {
    return (h & 0x80000000u) ? h : (h | 0x80000000u);
}
```
and call them from all three functions.

Risk: mechanical (pure functions, identical inputs/outputs to the inline code
they replace; compiler-checked).

Estimated line delta: -10 to -12.

### A8 — Minor: two differently-named typedefs for the same GetNPCObject signature in engine_player_party.cpp (engine_player_party.cpp:310, 366)

`GetPartyMembers()` declares a function-local
`typedef int (__thiscall* PFN_GetNPCObject)(void*, int, int, int);`
(line 310) and the file's anonymous namespace (lines 362-368, used by
`GetPartyNpcNameForSlot`) separately declares
`typedef int (__thiscall* PFN_PartyTableGetNPCObject)(void*, int, int, int);`
— same signature, same target address (`kAddrCSWPartyTableGetNPCObject`),
different name. Cosmetic only (typedefs cost nothing at runtime) but it's an
easy one-line consolidation: drop the function-local typedef in
`GetPartyMembers` and reuse `PFN_PartyTableGetNPCObject` (or vice versa).
Low value — noting it because it was found, not because it's worth a
dedicated pass on its own.

Risk: mechanical. Estimated line delta: -1.

### A9 — Stale includer count in engine_panels_state.cpp's file banner (engine_panels_state.cpp:14)

"Declarations stay in engine_panels.h, so all 39 includers are unaffected."
A whole-tree grep for `#include "engine_panels` today finds 46 includers
(candidate 23's `menus_listbox_picker.cpp`, `tutorial_popup.cpp` and others
were added since this comment was written). The claim behind the number —
declarations didn't move, includers are unaffected — is still true; only the
specific count is stale. Low-value fix (a comment number, not logic), but
matches the brief's "stale comments" category exactly, so noting it.

Proposed change: reword to "so includers are unaffected" (drop the specific
count) or update it to the current figure — the user's call.

Risk: mechanical (comment-only). Estimated line delta: 0.

### A10 — Two more unused includes plus an unused header in engine_picker.cpp and engine_panels_state.cpp

- `engine_picker.cpp:5` — `<cstdio>` is included but nothing from it
  (`printf`/`sprintf`/`FILE`/etc.) is used anywhere in the file; all string
  work goes through the file's own `CopyCStringSafe`/`ReadResRef`/
  `ReadExoString` helpers.
- `engine_panels_state.cpp:24` — `<cstring>` is included but no
  `memcpy`/`memmove`/`strlen`/`memset` call exists in this file (the
  `memmove`-using cache eviction lives in engine_panels.cpp, not here).
- `engine_panels_state.cpp:27-28` — `engine_offsets.h` and `engine_reads.h`
  are both included but nothing from either is used directly: the
  `kMgr*Offset` constants this file reads come from `engine_manager.h`
  (already included separately), and no `ReadCExoString`/`ReadGuiString`/
  `ExtractTextOrStrRef` call exists in this file — those live in the sibling
  engine_panels.cpp. Both headers are reachable transitively anyway (through
  engine_panels_internal.h → engine_player.h → engine_offsets.h), so this is
  a genuine "include what you use" cleanup, not a compile risk either way.

Proposed change: delete all four includes.

Risk: mechanical (compiler-checked).

Estimated line delta: -4.

## Section B — AI-pattern findings

### B1 — Eight panel-vtable structural detectors in engine_panels.cpp reimplement a helper the file already has (engine_panels.cpp:50-58, 195-203, 213-225, 232-242, 250-260, 266-276, 282-292, 299-309, 315-325)

What's there: the file defines `ControlHasVtable(void* control, uintptr_t
expected)` at the top (lines 50-58) specifically to check a child control's
vtable. Eight further functions — `IsLevelUpStructural`,
`IsCharGenStructural` (two vtables), `IsMainMenuOptionsStructural`,
`IsMainMenuStructural`, `IsPazaakStartStructural`, `IsPazaakWagerStructural`,
`IsQuestItemStructural`, `IsScriptSelectStructural` — each re-implement the
exact same shape inline instead of calling it:
```cpp
bool IsLevelUpStructural(void* panel) {
    if (!panel) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(panel);
        return reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiLevelUpPanel;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
```
`ControlHasVtable` takes a generic `void*` — nothing about it is
control-specific; it works identically for a panel pointer. This is a
copy-paste block an existing abstraction should own, one of the two AI-smell
categories the brief asks for. (`IsPowersLevelUpStructural`, lines 337-363,
also opens with this same shape for its primary vtable check before falling
through to a genuinely different structural fallback — worth folding the
first half only.)

Proposed change: replace each of the eight bodies with
`return ControlHasVtable(panel, kVtableX);` (two calls OR'd for
`IsCharGenStructural`). Optionally rename `ControlHasVtable` to something
scope-neutral like `PanelOrControlHasVtable` since it now serves both
call sites — naming call left to the user.

Risk: mechanical (identical null-check / SEH / compare semantics — the
extracted function already handles null exactly as each duplicate does;
compiler-checked, and behavior is provably unchanged since the inlined code
IS the helper's body verbatim).

Estimated line delta: -45 to -55.

### B2 — The AppManager→CClientExoApp chain-walk is reimplemented at ~11 call sites across this batch instead of sharing one primitive (engine_player.cpp:29-34,106-111,138-143; engine_player_party.cpp:36-40,86-91,266-271; engine_player_inputlock.cpp:71-77; engine_panels.cpp:512-515; engine_panels_state.cpp:171-174,208-212,231-233; engine_picker.cpp:98-108)

What's there: the two-pointer read
`appManager = *(void**)kAddrAppManagerPtr; exoApp/clientApp = *(void**)(appManager + kAppManagerClientAppOffset);`
is the common first step of essentially every accessor in this batch —
`GetPlayerServerObject`, `GetCameraPosition`, `GetCameraYawRadians`,
`GetClientLeader`, `IsAnyPartyMemberInCombat`, `GetPlayerCharacterName`,
`GetPlayerControl`, `ResolveGuiInGame`, `CloseInGameMenuToWorld`,
`GetInputClass`, `SetGuiInputClass` all open with it inline, each paying its
own `__try`/`__except` for the same two derefs. `engine_picker.cpp` is the
one file that already wrapped it as a private helper, `GetClientExoApp()`
(anonymous namespace, not shared outside that TU).

Why this is worth flagging as an AI-pattern item rather than pure Section A:
it's the same two-line block copy-pasted at a scale (11 sites, 6 files) that
suggests it should be one named, published primitive — and the codebase
already states the exact rationale for doing this kind of consolidation:
engine_player_internal.h's own comment on `GetPlayerServerObject` says
"Centralising the chain walk means the per-field readers each pay one SEH
frame instead of three; the cost is one extra call per read, negligible at
the rates the per-tick consumers invoke these." That reasoning applies
identically to this shorter, even-more-repeated chain.

Caution, explicitly per the brief's traps: candidates 13 and 24 both looked
like clean function-level extractions and both had to be reverted because
anonymous-namespace *state* was interleaved at variable granularity. This
case is different in kind — every one of the ~11 sites is a pure, stateless,
two-pointer read with no shared mutable state riding along — but the sheer
number of call sites (6 files) makes this a bigger, more visible change than
a typical Phase-3 item, so it is reported here for the user to size rather
than folded into a "just do it" bucket.

Proposed change (for discussion, not auto-approved): publish one
`void* GetClientExoApp()` (picker.cpp's existing name is a reasonable
starting point) from engine_player_internal.h or engine_player.h, and have
the ~10 other call sites use it as their first step instead of re-deriving
`exoApp`/`clientApp` inline. `GetPlayerServerObject` and similar longer
chains would call it instead of re-reading `appManager` themselves.

Risk: low — stateless, pure reads; but multi-file (6 files, ~11 sites), so
larger than a typical mechanical item. Needs a full-batch rebuild and a
spot-check that leader/camera/panel reads still resolve during a normal
session (existing behavior is already exercised by the Phase-1/2 smoke-test
list — leader announce, room/panel narration — nothing new to test beyond
that if taken).

Estimated line delta: -25 to -35 net (one ~10-line helper added, ~11 call
sites shrink by 2-3 lines each).

## Findings (possible bugs — user decides)

### F1 — Two panel-scanning functions in engine_panels_state.cpp skip the SEH guard their siblings use for the identical read (engine_panels_state.cpp:33-55 `HasActiveDialogPanel`, 322-348 `HasActiveSubScreen`)

What's there: both functions read `CSWGuiManager.panels[]` (size + data
pointer) directly off the manager object with no `__try`/`__except`:
```cpp
bool HasActiveDialogPanel() {
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return false;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    ...
```
Three sibling functions in the very same file — `HasActiveMapPanel`,
`HasActiveLevelUpPanel`, `IsInGameOptionsSubScreen` — read the exact same
two fields but wrap them in `__try`/`__except (EXCEPTION_EXECUTE_HANDLER)`
first. `FindPanelByKind` (engine_panels.cpp:782-804) does the same for the
same two fields, and its declaring comment in engine_panels.h:196-200 states
the reason explicitly: "The two manager-field reads are SEH-guarded (the
manager pointer can be mid-teardown on a module transition); a fault yields
nullptr."

Why it matters: if `HasActiveDialogPanel` or `HasActiveSubScreen` run during
that same mid-teardown window, the unguarded reads can raise an access
violation instead of returning `false` the way every neighbouring accessor
does. `IsForegroundUiBlocking()` calls `HasActiveDialogPanel()` first
(inheriting the gap) and then does its own unguarded second read of
`modal_stack` size/data a few lines later (line 375-378) — so the exposure
isn't confined to the two named functions.

Important scope caveat: this gap is not unique to this file. I read
`GetForegroundPanel` in engine_manager.cpp (used throughout the panel-
blocking chain, including by `IsForegroundUiBlocking` in this same file) and
it has the identical unguarded shape. That suggests the SEH guard was added
to specific functions after specific observed crashes (the comment history
around `FindPanelByKind` reads that way) rather than applied as a uniform
rule — so this may be intentional-by-omission rather than a slip, and a
proper fix would need to look wider than this batch's two files.

Reported here rather than as a cleanup candidate because it changes
fault-handling behavior, not just code shape, and per the phase rules a
found bug goes to the user as a finding, not a silent fix.

Risk if fixed: mechanical for the guard addition itself (mirrors the
existing `HasActiveMapPanel` shape exactly, compiler-checked) — but the
scenario it protects against (mid-teardown module transition) is exactly the
kind of timing window that's very hard to force on demand. If the user wants
this covered, the honest verification is: play through several module
transitions (fast-travel or a load-screen-heavy sequence, e.g. leaving Taris
for the Endar Spire flashback or exiting via an airlock) while watching for
any crash log — a negative result (no crash) doesn't prove it, but a
crash log naming `HasActiveDialogPanel`/`HasActiveSubScreen`/
`IsForegroundUiBlocking` on the stack before the fix, silent after, would.

## Candidate 28 — narrow-header include opportunities

- **engine_player.cpp / engine_player_party.cpp / engine_player_inputlock.cpp**
  — include their own module's `engine_player.h` (+ `engine_player_internal.h`).
  Unlike `engine_offsets.h`, `engine_player.h` was never split into narrower
  sub-headers (Phase 1 kept it as a single 246-line header, not an
  aggregator-over-pieces), so there is no narrower header to migrate to today.
  No action possible here without first deciding whether `engine_player.h`
  itself should be split — that's a Phase-2-style call, not a Phase-3 one.
- **engine_panels.cpp** — includes the full `engine_offsets.h` aggregator but
  needs symbols spanning three of its four split pieces (`CExoArrayList` from
  `_types.h`; `kPanelControlsOffset`/`kControlIdOffset`/reply-array offsets
  from `_fields.h`; `kVtableListBox`/`kVtableCSWGuiButton`/
  `kVtableCSWGuiPowersLevelUp` from `_addresses.h`) — not narrowable to one
  header, the aggregator is the right choice as-is.
- **engine_panels_state.cpp** — includes `engine_offsets.h` but (see A10)
  uses none of its symbols directly; this isn't a narrowing case, it's a
  removal case.
- **engine_picker.cpp** — includes the full `engine_offsets.h` aggregator but
  only needs `CExoString` (`_types.h`) and `kInvalidObjectId` (`_values.h`);
  the three AppManager-chain symbols its own include-comment attributes to
  `engine_offsets.h` (`kAddrAppManagerPtr`, `kAppManagerClientAppOffset`,
  `kClientExoAppInternalOffset`) actually live in `engine_player.h`, included
  separately right below it — the comment is simply wrong about which header
  provides them (worth fixing the comment regardless of whether the include
  itself is narrowed).
- **engine_panels.h / engine_picker.h** are each their module's only public
  header (46 and 4 includers respectively) — same situation as
  `engine_player.h` above: no sub-split exists yet, so candidate 28 has
  nothing to migrate includers *to* for these two without a prior Phase-2-
  style split decision.

## Files scanned with nothing to report

- engine_player_internal.h — small (35 lines), single declaration, comment
  accurately describes the current three-way split; nothing to change.
- engine_panels_internal.h — its own comment already documents and explains
  the Phase-1/candidate-6 constant-duplication history and the B5 fold this
  brief asked me to re-verify; verified clean (see the intro section above).
- engine_picker.h — clean; every declared function has exactly one
  definition in engine_picker.cpp, comments match the code precisely
  (addresses, offsets, field layout), no stale references found.
