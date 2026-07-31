# KOTOR 2 port — plan and findings

**Status: ACTIVE (started 2026-07-31).** Supersedes the conclusion of
`kotor2-port-feasibility.md`, whose *measurements* remain valid — the sigscan
result especially — but whose cost estimate predates the RTTI finding below.

## WHERE TO RESUME (read this first)

State as of 2026-07-31, end of the SECOND port session. KOTOR 1 untouched
throughout — every change is a constant gaining a KOTOR 2 column, which on
KOTOR 1 evaluates to exactly what it did before.

**What works:** KOTOR 2 speaks its main menu and options submenu. One hook is
installed (`CSWGuiPanel::SetActiveControl` → `menus_focus_k2.cpp`). Every other
hook handler is gated off by `acc::game::HandlerEnabled()`.

**Menu-subsystem worklist:** regenerate with

    python tools/re-scripts/port_worklist.py patches/Accessibility \
        menus.cpp menus_focus.cpp menus_chain.cpp menus_extract.cpp \
        menus_dispatch.cpp menus_internal.h engine_manager.h \
        engine_reads.cpp engine_panels.cpp

115 constants used, 90 resolved, **25 to verify** (was 79).

**Done this session** — all of it offline, no test round spent:
- Every panel-identity vtable in the codebase (see "Panel identity" below).
  `CSWGuiQuestItem` and `CSWGuiScriptSelect` established as K1-only.
- The shared control classes whole: `CSWGuiControl` tooltip + parent,
  `CSWGuiListBox` navigation state, `CSWGuiSlider`, `CSWGuiButtonToggle`.
- `CSWGuiListBox::SetSelectedControl` and `CTlkTable::GetSimpleString`.
- Panel layouts by .gui tag out of each panel's own constructor: portrait,
  map, level-up, store, journal, class selection, keymap row, editbox, wager,
  equip, item-entry row.
- `MoveMouseToPosition` — previously the doc's open item — plus
  `CAurGUIStringInternal`'s buffer pointer, previously the one gap in the text
  chain.

**The next task.** Finish the last 25. They are listed individually, with the
route for each, under "The last 25 in the menu subsystem" below — every one now
has a named starting point rather than being an open search. The pattern is the
same in each case: identify the KOTOR 2 counterpart of a specific KOTOR 1
function, then read the offset off the matching statement.

The user's standing decision (2026-07-31) is to finish the subsystem offline
rather than take an interim test round: on KOTOR 2 the surrounding machinery is
what makes a sub-panel reachable at all, so a half-ported subsystem is both hard
to navigate to and likely to fault on arrival. Spend a test round only when its
result would settle or change how the remaining investigation is done.

`MoveMouseToPosition` is now resolved and should replace the `SetCursorPos`
stand-in in `menus_focus_k2.cpp` when the menu path is enabled.

**Do not** repeat the minimal-slice approach — see THE METHOD.

**Tools built for this port** (all in `tools/re-scripts/`, all offline):
- `rtti_scan.py` — class → vtable map from KOTOR 2's RTTI
- `vtable_map.py` — KOTOR 1 → KOTOR 2 addresses by vtable slot
- `vtable_xrefs.py` — the functions that STORE a class's vtable, i.e. its
  constructor and destructor. This is the one that reaches panel layouts.
- `find_thiscall_targets.py` — methods called on a known singleton
- `port_worklist.py` — what a subsystem needs, and what is unresolved

`find_thiscall_targets.py` and `vtable_xrefs.py` both run against KOTOR 1 too,
given `build/re/imagedump/swkotor-image.bin` from `kdev dump-text` (the Steam
exe is SteamStub-encrypted, so its bytes are not readable from the file). Doing
the same scan on both games and diffing the results is what located
`MoveMouseToPosition`.

**A KOTOR 1 offset is often cheaper to look up than to decompile.** Lane's
`docs/llm-docs/re/swkotor.exe.h` carries the struct definitions with member
order and embedded types, and `k1_win_gog_swkotor.exe.xml` carries every
function and vtable SYMBOL by name. Between them most KOTOR 1 columns can be
had without a Ghidra run at all, which halves the rounds — decompile the
KOTOR 2 side only.

