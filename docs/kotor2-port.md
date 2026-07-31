# KOTOR 2 port — plan and findings

**Status: ACTIVE (started 2026-07-31).** Supersedes the conclusion of
`kotor2-port-feasibility.md`, whose *measurements* remain valid — the sigscan
result especially — but whose cost estimate predates the RTTI finding below.

## WHERE TO RESUME (read this first)

State as of 2026-08-01: **Batch 1 (GUI spine) is TESTED AND WORKING** — the
user confirmed menu navigation, Options + sub-panels, listbox rows and speech
all behave as intended on KOTOR 2. Next up: **Batch 2** (see THE BATCH PLAN).

The first test round surfaced two crash classes, both fixed and re-verified:

- **Unguarded engine reads in a K2-only diagnostic path.** AnnounceFocus read
  control captions via ReadCExoString/ReadU32 (guard-free by design) with no
  SEH — a garbage caption pointer crashed the process inside memcpy, including
  in a PRE-batch session. The probe now runs in one SEH-guarded pass
  (FocusProbe in menus_focus_k2.cpp) and an unreadable control is skipped, not
  announced. Lesson: the audit tool only sweeps files it is pointed at —
  K2-only TUs must be audited too, not just the shared chain.
- **The CGuiInGame slot walk ran on KOTOR 2.** IdentifyPanel's slot-table walk
  dereferences KOTOR 1's CGuiInGame layout and ran before the vtable fall-
  through; on KOTOR 2 it dereferenced a garbage base unguarded (WER fault in
  accessibility.dll, resolved by disassembling the DLL at the crash offset).
  Now: SlotTableLookup declines on KOTOR 2 outright and is SEH-guarded on
  KOTOR 1 too. Batch 2 ports the real table and clears the decline.

KOTOR 1's behaviour is intended to be untouched throughout, but this batch's
changes are the first that alter code KOTOR 1 executes (`Dispatch()` gained an
`if (k1)` structure, four handlers lost their `HandlerEnabled()` gates, the
slot walk moved into a guarded helper), so the next KOTOR 1 session should
sanity-run menus + one in-world area before trusting it.

**What Batch 1 shipped:**

- `kotor2.hooks.toml` now installs five hooks: SetActiveControl (was live) plus
  Update @0x004113A9, HandleInputEvent @0x00410AC8, HandleFocusChange
  @0x00418FE6, ListBox SetActiveControl @0x0041E9A4 (the implementation; the
  vtable slot is a forwarder). All cut bytes byte-confirmed off the exe.
- The three new frame-unpacking wrappers live at the bottom of
  `menus_focus_k2.cpp` (OnHandleInputEventK2 / OnHandleFocusChangeK2 /
  OnListBoxSetActiveControlK2), exported in exports.def. OnUpdate is hooked
  directly — it ignores its argument.
- **The HandleInputEvent hook uses `skip_original_bytes = true`** because its
  cut's first instruction loads EAX and the wrapper's consumed-exit TEST reads
  EAX after the cut replay (KPatchManager bug 2, by design). The wrapper
  emulates the cut's one effect (`this->input_code = param_1`, via the new
  `kMgrInputCodeOffset = Same(0x68)`). consumed_exit_address = 0x00410FA9, the
  single common epilogue where FS:[0] is unregistered — the engine's own
  repeat-debounce jumps there from mid-body, so the shape is engine-native.
- `Dispatch()` in core_tick.cpp is game-aware: KOTOR 2 runs the menu spine
  only (ValidatePanels, help, TickMonitors, PollHomeEnd, modsettings,
  update_checker, TickPendingOps, hotkeys/watchdog); every world / combat /
  camera / minigame / dialog phase sits in `if (k1)` blocks that later batches
  clear one by one.
- Panel-specific sub-handlers whose KOTOR 2 constants are still Todo/R now
  **decline at their own entry** with `if (acc::game::IsKotor2()) return...`:
  abilities, chargen feats, powers level-up, editbox (+ its monitor), galaxy
  map (+ Tick), keymap (+ Tick), pazaak board + deck builder, peek, cycle
  input, and the chargen-feats diagnostic dump. `grep -rn "KOTOR 2 (Batch"
  patches/Accessibility` enumerates them; clearing one means resolving its
  constants, deleting the decline, and re-running handler_chain_audit.py.

