# Phase 3 scan — engine_offsets family, address rebasing, small engine services

Scope (10 files, ~3116 lines):
- `engine_offsets.h` (28) — four-way aggregator, done in Phase 2 (C8).
- `engine_offsets_types.h` (62), `engine_offsets_values.h` (74),
  `engine_offsets_addresses.h` (727), `engine_offsets_fields.h` (1128).
- `engine_rebase.cpp` (175) / `engine_rebase.h` (56).
- `engine_scriptvar.cpp` (138) / `engine_scriptvar.h` (43).
- `engine_manager.cpp` (145) / `engine_manager.h` (83).
- `engine_options.cpp` (108) / `engine_options.h` (51).
- `engine_subscreen.cpp` (573) / `engine_subscreen.h` (99).

Method: full read of every file in the batch (no truncation). For every
constant I flagged as a possible duplicate or possibly-unused, I grepped the
whole `patches/Accessibility` tree for the bare identifier AND for its literal
hex/decimal value before writing it up — greps are quoted under each finding.
`engine_rebase_table.inc` / `engine_rebase_rdata.inc` were not opened, per the
brief.

## Section A — general low-level cleanup

### A1 — engine_subscreen.cpp re-declares four constants that already exist as public names elsewhere (engine_subscreen.cpp:47-48, 61, 172)

What's there now: four `constexpr` locals in `engine_subscreen.cpp`, each a
second definition of a value already published by another header:

- `kAddrAppManagerPtrLocal = 0x007A39FC` (line 47) — identical to
  `engine_player.h:195`'s `kAddrAppManagerPtr = 0x007A39FC`.
- `kAppManagerServerOff = 0x08` (line 48) — identical to
  `engine_player.h:228`'s `kAppManagerServerOffsetPlayer = 0x8`.
- `kAppManagerClientOffset = 0x4` (line 172) — identical to
  `engine_player.h:196`'s `kAppManagerClientAppOffset = 0x4`.
- `kAddrExoSoundPtr = 0x007a39ec` (line 61) — identical to
  `audio_bus.h:107`'s `kAddrCExoSoundPtr = 0x007A39EC`.

All four are the same struct-layout facts (AppManager pointer, AppManager+0x4
→ CClientExoApp, AppManager+0x8 → CServerExoApp, the ExoSound global), used
for the exact same walks the public names already document.

Why it's a problem: this is the "duplicate definitions of the same offset
under two names" pattern the brief specifically asked me to look for. The
file's own comment at line 45-46 says the AppManager pointer is "duplicate[d]
locally instead of including engine_panels.cpp's internal constants because
the latter are file-local" — true of `engine_panels.cpp`, but it doesn't
mention that `engine_player.h` already exports the identical constant
publicly, or that `audio_bus.h` already exports the ExoSound pointer
publicly. The rationale in the comment doesn't cover the fix that was
actually available.

Evidence (greps run):
- `kAddrAppManagerPtr\b` across the tree — confirms `engine_player.h:195` is
  the sole other declaration and is widely included (30+ call sites).
- `kAppManagerServerOffsetPlayer|kAppManagerServerOff\b|kAppManagerClientOffset\b|kAppManagerClientAppOffset\b`
  — confirms the value/meaning match with `engine_player.h:196` and `:228`.
- `0x007a39ec|kAddrExoSoundPtr|kAddrCExoSoundPtr` (case-insensitive) —
  confirms `audio_bus.h:107` publishes the same address under
  `kAddrCExoSoundPtr`.

Proposed change: `#include "engine_player.h"` and `#include "audio_bus.h"` in
`engine_subscreen.cpp`, delete the four local `constexpr` lines, and rename
their four use sites (lines 109, 113, 185, 267, 141) to the existing public
names. Neither include pulls in anything that could collide with
`engine_subscreen.cpp`'s own PFN typedefs or statics (checked both headers'
own include lists — `engine_player.h` only pulls `engine_offsets.h` +
`engine_rebase.h`; `audio_bus.h` the same).

Risk: mechanical (compiler-checked rename; both constants are byte-identical
so no behaviour changes). Estimated line delta: -4 (two new include lines,
minus four now-redundant `constexpr` declarations plus their comments,
roughly a wash to slightly negative).