Ghidra: project `kotor2`, program `swkotor2.exe`, at `C:\Tools\ghidra-projects`.
Decompile with
`KDEV_GHIDRA_PROJ=kotor2 KDEV_GHIDRA_PROGRAM=swkotor2.exe tools/ghidra-scripts/decomp.sh 0xADDR`.
Function catalogue (11,652 entries) at `docs/llm-docs/re/k2/k2-functions.csv`.
A space-free copy of the exe lives at `C:\Tools\k2re\swkotor2.exe` — the Ghidra
batch launcher cannot handle the spaces in the Steam path.

**Testing:** `kdev apply --game k2`, then read
`<K2 install>\logs\patch-*.log`. The K2 focus path logs under `K2.Focus`.

## Target

Steam / GOG KOTOR 2, Aspyr's 2015 rebuild. PE link timestamp
2015-09-23 19:41:17Z (`0x5603005D`). The installed Steam copy's SHA-256 matches
the `kotor2_steam_aspyr` entry in KPatchManager's `AddressDatabases/`, so the
framework and this patch agree on which binary this is.

The exe is **not** SteamStub-encrypted (no `.bind` section), so unlike KOTOR 1
it can be read straight off disk — no `kdev dump-text` step to get byte
reference material.

## Architecture decision — one binary, runtime dispatch

**Decided 2026-07-31.** A single DLL serves both games, selecting addresses and
struct offsets at runtime from the detected game.

The alternative considered and rejected was a second build target (per-game
compile-time constants, two `.kpatch` artifacts). It was rejected on
maintenance grounds, and the argument is worth keeping: a few hundred files
here encode behaviour that is *identical* between the games. With two binaries
every KOTOR 1 fix must be consciously re-applied to KOTOR 2, forever, and the
failure mode is silent — a fix that simply never arrives in the other game.
With one binary, divergence is explicit and local (`if (acc::game::IsKotor2())`
at the handful of places that genuinely differ) and everything else is shared
by construction.

The cost objection to the single binary turned out to be much smaller than it
first looked. Converting `constexpr size_t kFoo = 0x90;` to a runtime variable
leaves **every call site unchanged** — same name, same expression. Only the
declaration and a per-game table move. A codebase-wide check of the 243
constants in `engine_offsets_*.h` found **zero** used where C++ requires a
compile-time constant:

- array bounds: 0
- `static_assert`: 0
- `case` labels: 0
- template arguments: 0
- feeding another `constexpr`: 0

The conversion is mechanical.

## What the port actually costs

**Not the vtables.** The K2 exe ships full RTTI — 389 type descriptors with
class names identical to K1's. `tools/re-scripts/rtti_scan.py` walks
type descriptor → complete object locator → vtable and recovers **392 named
vtables**; output is checked in at `docs/llm-docs/re/k2/k2-vtables.csv`. All 19
of our vtable-identity constants resolve by name automatically. Beyond the
constants themselves, this gives named anchors for decompiling everything else,
which is the part the feasibility doc could not have priced.

Regenerate with:

    python tools/re-scripts/rtti_scan.py <path-to-swkotor2.exe> > out.csv

**Struct offsets shift by a constant delta per class, not randomly.** Diffing
the 21 offsets that both seeded databases share:

- Base/shallow classes are **identical**: `CAppManager`, `CExoString`,
  `CGameObject`, `CSWBaseItem`.
- Derived classes shift **uniformly**: every `CSWSObject` field by 4, every
  `CSWSCreatureStats` field by 4, both `CSWSCreature` fields by the same larger
  delta.

That is field insertion near the top of a base class, not a redesign. Verifying
two fields per class carries the rest — but confirm, don't assume: the delta is
piecewise, identity below the insertion point and constant above it.

**The offsets surface is about double what the headers suggest.** Codebase-wide
there are ~494 offset-shaped constants; only ~half live in `engine_offsets_*.h`.
The other ~258 are scattered across ~40 files — `engine_area.h` alone holds 60,
then `engine_radial`, the minigames, `engine_picker`, `engine_actionbar`. The
Phase-2 consolidation captured part of this, not all of it. Consolidating the
strays is part of the offsets step, and would have been needed under either
architecture.

**`kdev sigscan` contributes nothing here** and that has not changed. It finds
the same compiled bytes relocated; K2 is a recompile, so functions were
re-emitted rather than moved. 0 of 213. Its value stays confined to KOTOR 1
build variants.

## Delivery mechanism — already supported

Nothing new is needed to ship one mod into two games:

- `hooks.toml` already carries `[metadata] target_versions` keyed by executable
  SHA-256, and we already ship a second hooks file for K1's 2004 relink
  (`relink2004.hooks.toml`). K2 gets `kotor2.hooks.toml` the same way.
- `manifest.toml` `[patch.supported_versions]` takes the two K2 hashes.
- The installer already has the K2 half: game-version selection, K2 path
  detection, TSLRCM / K2CP / Tweak Pack flows, and it already applies two K2
  `.kpatch` files (4GB-aware, borderless) against known Steam and GOG hashes.
- `swkotor2.exe` imports `dinput8.dll`, so the proxy loader works unchanged.

## The lesson from the first working slice: addresses port, WORKAROUNDS do not

The first KOTOR 2 feature (menu focus announce) works. Getting there cost four
attempts, and none of them was an address being wrong — every offset, vtable
and function address derived for this port read correctly on first contact with
the running game. The whole cost was in engine BEHAVIOUR.

What went wrong: keyboard navigation only ever reached two menu entries, because
the mouse cursor was parked on a button and the engine kept handing it focus via
HandleFocusChange, overriding the arrow keys. KOTOR 1 has the identical
conflict. Our KOTOR 1 mod has solved it since forever, by warping the engine's
cursor onto the focused control with MoveMouseToPosition.

Why that was not obvious: in the KOTOR 1 codebase, that warp reads as
housekeeping. It is documented as "cursor move + hit-test + hover +
active-control update", with a note that "active control lags behind the cursor
unless we explicitly set it". Both true. Neither says *omit this and keyboard
navigation is unusable*. So when deciding what a minimal KOTOR 2 slice needed,
it did not look load-bearing.

**Every KOTOR 1 workaround is a fossilised bug fix, and fossils do not announce
what they are for.** The cursor warp, the click-simulation activation path, the
overlay-Esc latch, the DirectInput reacquisition fixes — each encodes a
behaviour fought once already. On KOTOR 2 the bug gets rediscovered BEFORE it is
recognised that the fix is already owned. Budget the port accordingly: the
addresses are the cheap half.

## THE METHOD: port whole subsystems, verify every constant first

**Decided 2026-07-31, after the minimal-slice approach cost four test rounds to
rediscover a countermeasure KOTOR 1 already had.** This supersedes the
"start minimal and grow" instinct. Do not repeat it.

**Do NOT build reduced KOTOR 2 paths that omit KOTOR 1 logic.** Omitting a
workaround does not defer its cost, it multiplies it: the bug gets rediscovered
from symptoms, debugged blind, and fixed again — and every cycle costs a test
round with the user at the keyboard.

The procedure for each subsystem:

1. Take the KOTOR 1 implementation **whole**, workarounds included.
2. Enumerate every address and offset its full call graph touches.
3. Verify each against the KOTOR 2 binary offline — vtable-slot correspondence,
   RTTI, decompiling both sides and comparing structure.
4. Port the entire subsystem, gate cleared, and test **once**.

The reasoning is about which resource is scarce. Analysis is cheap, offline, and
repeatable; test rounds need the user and are the real bottleneck. A minimal
slice optimises the developer's debugging convenience at the user's expense.
KOTOR 1's logic is known-good — the only genuine unknowns are the KOTOR 2
constants, and those are exactly what step 3 settles before anything runs.

Corollary: when a KOTOR 1 module contains something whose purpose is unclear,
**port it anyway**. The cursor warp looked like housekeeping and was
load-bearing. Assume every line earned its place until proven otherwise, rather
than the reverse.

One diagnostic note still worth keeping:

- **Identical failures mean the cause is untouched.** Three fixes failed in
  exactly the same way while hypotheses were refined (write the coordinates →
  also re-run the hover hit-test → convert the coordinate space). One log line
  showing the manager's cursor settled it immediately: the field read the same
  value regardless of what was written, because the engine re-derives it from
  the real mouse every frame. Different failures mean progress; identical ones
  mean measure instead of theorise.

## Hook status

The first hook (`CSWGuiPanel::SetActiveControl` → menu focus announce) is
installed and working. The rest are still gated off by
`acc::game::HandlerEnabled()`, for two reasons found by trying. Neither is a
reason to slow down — but each has to be cleared per hook, so "enable them all"
is not a single step.