The first test round exercised the full loop (2026-08-01, PASSED after the two
crash fixes above): main-menu arrow navigation with speech, Enter/Esc into
Options and its sub-panels, the Gameplay settings chain (13 entries), listbox
row announces. Log channels for future rounds: K2.Focus / Menus.Input /
Menus.ListBox / Menus.FocusChange, plus "probe faulted" lines naming controls
whose caption offsets need per-class fixing.

Every other hook handler stays gated off by `acc::game::HandlerEnabled()`.

**Menu-subsystem worklist:** regenerate with

    python tools/re-scripts/port_worklist.py patches/Accessibility \
        menus.cpp menus_focus.cpp menus_chain.cpp menus_extract.cpp \
        menus_dispatch.cpp menus_internal.h engine_manager.h \
        engine_reads.cpp engine_panels.cpp

115 constants used, 110 resolved, **5 to verify** (was 79). The menu-reading and
navigation path is COMPLETE; the 5 are dialog-reply fields, one party field
parked on a dependency, and the two activation-only click primitives.

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

**The next task: clear the handler gates and take the first KOTOR 2 menu test
round.** The constants no longer block this. What stands between here and a
build worth testing:

1. Each menu hook handler's whole read/call chain has to be resolved before its
   `HandlerEnabled()` gate is replaced with an explicit branch — that is the
   `IsLoadingSaveGame` → null-pointer trap from the first session, and it is
   what "clear the gate" actually costs. `port_worklist.py` drives this, but
   expect it to pull in constants outside the nine files currently listed.
2. Swap the `SetCursorPos` stand-in in `menus_focus_k2.cpp` for the real
   `MoveMouseToPosition`, now resolved.
3. Act on the three CODE divergences listed under "Divergences that are CODE"
   below. They will not appear in any constant count and each misbehaves
   silently if KOTOR 1's logic is reused.

The 5 remaining constants are listed with their routes under "The last 5" and
can be picked up whenever their subsystem's turn comes.

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

**A KOTOR 1 offset is almost always cheaper to look up than to decompile.**
Three sources, in order of usefulness:

- `third_party/Kotor-Patch-Manager/AddressDatabases/kotor1_0_3.db` — a SQLite
  database with **4720 offsets keyed class + member name** and 9710 named
  functions. This is the best KOTOR 1 reference we have and it was found late;
  prefer it over decompiling. `select member_name, offset from offsets where
  class_name='CSWGuiInGameEquip' order by offset` prints a whole panel's layout
  instantly. It independently confirmed every portrait and equip value derived
  from constructors this session, and the `CSWGuiControl` fields (parent 0x14,
  tooltip_string 0x28, gui_object 0x34) read out of the KOTOR 1 decompiles.
- `docs/llm-docs/re/swkotor.exe.h` — struct definitions with member ORDER and
  embedded types. Complements the database: the database gives offsets, the
  header gives the sequence, and the sequence is what pairs against a KOTOR 2
  constructor's construction order.
- `k1_win_gog_swkotor.exe.xml` — every function and vtable SYMBOL by name, which
  is what turns a KOTOR 1 address into a class name.

The matching `kotor2_steam_aspyr.db` holds only 48 functions, 21 offsets and 14
globals, all of them creature/VM/inventory level — it was checked against the
menu worklist and contributes nothing there. Do not re-check it for GUI work.

Ghidra: project `kotor2`, program `swkotor2.exe`, at `C:\Tools\ghidra-projects`.
Decompile with
`KDEV_GHIDRA_PROJ=kotor2 KDEV_GHIDRA_PROGRAM=swkotor2.exe tools/ghidra-scripts/decomp.sh 0xADDR`.

**Two `decomp.sh` runs against the SAME project cannot overlap** — the second
dies on the project lock with `DECOMP_ERROR: no output produced`. Pass several
addresses to one invocation instead; the ~30-60s startup dominates, so extra
addresses are nearly free while a second concurrent run is a wasted round. A
`kotor1` run and a `kotor2` run in parallel are fine, and that pairing is the
right way to work: one round per game, compared afterwards.
Function catalogue (11,652 entries) at `docs/llm-docs/re/k2/k2-functions.csv`.
A space-free copy of the exe lives at `C:\Tools\k2re\swkotor2.exe` — the Ghidra
batch launcher cannot handle the spaces in the Steam path.

