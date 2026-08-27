# Known Issues

Status tracker for accessibility-mod work, in five buckets:

- **Bugs** — regressions or broken behaviour to fix.
- **Unreproduced** — reported issues we can't reliably reproduce yet; need a repro before fixing.
- **Planned** — future feature work that's not currently in flight.
- **Monitor** — shipped features whose behaviour we're still watching in live play.
- **Polish** — quality-of-life refinements; the feature works but has rough edges.

When an entry is closed, move it out of this file (the corresponding fix or commit message is the durable record).

## Bugs

### Controller misbehaves on the ROG Ally, both games — diagnostics built 2026-08-27, awaiting a tester log

Several testers play on an ROG Ally (integrated controller) and report the pad
"not working properly" on K1 and K2 alike. No log yet: the diagnostic layer
below was built first, deliberately, so the tester's one session produces an
answer instead of another round of guesses.

**The first hypothesis — that a handheld sends "different keycodes" — is wrong
and should not be re-raised.** `pad_input` reads the pad through XInput, whose
button mask is fixed by the API: A is `0x1000` on every device that speaks it.
A pad visible to XInput at all is already fully bound. The failure therefore has
to be one of:

- The controller is not in XInput mode. Its vendor layer can put it in a
  desktop / keyboard-and-mouse emulation mode, where buttons arrive as real
  keystrokes. Nothing to bind, AND the emulated keys feed the mod's own keyboard
  detectors — so it misbehaves rather than merely going quiet.
- We bound the wrong XInput slot. A vendor layer can present a controller of its
  own beside the physical one; the old code took the first slot that answered
  and never re-evaluated, so a phantom in slot 0 killed the pad permanently
  while every log line still looked healthy.
- KOTOR 2 only: its engine enumerates game controllers once, at input init, and
  a multi-interface handheld gives it more devices to choose among.

**What now goes into every session log** (all of it in `Pad.Devices`, plus two
`Pad` lines):

- A `controller census` at session start and again whenever the device picture
  changes: all four XInput slots whether or not we chose them, each with
  capabilities, sub-type and — via the undocumented ordinal-108
  `XInputGetCapabilitiesEx` — its USB vendor/product ids, plus a live sample.
- The DirectInput half: the same `DI8DEVCLASS_GAMECTRL` / attached-only
  enumeration KOTOR 2's engine runs at input init, through the same `dinput8`
  module the process already has. Its payload is device NAMES, which XInput
  cannot give and which is the fastest way to tell a physical pad from a virtual
  one in someone else's log.
- A `READING:` line that states what the two counts together mean, so the
  no-controller-at-either-API case is not mistaken for a mod bug.
- `slot N has not changed state in … ms while slot M is producing input` — the
  idle-slot guard correcting a wrong bind. Its ABSENCE alongside a silent pad is
  itself a finding.
- K2: `engine code N ignored — its keyboard twin … is physically down`. This is
  the correct answer when the keyboard really sent the event, but it is also
  precisely what keyboard-emulation mode produces, and it used to be silent.

Implementation: `patches/Accessibility/pad_diag.{h,cpp}` (census; no input
driving, never acquires a device) and the slot-selection + idle-guard changes in
`pad_input.cpp`. Slot selection now prefers a slot whose capabilities report a
gamepad over the first that merely answers.

**Status: built and cleanly compiled; the empty-machine path and the DirectInput
struct/vtable decoding are verified by a standalone harness on the dev box (real
product names and vendor ids decoded correctly). The device-present XInput
branch is unverified — no controller was attached.** Next step is one tester
session on an Ally.

### K1 dialogue replies unnavigable for zack on 0.7.8.1 — cursor-state dependent; expected fixed by the v0.7.9 re-park (diagnosed 2026-08-25, awaiting zack's confirmation on the next release)

Zack's report (`userlogs/zack078dialoguesnotspeaking.7z`, 0.7.8.1, English,
`k1-patch-20260824-212526.log`): arrows in conversation click but never speak,
number keys select blind. Log forensics established TWO separate defects:

1. **During the NPC voice line** (reply store empty): our dialog listbox spec
   consumed Up/Down and drove the PREVIOUS node's stale rows — click, then
   silence, `Menus.DialogReply: selected (src=none)` (11× in zack's session).
   **FIXED 2026-08-25** (nav liveness gate + "Reply options not yet available"
   phrase, `navLive` in menus_listbox.cpp) — awaiting in-game test.

2. **After the reply break** (replies live): arrows did nothing on zack's
   machine — ~35 presses across 4 conversations, one exception. Mechanics,
   established from his log + dev logs + a live dev test 2026-08-25:
   - The mod never drove the live cinematic reply list: at the reply break the
     entry-text overlay panel is foreground, our chain declines it, and the
     keys fall through to the ENGINE, whose native CSWGuiListBox keyboard
     handler (`HandleInputEvent @0x0041ce20`, InputIndex 61/62 = the
     `KEYBOARD_K`/`KEYBOARD_L` log lines) moves the selection; our monitor
     speaks the change. The in-code claim "the engine does not navigate the
     reply listbox by keyboard" was true only for the droid/computer
     interfaces it came from.
   - That native path is CURSOR-STATE dependent. Zack's `CameraSpinDiag`
     mouseX trace correlates 1:1: cursor parked at 24 → his single working
     arrow press (21:31:36, inside the 21:31:16–36 parked window); cursor
     re-centered to 400 → every other press dead. The engine repositions the
     cursor between dialogue nodes (the same behaviour the K2 Peragus console
     fix documented — "the game can bring the cursor back between nodes"),
     and 0.7.8.1 parks only once per conversation, so from the first node
     transition on his cursor sat at screen centre and keyboard nav was dead.
   - The v0.7.9 re-park on every new reply set (7f3d919, written for the K2
     consoles) closes exactly this: live dev test 2026-08-25 13:35 shows the
     re-park firing at the break, mouseX pinned at 24 across it, and five
     arrow navigations all spoken (`patch-20260825-133518.log`).
   - Why most 0.7.8.1 players are fine: per-node cursor repositioning killing
     the native nav evidently depends on machine/session state not fully
     pinned down (zack: windowed 800x600, cursor never touched by hand; but
     the dev box is windowed too and July logs navigated fine with the same
     one-park-per-conversation code). The re-park makes the whole question
     moot by re-asserting the parked state at every node, so pinning the
     exact engine trigger is not planned unless it resurfaces post-0.7.9.
   - Follow-up: ship v0.7.9 to zack (or a test build) and have him confirm
     arrows speak replies after each voice line.

### KOTOR 2 crash entering the Peragus Fuel Depot — third-party item, engine null-deref (diagnosed 2026-08-24, no code change yet)

Kenny's game died on the area transition into the Fuel Depot
(`userlogs/kennycrashfuel0782208.7z`, `k2-patch-20260821-231904.log` ends
23:40:00, dump `k2-swkotor2.exe.17344.dmp`). Read of `[NULL+0x70]` on the main
thread, inside the engine's item loader — decompile of the K2 fault site
(`FUN_006014f0`, called from the GFF item loader `FUN_00601740`, kotor2 Ghidra
project) shows it calling `GetBaseItem(item+0xc)` and dereferencing the row
without a null check (it null-checks the row's *field* at +0x70, one
instruction too late).

The dump still holds the GFF being loaded: `heavymininglaser.uti`, tag
`HeavyMiningLaser`, `BaseItem=106`. Every baseitems.2da we ship stops at row
105 (verified against the local install with the same source pins), and no
bundled mod carries that template — it is a third-party mod on Kenny's machine
(likely a Steam Workshop subscription or a mod installed outside our
pipeline) whose accompanying baseitems.2da row never made it into the
effective Override. Recurs on every entry to that module — potentially
progress-blocking for Kenny.

Follow-ups to decide:
- Ask Kenny for their mod list / Workshop subscriptions; the mod that ships
  `heavymininglaser` needs its baseitems.2da row restored (reinstall after our
  bundle) or the mod removed.
- Check whether our bundled-mods install can clobber a pre-existing modded
  baseitems.2da (install-order conflict) — if yes, this class of crash is
  partly ours to prevent.
- Defensive patch SHIPPED 2026-08-25 (both games + the 2004 relink, fallback
  row 0), live-tested in BOTH games the same day: CLI-injected
  CreateItemOnObject of a BaseItem=200 item survived, item usable in
  inventory, `BaseItemGuard` log lines present (K1: id=200/92 rows,
  ~28k dedup-collapsed wrapper hits; K2: id=200/106 rows — the same
  106-row table Kenny's id=106 item overruns). What remains open here is
  only Kenny-side confirmation that the Fuel Depot now loads. Target
  confirmed by decompile in both games: the baseitems-ARRAY accessor
  `CSWBaseItemArray::GetBaseItem(int id)` — K1 `0x005b31a0` (Lane's DB names
  it; identical shape to K2's `0x006d5200`: NULL for table-not-loaded / id<0
  / id>=count, count at this+0, array at this+4, 200-byte stride). Hooks
  land AFTER the engine's table-loaded check, so the startup-probe NULL
  stays untouched; out-of-range ids clamp to row 0 and log under the
  `BaseItemGuard` tag. Handlers in `baseitem_guard.cpp`.
- First live test (CLI-injected CreateItemOnObject of a BaseItem=200 .uti,
  2026-08-25) CAUGHT A HOLE: K1 still crashed in CSWSItem::LoadDataFromGff
  — its `CALL CSWItem::GetBaseItem` returned NULL despite the accessor hook
  firing, because K1's optimised build INLINED the accessor into that
  wrapper (`0x005b4790`). Fixed with a second K1 hook on the wrapper's
  inlined bounds check (`0x005b47a3`), same handler. An IMUL-by-200 sweep
  over K1's whole .text found exactly three sites (accessor, wrapper copy,
  loader's count*200 allocation) — coverage now complete. K2's unoptimised
  wrapper CALLs the accessor (byte-verified) — one hook suffices there.
  Relink addresses `0x005b340b` + `0x005b4a03` via sigscan, cut bytes
  verified against the Allard exe.
- Test tooling (all in place): `Override\acc_test_baditem.uti` (BaseItem=200)
  + `Override\acc_givebaditem.ncs` in both games,
  `build\runscript\runscript.exe <script>` injects the engine's runscript
  cheat via KotorMessageInjector (no console needed — the console key is
  broken on non-US layouts anyway). Verification: run
  `runscript.exe acc_givebaditem` with a save loaded → no crash, item in
  inventory, `BaseItemGuard` line in the patch log. On Kenny's save the
  Fuel Depot should load with the id=106 line. Remove the two test files
  from both Overrides before release.

### Galaxy-map travel never loaded the destination — fix built 2026-08-20, awaiting test

A tester finished KOTOR 2's prologue, flew to Peragus, watched the movie and
landed back on the Ebon Hawk with the star map open — three times in a row,
which is where the run ended (`k2-patch-20260820-143622.log`, 14:49-14:51,
bundle `userlogs/tutorialreset0781.7z`).

Cause: one Enter drove both the star map's accept (`GalaxyMap: accept/travel`)
and the in-world interact dispatcher (`Interact: Enter gate -- ALLOW,
kind=InGameGalaxyMap` → a queued `AddUseObjectAction` on the Galaxy Map
placeable). `IsForegroundUiBlocking`'s kind blacklist did not carry
`InGameGalaxyMap`, and the engine does not push that panel onto `modal_stack`,
so neither of the other two block rules covered it. The queued action outlived
the 25-second movie and fired at the end of the travel cutscene — the star map
re-opened with no keypress behind it, and `Transition: pre-load` never appeared
for any of the three attempts, so the module change was never requested at all.

Fixed by blacklisting the kind. Same failure class as the save-name popup
(commit `21d8a99`), which was closed with a per-popup latch rather than at the
gate; a latch cannot fix this one, because the poll router runs *before* the
menus consume in the same tick (visible in the log — the interact dispatch is
logged above `accept/travel`).

Test: KOTOR 2 prologue, Galaxy Map → Peragus II → Enter. Expect the movie, then
Peragus, and a `Transition: pre-load` line in the log. Then the same on KOTOR 1
(Ebon Hawk → any planet), where the double-fire also existed but the immediate
module load hid it.

### Chain drives a freed panel when its address is reused — fix built 2026-08-20, awaiting test

Two KOTOR 2 chargen runs crashed while arrowing the Fähigkeiten list
(logs `patch-20260819-063818` / `patch-20260819-221733`, dump
`swkotor2.exe.3664.dmp`). The access violation is in the engine, inside
`CSWGuiSkillsCharGen::IsClassSkill`'s K2 twin, dereferencing the panel's
`chargen_creature` (`+0x68`) — a field the constructor sets from its
`param_2` and nothing rewrites.

Root cause is NOT a chargen binding problem. The chain was still bound to
the Attribute panel after the engine freed it: its buttons dispatched with
`is_active` reading 2622030025 (freed memory), and the Skills panel was
then allocated onto the same block, which is why it appeared at the
Attribute panel's old address with a half-written creature field. The
engine's own handlers then walked that field and died.

`ValidateChainPanel` could not catch it: it tests membership in `panels[]`,
which the address passes again as soon as a new panel occupies it.

Evidence it is address reuse and not a chargen invariant: the healthy run
`patch-20260820-105008` shows the Skills panel at its own address with
`+0x68` valid and stable, costs resolving (Preis 2 / 2 / 1), descriptions
and remaining points all correct — on the same screen the user has used
for months.

Fix: `ChainPanelIdentityHolds` records the panel's vtable at RebindChain
time and compares it before anything drives that panel — the FireActivate
dispatch, the cursor warp, and the chain step (which now rebinds instead).
Trips loudly as `ChainIdentity STALE`.

Known gap: reuse by a panel of the SAME class passes the vtable compare.
That needs a generation token; the vtable version landed first because it
is falsifiable on the known repro.

Two dead ends worth not repeating: the crash is not a transient
init race (the field stayed zero for 7s), and gating our announcements on
a "binding ready" probe is wrong — it suppressed cost, description and
warp on healthy panels while fixing nothing, because the probe was reading
a symptom of the recycled memory.

## Unreproduced

### Dead keyboard on a machine with no mouse — fix built, awaiting a no-mouse session

kenny's KOTOR 2 0.7.5 session (`userlogs/kenny075keyboarddeath.7z`) reached the main
menu with speech and cursor working and never delivered one keystroke to the engine.
Root cause is the engine's own DirectInput teardown: any failure creating the mouse
device releases the keyboard device too, and nothing rebuilds it — see
`docs/llm-docs/gui-and-input-internals.md` → `directinput_device_lifecycle`.

The fix (mouse-teardown block + per-frame retry latch + a DirectInput state line in
the log) is in and verified on the HEALTHY path in both games: the block installs
(5 call sites on KOTOR 2, 2 on KOTOR 1) and the state probe reports the interface,
keyboard and mouse correctly through startup and focus loss.

What is NOT verified is the failure path itself, because this dev machine has a
mouse and DirectInput creates the device normally. Unplugging a USB mouse may not
reproduce it either — the failing step could be any of CreateDevice, SetDataFormat,
SetCooperativeLevel, SetProperty or Acquire, and the handler now names which one in
the log. Confirmation has to come from kenny's next session: expect either a
`MouseTeardownBlock: mouse bring-up failed at <step>` line with a working keyboard,
or no such line at all (in which case the teardown comes from somewhere else and the
`DirectInput state` lines will show where).

## Planned

### Beacon-active navigation announcements (remaining-route reading)

While a beacon (or autowalk) is active, the route announcements should describe the *remaining* way to the active waypoint — leading with the range and direction of the current target — rather than the full original route. The hard part is disambiguating intents: if the player selects another object just to hear where it is, the announcement must not balloon into the long, confusing full-route description for the beacon target. Design questions to resolve: how to keep "where is this thing I just selected" reads short while a beacon is running, and whether the separate Shift+Enter autowalk / Shift+`-` gestures are still needed at all, or whether they can be folded into / replaced by the plain selection + beacon flow. See `project_narrated_target_unified.md` and `project_map_cycle_architecture.md`.

### Nameable personal map pins

Let players give their own map pins a name when they drop one, and read that name back when cycling to the pin. Personal pins currently announce only a generic label plus position, so a player who marks several spots can't tell them apart. Needs a text-entry path that works from the keyboard with a screen reader (the editbox handling shipped for chargen + save naming — `menus_editbox.cpp` — is the closest existing mechanism, though map pins aren't an engine editbox so the entry surface differs), storage of the label alongside the pin in the save, and the cycle/focus announcements updated to speak it. See `project_narrated_target_unified.md` / `map_user_markers.cpp`.

### Bundle the resolution / widescreen patch — once we are out of beta

Offer the community resolution / widescreen + HR-menus patches as an installer option, and make our own code robust to the layout changes they introduce. Deliberately deferred until the mod leaves beta: it widens the support surface (several of our paths still assume a particular screen geometry — screen-pixel coordinates, the OS cursor, hardcoded hit-test offset compensations), and a beta is the wrong time to add a second variable to every bug report. Prerequisite when we do take it on: audit every place we depend on screen geometry and read it live from the engine instead of assuming. See `docs/installer.md` (Widescreen / HR-menus bundling) and the `project_radial_cursor_coupling` memory.

## Monitor

### World hotkeys that still act while a screen owns the keyboard

Audit run 2026-08-20 alongside the galaxy-map fix above, over every Win32-polled
hotkey that can reach an engine dispatch. Three surfaces remain gated on
"is there a world" alone (`GetPlayerPosition`), with no UI-block check:

- **Shift+H combat queue** — arms over any panel, and its rows call
  `RemoveLastAction` on the live combat round. Worst case is mutating a round
  from inside a screen; it cannot injure a module load. One-line fix if wanted:
  `ForegroundPanelBlocks()` at the opener, same call the action menu uses.
- **Camera orient (turn-to-cardinal / turn-to-beacon)** — synthesises the
  player's bound turn key via `SendInput`, so with a screen up those scancodes
  land in the screen. Injects no action; see the
  `lesson_mod_injected_input_feeds_own_detectors` memory for the related trap.
- **Shift+L level-up** — opens the engine's level-up wizard over whatever is
  foreground. Has its own already-open and can-level-up guards, so it stacks a
  panel at worst.

None is reproduced in a log; all three are the same *shape* as the galaxy-map
fault (one press, two consumers, one of them the world) and each would cost a
test round to change, so they are listed rather than fixed. The detector for
the rest is now in the log: `Interact: <key> GATE GAP?` fires whenever an
in-world dispatch runs over a foreground panel that is not a world-level
surface (`IsWorldLevelForegroundKind` — HUD, pause, bark, fade, Unknown). Grep
tester bundles for `GATE GAP` before adding kinds by guesswork.

Also open: three separate lists now describe "is the UI in the way" —
`IsForegroundUiBlocking`'s blacklist, `view_mode.cpp`'s stricter copy, and the
new world-level allow-list. They agree today (all three were updated together);
they are a consolidation candidate, not a bug.

### Grass crash — worked around with `Grass=0`, root cause not fully pinned

Shipped in 0.7.7: the installer sets `Grass=0` in `[Graphics Options]` on both games, which removes the faulting code path. Full analysis in `docs/grass-crash-analysis.md`.

What is confirmed: the fault is an access violation inside the Intel OpenGL driver, called from `GLRender::DrawLightmappedGrass`; grass is always submitted as client-side vertex arrays; and `DrawLightmappedGrass` derives its attribute base as `param_1 + param_6 * 0x30`, which is only correct when `param_6` is the bin's *total* quad count — so the per-sub-bin draw loop in `RenderGrassPolys` feeds the driver attribute pointers aliasing into the position block.

What is **not** explained: that defect reads wrong data but stays inside the allocation for every stride the engine uses, so it does not by itself produce the page fault. Most likely remaining candidate is a sub-bin count list disagreeing with `field6_0xc`. `CAurTriangleBin` is a Ghidra PlaceHolder struct, so per-field reasoning is unreliable until it is re-typed.

Watch for: any grass-area crash report from a user who has re-run the 0.7.7 installer (would mean `Grass=0` does not gate the path as assumed — we traced the option to the `enableGrass` global but never traced which caller reads it).

### Tick-clock handling is inconsistent across the codebase; a mass change may be needed

A tick is one rendered **frame** — `core_tick` fans out from a detour on `CSWGuiManager::Update`, called once per frame from the engine main loop — so the interval between ticks is whatever the framerate is: ~16.7 ms at 60 Hz vsync, ~6.9 ms at 144 Hz, less with vsync off. There is no fixed tact to assume.

Until 0.7.3 nothing published a tick timestamp. `core_tick` was built as a dispatcher (fan-out ordering) and its `QueryPerformanceCounter` reads were private to the watchdog's SLOW FRAME / SLOW TICK diagnostics, so all 132 clock reads across 42 files call `GetTickCount` independently, at ~15.6 ms resolution. `acc::tick::NowMs()` now publishes the tick-start stamp (same QPC read the watchdog already takes, so it costs nothing extra), but only `camera_announce` consumes it.

**Why this was not caught by any duplication sweep:** there is no duplicated helper to find. A `GetTickCount()` call looks identical and idiomatic at every site; what distinguishes a safe use from an unsafe one is what the caller does with the subtraction two lines later, which no naming or structure pass inspects.

Sorted by what the clock is used for:

- **Deadlines** ("have N ms passed") — the large majority, and correct as-is: one quantum of error against a window of hundreds of ms. No migration needed.
- **Integration** (`speed × dt` — `map_ui_cursor`, `view_mode` cursor stepping) — also correct, because both update their last-stamp unconditionally, so a zero-length tick contributes nothing and the next tick's dt covers the gap; the dts still sum to real elapsed time.
- **Differentiation** (`rate = delta / dt`) — the only unsafe pattern, because a near-zero interval does not degrade a rate, it falsifies it. Three sites: `camera_announce` (fixed in 0.7.3 — it reported 0°/s mid-turn and fired three spurious stop cues in one second), `audio_footstep_suppress` (divides over a ≥500 ms window, ~3% error, fine), and `camera_spin_guard` (per-tick yaw delta; a `dt > 0` guard prevents a divide-by-zero but a zero-length tick reports 0°/s, so the edge-spin guard can skip a correction tick — shipped behaviour, self-correcting next frame, not yet fixed).

**Open question:** whether to migrate more broadly or leave the deadline sites alone. Current judgement is to leave them — they are correct, and a mass change would be a lot of untested behaviour for no benefit. Revisit if a third differentiation site appears, or if a high-refresh-rate tester reports timing-dependent oddities.

**Constraint on any migration:** `NowMs()` is the *current tick's* timestamp, not a live clock. It is only valid inside the Dispatch fan-out; code reached from an input hook, a menu callback or a hotkey runs outside the tick and would read a stale value.

### KOTOR 2 does not refuse impossible actions — we announce their absence instead

A medkit used on a full-health character behaves differently in the two engines, and only one of them tells the player anything.

KOTOR 1 refuses it at the GUI layer: `DoPersonalAction` finds the entry's usable bit clear, plays its error sound, and never dispatches. The entry's flag word carries a reason code, so we speak the engine's own sentence — logged as `ActionBar: variant flags ... usable=0 reason=5` followed by `refused — speaking engine reason strref=0xa602 [Volle Gesundheit]`.

KOTOR 2 mostly cannot: the answer to this entry's original open question (decompiled 2026-08-12, the dead-Medicine-row investigation) is that K2's item appender **never initialises the flag word for inventory items** — only the three equipped-slot entries get one. What we read there is heap garbage (ASCII string fragments in the logs), the engine gates on the same garbage, and `VariantRefusal` therefore skips the refusal verdict for K2 item entries entirely (a garbage bit would fake a refusal with a random reason). Force/feat entries DO compute real flags on K2 — "Macht erschöpft" (`usable=0 reason=1`) was observed live — so the mechanism exists, it is just never fed for items. The no-op watch stays as the honest answer for silent item refusals.

**The K2 Medicine row needed a deeper fix (2026-08-12).** K2 gives every medical-category item a TWO-STEP use flow: the press arms a target-pick (MainInterface `+0x15230` armed / `+0x15234` party slot) that only a mouse click on a party portrait completes — and `DoPersonalAction`'s preamble wipes the state before the medical handler reads it, so from the keyboard the row can never fire at all (one press wedges it armed-with-no-slot; the 2026-08-11 21:58 "flip" in this playthrough was simply the first medical press whose base-item row byte routes into the two-step flow). The shipped answer bypasses that machinery: `SendUseItemRequestK2` emits the same client→server use-item message the portrait click's consume path emits, and the unified menu adds a party-member picker (narrated member preselected, Shift+Enter = instant self, bare 5 = instant self). The `Diag.UseItemReq` hook on the message writer doubles as the no-op watch's cancel signal: a sent request proves the press acted, which also fixed mines announcing "Nicht möglich" while visibly planting (they act without a combat-round add).

**One false-positive class found and fixed (2026-08-08).** The watch was armed for every menu fire, and target-row actions tripped it three for three on a footlocker: Sicherheit, Sicherheits-Überbrücker, and a Schallmine that visibly blew the lock open all announced "Nicht möglich" while working perfectly (`patch-20260808-103232.log`). Target rows act without a combat-round add as a matter of course, so the watch is now armed only for the personal block — `ArmNoOpWatch`, called from the menu's personal path and from bare 4..7, never from `ArmUserQueueAdd` itself.

**Still watching:** KOTOR 2's Combat Behaviour column (key 8) may legitimately queue nothing — if it does it will wrongly announce "Nicht möglich" and needs the same send-witness treatment. And a server-side drop of a direct-sent medical item (e.g. full health) is silent-then-generic rather than reasoned — the engine's client-side CanUseItem gate (`0x005D2F80`) is skipped by the direct send and could later supply real reasons.

Code: `engine_actionbar.cpp` (`VariantRefusal`, `SendUseItemRequestK2`, the always-on flags log), `combat_queue.cpp` (`ArmUserQueueAdd` / `TickNoOpWatch`), `combat_diag.cpp` (`OnUseItemRequestK2`), `unified_action_menu.cpp` (`SpeakRefusal`, the medical party picker).

## Polish

### Starting an autowalk/beacon while one is active should switch, not cancel

If an autowalk or beacon is already running and the player triggers a new autowalk or beacon on a different target, the current behaviour just cancels the existing one. Instead it should immediately start the new action on the new target — switching the route in one gesture rather than requiring cancel-then-start.

### Mod-settings sliders only save after pressing Enter

The sliders under Mod-Einstellungen (e.g. hint-sound volume) only persist their new value to `acc_settings.ini` once the user presses Enter on the row. Adjusting a slider with Left / Right changes it for the session and previews the new level, but the change isn't written until an explicit Enter — so a player who tweaks a slider and leaves the menu without pressing Enter loses the change on next launch. Slider adjustments should persist on each Left / Right step (the same way the value already updates live), without needing a confirming keypress.

### Fold in human translation contributions as they arrive

Most of the mod's non-German, non-English text is machine-drafted — the in-game strings for French, Italian, Spanish, Russian and Polish, and the installer's `Locales/{fr,it,es}.json`. They are correct in structure (key parity verified) but the phrasing, formal-vs-informal address, and modding terminology may read awkwardly to a native ear. German is human-authored by the maintainer and is the quality bar. As native speakers contribute corrections, merge them; PRs against the individual string files are a well-scoped contribution to point people at.