**1. Handlers are not self-contained.** `OnSetActiveControl` calls
`IsLoadingSaveGame`, which dispatches through
`CServerExoApp::GetLoadFromSaveGame` — an address that resolves to **0** on
KOTOR 2. Installing that hook calls a null pointer on the first focus change.
With ~230 addresses and ~470 offsets still unresolved, most handlers have a
chain like that somewhere in them. This is what `acc::game::HandlerEnabled()`
now guards: every hook handler default-denies on KOTOR 2, and a handler is
cleared by replacing that call with an explicit branch once its whole
read/call chain is ported. `grep -c "HandlerEnabled()"` counts what is left.

**2. KOTOR 2's GUI code is compiled UNOPTIMISED, so hook designs do not
transfer.** Its `SetActiveControl` opens with a textbook frame — `PUSH EBP /
MOV EBP,ESP / SUB ESP,0x8 / MOV [EBP-8],ECX` — and every subsequent access
reloads `this` from `[EBP-8]`. KOTOR 1's equivalent is optimised and keeps
`this` in a register, which is why our hook takes it from EDI/ESI mid-function.

So a KOTOR 2 hook cannot reuse the KOTOR 1 cut point, cut length or register
sources. Each needs its own listing read (`PrintListing.java`) to pick a safe
cut of relocatable instructions and decide where its arguments actually live —
for `SetActiveControl` that is `panel = [EBP-8]`, `newControl = [EBP+8]` after
the prologue at `0x0040EC09`.

Note this also makes KOTOR 2 *easier* to read and *harder* to hook: unoptimised
code gives cleaner decompiles (which is why the offsets came out so fast) but
frame-relative arguments instead of register ones, and `esp+X` parameter
sources are the one KPatchManager feature with a known bug.

## Steps

1. **Game-identity seam.** *(done 2026-07-31)* `engine_game.{h,cpp}` owns
   "which game, which build", detected from the game image's PE link timestamp
   — safe from static init and DllMain, the same constraint that shaped
   `engine_rebase`. `engine_rebase` now consumes it rather than detecting
   separately. Logged as the first line of the startup snapshot
   (`Game.Identity title=… build=…`).
2. **Load-and-log on K2.** K2 hashes in the manifest, a minimal
   `kotor2.hooks.toml`, everything else gated off. Proves the framework
   end-to-end before any mass change.
3. **Offsets go runtime.** *(done 2026-07-31)* `engine_offsets_select.h`
   introduces `Same` / `Pick` / `Todo` / `Kotor1Only`; 246 constants in
   `engine_offsets_fields.h` plus 255 scattered across 43 other files were
   converted. The strays were marked **in place**, not relocated — a named
   constant next to the subsystem that reads it is good cohesion; what was
   missing was a marker saying "engine-version-dependent", and the marker is
   greppable wherever it lives.
4. **Populate K2 values.** *(in progress)* See the coverage table below.
5. **Feature-gate the K1-only modules**, then walk the pillars up.

## Coverage (2026-07-31, end of second port session)

Struct offsets — `acc::off`:
- `Todo` (K2 unknown): 422
- `Same` (verified identical): 15
- `Pick` (verified different): 60
- `Kotor1Only` (no K2 counterpart): 13

Addresses — `acc::addr`:
- `R` (K2 unknown, resolves to 0): 200
- `Pick` (.text/.rdata known): 64
- `PickGlobal` (.data known): 4
- `TodoGlobal` (.data unknown, resolves to 0): 10
- `Kotor1Only` (no K2 counterpart): 2

`grep -c "Todo("` and the `R(` count are the remaining-work counters.

Menu subsystem specifically (the `port_worklist.py` invocation in WHERE TO
RESUME): 115 constants used, 90 resolved, **25 unresolved** — down from 79.

### The cross-check that makes the seeded database usable

On every field and pointer the two databases share, upstream's **KOTOR 1**
column matches ours exactly — offsets 0x4, 0x8, 0x8c, 0x90, 0x9c, 0xa2c, 0xa74,
and globals 0x7A39F4, 0x7A39FC, 0x7A3A08, 0x7A3A28. Two independent
reverse-engineering efforts agreeing on the column we can verify is what earns
trust in the column we cannot.

