# KOTOR 2 port — plan and findings

**Status: ACTIVE (started 2026-07-31).** Supersedes the conclusion of
`kotor2-port-feasibility.md`, whose *measurements* remain valid — the sigscan
result especially — but whose cost estimate predates the RTTI finding below.

## WHERE TO RESUME (read this first)

**State as of 2026-08-01 (sixth session): Batch 3 is IMPLEMENTED — address
round complete, hooks written, gates cleared, `k2_hook_status.py` reports
12 of 25 READY. Built clean, NOT tested in game, NOT committed.** The whole
address round ran offline (two parallel Ghidra rounds + capstone scans, zero
test rounds spent). See "Batch 3" under THE BATCH PLAN for everything that
resolved this session and the test round it will eventually need.

**USER DECISION (2026-08-01): the Batch 3 test round is DEFERRED.** Too much
of the in-game loop is still silent for testing to be meaningful, so two new
batches were scoped and come first: **Batch 3b — Dialog** and **Batch 3c —
Interaction** (walk-to-target, Enter-interact, action surfaces; 37 unresolved
constants, several sessions). The risk this accepts: more untested code
stacks up before the first combined test round — mitigated by keeping every
batch's offline verification at the Batch 3 bar (worklist to zero on live
paths, chain audit clean, byte-confirmed cuts). Do not let a KOTOR 1
regression run slip much further — Dispatch has now been restructured THREE
times without one.

**Batch 3b — Dialog is IMPLEMENTED (2026-08-01, same session): 12/12
constants resolved, slot rows closed, dialog_speech phase live on both
games, built clean. NOT tested, NOT committed.** See its section under THE
BATCH PLAN for every witness. Next session: **Batch 3c — Interaction** (its
scoping is in its section; start from the engine_picker descriptor table and
the action-queue primitives, whose K1 reference addresses are all in
engine_picker.cpp / guidance_autowalk.h).

Batch 1 (GUI spine) is TESTED AND WORKING — the user confirmed menu navigation,
Options + sub-panels, listbox rows and speech on KOTOR 2. Batch 2 (in-game GUI
lifecycle) is the four hook targets with byte-confirmed cut points plus
Show/Prev/SetInputClass (see "The four hook addresses — ALL IDENTIFIED" under
Batch 2).

**Batch 2 is IMPLEMENTED (2026-08-01, same session) — built clean, NOT yet
tested in game, NOT committed.** `k2_hook_status.py` now reports **9 of 25
READY** (GUI spine + the four in-game-GUI-lifecycle handlers). What landed:

- `kotor2.hooks.toml`: five new entries — Switch @0x007CA575, Hide
  @0x007CA066, SetSWGuiStatus @0x007C9C46, and BOTH AppendToMsgBuffer rings
  @0x007BE093 / @0x007BE1B3 sharing one wrapper. All cuts byte-confirmed.
- K2 wrappers: `OnSwitchToSWInGameGuiK2` / `OnHideSWInGameGuiK2` /
  `OnSetSWGuiStatusK2` at the bottom of `engine_subscreen.cpp`,
  `OnAppendToMsgBufferK2` at the bottom of `msg_router.cpp`; exported. The
  address-style handlers keep their caller_eip trick because EBP+8 IS the
  esp+4-LEA address and [EBP+4] the return address.
- Constants: PrevSWInGameGui, HideSWInGameGui, SetInputClass (facade
  0x0073FEE0), SetSWGuiStatus, GetPlayerCreature (facade 0x0073F450),
  GetServerCreature (Pick 0x0060FB20 / 0x0077D800), GetLoadFromSaveGame
  (Pick 0x004af050 / 0x0051CDE0, via facade-cluster alignment), input_class
  +0x9C now a named Same constant (`kClientInternalInputClassOffset`,
  engine_app.h), slot rows InGameMenu Same(0x8) and MainInterface
  Pick(0x90, 0x98) (+ the canonical `kGuiInGameMainInterfaceOff`).
- `GetPlayerServerObject` gained a K2 branch calling the engine's own
  `CSWCCreature::GetServerCreature` instead of the K1 field read (+0xf8 is
  unestablished on K2 and the resolver is layout-proof). This makes
  `GetPlayerPosition` REAL on KOTOR 2 — without it the msg handler's replay
  gate would have silently suppressed every feedback line.
- Gates cleared: the three in `engine_subscreen.cpp` + msg_router's. NOT
  OnSetPauseState (Batch 4). `handler_chain_audit.py` over the whole chain
  set: 1 flagged line, in K1-gated `TickCombatLog` — unreachable on K2.
- Deliberately deferred: kAddrSetPauseState / kAddrSetSoundMode /
  kAddrExoSoundPtr stay unresolved. Their only consumers
  (TickInputClassReassert → DispatchUnpauseCleanup, tutorial_popup) are
  K1-gated ticks. CAUTION for whoever resolves SetSoundMode: KOTOR 2's
  (0x0070BC60, ExoSound global 0xA1B494) takes TWO args where KOTOR 1's
  takes one — banking the address without adapting the call corrupts the
  stack.

