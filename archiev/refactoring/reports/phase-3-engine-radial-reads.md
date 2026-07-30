# Phase 3 scan — engine radial, reads, input and action bar

Scope (12 files):
- engine_radial.cpp (937) / engine_radial.h (149)
- engine_reads.cpp (361) / engine_reads.h (256) / engine_reads_items.cpp (694)
- engine_keymap.cpp (460) / engine_keymap.h (102)
- engine_actionbar.cpp (312) / engine_actionbar.h (95)
- engine_levelup.cpp (306) / engine_levelup.h (49)
- engine_input.cpp (235) / engine_input.h (132)

Method: full `Read` of every file listed above (no truncation — all are under
1000 lines). Cross-file verification via targeted `Grep`:
- `kLinkedListHeadOffset|multi-hop|single-hop` and
  `kListInternal|kLinkedList|LinkedListNode|ActionNodesOffset` across the
  batch and the whole `patches/Accessibility/` tree, to check the known
  single-hop linked-list bug pattern — **no hits in this batch's files**
  (the radial/actionbar "action lists" are `CExoArrayList`, a different,
  size+data-pointer struct with no node-walk at all; the only
  `kLinkedListHeadOffset` user in the whole tree is `combat_special_watch.cpp`,
  outside this batch).
- `ReadControlNameFields`, `ReadCExoString\(|ReadU32\(|ExtractTextOrStrRef\(|ReadToggleState\(|DumpControlVtable\(`,
  `GetClientExoApp\(\)|GetClientExoAppInternal\(|GetGuiInGame\(|GetMainInterface\(`,
  `ResolveItemFromServerHandle|GetRulesGlobal`, `kResRefMaxLen|kResRefSize`,
  `AsSWCCreatureOffset|AsSWCDoorOffset|...` — used to trace every call site of
  functions flagged below, confirm duplication is byte-for-byte, and confirm
  two file-local helpers have no header declaration and no external caller.