The structural predictions held exactly:
- `CGameObject` (shallow root) — unchanged
- `CSWSObject` — uniformly +4
- `CSWSCreature` — uniformly +0x724
- the whole `.data` globals block — uniformly +0x2A1AA8, i.e. relocated intact

## The GUI hook set — verified by decompiling both games

The three hooks all menu accessibility is built on now have KOTOR 2 addresses,
each confirmed by decompiling the KOTOR 1 and KOTOR 2 functions and comparing
structure — not inferred from a delta or a single witness.

- **`CSWGuiPanel::SetActiveControl`** (focus signal) — `0x0040A630` →
  `0x0040EC00`. Found by vtable slot: 71 classes inherit it, all in the same
  slot, all agreeing. Both decompiles show the same active-control comparison,
  the same focus-change virtual fired on old then new, the same gui-sound call.
- **`CSWGuiManager::HandleInputEvent`** (input dispatcher) — `0x0040C8E0` →
  `0x00410AA0`. Same `this->field_0x68 = code` write, same `value == 0`
  early-out, and a switch whose case values and axis-to-direction translations
  match byte for byte.
- **`CSWGuiManager::Update`** (per-frame tick) — `0x0040CE70` → `0x004113A0`.
  Both take a float and both read `panels.size` (+0x8c) as their first action,
  which is exactly what the KOTOR 1 hook point does.

Also identified: `CSWGuiManager::HitCheckMouse` → `0x00411030`.

### Panel-internal offsets: why they are the expensive part, and what cracked them

Panel-internal offsets — a named child control or a cached handle at a fixed
position inside one huge panel struct — follow no delta rule at all. Measured
witness: `CSWGuiPortraitCharGen::OnPanelAdded` stores the same
`rand()%300 * 0.01 + 1.0` float at +0x1230 in KOTOR 1 and **+0x1ce8** in
KOTOR 2, a shift of 0xAB8 inside one class. Panels embed hundreds of controls
and every one that grew pushes everything after it. Worse, the drift is not even
monotonic: KOTOR 2's map panel drops three controls, so its arrow buttons land
*lower* than KOTOR 1's. "K2 should be bigger" is not a valid sanity check.

**What works: the constructor, matched by .gui tag.**

A panel's constructor builds every embedded control in declaration order and
binds each to its layout tag — `InitControl(panel, &member, "BTN_ARRR")`. The
tag is the identity; the offset is whatever it is. One decompile per panel
yields its whole control layout, and no arithmetic is involved.

Constructors are not virtual, so the slot map does not reach them. Find them
with `tools/re-scripts/vtable_xrefs.py`, which scans .text for the class's
vtable address as an immediate: a constructor stores it into `[this]`, and
apart from the destructor almost nothing else mentions it. Two hits per class,
constructor first.

Signs the reading is right, all of which held: the label stride is 0x140 in
KOTOR 1 and 0x148 in KOTOR 2, the button stride 0x1c4 and 0x1d0, the listbox
0x2e0 and 0x2f0 — that last one matching the +0x10 `CSWGuiListBox` growth
measured independently from `HandleInputEvent`. Consecutive controls should sit
exactly one stride apart in both games.

**Constructors also settle plain fields**, not just controls, because they
initialise them — and distinctive initialisers are what pin the alignment.
`CSWGuiKeyMapButton` fell to a nine-field write sequence containing two -1s;
`CSWGuiInGameItemEntry` to a 0x7f000000 "empty" sentinel; `CSWGuiInGameEquip`'s
trailing block to the same sentinel in the same position within six writes.

**Routes tried and found weaker:**

- *The destructor*, to recover the embedded-member layout in one pass. Vtable
  slot 0 is the scalar-deleting-destructor **thunk** in both games, so the real
  destructor is one CALL further in, and it only covers members with non-trivial
  destructors — not the plain `ulong` handles.
- *Live observation* (`PanelProbe` dumps), which is how most of these were
  established for KOTOR 1. Accurate and fast per panel, but it spends test
  rounds, which is the resource THE METHOD exists to protect.

### The last 25 in the menu subsystem, with the route for each

These are the ones where the constructor alone did not finish the job. Each
needs its KOTOR 2 counterpart function identified first, which is the actual
remaining work.