**Crash found by the first Batch 2 test attempt (2026-08-01, FIXED, needs
retest):** opening the chargen NAME field on KOTOR 2 crashed the process —
WER: c0000005 in accessibility.dll @0x198511. The faulting line was
`TryPartyPortrait`'s vtable read, which ran BEFORE its own `__try`: the
panel-walk's control array held a non-null garbage entry after the name
panel's last real control, every OTHER extractor in the ladder faulted
quietly inside its own guard, and this one unguarded head killed the
process. `TrySpeculativeVtableRead` had the identical unguarded head; both
now read the vtable under SEH and skip the control. Same crash class as
Batch 1's FocusProbe lesson — and NOT an input-field/editbox gap: the
editbox handler correctly declines on KOTOR 2 (typing is not narrated yet;
that surface comes with its own batch). The K1 crash-history dumps show
three identical chargen crashes on the Batch 1 build the evening before, so
this predates Batch 2 entirely.

### The poison only degrades safely if nothing FORMS A POINTER from it

**Learned 2026-08-01, by crashing on the first in-world arrow key — and the
most important structural lesson since the batch plan itself, because it
affects every remaining batch rather than one feature.**

`acc::off::Todo()` poisons to `kUnportedOffset` (0x7BAD0000) so a premature
read faults instead of silently returning a neighbouring field. That contract
holds for a READ. It does NOT hold for `base + offset`, which is ordinary
arithmetic yielding a wild but emphatically **non-null** pointer — and a
non-null pointer passes every `if (!p)` check between there and whatever
finally dereferences it, possibly in another TU.

The crash: an arrow key with a dialogue panel foreground. Panel identity
resolved fine (DialogCinematic is a ported slot), the listbox spec matched,
and its finder returned `panel + kDialogRepliesListBoxOffset` — still `Todo`.
The caller's `lb && ...` guard passed, and `DriveListBoxSelection` — which had
no SEH, because on KOTOR 1 that pointer is always real — dereferenced it.

**Why KOTOR 1 never saw this, though the code is identical:** on KOTOR 1 every
one of these offsets is a real value, so the pointer is always valid and the
missing guard never mattered. The defect cannot fire there. It is not
"unported in-world logic misbehaving" either — that class degrades correctly
by design. It is the *decline mechanism itself* having a hole, which is why
porting more code would not have fixed it: every future `Todo` offset used
this way crashes the same way, and Batch 3 alone carries ~60.

The fix, in three parts:

- `acc::off::Ok(off)` and `acc::off::Ptr(base, off)` in
  `engine_offsets_select.h`. **Use `Ptr` wherever an interior pointer is
  RETURNED or STORED** rather than read immediately under SEH; it converts a
  wild pointer into an honest null that existing guards handle.
- `DriveListBoxSelection` / `DriveListBoxSelectionEngine` now run their bodies
  under SEH (split into `*Body` helpers, since C2712 forbids objects in a
  `__try` frame). They are engine reads and every other engine read here is
  guarded.
- Converted the reachable-on-KOTOR-2 pointer-formers: the three listbox
  finders in `menus_listbox.cpp`, `GetServerPartyTable` (its comment
  explicitly reasoned "address arithmetic only, no guard needed" — precisely
  the assumption that breaks), and `PlayerVarTable`.

Still on raw addition, deliberately: the finders in `engine_radial.cpp`,
`engine_actionbar.cpp` and `minigame_pazaak.cpp`. Their modules are gated on
`IsKotor1()`, so they are unreachable on KOTOR 2 today — convert them when
their batch clears the gate, and prefer `Ptr` in new code from the start.

**The test round this batch needs (chargen + arrow-key crash fixes included,
retest from step 0):**

0. Character creation: open the name field (crashed before the fix), type or
   take the default/random name, proceed into the world. Typing is NOT
   expected to speak yet — the editbox surface is a later batch; the field
   itself, the buttons around it and the rest of chargen should navigate
   and speak, and nothing may crash.

1. `kdev apply --game k2`, launch, load into the world.
2. Sub-screen lifecycle: open Equipment/Inventory/Map/Journal by hotkey,
   switch between them by hotkey while one is open (the Switch handler's
   PrevSWInGameGui cleanup), Esc to close, Esc from world into the pause
   menu. Watch `SubScreen.Switch` / `SubScreen.Status` / `SubScreen.Hide`
   log channels; the Switch first-fire line proves the hook installed.
3. Feedback lines: pick up loot / get hit in combat and confirm spoken
   output + `Combat.MsgBuf raw:` lines for BOTH rings (combat and feedback
   categories land in different rings — exercise one of each).
4. Save-load replay suppression: quick-load and confirm the historical
   lines log as `replay-suppressed` rather than speaking.
5. KOTOR 1 regression pass afterwards (shared code moved: GetInputClass
   ungated, MainInterface offset now Pick, GetPlayerServerObject branch,
   four handlers ungated): menus + one in-world area, sub-screen open/
   close, one combat message.

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
- `k2_caller_trace.py` — call-site census with pre-call context windows,
  forwarder-shape scan, and accessor-follow method tally. Caller COUNTS are a
  cross-game fingerprint usable before any decompile round; this is what
  identified all four Batch 2 hooks.

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

*Started 2026-08-01.* The constant surface is tiny — `port_worklist.py` over
`engine_subscreen.cpp` + `msg_router.cpp` reports **7 constants, 3 resolved, 4
unresolved**, and all four belong to `tutorial_popup.cpp` / the combat-pause
setter rather than to the handlers themselves. So Batch 2's real work is the
slot table and the four hook addresses, not offset archaeology.

#### The CGuiInGame slot table — RECOVERED (2026-08-01)