### A2 — the same AppManager+0x8 offset exists under three names in the codebase; one of the three lives in this batch (engine_offsets_fields.h:1098-1100)

What's there now:
```cpp
// AppManager indirection to CServerExoApp. AppManager+0x8 → CServerExoApp*.
// (Same constant as engine_player.h's kAppManagerServerOffsetPlayer.)
constexpr size_t kAppManagerServerExoAppOffset          = 0x8;
```
This is a second published name for the exact value `engine_player.h:228`
already publishes as `kAppManagerServerOffsetPlayer`, and the comment says so
itself. `minigame_swoop_race.cpp:309` then adds a THIRD copy — a local
`constexpr size_t kAppManagerServerExoAppOffset = 0x8` that shadows the
`engine_offsets_fields.h` one under the identical name in its own
translation unit, rather than including the header. (`minigame_swoop_race.cpp`
is outside this batch, so I'm not proposing a fix there — flagging for
whichever batch covers it, and for the sibling magic-number-consolidation
pass, since it's the same "field offset re-declared instead of shared"
pattern A1 shows.)

Evidence: `grep -n "kAppManagerServerExoAppOffset"` across the tree — 3 users
of the `engine_offsets_fields.h` copy (all in `engine_reads_items.cpp`, out
of this batch) plus the standalone `minigame_swoop_race.cpp` redeclaration.

Proposed change: this is a judgement call I'm leaving to the user rather than
scoping myself, because acting on it means touching `engine_reads_items.cpp`
and `minigane_swoop_race.cpp`, both outside my batch. Options: (a) drop
`kAppManagerServerExoAppOffset` from `engine_offsets_fields.h` and repoint its
three users at `engine_player.h`'s `kAppManagerServerOffsetPlayer`; (b) leave
it as a documented alias (it already says "same constant as...") and just fix
`minigame_swoop_race.cpp`'s independent redeclaration to use one of the two
public names. Either way, no numeric value changes.

Risk: mechanical once a fix is scoped. Not executed here — needs a batch that
owns `engine_reads_items.cpp` / `minigame_swoop_race.cpp`.

### A3 — engine_rebase.cpp's two binary searches are copy-pasted (engine_rebase.cpp:129-153)

What's there now: `R()` runs an identical binary-search loop twice in a row —
once over `kTable` (lines 129-140), once over `kRdataTable` (lines 142-153).
Same loop shape, same three-way branch, only the array/count identifiers
differ.

Why it's a problem: small, self-contained repetition inside one function.

Proposed change: extract a local helper (e.g.
`const Entry* Find(const Entry* table, size_t count, uintptr_t va)` returning
a pointer or `nullptr`) and call it twice; `R()` keeps its own fallthrough to
the two-entry `kXrefTable` linear scan unchanged. No behaviour change — same
comparisons, same order (table then rdata then xref).

Risk: mechanical (compiler-checked; pure refactor of address-resolution code
that already has extensive comments proving intent — a good candidate to
build-verify with `kdev build` and re-run on both the reference and Allard
builds' address set before trusting, given this function backs every R()
call in the mod). Estimated line delta: -10.

### A4 — ToggleMouseLook re-implements GetMouseLook's read instead of calling it (engine_options.cpp:90-101)

What's there now:
```cpp
bool ToggleMouseLook(bool& outNew) {
    void* options = GetClientOptions();
    if (!options) return false;
    bool current = false;
    __try {
        unsigned int bits = *reinterpret_cast<unsigned int*>(
            reinterpret_cast<unsigned char*>(options) +
            kClientOptionsBitFieldOffset);
        current = (bits & kClientOptionsMouseLookMask) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    ...
```
`GetMouseLook(bool&)` (lines 37-49) already does exactly this read
(`GetClientOptions()` + the same bitfield mask), just returning it via an
out-param instead of a local. `ToggleMouseLook` calls `GetClientOptions()` a
second time and duplicates the SEH-guarded read verbatim instead of calling
`GetMouseLook`.