- `uint32_t|uint8_t|int32_t|uintptr_t|int16_t` and
  `\bstr[a-z]+\(|\bmem[a-z]+\(` / `snprintf|printf|fopen|fgets|fclose` per
  file, to verify unused-include claims are real (not just "not obviously
  used") before reporting them.
- Read `engine_offsets_fields.h:640-720` in full for the documented
  single-hop→multi-hop `CExoLinkedList` fix, and `engine_offsets_fields.h`
  around every offset constant cited below, to confirm whether a matching
  named constant already existed before flagging a local redefinition.
- Skimmed `docs/refactoring/STATE.md` "Execution findings" (Phase 1
  candidates 13/22/24) and "Phase 2 status" per the brief, to avoid
  re-proposing the reverted `engine_radial.cpp` diagnostics split or the
  `engine_offsets.h` regroup.

## Section A — general low-level cleanup

### A1 — Unused `<cstring>` include (engine_radial.cpp:6)
No `str*`/`mem*` function is called anywhere in the file (verified: grep for
`\bstr[a-z]+\(|\bmem[a-z]+\(` returns nothing). All string copies in this
file are hand-rolled character loops.
Proposed change: remove the include.
Risk: mechanical. Delta: -1 line.

### A2 — Unused `<cstdio>` include (engine_reads_items.cpp:21)
No `printf`/`snprintf`/`fopen`/`fgets`/`fclose` call in the file — all
logging goes through `acclog::Write`.
Proposed change: remove the include.
Risk: mechanical. Delta: -1 line.

### A3 — Unused `<cstdio>` and `<cstring>` includes (engine_actionbar.cpp:4-5)
Same check as A1/A2: no `printf`-family call, and no `str*`/`mem*` call —
`ReadCExoStringLocal` in this file copies characters in a hand-rolled loop,
same as engine_radial.cpp.
Proposed change: remove both includes.
Risk: mechanical. Delta: -2 lines.

### A4 — Unused `<cstdint>` include (engine_keymap.cpp:4)
Grepped for `uint32_t|uint8_t|int32_t|uintptr_t|int16_t` — zero matches.
Every integer in this file is a plain `int`.
Proposed change: remove the include.
Risk: mechanical. Delta: -1 line.

### A5 — `GetRowActionButton` recomputes what `RowActionButtonAddr` already does (engine_radial.cpp:721-730 vs 271-275)
`GetRowActionButton` (the public API) manually re-derives
`tam + kTamTargetActionsOffset + row*kTargetActionStride + kRowActionButtonOffset`
instead of calling the file-local `RowActionButtonAddr(tam, row)` defined
earlier in the same anonymous namespace, which computes the identical
address (via `RowActionAddr` + the same offset). Both are correct and
produce the same result; one is dead weight.
Proposed change: replace `GetRowActionButton`'s body with
`return RowActionButtonAddr(tam, row);` (needs the bounds re-check kept, or
just delegate — `RowActionButtonAddr`/`RowActionAddr` already do the same
`row` bounds check `GetRowActionButton` does).
Risk: mechanical (same computation, compiler-checkable via the existing
callers/tests). Delta: -8 lines.

### A6 — `engine_radial.cpp` redefines `engine_offsets_fields.h`'s `kResRefSize` under a different name (engine_radial.cpp:63)
`constexpr size_t kResRefMaxLen = 16;` duplicates
`engine_offsets_fields.h:264`'s `constexpr size_t kResRefSize = 16;`, which
is already in scope (the file includes `engine_offsets.h`). Not a raw hex
literal, but the same "already-named constant re-invented locally" pattern
the brief calls out — and it isn't unique to this file: `engine_picker.cpp`
(outside this batch) does the identical thing with the identical name and
value.
Proposed change: use `kResRefSize` from `engine_offsets_fields.h` in place
of the local `kResRefMaxLen`, in this file at minimum (flagging
`engine_picker.cpp`'s copy for whichever batch covers it).
Risk: low (two use sites, both just `if (lim > kResRefMaxLen) lim = kResRefMaxLen;`
in `ReadResRefLocal`). Delta: -1 line here.

### A7 — Two file-local helpers have external linkage but no declaration or external caller (engine_reads_items.cpp:123, 154)
`ResolveItemFromServerHandle` (line 123) and `GetRulesGlobal` (line 154) are
defined directly inside `namespace acc::engine { ... }`, NOT inside the
file's anonymous namespace, and are not declared in `engine_reads.h`. Grepped
the whole tree for both names — every use is inside this same TU
(`engine_reads_items.cpp`). Per the codebase convention stated in the brief
("anonymous namespaces hold file-local state"), these two should be
`static`/anonymous-namespace like every other file-local helper in this
batch (`RowActionAddr`, `GetClientExoApp`, `DescriptorAddr`, etc. are all
correctly anonymous elsewhere in the same batch).
Proposed change: move both into the file's existing anonymous namespace
block (the one already used for `ReadItemStackSize`, `IsItemEntryRow`, etc.
starting at line 256) or wrap them in their own.
Risk: mechanical (internal-linkage change only, no declared external
surface exists to break). Delta: 0 (namespace move only).

### A8 — `engine_reads.h`'s file header overstates its own guarantee (engine_reads.h:1-3)
The header comment reads: "SEH-guarded read helpers for KOTOR GUI controls.
... every deref is __try-wrapped, every potentially-stale pointer is
validated before use." That is true for most of the file
(`ReadGuiString`, `ReadControlTooltip`, `ReadLabelText`, `IsSlider`, etc.),
but five functions in `engine_reads.cpp` have neither a null check nor an
`__try`:
- `ReadControlNameFields` (engine_reads.cpp:13-20) — dereferences
  `control + 0x28/0x2c/kControlIdOffset` unconditionally.
- `ReadCExoString` (engine_reads.cpp:36-45) — dereferences `base + offset`
  unconditionally.
- `ReadU32` (engine_reads.cpp:47-50) — same.
- `ReadToggleState` (engine_reads.cpp:346-348) — calls the unguarded
  `ReadU32` with no check of its own.
- `DumpControlVtable` (engine_reads.cpp:350-359) — dereferences `control`
  unconditionally with no null check at all.
This isn't hypothetical inconsistency: at least two other files in this same
batch had to write their own SEH-guarded, null-checked local copy of the
gui-string/CExoString read path instead of trusting the public one (see B3
below) — which is exactly the situation you'd expect if the public
surface's safety guarantee doesn't actually hold everywhere its doc comment
claims.
Proposed change: either narrow the header comment to name the functions it
actually covers, or add the missing guards to the five functions above (the
second is the real fix; see the possible-bug entry below for the one call
site where this is not just theoretical). Full-codebase caller audit is out
of this batch's scope — a fuller sweep of all ~30 call sites of
`ReadCExoString`/`ReadU32`/`DumpControlVtable` would need its own pass.
Risk: comment-only change is mechanical; adding guards is low (each function
is small, the pattern is copy-pasted from a dozen other functions in the
same file).

## Section B — AI-pattern findings

### B1 — Triplicated `AppManager → ServerExoApp` resolve walk (engine_reads_items.cpp:34-64, 66-97, 123-152)
`ClientToServerObjectId`, `ResolveItemFromClientHandle`, and
`ResolveItemFromServerHandle` each independently re-implement the identical
two-step, SEH-guarded pointer walk:
```cpp
void* appMgr = nullptr;
__try { appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr); }
__except (EXCEPTION_EXECUTE_HANDLER) { return <fail>; }
if (!appMgr) return <fail>;

void* serverApp = nullptr;
__try {
    serverApp = *reinterpret_cast<void**>(
        reinterpret_cast<unsigned char*>(appMgr) + kAppManagerServerExoAppOffset);
} __except (EXCEPTION_EXECUTE_HANDLER) { return <fail>; }
if (!serverApp) return <fail>;
```
byte-for-byte, three times in one file (~17 lines each = ~51 duplicated
lines). No shared helper exists anywhere in the tree for this walk (grepped
`kAppManagerServerExoAppOffset` codebase-wide — the only other user,
`minigame_swoop_race.cpp`, also reimplements it inline rather than sharing).
Proposed change: extract a small file-local `void* GetServerExoApp()`
(mirroring the existing `GetRulesGlobal()` pattern two functions below it)
that does this walk once; have all three functions call it.
Risk: mechanical/low — pure behavior-preserving extraction of an already
SEH-guarded block, same return-on-null semantics throughout.
Estimated delta: -35 lines.

### B2 — The four-function client/GUI resolve chain is duplicated verbatim across files (engine_radial.cpp:154-197, engine_actionbar.cpp:72-115)
`GetClientExoApp`, `GetClientExoAppInternal`, `GetGuiInGame`, and
`GetMainInterface` — each a small SEH-guarded pointer hop
(`AppManager → client_app → gui_in_game → main_interface`) — are defined
identically (same bodies, same names) in both files' anonymous namespaces,
~44 lines each. Per their own comments both are explicitly copied from a
third: `engine_picker.cpp` (outside this batch) has the same four functions
at lines 98-142 (confirmed via grep — not fully read as part of this
batch). The matching offset constants
(`kInternalGuiInGameOffset = 0x040`, `kGuiInGameMainInterfaceOffset = 0x90`)
are also redefined identically in both files.
This is a different, much narrower case than the Phase-1 candidate 24 file
split that was reverted: these four functions are pure, stateless pointer
walks with no anonymous-namespace *constants* or *state* shared with
anything else in either file (unlike `CallVtableAsClass`, which candidate 24
found was entangled with production code) — so the trap that sank
candidate 24 does not apply here.
Proposed change: extract the quartet into a small shared header (or add to
`engine_player.h`, which already owns `kAddrAppManagerPtr` and
`GetClientLeader`) — but note this spans at least 3 files and one of them
(`engine_picker.cpp`) is outside this batch, so verify the same exact-match
before executing.
Risk: low — mechanical extraction of dependency-free, side-effect-free
helper functions; flagging as a question for the user rather than a
mechanical fix because it's a cross-file/cross-batch change.
Estimated delta: -80 to -120 lines net across the three files once done.

### B3 — Two files reimplement `acc::engine::ReadGuiString` / a safer `ReadCExoString` instead of calling the existing engine_reads.h exports (engine_radial.cpp:214-239, 243-263; engine_actionbar.cpp:156-177)
`engine_radial.cpp`'s `ReadGuiStringLocal` (lines 214-239) is logically
identical to the already-exported `acc::engine::ReadGuiString`
(engine_reads.cpp:199-224) — same offsets, same vtable check
(`kVtableCAurGUIStringInternal`), same SEH guard, same truncate-and-NUL
semantics.
Separately, `engine_radial.cpp`'s `ReadCExoStringLocal` (lines 243-263) and
`engine_actionbar.cpp`'s `ReadCExoStringLocal` (lines 156-177) are
byte-for-byte identical to **each other** — but NOT identical to the public
`acc::engine::ReadCExoString` (engine_reads.cpp:36-45): the two local copies
are null-checked and SEH-guarded; the public one is neither (see A8). So two
files independently wrote the *same*, *safer* replacement for a function
that already has a name and a header declaration.
The existing in-file comments already flag this as deliberate ("pulled in
here so the engine layer doesn't depend on menus.cpp's helpers" —
engine_radial.cpp:210-211; "Re-stated here so the wide-diagnostic peek ...
doesn't depend on the picker module" — engine_radial.cpp:56-58). Both
justifications describe avoiding a dependency on `menus.cpp` or on the
picker module, but the actual function being duplicated
(`acc::engine::ReadGuiString`/`ReadCExoString`) lives in `engine_reads.h` —
a peer engine-layer header, not a menu-layer one — so the stated reason
doesn't match what's actually being avoided. That may still be an
intentional isolation choice (each engine-* module deliberately depends on
nothing but `engine_offsets.h`/`engine_player.h`/`log.h`); flagging as a
question rather than asserting it's a mistake.
Proposed change (if the isolation is NOT intentional): have
`engine_radial.cpp` and `engine_actionbar.cpp` include `engine_reads.h` and
call `acc::engine::ReadGuiString`/fix `ReadCExoString` to match the safer
local semantics, removing ~65 duplicated lines across the two files (plus
whatever `engine_picker.cpp` carries, unverified/out of batch).
Risk: low, but explicitly a design-intent question, not a mechanical
cleanup — do not execute without the user weighing in on the isolation
tradeoff.

## Findings (possible bugs — user decides)

### 1 — `ReadControlNameFields` is unguarded and is called unguarded from a hook that fires during engine teardown
`ReadControlNameFields` (engine_reads.cpp:13-20) has no null check on
`control` and no `__try`/`__except` — it directly dereferences
`control + 0x28`, `control + 0x2c`, and `control + kControlIdOffset`. This
directly contradicts `engine_reads.h`'s own stated contract that "every
deref is __try-wrapped" (see A8) and is the one call site in this batch
where that gap is concretely exercised, not just theoretical:

`menus_dispatch.cpp:81-90`'s `OnHandleFocusChange` — a hook mid-function on
`CSWGuiControl::HandleFocusChange` — calls
`ReadControlNameFields(thisPtr, tip, tipLen, id);` with **no** `__try`
around the call either. `thisPtr` is the hooked function's own `this`, so it
is very unlikely to be literally null, but the header's own rationale for
existing ("safe from hook handlers that may run during engine mid-teardown
... the freed control's vtable slot yields garbage; without SEH the deref
would crash") describes exactly this shape of risk: a stale, freed-but-non-null
control surviving into a hook fire. The project already hit and fixed this
exact failure mode once — `IsSlider`'s SEH guard (engine_reads.cpp:303-311)
cites a real crash dump (`swkotor.exe.14028.dmp`, 2026-05-11) from a freed
control read during a `SubScreen.Status` transition.

Contrast: the *other* call site of the same function,
`menus_extract.cpp:606-618`, wraps the call in its own `__try`/`__except`
specifically because (per its comment) `ReadControlNameFields` "doesn't go
through CallDowncast" and therefore isn't otherwise protected —
i.e. that call site's author already knew this function needs external
SEH. `OnHandleFocusChange` doesn't have that wrapper.

Risk: needs-in-game-test. Hard to force reliably (needs a focus-change
event to land on a stale-but-live control during a teardown window), so a
deterministic repro isn't available; the safe fix is cheap regardless — add
the same `__try`/`__except` `menus_extract.cpp` already uses at its call
site, either inside `ReadControlNameFields` itself (fixing it for both
callers and matching the header's contract) or by wrapping the
`OnHandleFocusChange` call site the way `menus_extract.cpp` does.
Exercising it in-game: rapid area transitions / menu open-close while
mousing/tabbing over controls during load, matching the conditions in the
cited crash dump, is the closest available repro; not a guaranteed trigger.

## Candidate 28 — narrow-header include opportunities

- `engine_radial.cpp` — needs `CExoString` (types), `kVtableCAurGUIStringInternal`
  (addresses), and five field offsets (`kButtonGuiStringPtrOffset`,
  `kButtonTextOffset`, `kLabelGuiStringPtrOffset`, `kLabelTextOffset`,
  `kAurGuiStringCStrOffset`) — 3 of the 4 split headers. No narrowing value.
- `engine_reads.h` / `engine_reads.cpp` / `engine_reads_items.cpp` — use a
  broad mix of `_types.h` (`CExoString`, several `PFN_*` typedefs),
  `_addresses.h` (~15 `kAddr*` constants), and `_fields.h` (~20 field
  offsets). No narrowing value.
- `engine_actionbar.cpp` — includes `engine_offsets.h` (line 7) but only
  actually uses `CExoString` from it; every offset/address constant it
  touches is either locally defined in its own anonymous namespace or comes
  from `engine_player.h` (already a separate include). Real narrowing
  opportunity: swap to `engine_offsets_types.h`.
- `engine_levelup.cpp` — includes `engine_offsets.h` (line 8) for exactly
  one symbol, `kCreatureStatsPointerOffset` (a single `engine_offsets_fields.h`
  constant). Real narrowing opportunity: swap to `engine_offsets_fields.h`.
- `engine_keymap.h`/`.cpp`, `engine_input.h`/`.cpp` — include neither
  `engine_offsets.h` nor any of its siblings at all. Nothing to narrow.

## Files scanned with nothing to report

- engine_radial.h — documentation-only header, matches implementation.
- engine_reads.h — see A8 for the one doc-accuracy note (kept there since
  it's about the implementation, not the header's own structure).
- engine_keymap.h — clean; matches implementation exactly.
- engine_levelup.h / engine_levelup.cpp — clean; no dead code, no
  redundant guards, no duplication, function sizes reasonable, comments
  explain "why" throughout (the biggest function, `TriggerLevelUp`, is a
  clear two-attempt-then-fallback sequence, not a decomposition candidate).
- engine_input.h — clean.
- engine_input.cpp — minor-only: `EnsureInputAcquired`, `ForceReacquireInput`,
  and `ReleaseInput` each repeat the same 6-line "read `ExoInputGlobal`,
  null-check, resolve `SetActive` fn pointer" preamble before diverging on
  which `SetActive` calls to make. Small enough (3× ~6 lines) that it's
  noted here rather than as a numbered finding — a `ResolveExoInput()`
  helper would save perhaps 12 lines total for low but nonzero risk (three
  behaviorally-distinct functions would need to agree on one shared
  early-return shape). Leaving as informational rather than a candidate.
- engine_keymap.cpp — see A4; otherwise clean, no duplication, no
  over-defensive checks, `s_gameLoaded`-gated auto-load pattern is used
  consistently across all five public entry points that need it.

No instance of the single-hop `CExoLinkedList` walk bug pattern was found in
this batch — none of these files walk a `CExoLinkedList` at all; the
"action lists" in `engine_radial.cpp`/`engine_actionbar.cpp` are
`CExoArrayList` (contiguous data pointer + size + capacity), a structurally
different type with no node-chain to walk correctly or incorrectly.