29 of ~35 slots, by a method that needs no decompiler and is now a tool:
`tools/re-scripts/k2_slot_table.py`. RTTI names each panel class's vtable; the
constructor is whoever stores that vtable into `[this]`; CGuiInGame's creator
(`0x007BE4C0`, with a smaller second creator at `0x007D0760`) calls each
constructor and files the result into its own slot. KOTOR 2's unoptimised build
is what makes the last step tractable — every intermediate lands in a named
stack temporary, so following the returned pointer to its `mov [this+off], reg`
is a short chain of `mov`s.

    python tools/re-scripts/k2_slot_table.py C:/Tools/k2re/swkotor2.exe \
        docs/llm-docs/re/k2/k2-functions.csv docs/llm-docs/re/k2/k2-vtables.csv \
        0x007be4c0 0x007d0760

**The result is the port's structural model in miniature: identical up to
+0x74, then KOTOR 2 inserts members and everything above shifts.** Do NOT mark
this table `Same` wholesale.

Unchanged (K1 == K2): Equip 0x0c, Inventory 0x10, Abilities 0x18, Journal 0x20,
Map 0x24, Options 0x28, DialogCinematic 0x40, DialogComputer 0x44, BarkBubble
0x4c, Examine 0x50, Container 0x54, CreateItemMenu 0x58, CreateItemSubMenu
0x5c, DialogLetterbox 0x60, Fade 0x6c, LoadModuleDebugMenu 0x70,
PowersFeatsSkillsDebugMenu 0x74, InGamePause 0x7c, Store 0x84.

Moved — and these are the ones that would misclassify silently:
- **InGameMessages 0x1c → 0x78.** Worse than a shift: KOTOR 1's 0x78 is
  PartySelection, so a stale table maps the two onto each other.
- SoloModeQuery 0x8c → 0x94, AreaTransition 0x94 → 0x9c, MessageBox 0x98 →
  0xa0, SkillInfoBox 0x9c → 0xac, TutorialBox 0xa0 → 0xb0, StatusSummary
  0xa8 → 0xb8.
- **0x14 is CSWGui3DSceneView on KOTOR 2**, where KOTOR 1 has InGameCharacter.
  KOTOR 2 also files 3DSceneView at two further slots. Check what KOTOR 2's
  character sheet actually is before mapping `InGameCharacter` at all.
- 0x48 is CSWGuiBlackenedLabel, where KOTOR 1 has DialogComputerCamera.

Still open: InGameMenu (its call at 0x007BEF23 does not follow the common
dataflow shape), one of the three MessageBox instances, GalaxyMap,
MainInterface, ControllerLossBox, DialogCinematicCopy, and the two
DialogMessages routing slots. The unresolved entries from the second creator
are duplicates of classes the first creator already settled, so they cost
nothing.

**The table is now IN the code** (engine_panels.cpp): 22 rows `Same`, 6 `Pick`,
10 `Todo`. `SlotTableLookup` runs on both games again — Todo rows poison to
`kUnportedOffset` and are skipped alongside the no-slot sentinel, so an
unported row costs its kind the slot-table route and falls through to the
structural / vtable detectors, never a fault that would abandon the walk.

#### The CGuiInGame pointer chain — also settled, from the same listing

The table is useless without a correct `CGuiInGame*`, and that chain fell out
of the creator's own callers. Two forwarders lead to it:

    0x0073F870:  MOV ECX,[this+0x04]  → call 0x0078C330
    0x0078C330:  MOV ECX,[this+0x40]  → call 0x007BE4C0   (the panel creator)

So `kClientExoAppInternalOffset` (0x4) and `kClientExoAppGuiInGameOff` (0x40)
are both confirmed identical on KOTOR 2, witnessed by a chain that provably
ends at the very object the slot table was read out of. `kClientExoApp-
InternalOffset` moved Todo → Same; the SERVER-side twin did NOT — same shape
is not evidence, and it has no witness yet.

**Unblocking a chain root unblocks its consumers, and that needs auditing.**
While `GetClientAppInternal()` returned null on KOTOR 2, everything downstream
failed safe for free. It no longer does. Most consumers stayed safe because
their own offsets are still `Todo` (kClientAppOptionsOffset and friends poison,
the read faults, SEH returns null) — but two did not, and both are now handled:

- `GetInputClass` reads a RAW `+0x9c` literal, not a marked constant, so it
  would have returned a plausible integer from the wrong field rather than
  failing. Now declines on KOTOR 2 until the field is resolved. Raw literals
  are invisible to `port_worklist.py`, which is why this needed reading rather
  than counting.
- `SetGuiInputClass` called its engine setter with no `acc::addr::Ok()` check
  (unlike its sibling `CloseInGameMenuToWorld`), so it would have faulted into
  its own SEH on every call instead of declining cheaply.

#### The four hook addresses — ALL IDENTIFIED (2026-08-01, fourth session)

Every Batch 2 hook target now has a KOTOR 2 address, each confirmed by
decompiling the KOTOR 2 candidate and the KOTOR 1 original in parallel Ghidra
rounds and matching structure landmark by landmark (sound-mode calls, script
names, the status switch, the ring-shift loop). Offline only — none of this
has run in game yet.

The route that worked, in order:

1. The `case 0xdf` prediction from last session was right in substance:
   scanning for `PUSH 7` immediately before a call through the `[this+0x40]`
   CGuiInGame chain found the Esc path inside one large 5-caller function —
   `CClientExoAppInternal::HandleInputEvent` = **0x007B12C0** (K1 0x00621210).
2. Caller-COUNT fingerprints then matched K1 to K2 before any decompile:
   K1 SwitchToSWInGameGui has 9 sites (1 in HandleInputEvent + 8 per-GUI-id
   trampolines); exactly one K2 candidate has 9 sites in the same pattern
   (8 id-wrappers at 0x757A50..0x757BA0 pushing ids 0-7). Same logic paired
   Show (3 vs 4 sites, two inside the dispatcher in both games).
3. One Ghidra round per game (11 K2 functions + 6 K1 references, run in
   parallel) settled every identity. The decompile pairs read like the same
   source compiled twice.

**The four hooks (KOTOR 1 → KOTOR 2), with byte-confirmed cut points.** All
cuts are frame-relative with no relative operands; at every cut ECX still
holds `this` and the params sit at [EBP+8]/[EBP+0xC]/[EBP+0x10], so the
Batch 1 frame-unpacking-wrapper pattern (ECX + EBP sources) applies directly.

- **SwitchToSWInGameGui** — 0x0062cf10 → **0x007CA550**. Cut at **0x007CA575**,
  7 bytes `89 4d e0 83 7d 08 00` (`MOV [EBP-0x20],ECX` + `CMP [EBP+8],0`).
  Stops before the `JL`; the trampoline replays the CMP right before the JL,
  the same flags-across-handler shape as Batch 1's HandleFocusChange. Like the
  K1 hook, this fires pre-guard (K1's 0x0062cf2d cut also precedes the range
  checks). `this`=ECX, GUI_id=[EBP+8].
- **HideSWInGameGui** — 0x0062cba0 → **0x007CA060**. Cut at **0x007CA066**,
  6 bytes `89 4d d4 8b 45 d4`. `this`=ECX, param_1=[EBP+8].
  CORRECTION: last session's forwarder note characterised 0x007CA060 as a
  "status getter" because its return is tested. It is Hide — the tested
  return is K1's own `if (HideSWInGameGui(0)) SetInputClass(0,1)` pattern.
- **SetSWGuiStatus** — 0x0062aa00 → **0x007C9C40**. Cut at **0x007C9C46**,
  6 bytes `89 4d fc 8b 45 08`. `this`=ECX, status=[EBP+8], p2=[EBP+0xC].
  The status machine is byte-identical to K1's (cases 1-4, values 1/2/3);
  **sw_gui_status lives at +0x34 on KOTOR 2.**
- **AppendToMsgBuffer** — 0x0062b5c0 → a PAIR: **0x007BE090** (67 call sites)
  and **0x007BE1B0** (23 call sites). KOTOR 2 split KOTOR 1's single message
  ring (78 callers) into two category rings — same body otherwise: empty-string
  guard, 0x40-capacity ring, 16-byte stride, shift-down loop, then store of
  (CExoString msg, dword type at +8, byte color at +0xC) and count++. Same
  `(CExoString*, ulong, byte)` signature, `ret 0xc`. Ring A: buffer ptr at
  gui+0x110, count at +0x11C. Ring B: ptr +0x118, count +0x124 (K1: +0xF8 /
  +0x100 — the +0x20 shift is why the K1-offset fingerprint scan failed).
  **Hook BOTH with the same handler** to reproduce K1 coverage; the dense
  sequential caller block at 0x82E000-0x830000 feeding ring A is the K2 twin
  of K1's 0x653000-0x665400 feedback-builder block. Cut for both at entry+3:
  **0x007BE093** / **0x007BE1B3**, 6 bytes `83 ec 10 89 4d f0` each.
  msg=[EBP+8], type=[EBP+0xC], color=[EBP+0x10].

**Identified alongside, needed by the same handlers:**

- `ShowSWInGameGui` — 0x0062c9b0 → **0x007C9DF0**. Confirmed by
  SetSoundMode(4), `"k_sup_guiopen"`, SetSWGuiStatus(3,1), the CanLevelUp
  default-panel branch — every K1 landmark in order.
- `PrevSWInGameGui` — 0x0062cdf0 → **0x007CA3C0** (decrements last_gui_panel,
  wraps -1→7). Its twin **0x007CA230** is NextSWInGameGui (wraps 8→0). Our
  Switch handler calls Prev; do not swap them.
- `CClientExoAppInternal::SetInputClass` = **0x007B3050**, and the input-class
  field is confirmed at **+0x9C** — which unblocks the two guards noted below
  (`GetInputClass`'s decline, `SetGuiInputGuiClass`'s missing Ok() check).
- `CClientExoApp::GetInGameGui` = **0x0073F750** (632 call sites — the
  app-wide accessor; body is exactly the documented `[this+4]` → `[+0x40]`
  chain). `GetSWGuiManager` = **0x0073FEA0**.
- `UpdateCreatedInGameGUI` = **0x007D0760** — last session's unexplained
  "smaller second creator" is this; both Show and Switch call it with
  (old_id, new_id).
- CSWGuiManager methods on KOTOR 2: AddPanel **0x00410530**, RemovePanel
  **0x00410670**, SendPanelToBack **0x00410780**, PanelExists **0x00410800**,
  PlayGuiSound **0x004122A0**. `CExoSoundInternal::SetSoundMode` =
  **0x0070BC60** (ExoSound global at 0xA1B494).