Proposed change:
```cpp
bool ToggleMouseLook(bool& outNew) {
    bool current = false;
    if (!GetMouseLook(current)) return false;
    void* options = GetClientOptions();
    if (!options) return false;
    bool target = !current;
    if (!WriteMouseLook(options, target)) return false;
    outNew = target;
    return true;
}
```
(Still calls `GetClientOptions()` once more for the write, matching the
existing `SetMouseLook` shape — no new failure mode.)

Risk: mechanical. Estimated line delta: -8.

### A5 — kActionType* enum-byte guess in engine_offsets_values.h is unused, and the codebase's own later RE work found a different mapping (engine_offsets_values.h:51-62)

What's there now:
```cpp
// Inferred action_type byte mapping. The engine's AddX adders on
// CSWSCombatRound are declared in this order; matches typical enum-by-
// declaration patterns. Validate via DumpBytes on each AddX path.
constexpr unsigned char kActionTypeAttack          = 0;
constexpr unsigned char kActionTypeSpellCast       = 1;
constexpr unsigned char kActionTypeItemCast        = 2;
constexpr unsigned char kActionTypeEquip           = 3;
constexpr unsigned char kActionTypeUnequip         = 4;
constexpr unsigned char kActionTypeMove            = 5;
constexpr unsigned char kActionTypeUseTalent       = 6;
constexpr unsigned char kActionTypeHeal            = 7;
constexpr unsigned char kActionTypeCutscene        = 8;
```
The comment itself flags these as an unverified guess ("Inferred... Validate
via DumpBytes"). A grep for every one of the nine names
(`kActionType(Attack|SpellCast|ItemCast|Equip|Unequip|Move|UseTalent|Heal|Cutscene)\b`)
across the whole tree finds them ONLY inside this declaration block — no
caller anywhere.

Separately, `combat_queue.cpp:154-184` documents a mapping for the same byte
that was actually validated later — "Enum confirmed 2026-05-14 by
decompiling `CSWGuiMainInterface::GetActionIcon` @0x686fb0" — and it does not
match this file's guess at all: action_type 1→attack, 6→equip, 7→unequip,
9→spell-cast, 10→item-cast, 11→use-feat/talent (`VerbForActionType`'s
`switch`, written with raw literals 1/6/7/9/10/11, not named constants).
Compare that to this block's guess: attack=0, spellcast=1, itemcast=2,
equip=3, unequip=4, move=5, usetalent=6, heal=7, cutscene=8. Every single
value differs.

Per the brief's explicit caution, I am NOT proposing removal — an unused
offset/enum constant is a question, not a mechanical deletion, and the values
themselves are untouchable RE facts either way. What I am flagging: this
block is simultaneously (a) unused anywhere in the mod, and (b) directly
contradicted by validated, in-use RE work elsewhere in the same codebase, and
its comment doesn't say so. Leaving it as-is risks a future reader trusting
the named constants over the correct, already-shipped `combat_queue.cpp`
mapping.

Question for the user: (a) delete the block (nothing references it, and it's
provably wrong); (b) keep it but rewrite the comment to say it was superseded
by the 2026-05-14 `GetActionIcon` decompile and point at
`combat_queue.cpp:VerbForActionType` for the real values; (c) leave it
untouched. No numeric value would change under any option — (a) and (b) are
about the dead/misleading state of the block, not the RE facts.

Risk: none of the three options touches build behaviour (the block is
unreferenced either way) — this is a documentation-accuracy call for the
user, explicitly not a mechanical candidate.

### A6 — stale in-file cross-reference: "LookupTlk above" isn't above (engine_offsets_addresses.h:653)

What's there now, in the `CSWSItem::GetPropertyDescription` comment:
"...heap ownership across the DLL/EXE boundary risks CRT mismatch; see the
same pattern in LookupTlk above)." `LookupTlk` is not declared or discussed
anywhere in `engine_offsets_addresses.h` — it's a function in
`engine_reads.cpp`/`engine_reads.h` (`bool LookupTlk(...)`, `engine_reads.h:36`).
The "above" is wrong; there is nothing named `LookupTlk` earlier in this
file to point at (verified: `grep -n LookupTlk engine_offsets_addresses.h`
returns only this one line).