**Testing:** `kdev apply --game k2`, then read
`<K2 install>\logs\patch-*.log`. The K2 focus path logs under `K2.Focus`.

## THE BATCH PLAN (decided 2026-07-31)

**Frontload everything offline; test only when a whole system exists.** Decided
after a single cleared gate produced a KOTOR 2 build that navigated correctly
and spoke nothing — see "The hook gate is not the unit of work" below.

The work is split into batches so a FRESH SESSION can take one without
re-deriving context. Each batch is cut along dependency lines rather than
convenience: every batch closes at least one producer→consumer loop, so it is
independently testable and cannot produce the half-system failure again.

Start any session by running

    python tools/re-scripts/k2_hook_status.py patches/Accessibility

which reports, per hook, whether KOTOR 2 has it installed AND has its
`HandlerEnabled()` gate cleared. Both halves are needed; either alone is worse
than neither. After Batch 1: **5 of 25 READY** (the whole GUI spine).

Each batch means the same three things: resolve the constants its handlers'
call graphs touch (`port_worklist.py`), find its KOTOR 2 hook cut points (one
`PrintListing` read per hook — KOTOR 2's unoptimised build means NO KOTOR 1 cut
point, cut length or register source transfers), and clear its gates. Then one
test that exercises a complete loop.

Before each handler goes live, run

    python tools/re-scripts/handler_chain_audit.py patches/Accessibility <files>

and fix anything it calls UNGUARDED. That is what separates "degrades on
KOTOR 2" from "crashes on KOTOR 2".

**Batch 1 — GUI spine.** `OnUpdate` (the per-frame tick, and the ONLY thing that
drains the pending-announce slot), `OnHandleInputEvent` (input dispatch + the
navigation chain), `OnHandleFocusChange`, `OnListBoxSetActiveControl`. Gates in
`core_tick.cpp`, `input_pipeline.cpp` (2), `menus_dispatch.cpp` (2). Completes
the menu system, whose constants are already 110/115 — so this is mostly hook
cut-points. KOTOR 2 addresses already known: Update 0x004113A0,
HandleInputEvent 0x00410AA0, HandleFocusChange 0x00418FE0, ListBox
SetActiveControl 0x0041FEE0 (a forwarder to 0x0041E9A0 — decide which to hook).

### Batch 1 cut points (listings read 2026-07-31; WRITTEN + byte-confirmed)

All four are now in `kotor2.hooks.toml`. The bytes were confirmed straight off
the exe (it is not SteamStub-encrypted; `capstone` is installed for the Python
at reference_python_path, so full-function disassembly needs no Ghidra round).
Every cut is frame- or register-relative with no absolute operand, which is
what makes it safe to relocate into a trampoline. The listings below are kept
as the design record; where the implementation differs (HandleInputEvent's
skip_original_bytes), WHERE TO RESUME is authoritative.

**`OnUpdate` — `CSWGuiManager::Update` @ 0x004113A0. Cut at 0x004113A9, 9 bytes.**

    004113a6  MOV [EBP-0x4c],ECX          ; this stored
    004113a9  MOV EAX,[EBP-0x4c]          ; \ cut, 3 bytes
    004113ac  MOV ECX,[EAX+0x8c]          ; / cut, 6 bytes — panels.size

The +0x8c read is the same field KOTOR 1's hook point reads, which confirms the
function identity a third time. `OnUpdate` ignores its argument, so pass EBP.
**This is the hook the whole announce path depends on** — nothing else drains
the pending-announce slot.

**`OnHandleInputEvent` — `CSWGuiManager::HandleInputEvent` @ 0x00410AA0. Cut at
0x00410AC8, 9 bytes.**

    00410ac5  MOV [EBP-0x68],ECX          ; this stored
    00410ac8  MOV EAX,[EBP-0x68]          ; \
    00410acb  MOV ECX,[EBP+0x8]           ;  } cut, 3+3+3 = 9 bytes
    00410ace  MOV [EAX+0x68],ECX          ; / this->input_code = param_1