- **The nine `kEquipPanel*IdOffset` handles.** The anchor is already found and
  is unambiguous: both constructors end with six writes whose second value is
  0x7f000000, at KOTOR 1 0x42a4.. and KOTOR 2 0x50cc.. So KOTOR 2's `field46`
  is 0x50cc and the id block walks back from there — but KOTOR 2 appears to have
  one extra field ahead of `selected_slot` (its block starts 3 dwords after
  BTN_SWAPWEAPONS where KOTOR 1 uses 2), so walking back is exactly the
  extrapolation this port keeps getting punished for. Get one confirming
  witness: KOTOR 1's `CSWGuiInGameEquip::UpdateInventory` @0x006b9970 writes all
  nine, so find its KOTOR 2 twin (start from the equip constructor's slot-label
  loop, which in KOTOR 1 walks the id array in parallel with `env_slot_labels`).
- **The four `kPartyPortrait*` fields.** KOTOR 2's `CSWGuiPartySelection`
  constructor declares `party_data` as an array at +0x84, element size **0x478**,
  count 12, built by element-constructor 0x0089dac0. Decompile that against
  KOTOR 1's equivalent. Note KOTOR 1's own `CSWGuiPartySelectionButton` is an
  empty PlaceHolder in Lane's database, so the KOTOR 1 column here came from
  live probing, not from the struct DB — treat it as the less certain side.
  One field is already effectively settled: KOTOR 2's button constructor writes
  a single field right after the embedded button, at 0x1d0, where KOTOR 1 writes
  at 0x1c4.
- **`kAddrManagerLMouseDown` / `LMouseUp`.** Each has exactly ONE caller in
  KOTOR 1 — `CClientExoAppInternal::PerformLButtonDownAction` / `...UpAction`,
  called from adjacent sites in `CClientExoAppInternal::HandleInputEvent`. That
  class is absent from the RTTI slot map, so the forwards-from-callers trick
  that cracked `MoveMouseToPosition` needs one more hop: find something virtual
  above `CClientExoAppInternal::HandleInputEvent` (its own callers are
  `PlayBackInputEvents` and `ProcessInput`). A structural search for "two
  functions called adjacently, each making one GuiManager-mediated call" was
  tried and does NOT find them — only ONE of the KOTOR 1 pair reaches the
  manager through the global.
- **`kPortraitIdOffset`** — KOTOR 1's `CSWGuiPortraitCharGen::UpdatePortraitButton`
  @0x006f8ad0 writes it; find the KOTOR 2 twin. Derivation from the OnPanelAdded
  float anchor (0x1230 -> 0x1ce8, so 0x1238 -> 0x1cf0) is available but unverified.
- **The five `kUpgrade*` plus `kAddrUpgradeSlotTypeTable`.** The table is
  .rdata, not a function, so neither RTTI nor the slot map applies; reach it
  through the code that indexes it. `kUpgradeSlotCustomValueOff` is a
  `CSWGuiControl` field (+0x58) and almost certainly +0x5c by the same +4 the
  rest of that region takes, but no direct witness has been read.
- **`kCGuiInGameReply*` (2)**, **`kItemLocNameOffset`** — different classes
  (dialog reply list, `CSWSItem`), not reached by any menu-panel constructor.

**`MoveMouseToPosition` is NOT yet found.** Its KOTOR 1 body is four
statements (store x/y, `CExoInput::SetMousePos`, `HandleMouseMove`), but none
of the three KOTOR 2 callers of the apparent `HandleMouseMove` matches its
shape, so KOTOR 2 reaches the hit-test by a different route. It is needed for
click-simulation (activation), not for reading and announcing, so it can wait —
but do not guess it: activation through a wrong address is how the
`SetActiveControl` crash class happened on KOTOR 1.

### What the decompiles said about layout

Worth more than the addresses themselves, because it constrains everything else:

- **`CSWGuiManager` did not grow.** Its input-code field is at +0x68 in both
  games, and `HitCheckMouse` reads the panel arrays at +0x88/+0x8c and
  +0x94/+0x98 — identical to KOTOR 1. So foreground-panel resolution, which the
  whole navigation chain depends on, carries over unchanged.
- **`CSWGuiPanel` and `CSWGuiControl` both shift +4** in that region:
  `active_control` 0x1c→0x20, `manager` 0x18→0x1c, the control's gui-sound byte
  0x55→0x59.
- **The engine input codes are identical.** `HandleInputEvent`'s switch uses the
  same case values and produces the same translated direction codes in both
  games, so `engine_input.h`'s InputIndex constants should carry over as-is.