- CGuiInGame KOTOR 2 fields witnessed in the decompiles: in_game_menu +0x8,
  panel slot table from +0xC (as recovered), last_gui_panel +0x2C, gui-open
  flag +0x30, sw_gui_status +0x34, manager +0x38, in_game_pause +0x7C,
  **main_interface +0x98** (a slot the table had open — witnessed by
  SetSWGuiStatus adding/removing it on status 1), message rings
  +0x110/+0x118 with counts +0x11C/+0x124, initialized +0x128, and the twins
  of K1's +0xB38/+0xB3C pause-mode pair at **+0xF18/+0xF1C**.
  The InGameMenu slot is +0x8 on both games (witnessed by Show/Hide panel
  adds), closing another open row.
- Ruled out while searching: 0x007CBB40 is the fade starter (fade panel slot
  +0x6C, 20 callers), 0x007D0AF0 is a hide-request refcounter at +0xF0
  (17 callers), 0x007CE740 is a 16-byte item-notification setter at +0x100C.

The dispatcher's identity is triple-witnessed: 5 callers (K1's is called from
ProcessInput/PlayBackInputEvents), the case-0xdf Esc path with the in-world
guard and Show(7), and the hotkey triple (same-id → Hide via helper,
different-id → Switch, in-world → Show) at 0x7B1F70-0x7B2010 matching K1's
case 0xd1-0xd8 line for line.

The scan tooling from this session is promoted to
`tools/re-scripts/k2_caller_trace.py` (call-site census + forwarder shapes +
pre-call context windows for a target list; the census halves are what turned
caller COUNTS into a fingerprint usable before any decompile).

The seven still-`Todo` slots (InGameCharacter, GalaxyMap, PartySelection,
ControllerLossBox, DialogCinematicCopy, DialogComputerCamera, the
DialogMessages pair) can follow, or wait for their subsystem's batch.

**Batch 3 — World, area, transitions.** `OnSetMoveToModuleString`, `OnDoorOpen`,
`OnShowObject`; gates in `transitions.cpp`, `door_announce.cpp`,
`passive_narrate.cpp`. Largest offset surface — `engine_area.h` alone holds ~60.

*Started 2026-08-01 (fifth session). OFFSET FOUNDATION LANDED, gates NOT yet
cleared — not testable yet.* The closure worklist went **17→80 resolved of 137**
(`port_worklist.py` over the 43-file Batch 3 closure). Everything the area /
object / door / waypoint / trigger / placeable / path-graph read paths touch is
now resolved from decompiled load/save/ctor witnesses, not derivation:

- **CSWSArea** (loader 0x00523870, dtor 0x0052b4e0, GetRoom 0x0054b1d0): the
  game-object list and rooms array consolidated on K2 — game_objects
  0x190→0x194 / count 0x194→0x198, rooms 0x230→0x254 with its count 0x268→0x250
  now *adjacent*, room_names 0x25c→0x280 (stride still 8), name 0x150→0x154,
  tag 0x158→0x15c. Path graph: points count/ptr 0x238/0x23c→0x25c/0x260,
  connections 0x240/0x244→0x264/0x268 (per-point layout unchanged). GetRoom
  address 0x004BB600→0x0054b1d0.
- **Objects**: tag 0x18 Same; script_var_table 0x100→0x104, fixed CSWVarTable
  0x110→0x114 (both witnessed at the object serializer thunk 0x00540660, one
  slot apart). Door band shifted large (LocName 0x39c→0x3ec, GenericType
  0x2a1→0x2e1, Locked 0x2c4→0x304, OpenState 0x2cc→0x31c, Static 0x3c0→0x410,
  TransitionDest 0x3c8→0x418). Trigger / waypoint / placeable bands took a
  uniform **+0x40** (LocName 0x228→0x268, waypoint map-note 0x228/0x22c/0x230
  →0x268/0x26c/0x270, placeable Useable 0x328→0x380 / HasInventory 0x324→0x37c
  / ItemRepo 0x36c→0x3c4, trigger geometry 0x284/0x288→0x2c4/0x2c8, IsTrap
  0x2bc→0x2fc). CreatureStats FirstName 0x14→0x34.
- **The whole walkmesh mesh block is `Same`** — witnessed byte-for-byte in K2's
  BWM writer (0x005ea490) and CSWSRoom ctor (0x005ff440): surface mesh +0x3c,
  verts +0x54, face_count +0x58, faces +0x60, materials +0x64, adjacencies
  +0x88, stride 0xc. Base-engine walkmesh code KOTOR 2 did not touch.
- **CSWPartyTable** (SaveTableInfo 0x005fb1a0): server-internal→table
  0x1b770→0x1f0b4, member ids 0x4→0x8 (the +4 slot became num_puppets),
  solomode 0x190→0x238.
- **CSWSScriptVarTable API** (var-table cluster 0x005e6580..): GetInt
  0x0059a530→0x005e67d0, GetString →0x005e6850, SetInt →0x005e6a00, SetString
  →0x005e6ce0; CExoString dtor →0x00733780. Area map fog grid `Same`
  (+0x8/0xc/0x18/0x1c), module→areamap 0x218→0x238.