Arguments: `this` = [EBP-0x68], param_1 = [EBP+8], param_2 = [EBP+0xC] — so pass
EBP and read them, as `OnSetActiveControlK2` does. KOTOR 1 takes three registers;
none of that transfers.

**The two open problems this section used to list are SOLVED** (third session,
whole-function disassembly via capstone):

- The consumed-exit target is **0x00410FA9** — the function's single common
  epilogue (one RET; every path funnels there), which restores FS:[0] from
  [EBP-0xC] and so unregisters the SEH frame itself. The engine's own
  repeat-debounce consumes events by jumping there from mid-body (0x00410BF1,
  0x00410C45), so the jump shape is engine-native. At the hook the SEH scope
  index [EBP-4] is still -1 and stack depth matches the natural fall-through.
- A third problem surfaced and forced a design change: the cut's first
  instruction loads EAX, and the wrapper's consumed-exit TEST reads EAX after
  the cut replay (KPatchManager bug 2 — unfixable by design). Hence
  `skip_original_bytes = true` with the handler emulating the cut's one effect,
  the `this->input_code = param_1` store (`kMgrInputCodeOffset = Same(0x68)`).
  Register liveness at the resume CMP was checked across both branch paths:
  EAX/ECX/EDX are each written before their next read.

**`OnHandleFocusChange` — `CSWGuiControl::HandleFocusChange` @ 0x00418FE0. Cut at
0x00418FE6, 7 bytes.**

    00418fe6  MOV [EBP-0x10],ECX          ; \ cut, 3 bytes
    00418fe9  CMP [EBP+0x8],0x0           ; / cut, 4 bytes
    00418fed  JZ 0x0041904e               ; NOT in the cut — relative

Stop before the JZ: it is a relative jump and relocating it changes its target.
`this` is NOT yet stored when the handler runs, so take it from ECX and pass EBP
alongside for param_1 at [EBP+8]. The trampoline replays the CMP immediately
before returning to the JZ, so EFLAGS are set correctly — provided the wrapper
preserves flags across the handler call, which the local KPatchManager does
(see project_kpatchmanager_consume_test_bugs).

**`OnListBoxSetActiveControl` — hook the IMPLEMENTATION at 0x0041E9A0, not the
vtable entry. Cut at 0x0041E9A4, 6 bytes.**

Vtable slot 2 (0x0041FEE0) is only a forwarder; 0x0041E9A0 is the real body and
is what KOTOR 1 hooks the equivalent of. Its listing confirms the identification
outright, and incidentally confirms two constants resolved separately:

    0041e9b2  ADD ECX,0x2ac               ; kListBoxControlsOffset  = 0x2ac
    0041e9c1  CALL 0x0041e870             ; SetSelectedControl      = 0x0041E870

Cut covers `MOV [EBP-0x4],ECX` (3) + `MOV EAX,[EBP+0xc]` (3). `this` is in ECX at
entry; param_1 = [EBP+8], param_2 = [EBP+0xC].

**Batch 2 — In-game GUI lifecycle.** `OnSwitchToSWInGameGui`,
`OnHideSWInGameGui`, `OnSetSWGuiStatus`, `OnAppendToMsgBuffer`; gates in
`engine_subscreen.cpp` (4) and `msg_router.cpp`. Includes porting the CGuiInGame
slot table, which is what makes equipment / inventory / journal / map classify
at all — until then they fall through to vtable identification and read Unknown.

**Batch 3 — World, area, transitions.** `OnSetMoveToModuleString`, `OnDoorOpen`,
`OnShowObject`; gates in `transitions.cpp`, `door_announce.cpp`,
`passive_narrate.cpp`. Largest offset surface — `engine_area.h` alone holds ~60.

**Batch 4 — Combat.** The four CombatRound hooks plus `OnSetPauseState`; gates in
`combat_diag.cpp` (3) and `combat_queue_hooks.cpp`.

**Batch 5 — Audio.** `OnPlayFootstep`, `OnSetListenerPosition`,
`OnCalculatePitchVarianceFrequency`; gates in `audio_bus.cpp`,
`audio_pitch.cpp`, `audio_footstep_suppress.cpp`.