### Panel identity: all of it, from RTTI class names (2026-07-31)

Every vtable-identity constant in the codebase now has a KOTOR 2 value except
two, and none of it needed a decompile. The route is:

1. Look the KOTOR 1 address up in Lane's Ghidra XML — vtables carry a
   `<Class>_vtable` SYMBOL, so the address yields a class NAME.
2. Look that name up in `docs/llm-docs/re/k2/k2-vtables.csv`.

23 of 25 resolved that way, including the nine title-screen Options sub-screens
(whose KOTOR 1 values had been captured from a live probe and carried no name
until this lookup gave them one). The convention was checked before trusting it:
for each candidate, `vtable_va - 4` holds the complete-object locator and slot 0
points into `.text`, which is what makes `vtable_va` the pointer an object
actually stores.

Two panels are **absent from KOTOR 2**, and this is measured rather than
assumed: diffing the 110 `CSWGui*` classes Lane's KOTOR 1 database names against
the 122 in KOTOR 2's RTTI leaves exactly two on the KOTOR 1 side —
`CSWGuiQuestItem` (the journal's quest-items sub-screen) and `CSWGuiScriptSelect`
(the character sheet's combat-behaviour picker). KOTOR 2's exe contains no
`questitem` or `scriptselect` string either. They are marked with a new
`acc::addr::Kotor1Only()`, mirroring `acc::off::Kotor1Only()`: same run-time
behaviour as a bare `R()`, but it keeps the remaining-work counter honest.

KOTOR 2 *adds* fourteen GUI classes — the workbench item-creation screens, a
death display, a legal screen, the three iOS gamepad panels, a tutorial box.
Those are surfaces the KOTOR 1 code has nothing to say about yet.

### The deltas ACCUMULATE through embedded sub-objects

The single most important structural finding so far, because it invalidates the
obvious shortcut.

The seeded database suggested "a constant delta per class". That is true within
a flat class, but KOTOR 1's GUI classes are built by embedding whole
sub-objects, and each one that grows shifts everything after it. Observed in
`CSWGuiLabelHilight::Draw`, decompiled in both games:

- `CSWGuiControl` base: 0x5c → **0x60** (+4)
- the embedded border: 0x74 → **0x78** (+4)
- so `CSWGuiLabel`'s text sub-object: 0xd0 → **0xd8** (+8, not +4)

Both borders confirm it: the label's own border moved 0x5c → 0x60, and the
hilight's second border sits at +0x148.

So **do not extrapolate +4 down a class**. The correct model is: sum the growth
of every base and embedded member that precedes the field. Anything at or below
+0x14 in `CSWGuiControl` is unshifted (the extent is proof); past the insertion
point the delta is +4 per grown object crossed, not +4 total.

Left as `Todo` because of this: `kLabelTextOffset` / `kLabelStrRefOffset` /
`kButtonTextOffset` / `kButtonStrRefOffset` and the `kTextObject*` internals.
The arithmetic says 0xe8 → 0xf0 and 0xf0 → 0xf8 for the label, but that assumes
`CSWGuiText`'s own layout did not change, which has not been checked. These
feed every spoken string, so they want observing rather than deriving.

### The shared control classes are done (2026-07-31)

Everything the whole GUI is built out of — `CSWGuiControl`, `CSWGuiListBox`,
`CSWGuiSlider`, `CSWGuiButtonToggle` — now has observed KOTOR 2 offsets. The
method that produced them scales, and is worth stating because it is cheaper
than it looks:

`k2-vtable-slots.csv` maps KOTOR 1 virtual methods to KOTOR 2 addresses **by
name**. So for any offset, find a KOTOR 1 virtual that touches the field, look
its KOTOR 2 twin up in that file, decompile both, and read the offset off the
matching statement. No searching, no guessing which function is which.

What that produced:

- `CSWGuiControl` parent / tooltip strref / tooltip string: **+4** each
  (0x14→0x18, 0x24→0x28, 0x28→0x2c), from `DisplayToolTip` (slot 36), with the
  parent corroborated independently by `Load` (slot 18) storing its
  `Obj_ParentID` lookup at this+0x18. The insertion point is now pinned between
  +0x10 and +0x14: the extent at 0x4..0x10 is unshifted, +0x14 is not.
- `CSWGuiListBox` controls / bit_flags / items_per_page / selection_index /
  top_visible_index: **+0x10** each, from `HandleInputEvent` (slot 15). Every
  landmark of the KOTOR 1 version reappears at exactly +0x10 — the -1 test on
  selection_index, the 0x40 / 0x200 / bit-12 flag tests, `size -
  items_per_page`, and the closing `controls.data[selection_index]` dispatch.
- `CSWGuiSlider` max / cur: **+4**, from `HandleInputEvent` (slot 15).
- `CSWGuiButtonToggle` state: **+0xc**, from `Load` (slot 18) masking the
  "ISSELECTED" .gui byte into bit 0.
- `CSWGuiListBox::SetSelectedControl` (an address, not an offset) fell out of
  the same listbox decompile: KOTOR 2 calls it at all five of the places
  KOTOR 1 does, with the same `(index, playSound)` pair.

Note the three different deltas — +4, +0xc, +0x10 — in four classes that all
derive from the same base. That is the accumulation rule below, in evidence.

Two incidental cross-checks worth recording, because they cost nothing and
constrain a lot: KOTOR 2's slider reads its extent width/height at +0xc/+0x10,
exactly as KOTOR 1 does (so `kControlExtentOffset` really is `Same`), and its
`CSWGuiManager::HandleInputEvent` reads the same +0x64 / +0x68 / +0x72 /
+0x88 / +0x8c / +0x94 / +0x98 the KOTOR 1 one does (so the manager really did
not grow).

### Values that are derived rather than verified

Flagged in the code, listed here so they are not forgotten:
- `kWaypointPositionOffset` — `CSWSObject.Position` read on a waypoint, so it
  should inherit the +4 shift, but the database says nothing about waypoints.
- `kScriptVarTableOffset` (+0x100) — left `Todo`. `CSWSObject`'s shallow fields
  shift +4, but +0x100 is far enough down the class that the same shift cannot
  be assumed. Save-persistent mod state on KOTOR 2 depends on getting this
  right.

Controller support comes after the mod runs on K2 at all — see below.

## The pass-through hazard (fixed, worth understanding)

Before step 1, `acc::addr::R()` returned the *reference value* for any
unrecognised build. On KOTOR 2 that would have handed out 267 KOTOR 1 addresses
pointing into unrelated K2 code — silent jumps into the middle of other
functions, which is precisely the failure mode `engine_rebase.h` warns about
for stale addresses.

`R()` now returns **0** for KOTOR 2. That faults recognisably at address 0, and
the ~12 call sites already wrapped in `acc::addr::Ok()` degrade gracefully with
no changes. But `Ok()` is not what makes K2 safe — it covers 12 of 267 sites.
**Engine-touching code must be gated on `acc::game::IsKotor1()` before it
runs.** That gating is step 5, and until it exists K2 must not be given a hook
set beyond the minimum.

## Controller support

KOTOR 2's native pad support came from Aspyr's iOS/Android port, and the RTTI
names show it plainly: `CSWGamepadMenuIos`, `CSWGuiActionMenuIos`,
`CSWGuiHelpPanel` (all nested in `CSWGuiMainInterface`), plus
`CSWGuiControllerLossBox` and a `CExoInputeventDesc2ButtonAxis` input
descriptor. It is a **parallel UI**, not a remapping of the keyboard one — so
it is not something the K1 nav-chain work ports into directly.

Two facts that will shape the design:

- The exe does **not** import XInput. The pad goes through DirectInput. If we
  poll the pad ourselves we would be a second reader of the same device, and
  both we and the engine would see every press — the same double-fire problem
  documented for keyboard polling in `controller-mod-techniques.md` §4.
- Our mod adds many keyboard-only affordances (discovery cycling, the unified
  action menu, interact hotkeys). These need pad bindings that do not collide
  with what K2 already binds.

`docs/controller-mod-techniques.md` is the existing hand-off note on our input
pipelines and is the right starting point, but it describes K1 surfaces.

## Sources

- `kotor2-port-feasibility.md` — the original measurement (sigscan 0/213) and
  why it will not improve.
- `docs/llm-docs/CLAUDE.md` — which modules are K1-story-only, which minigames
  do not carry.
- `docs/controller-mod-techniques.md` — input pipelines, nav chain, activation.
- `archiev/refactoring/END-REPORT.md` — what the pre-K2 refactoring bought.