New offline RE tools this session (all in `tools/re-scripts/`):
`string_xref_stores.py` (GFF field-name → struct store: the workhorse for
load/save offsets on K2's unoptimised build) and `call_sites.py` (caller-side
`add ecx, <offset>` census — how the party-table root and object var-table
offsets fell out). The three hook cut points are byte-confirmed off the exe
(see below) but NOT yet written to `kotor2.hooks.toml`.

**THE ADDRESS ROUND IS DONE (2026-08-01, sixth session).** Every blocker from
the three tiers below resolved offline, each with an independent witness:

1. **Camera tier — RESOLVED.** The vtable slot map (`k2-vtable-slots.csv`)
   pairs `Gob::GetPosition`/`GetOrientation` and `Camera::GetPosition`/
   `GetOrientation` across the games; disassembling the K2 accessors read the
   fields straight off: Gob position `+0xa4`, quaternion `+0xb0` (K1 `+0x78`/
   `+0x84`), Gob still embedded at `Camera+0x4`. `kCameraGobPositionOffset` =
   Pick(0x7c, 0xa8), `kCameraOrientationOffset` = Pick(0x88, 0xb4). The
   chain roots got witnesses too: `kClientInternalModuleOffset` Same(0x18)
   (K2 GetModule facade internal 0x00726F80), `kCSWCModuleCameraOffset`
   Same(0x40) (GetModuleCamera internal 0x00781810), and
   `kServerExoAppInternalOffset` Same(0x4) (SetMoveToModuleString reads
   [this+4]). The pitch/yaw block near `+0x204` from last session's note was
   a red herring — the quaternion is the yaw source, as on KOTOR 1.