**Batch 6 — Minigames and leftovers**, AFTER a triage pass deciding what KOTOR 2
even has. `OnTurretBulletHit`, `OnPlayerFire`, `OnRulesInit`. Do not spend
constant work here before triage: several KOTOR 1 modules are story- or
minigame-specific and may have no KOTOR 2 counterpart at all, the way
CSWGuiQuestItem and CSWGuiScriptSelect turned out not to.

Scale, stated plainly so no batch is under-estimated: ~400 offsets and ~200
addresses remain overall, and each of the 24 unhooked handlers needs its own
KOTOR 2 listing read. The big batches are several sessions each.

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

### The hook gate is not the unit of work — the SYSTEM is

**Learned 2026-07-31, by shipping a half-system and spending a test round on
it.** This is the most expensive version of the minimal-slice mistake so far,
because it survived the whole constant-resolution effort and then reappeared at
the gate level.

With the menu constants done, one gate was cleared — the focus handler — and
that was tested. KOTOR 2 navigated correctly and spoke nothing. There was no
defect: `AnnounceNewFocusedControl` does not speak, it WRITES the pending-announce
slot, and `DrainPendingAnnounce` speaks it from the per-frame tick
(`TickGeneralMonitors` ← `acc::tick::Dispatch()` ← the `CSWGuiManager::Update`
hook). KOTOR 2 had no Update hook and its `OnUpdate` was still gated, so every
announcement was queued and discarded. Panel titles were audible only because
`SpeakPanelTitleOnFirstSight` speaks directly.

Half of a well-tested mechanism behaves like a bug. That is worse than the
feature being fully off, because it invites debugging code that is fine.

**The rule: frontload everything offline; take the first real test only when the
FULL system exists** — not the full constant set, the full system, meaning every
hook it needs installed and every gate it needs cleared.

Before proposing any KOTOR 2 test, trace the feature end to end — signal in,
state written, tick that reads the state, speech out — and confirm every hop has
BOTH a hook and a cleared gate. That is an offline question with an offline
answer. Discovering it from silence in game is pure waste.

Specifically for anything announce-shaped, the producer/consumer split is the
trap: the announce paths are documented as three producers and two dedups, and
the consumer is always the tick. A hook set without the tick hook cannot speak.

### The counter-corollary: a KOTOR 2 workaround is a fossil too

**Learned the hard way 2026-07-31, by regressing working behaviour.**

KOTOR 2's cursor warp was written as an OS-level `SetCursorPos` because
`MoveMouseToPosition` had no KOTOR 2 address yet. When that address was found,
it looked obvious to swap the engine call back in — THE METHOD says prefer
KOTOR 1's known-good line over a reimplementation. That reasoning was wrong and
the swap regressed menu navigation to the exact bug the function exists to fix.

The address was right. The assumption was that the two functions do the same
thing, and they do not: **KOTOR 2's `CExoInput::SetMousePos` does not move the
OS cursor**, it writes only the engine's own copy, which the engine overwrites
from the true mouse on the next frame. KOTOR 1's moves the real pointer.

So the rule cuts both ways. A KOTOR 2 divergence that exists because someone
MEASURED a behavioural difference is itself a fossilised bug fix, and the fact
that the KOTOR 1 address later becomes available is not evidence that the
divergence is obsolete. Before replacing KOTOR 2-specific code with the KOTOR 1
line, ask what measurement produced it — and if the answer is in the comments,
believe them.

Practical test for telling the two cases apart:
- KOTOR 2 code written because an address was MISSING → replace it once the
  address exists.
- KOTOR 2 code written because a behaviour was MEASURED → the address changes
  nothing; leave it alone.

The cursor warp reads like the first and is the second. Its comments said so;
they were read as "temporary" when they were describing a KOTOR 2 fact.

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
- `Todo` (K2 unknown): 402
- `Same` (verified identical): 17
- `Pick` (verified different): 78
- `Kotor1Only` (no K2 counterpart): 13

Addresses — `acc::addr`:
- `R` (K2 unknown, resolves to 0): 199
- `Pick` (.text/.rdata known): 65
- `PickGlobal` (.data known): 4
- `TodoGlobal` (.data unknown, resolves to 0): 10
- `Kotor1Only` (no K2 counterpart): 2

`grep -c "Todo("` and the `R(` count are the remaining-work counters.

