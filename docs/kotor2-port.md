# KOTOR 2 port — plan and findings

**Status: ACTIVE (started 2026-07-31).** Supersedes the conclusion of
`kotor2-port-feasibility.md`, whose *measurements* remain valid — the sigscan
result especially — but whose cost estimate predates the RTTI finding below.

## WHERE TO RESUME (read this first)

State as of 2026-07-31, end of the first port session. Working tree clean, 14
commits on `main`, KOTOR 1 tested and behaving normally throughout.

**What works:** KOTOR 2 speaks its main menu and options submenu. One hook is
installed (`CSWGuiPanel::SetActiveControl` → `menus_focus_k2.cpp`). Every other
hook handler is gated off by `acc::game::HandlerEnabled()`.

**The next task, already scoped:** port the menu subsystem *whole*, per THE
METHOD below. Regenerate the worklist with

    python tools/re-scripts/port_worklist.py patches/Accessibility \
        menus.cpp menus_focus.cpp menus_chain.cpp menus_extract.cpp \
        menus_dispatch.cpp menus_internal.h engine_manager.h \
        engine_reads.cpp engine_panels.cpp

At the time of writing: 114 constants used, 35 resolved, **79 to verify**.
Suggested order — the ten `kVtableCSWGui*` panel-identity constants in
`engine_panels.cpp` first (the RTTI map should resolve them by class name for
free), then the panel field offsets by decompiling KOTOR 2's own `Load` methods,
which is the technique that produced the text chain. `MoveMouseToPosition` is on
the list and should replace the `SetCursorPos` stand-in in `menus_focus_k2.cpp`.

**Do not** repeat the minimal-slice approach — see THE METHOD.

**Tools built for this port** (all in `tools/re-scripts/`, all offline):
- `rtti_scan.py` — class → vtable map from KOTOR 2's RTTI
- `vtable_map.py` — KOTOR 1 → KOTOR 2 addresses by vtable slot
- `find_thiscall_targets.py` — methods called on a known singleton
- `port_worklist.py` — what a subsystem needs, and what is unresolved

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

## Coverage (2026-07-31)

Struct offsets — `acc::off`:
- `Todo` (K2 unknown): 488
- `Same` (verified identical): 4
- `Pick` (verified different): 7
- `Kotor1Only` (no K2 counterpart): 10

Addresses — `acc::addr`:
- `R` (K2 unknown, resolves to 0): 253
- `Pick` (.text/.rdata known): 19 — all the vtable-identity constants, from RTTI
- `PickGlobal` (.data known): 4
- `TodoGlobal` (.data unknown, resolves to 0): 10

`grep -c "Todo("` and the `R(` count are the remaining-work counters.

Everything with a K2 value so far came from the seeded upstream database or the
RTTI scan — **no fresh reverse-engineering yet**. The 253 function addresses are
where that starts.

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