2. **Walls / map tier — RESOLVED where reachable.**
   `CSWCollisionMesh::LocalToWorld` → **0x005EE7B0** (found via K2
   ShowObject's renderDEV door path making K1's exact 18-call pattern;
   body verified: world_coords identity head, position +0x2c, quaternion
   +0x38). `CSWSObject::GetArea` → **0x005453C0** (calls the two
   already-banked K2 twins — GetObjectArray facade 0x0051C080 +
   CGameObjectArray::GetGameObject 0x0053DFB0 on [this+0x90] — and sits
   before GetGender exactly as K1's does). Client `GetGameObject` →
   **0x0073F4D0** (facade-cluster alignment, confirmed by its body calling
   the banked 0x0053DFB0 on [internal+0x14]). `GetObjectName` →
   **0x0073F0E0** (19-facade walk-back, every intermediate body matching its
   K1 role). Map-pin / fog accessors stay `R()` — map_ui_cursor remains
   K1-gated (its own offsets are still Todo), and every touch point is
   SEH-guarded.
3. **Party-roster tier — RESOLVED.** `CSWPartyTable::GetNPCObject` →
   **0x005FAAF0**, decompile-matched line for line (avail check, cached id
   at table+0x1c+slot*4, template-load with CSWSCreature(0x7f000000,0), the
   +0x9c dead-check + resurrection, <0xc bounds for K2's 12-NPC roster).
   CAUTION: its sibling **0x005FAD70 is the K2-only PUPPET variant** (cache
   at +0x14c, 3 slots) — same shape, do not confuse them.
   `GetIsNPCAvailable` → **0x005FA960** (avail array table+0x4c).
   `GetNPCSelectability` has NO confirmed twin — 0x005FA9C0 (array +0x11c)
   lacks K1's avail gate / 0xff default and may be K2's influence accessor;
   it stays `R()` and PartyTableIsNPCSelectable declines under SEH.
   Bonus: `kAddrCClientExoAppInternalHandleInputEvent` became
   Pick(0x00621210, 0x007B12C0) — the Batch 2 dispatcher — so the Q/E
   synthetic-retry path is live.

**What landed in code (sixth session):**

- `kotor2.hooks.toml`: three new entries — SetMoveToModuleString @0x0051BFD9
  (7-byte cut), DoorOpen @0x00619DAA (6 bytes), ShowObject @**0x00798D98**
  (6 bytes; ShowObject pinned to **0x00798D70** this session by decompile:
  SetMainInterfaceTarget head, LookAt null path, hostile-hilite array). All
  three cuts byte-verified off the exe; all frame-relative, EBP-only params.
- K2 wrappers `OnSetMoveToModuleStringK2` (transitions.cpp),
  `OnDoorOpenK2` (door_announce.cpp), `OnShowObjectK2` (passive_narrate.cpp)
  — EBP frame-unpacking, exported. The ShowObject wrapper computes the
  handle itself (obj at [EBP+8], id at obj+0x4 — byte-witnessed in
  0x00796B50); K1's cut had it precomputed in EAX. SetMoveToModuleString's
  K2 param is a clean frame VALUE — the K1 LEA-double-deref does not apply.
- Gates cleared: the three handlers lost `HandlerEnabled()`, and Dispatch()
  now runs `passive_narrate`, `camera_announce`, `door_announce`,
  `spatial.change_detector`, `transitions` on BOTH games (order preserved;
  camera_orient + camera_spin_guard stay K1 — the spin guard belongs to the
  ACTIVE edge-turn driver, unported; locked_recall / discovery / view_mode /
  map_ui_cursor / trap_watch stay K1).
- `handler_chain_audit.py` over the whole Batch 3 closure: 3 flagged lines,
  all the TrapDetectedByAnyOf offset ASSIGNMENTS whose reads are SEH-guarded
  two lines later — the degrade-by-design path, no action.
- `port_worklist.py` over the five now-live tick TUs: **0 unresolved**.
- `k2_hook_status.py`: **12 of 25 READY** (+ its shim table now knows the
  three Batch 3 wrapper names).

**Deliberately NOT resolved, with reasons:** GetNPCSelectability (identity
unproven, party-select screen only); the map-pin/fog cluster (map_ui_cursor
stays gated); `MaybeDrivePassiveSelection`'s IsGlobalFading /
DoPassiveSelection (K1-story fade workaround; its own local Todo chain
poisons first, so it declines safely on K2). Suspected but unbanked: K2
DoPassiveSelection ≈ 0x0079A4C0 and SelectNearestObject ≈ 0x0079B700 — the
only two functions calling K2 ShowObject (7 and 2 sites), matching K1's
caller pair by size and role; confirm before use.

**The Batch 3 test round (KOTOR 2):**

1. `kdev apply --game k2`, launch, load into the world.
2. Module transition: walk through an area-transition door. Expect the
   pre-load destination announce (`Transition` channel, the
   OnSetMoveToModuleStringK2 first fire) and the post-load area announce
   from transitions::Tick.
3. Room topology: walk between rooms; expect room announces (wall cache +
   GetRoom + path-graph offsets all landed last session; LocalToWorld now
   live for the wall scan).
4. Door facing: open a door as the leader; expect the facing readout
   (`DoorAnnounce` channel — proves OnDoorOpenK2 + camera yaw).
5. Q/E targeting: cycle targets in and out of combat; expect spoken target
   names (proves OnShowObjectK2 + GetNPCObject + GetObjectName + the
   party-roster filter).
6. Watch the log for `probe faulted` / SEH-decline lines naming anything
   still unported that got reached.
7. KOTOR 1 regression pass afterwards (Dispatch restructured, four shared
   constants became Pick, three handlers ungated): menus, one in-world
   area with room/door announces, Q/E, one module transition.

**Byte-confirmed K2 hook cut points (design record; write these when the gates
above are cleared):**

- **OnSetMoveToModuleString** — `CServerExoApp::SetMoveToModuleString`
  0x004aecd0 → **0x0051bfd0** (writes `internal+0x1008c`, 7 callers incl. the
  door-transition EventHandler; K1 had 6). Unoptimised prologue; hook after it
  at **0x0051bfd9**, cut `c7 45 fc 00 00 00 00` (7 bytes, `mov [ebp-4],0`).
  `this`=[EBP-0xc] (facade, already stored), dest CExoString*=[EBP+8] — a clean
  frame param, so **no LEA-vs-MOV double-deref** unlike K1. Needs an in-game
  first-fire check: the setter identity is caller-count-inferred, not yet
  landmark-confirmed.
- **OnDoorOpen** — `CSWSDoor::OpenDoor` 0x00589ceb → **0x00619d00**. The opener
  stamp is `mov [eax+0x36c],ecx` at 0x00619db0 (K1 stamped `[esi+0x31c]`). Hook
  at **0x00619daa**, cut `8b 45 c4 8b 4d 08` (6 bytes, `mov eax,[ebp-0x3c]` +
  `mov ecx,[ebp+8]`) — both frame-relative. `this`=[EBP-0x3c], opener id=[EBP+8].
- **OnShowObject** — `CClientExoAppInternal::ShowObject` 0x005f9c60 → **not yet
  pinned**. K2's `SetMainInterfaceTarget` is **0x007ce710** (reads `[this+0x98]`
  main_interface, matching the Batch 2 `main_interface +0x98` finding); its head
  wrapper 0x00796b50 computes `param_1 ? param_1->id : 0x7f000000` exactly like
  K1's ShowObject head. The K2 ShowObject that calls it through the
  `internal+0x40 → CGuiInGame` chain is the hook target — decompile 0x00796b50's
  callers (0x007401f0, 0x00798d70, 0x007b3050) to find it. K1 hooks mid-function
  at 0x005f9c8e reading EBX=obj / EAX=id; K2's unoptimised frame will expose
  both as `[EBP±x]`.

The offset conversions are safe on both games (`Pick` returns the K1 value on
KOTOR 1, identical to the prior `Todo(k1)`; KOTOR 2 handlers stay gated), so this
session's build changes nothing observable — it is pure offline groundwork that
turns Batch 3's remaining cost into one address round plus the gate-clear.

**Batch 3b — Dialog (ADDED 2026-08-01; user decision: this and 3c come BEFORE
the Batch 3 test round, since in-game testing is not meaningful while
conversations and interaction are silent).** The original plan cut batches
along HOOK lines and dialog/interaction are poll-driven, so they never got a
number — that was a planning gap, not a judgment that they come later.

*IMPLEMENTED 2026-08-01 (same session, one offline round — two parallel
Ghidra rounds + capstone/scan work, no test rounds). All 12 constants in the
dialog_speech closure resolved, each with an engine witness:*

- **Panel layouts from K2 constructors** (vtable_xrefs.py → ctor → tag
  wiring): DialogCinematic ctor 0x008BBA80 wires "LB_REPLIES" EMBEDDED at
  panel+**0x2760** and "LBL_MESSAGE" at +**0x2A50** (K1 0x19c4/0x1ca4;
  embed-not-pointer confirmed by the ctor's virtual call through the
  embedded control's own vtable). DialogComputer ctor 0x008BC620 wires
  "LB_MESSAGE" at +**0x35FC** (K1 0x2cfc) and repeats LB_REPLIES at 0x2760,
  confirming the shared CSWGuiDialog base layout on K2.
- **Reply block: the collision trap confirmed and resolved.** K2 SetReplyData
  = **0x007C0C70**, found by its unique 19-param `ret 0x4C` signature (one
  hit in the whole GUI range); body is K1's sixteen parallel arrays, types
  and order identical, all +0x20: count **gui+0x134**, text array
  **gui+0x138** (K1 +0x114/+0x118 — which on K2 are message-ring fields, as
  predicted).
- **Speaker block +0x20 too**: K2 HandleDialogEntry = **0x007CBF60** (unique
  `ret 0x5C`; identity by fade latch → fade starter 0x007CBB40, the
  SetReplyData loop, TLK gender dance, camera dispatch — which uses behavior
  id 0x106D where K1 uses 0x106A, noted for the camera batch). Speaker
  **+0x190**, listener +0x194, previous +0x198/+0x19C, latch +0x1A4.
- **CSWSObject.dialog_owner = Same(0x54)** — K2 setter twin 0x00546DB0 is
  K1's one-line store, called 3× from the K2 server dialog cluster
  (0x006C5xxx, located via the "EndConverAbort" GFF label). So the
  CSWSObject insertion sits ABOVE +0x54.
- **CreatureStats: Race 0xdc→0xe0, Appearance_Type 0x186→0x194**, each
  double-witnessed in the K2 stats GFF loader (0x006AFED0) and saver
  (0x006B3D10) via string_xref_stores.
- **BarkBubble object id 0x1c0→0x1CC** — K2 Draw (0x008BE740, vtable-slot
  paired) guards it against 0x7f000000, resolves through the client
  GetGameObject facade 0x0073F4D0 (Batch 3's find, mutually confirming) and
  runs the `< 36.0` six-metre-squared cull, K1's exact shape.
- **Slot rows closed**: DialogCinematicCopy = **Same(0x3c)** (it is the
  ACTIVE-dialog-panel pointer, not creator-built; witnessed by helper
  0x007CB750). DialogComputerCamera = **Same(0x48)** — the creator stores
  the 0x008BD910 ctor result at [gui+0x48]; **Batch 2's "0x48 is
  BlackenedLabel" note was a slot-table-tool misattribution** (the
  BlackenedLabel allocation follows immediately). The DialogMessagesAux/
  DialogMessages rows (K1 0xf8/0xfc) stay Todo deliberately: no consumer
  logic exists, and unresolved rows fall through to the vtable detector.
- Gates: no per-file declines existed; Dispatch's dialog_speech phase now
  runs on both games. One poison-pointer conversion applied along the way
  (the chargen-feats description-listbox finder in menus_listbox.cpp formed
  a raw base+Todo pointer and the feats panel can classify via RTTI on K2).
- No new hooks, as scoped. `port_worklist.py dialog_speech.cpp`: **12/12,
  0 unresolved**; chain audit over the dialog files: clean after the Ptr
  conversion.

The Batch 3b test items (fold into the combined round): talk to an NPC —
entry text + replies spoken, reply arrow-navigation and selection; a
computer/droid terminal (LB_MESSAGE path + the copy-slot alias); an
overheard NPC bark (bark-bubble path + speaker classification).

**Batch 3c — Interaction: walk-to-target, Enter-interact, action surfaces
(ADDED 2026-08-01, same decision).** The big one — `port_worklist.py` over
`interact_dispatch.cpp, input_poll_router.cpp, guidance_approach/autowalk/
beacon/description/pathfind.cpp, narrated_target.cpp, cycle_input.cpp,
engine_picker.cpp, engine_actionbar.cpp, engine_player_inputlock.cpp`
reports **46 constants, 37 unresolved**, concentrated in:
- `engine_picker.cpp` (16): the internal action-descriptor table
  (stride/id/label/icon/fn/target), hover/last-clicked/last-target fields,
  plus R() addresses: GetDefaultActions, HandleMouseClickInWorld,
  ActionInitiateDialog, PopulateMenus (SetMainInterfaceTarget's K2 twin
  0x007CE710 is already known from Batch 2/3).
- `engine_actionbar.cpp` (9): MainInterface personal-action lists + strides,
  DoPersonalAction, RePopulateMainInterface.
- Action-queue primitives (all R()): CSWSCreatureActionManager,
  AddMoveToPointAction, ForceMoveToPoint, AddUseObjectAction,
  ClearAllActions — the autowalk/approach backbone.
- Player-control: kClientAppPlayerControlOffset + SetEnabled.
- The action-node list fields (kObjectActionNodesOffset + linked-list
  internals) shared with combat diagnostics.
Gates: the Dispatch `if (k1)` phases interact / guidance.approach /
guidance.cancel / guidance.beacon / engine.inputRestore / cycle_input /
announce_degrees, and cycle_input's own Batch-1 decline. May also need the
OnClientHandleInputEvent hook on K2 (target already known: the Batch 2
dispatcher 0x007B12C0) — decide when the chain enumeration says who consumes
it. Several sessions, Batch-3-sized. Port it WHOLE per THE METHOD — the
unified action menu, narrated-target slot and native walk-then-talk dispatch
carry several fossilised workarounds (see the memory notes on the unified
action menu and distant-NPC dialogue).

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