Menu subsystem specifically (the `port_worklist.py` invocation in WHERE TO
RESUME): 115 constants used, **110 resolved, 5 unresolved** — down from 79 at
the start of the second session. None of the 5 blocks menu reading or
navigation; see "The last 5" below.

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

### The last 5 in the menu subsystem

None of these blocks menu READING or NAVIGATION — that path is complete. Each is
listed with why it is still open and where to resume.

- **`kCGuiInGameReplyCountOffset` / `kCGuiInGameReplyTextArrayOffset`** (KOTOR 1
  +0x114 / +0x118). `CGuiInGame` has NO vtable in KOTOR 2's RTTI, so neither the
  class map nor the slot map reaches it, and no `CGuiInGame` address is resolved
  for KOTOR 2 yet. The KOTOR 1 call chain is
  `CSWSDialog::SendDialogReplyNode` → `HandleDialogReplies` → `ShowDialogReplies`
  → `SetReplyData`, and `UpdateDialog` is reached from `CSWGuiDialog::SelectReply`
  — none of them virtual under their own name. `CSWSDialog` DOES have a KOTOR 2
  vtable (0x0099A080), so `vtable_xrefs.py` gives its constructor as a foothold.
  Note the slot map's rows for `CSWGuiDialog::SelectReply` map all four dialog
  classes to one KOTOR 2 address while KOTOR 1 has two distinct ones — that
  alignment is suspect; do not trust those rows without checking.
  These belong to the DIALOG pillar rather than menus, so they can also simply
  wait for that subsystem's turn.
- **`kPartyPortraitNpcSlotOffset`.** KOTOR 2's element has four trailing dwords
  where KOTOR 1 has three, so position alone cannot pick between 0x470 and
  0x474, and neither game's `OnPanelAdded` writes it. Deliberately parked: its
  only consumer resolves through `kCompanionNamesBySlot`, a table of KOTOR 1
  story characters KOTOR 2 shares none of, so the offset buys nothing until that
  path has a KOTOR 2 name source. See also the 12-vs-9 roster note at
  `kPartyRosterSlotCount`.
- **`kAddrManagerLMouseDown` / `LMouseUp`.** Activation only, so nothing that
  reads or announces needs them. Each has exactly ONE caller in KOTOR 1 —
  `CClientExoAppInternal::PerformLButtonDownAction` / `...UpAction`, called from
  adjacent sites in `CClientExoAppInternal::HandleInputEvent`. That class is
  absent from the RTTI slot map, so the forwards-from-callers method that
  cracked `MoveMouseToPosition` needs one more hop up (`HandleInputEvent`'s own
  callers are `PlayBackInputEvents` and `ProcessInput`).
  **Do not retry this**: a structural search for "two functions called
  adjacently, each making one GuiManager-mediated call" was written and does NOT
  find them, because only ONE of the KOTOR 1 pair reaches the manager through
  the global.

### Divergences that are CODE, not constants

Found while resolving the above. These will not show up in any worklist count,
and each will silently misbehave on KOTOR 2 if the KOTOR 1 logic is reused:

- **The upgrade slot-type table index.** Stride is 0xc in both, but KOTOR 1
  packs FOUR slot types per category and indexes
  `((custom_value - 4) + category * 4) * 0xc`, while KOTOR 2 packs SIX and
  indexes `slot * 0xc + (category - 1) * 0x48`. Swapping only the base address
  reads the wrong entry. Needs an `acc::game::IsKotor2()` branch at the call
  site.
- **The party roster is 12 slots on KOTOR 2, 9 on KOTOR 1.**
  `kPartyRosterSlotCount` must stay `constexpr` (it sizes a real array), so this
  is documented at the declaration rather than converted. The current bound
  truncates on KOTOR 2 rather than reading out of range, which is the safe
  direction, but KOTOR 2 needs its own roster/name table.
- **Panels lose members.** KOTOR 2's map panel drops the compass label,
  BTN_RETURN and BTN_PRTYSLCT; its journal drops the quest-items button; its
  equip panel drops both party-portrait buttons and moves the prev/next arrows
  to the end of the struct. Marked `Kotor1Only` where a constant existed, but
  any code that ASSUMES those controls are present needs a look.

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