Proposed change: reword to "...same pattern as `engine_reads.cpp`'s
`LookupTlk`)." or simply drop the cross-reference — the heap-leak rationale
is already self-contained in the surrounding comment.

Risk: mechanical, comment-only. Estimated line delta: 0.

## Section B — AI-pattern findings

### B1 — engine_subscreen.cpp: ResumeWorldIfPaused re-walks the same chain DispatchOverlayPause already walks (engine_subscreen.cpp:176-201 vs. 260-281)

What's there now: `DispatchOverlayPause` (private helper, lines 176-201) and
`ResumeWorldIfPaused` (public, lines 247-282) both do: read
`*kAddrAppManagerPtrLocal`, null-check with a log line, offset by
`kAppManagerClientOffset` to get the client app, null-check with a log line,
then call `SetPausedByCombat` through the same `PFN_SetPausedByCombat`
pointer — the only real differences are the `g_inOwnPauseCall` flag
(`DispatchOverlayPause` sets it, `ResumeWorldIfPaused` deliberately does not,
so the engine's own "Fortgesetzt" cue fires) and the log trigger text.

Why it's a problem: this is a genuine copy-paste block, not just a similar
shape — the AppManager→client resolution with its two guarded null-checks is
byte-for-byte the same walk in both places. The file's own commenting style
elsewhere (e.g. the `OverlayPauseOwner` doc block) shows the author cares
about exactly this kind of shared-state trap, which makes the un-shared walk
here stand out.

Proposed change: factor out a `void* ResolveClientApp()` (or similar) that
does the AppManager→client walk with its null-check logging, and have both
`DispatchOverlayPause` and `ResumeWorldIfPaused` call it, keeping their
different `g_inOwnPauseCall` handling and log wording on top. (This shrinks
once A1's rename to the public `kAddrAppManagerPtr` / `kAppManagerClientAppOffset`
lands, since both call sites already share those constants.)

Risk: low, not mechanical — this is the exact pause/resume codepath the
overlay stack and the pause-key both rely on. Recommend build-verify plus an
in-game check: open a keyboard-driven overlay (examine view or the combat
queue), confirm the world freezes, close it and confirm the world resumes
and the resume cue speaks; then nest two overlays (combat queue opened from
inside the unified action menu) and confirm closing the inner one keeps the
world paused until the outer closes too (the "combat-queue-Esc-unpauses" bug
this file's own comments describe fixing).

Estimated line delta: -12.

### B2 — engine_manager.cpp: the same "read count+data, clamp to a cap, linear-scan" shape appears seven times (engine_manager.cpp:36-51, 60-64, 88-96, 100-101, 121-124, 133-141)

What's there now: `IsPanelInManager`, `FindOwningPanel`, `GetForegroundPanel`,
and `LogManagerStack` each independently do:
```cpp
int   n    = *reinterpret_cast<int*>(base + kSizeOffset);
void** data = *reinterpret_cast<void***>(base + kDataOffset);
int clamped = n > CAP ? CAP : n;
for (int i = 0; i < clamped; ++i) { ... }
```
against `panels[]` and/or `modal_stack[]`, with the cap varying (32 for most,
16 for `FindOwningPanel`'s outer panel loop, 256 for its inner controls[]
loop). Four functions, seven near-identical inline blocks.

Why it's a problem: this is the copy-paste-block-an-abstraction-should-own
pattern the brief's Section B calls out — not a bug (each site clamps
correctly and independently, so nothing is currently broken), but the same
"walk this CExoArrayList-shaped pair of offsets, capped" logic is
hand-written four times instead of once. It's also the same *category* of
duplication the recent `FindPanelByKind` cleanup (mentioned in
`docs/refactoring/STATE.md`'s Phase 2 status) collapsed elsewhere in the
codebase for a sibling case (eight hand-copied `panels[]` scans) — this file
just wasn't part of that pass.

Proposed change: a small local helper, e.g.
```cpp
struct ArrayView { void** data; int count; };
static ArrayView ReadCappedArray(void* base, size_t dataOff, size_t sizeOff, int cap) {
    auto* b = reinterpret_cast<unsigned char*>(base);
    int n = *reinterpret_cast<int*>(b + sizeOff);
    if (n < 0) n = 0; else if (n > cap) n = cap;
    return { *reinterpret_cast<void***>(b + dataOff), n };
}
```
and rewrite the seven call sites against it, preserving each site's existing
cap value exactly (32/32/16/256/32/32/32 — no cap changes).

Risk: low, not purely mechanical — this is the exact panel/modal-stack walk
that a stale-DLL testing gap already flagged as unverified in the current
session (STATE.md's "THE VERIFICATION GAP" note covers the sibling
`menus_listbox_picker.cpp` split, same risk class: panel-walk code that
hasn't been exercised in-game since the last edit). Recommend build-verify
plus the same in-game action already on Phase 2's smoke-test list: open a
sub-screen with a picker (equipment or workbench), Esc back to the world,
confirm input still works, and open/close a modal message box to exercise
`GetForegroundPanel`'s modal-stack path.

Estimated line delta: -25.

## Findings (possible bugs — user decides)

None found in this batch. Everything read as behaviourally sound; the one
item that looked bug-shaped at first (A5's action_type mismatch) turned out
to be an unused, superseded constant block rather than a live bug — the
actually-used mapping in `combat_queue.cpp` is the one already shipping.

## Candidate 28 — narrow-header include opportunities

- `engine_manager.cpp:6` includes the full `engine_offsets.h` aggregator but
  only uses `CExoArrayList` (from `engine_offsets_types.h`) and
  `kPanelControlsOffset` (from `engine_offsets_fields.h`) — could narrow to
  those two headers.
- `engine_options.cpp:6` includes `engine_player.h` (itself an aggregator per
  the brief) for `kAddrAppManagerPtr` / `kAppManagerClientAppOffset` /
  `kClientExoAppInternalOffset` — a narrow address+field slice, not the
  function surface `engine_player.h` also carries.
- `engine_scriptvar.cpp:12-13` includes `engine_player.h` and `engine_reads.h`
  purely for one function declaration each (`GetPlayerServerCreature`,
  `ReadCExoString`) — no offsets/addresses needed from either.
- `engine_subscreen.cpp:8` includes `engine_panels.h` (aggregator) for two
  function declarations only (`HasActiveSubScreen`, `CallPrevSWInGameGui`).
- `engine_rebase.cpp/.h`, all four `engine_offsets_*.h` files themselves, and
  `engine_manager.h` / `engine_options.h` / `engine_subscreen.h` do not
  over-include — nothing to narrow.

## Files scanned with nothing to report

- `engine_offsets.h` — thin aggregator, already Phase-2-verified; comment
  accurately describes the four-file split.
- `engine_offsets_types.h` — three structs, no addresses, no offsets; clean.
- `engine_scriptvar.h` — clean, accurate doc comments matching the .cpp.
- `engine_scriptvar.cpp` — clean; the SEH/heap-ownership pattern is
  consistent across all four functions (no drift between Get/Set Int/String).
- `engine_manager.h` — the file-local `kAddrGuiManagerPtr` / `kMgr*Offset`
  constants are a deliberate, already-documented exception (STATE.md: "Same
  treatment as kAddrGuiManagerPtr... kAddrAppManagerPtr... kAddrCExoSoundPtr"),
  not a duplication bug — not re-flagged here.
- `engine_options.h` — clean; both bitfield groups (mouse-look at +0x8,
  autopause at +0x14) are correctly distinguished and documented as separate.
- `engine_subscreen.h` — clean interface; `OverlayPauseOwner` bitmask design
  is well-documented and matches its .cpp usage exactly.

I did not check the internal taxonomy split (types/addresses/fields/values)
for misplacement beyond spot-checking every constant's category against the
file-header taxonomy while reading — found no misplaced constants and no
duplicate declarations *within* the four-file family itself (checked via
`grep` for repeated `constexpr ... kName` declaration lines across all four
files: zero hits). The duplicates found (A1, A2) are all against files
*outside* the family (`engine_player.h`, `audio_bus.h`, `minigame_swoop_race.cpp`).
