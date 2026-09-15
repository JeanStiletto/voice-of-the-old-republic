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

### KOTOR 2 swoop race — controls fixed in round 1, hazards rebuilt after round 2 (2026-09-14), awaiting round 3

Round 1 (`patch-20260914-143533.log`): the bike never launched — Space is the
JUMP on KOTOR 2 (event `0xd9` → OnBrake slot, `onjump.ncs`), the gear ladder
sits on the OnAccelerate slot reachable only from the left mouse button / pad
B. Fixed: Space / pad A re-routed to the shift at the client input hook, W (new
`SwoopJump` hotkey) / pad B jump, gear from `MIN_RACE_GEAR`, K2's lower gate
ladder.

Round 2 (`patch-20260914-163810.log`): shifting, jumping and the gear
announcements all worked (gear 3 reached, four jumps with peak 10.0), but the
race was unplayable for four reasons, all now fixed:

- The pad co-pilot steered the bike into two MINES (they share the enemy pool
  with the pads; 23 pads + 24 mines on Citadel). Mines are now told apart by
  model name, excluded from targeting and sounded as blocks.
- The obstacle loop sounded 90 lane-edge posts that cannot be hit from the
  lane ("enormous amounts of obstacles") and placed the real hazards at their
  object origins, up to 46 units from the collision mesh. Every K2 obstacle is
  now classified from its collision box (bar / block / post) — see
  `docs/llm-docs/swoop-k2-controls-and-jump.md` section 7 for the atlas.
  Posts are silent, bars get a jump cue (the jump itself stays the player's —
  an auto-jump was built and removed the same day at the user's request),
  blocks keep the panned loop at the right place.
- The shift-ready cue fired eight times but was silent: its sample does not
  exist in K2's archives. K2 now plays a sibling sample that does.
- A bar hit bounced the bike against a post and the mod spoke "23.22 seconds,
  race ended" (coast-to-a-stop fallback, now K1-only; finish is the scripts'
  `MIN_RACE_GEAR == 6`). The bike then stayed pinned for 20 s; the magnet kept
  pulling relaunches back into the bar (now off in gear 0) and nothing said
  what to do (a stuck hint now does).

Round 3 (`patch-20260914-204103.log`): everything above worked — 9 bars / 6
blocks / 90 posts classified from the hitbump boxes exactly as the offline
atlas says, 23 pads / 24 mines split, no mine hits, jump beeps at the right
bars, finish via the script flag. New findings, fixed the same evening:

- The race "finished" at 72 s at y≈2800 of 6000: it was a WRECK. The hit
  script takes 50 of 600 hit points per collision and the heartbeat ends the
  race at 0; the log shows exactly twelve gear-to-0 events. Hit points
  (follower +0x8c) are now watched: "hit, N left" per drop, "swoop wrecked"
  instead of a time at zero.
- The shift-ready cue fired in the same tick as three of the gear changes (a
  late shift is already above K2's next gate), which the player heard as a
  shift confirmation. It now waits 0.8 s after a gear change.
- Jump timing: the player pressed W within 0.1 s of the 1.35 s beep and
  landed just short of the bar (MGC02, second press saved it), pressed too
  early on his own at MGC01 and not at all at MGC04. The beep now fires at
  1.0 s ("press now") and repeats once if the bike touches down with the bar
  still ahead.

Round 4 (`patch-20260915-075412.log`): hits spoken with the count, shift
cues ≥ 1 s after the gear, beeps at 1.00 s, first three bars cleared. Then:

- Two cascades (7 + 3 hits, 500 of 600 hp): the pad co-pilot swung the bike
  to the next pad's lane (x 11) while it was still airborne from MGC03 /
  MGC05, into the right-half bars MGC04 / MGC06 that guard those pads — the
  bar pairs are closer than one jump's air distance. OPEN: bar-aware guide.
- The end at 61.56 s was a wreck on mine 137, two units beside the pad lane
  the guide was steering to; mines deal 100 (script disassembled), the
  follower's hp mirror lagged three seconds, so the mod spoke a time. FIXED:
  wreck decided at the finish flag from the heartbeat's 59:59.99 forfeit
  stamp in `<AREA>_SWOOP_MIN/SEC/MSEC` (hp == 0 fallback).
- Six `211TEL_MGE*` scenery objects sounded the block loop at phantom
  positions, two of them right before the first bar. FIXED: skipped by name.
- "Treffer. Noch N." was noise in the chaos (and said "Noch 19" after a
  5-point bump). REMOVED — only finished-with-time vs "Swoop zerstört" is
  spoken now.
- Keys swapped on request: Space jumps, W shifts (pad A jump, pad B shift).

Round 5 (`patch-20260915-083506.log`, two races: 104.88 s finish at 95 hp,
then a wreck at 78.6 s): scenery skipped (`scenery=6`, no phantom loops), Space
= jump consumed 55×, wreck detected from the 59:59.99 stamp, no hit speech.
Two more root causes of "I cannot do this":

- The K2 shift gates were wrong (misread disassembly): the engine accepts a
  shift only above the NEXT gear's minimum (60 / 100 / 150 / 210, K1's
  ladder) — every accepted shift landed at 70 / 105-110 / 155 / 214, every
  earlier W press ran the script with no gear change. The cue fired up to 40
  units early, so W "did nothing" and the next cue came right after the
  engine's own shift sound. FIXED: K2 table = K1 table.
- The pad co-pilot caught 23 of 23 pads (race 1) with the guide tone resting
  ~90 % of the race, and no pad script plays a sound — so by ear nothing
  happened at pads. FIXED: one burst of the pad tone per caught pad.
- Still OPEN, the real time sink: the magnet's lane passes over mines (mine
  142 at x 12 / y 4443 on the pad-132 lane, mine 137 beside pad 125) and
  through right-half bars that guard pads; 10 obstacle hits + 2 mines in
  race 1, 9 + 1 in race 2. Next lever: bar- and mine-aware guide.

Round 6 (`patch-20260915-090027.log`, wreck at 37 s): gates right (cues at
60 / 104 / 150, each shift within a second), pad burst on the hits. Then a
mine on the centre lane (126 at y 320, 105 hp), a jump from x 11 that landed
drifting into the right wall at x 20, and nine relaunches into the post — the
predictive wall cue never fires while parked, and "steer aside" sent the bike
across to the LEFT wall for the same again. ADDED: wall-pinned cue (thump on
the wall's side every 0.7 s in gear 0 at |x| ≥ 16) and the jump beep panned
to a half-bar's free side (centre = must jump).

What round 7 must show:

- `jump cue: … side=left|right|centre` matching the bar's x span; the beep
  heard on that side.
- Round 7 (patch-20260915-091337, two wrecks): 22 of 26 hits were posts —
  six arriving at the wall ~0.7 s after the single thump, sixteen relaunching
  from a park at |x| = 20 where the gear-0 thump had stopped on each relaunch.
  Player chose an audible warning over a rail. REPLACED by the K2 wall LOOP:
  `wall loop: side=…` whenever |tunnelX| ≥ 12.5 or the 20-tick projection
  crosses 16, any gear; `wall loop: stop` once back inside 11.
- Round 8 must show `wall loop: side=…` BEFORE every post `gear -> 0` (if any),
  the loop still running through a relaunch from the wall, and post hits down
  from ~11 per race.
- Round 9 (patch-20260915-142030, wreck at 41 s and at 86 s reaching y≈5300
  of 6000): posts down to 1-3 per race. Race 2 lost 420 hp to FOUR mines (126
  on the centre lane at the start, 141 / 142 / 145 on the pad diagonals) and
  the rest to two bars each followed by relaunch hits with no beep. The wall
  loop fired on every fast pad swing (proj from x 6). ADDED: mine-aware guide
  line (`detour: mine slot=…` / `detour: clear`), wall projection only on
  overshoot (`overshoot=1`), bar beep re-armed while parked.
- Round 10 must show `detour:` lines before the mines that used to hit, no
  `hit: … drop 105` on a detoured mine, `jump cue:` before each relaunch in
  front of a bar, and `wall loop:` only with `overshoot=1` or |tunnelX| ≥ 12.5.

- `shift-ready cue: gear=1 … gate=60.00`, `gear=2 … gate=100.00`,
  `gear=3 … gate=150.00`, `gear=4 … gate=210.00`, and W shifting within a
  second of each cue (no `shift (OnAccelerate): ran` without a `gear ->`).
- A pad-tone burst on every `CROSS … hit=1`.
- With the K2 assist gap at 6: `guide=1` for a good share of each pad
  approach (was ~10 %), `mag=1` only in the last ~6 u; the `CROSS` hit rate
  tells whether 6 is too tight (misses with `abs` between 4 and 6 → raise
  the near gain, not the gap).

What round 5 had to show (met):

- Inventory lines `class=scenery` for the six `211TEL_MGE0*` objects and no
  `loop start slot=9[3-8]`; `hazards: posts=90 scenery=6 bars=9`.
- `Diag.ClientHIE … CONSUMED — K2 swoop jump` on Space, `shift (OnAccelerate)`
  runs on W; gears climb on W, the bike hops on Space.
- No `Speech.spoke … Treffer` lines; at the end either `race end: finish
  (area=211TEL stamp=M:S.C hp=N)` + the time, or `race end: WRECK (… stamp=
  59:59.99 …)` + "Swoop zerstört. Rennen vorbei."

Round-4 expectations (kept for reference):

- `hit: hp 600 -> 550 (drop 50 of 600)` on the first collision.
- `shift-ready cue:` lines at least 0.8 s after the preceding `gear ->`.
- `jump cue:` at `ttc=1.00s`; `jump cue (again):` only after an early jump.

Round-3 expectations (kept for reference):

- `enemy pool: accelpads=23 mines=24`, `mine slot=… model=[mgf_mine01]`, and
  no `magnet slot=` line whose slot is a mine slot.
- Obstacle inventory lines with `box=hitbump … class=post|bar|block`; the
  `hazards: posts=… bars=…` trace with posts ≈ 90 on Citadel.
- `jump cue: slot=… bar y=880.1` about 1.4 s before y=880 when driving near
  the centre; after the W press `jump end: … peak_z≈10` and NO `gear -> 0`
  at y≈880.
- `shift-ready cue:` lines still present AND the cue audible this time.
- `finish: script=1` at the real finish; no race-end speech mid-race.
- If pinned anyway: `stuck hint:` once, and the hint spoken.

KOTOR 1 regression to schedule: one Taris race — the gear source, the finish
trigger and the magnet gear gate are shared; the bar/post classification,
mines and the jump cue are K2-only paths.

### Lost key releases (stuck turn / walk) — engine-level fix built 2026-09-14, KOTOR 2 confirmed the same day, KOTOR 1 untested

The KOTOR 2 uncommanded-camera-spin report of 2026-09-14 (`patch-20260914-085546.log`,
a clockwise spin from 09:10:58 to the quit at 09:12:24, cursor NOT in the edge
band) was a stranded turn-key release. Decompiled on both games the same day:
the engine reads the keyboard as buffered DirectInput transitions and never
re-reads the device, so a release discarded by any of its own gaps stays "down"
until the key is pressed again. The gaps: every raw-input SetActive 0→1 edge
(dialogue start/end, pause and quit popups, area transitions, load screens,
movies, window focus) and the reader's own "input lost / not acquired" path,
which answers an empty batch. The pause key's own release is lost on
essentially every press in the KOTOR 2 logs (`Diag.ClientHIE` 218 down, never
up) — harmless there, proof of the mechanism. Input-class switches also drain
the buffer but zero every key state first, so they release rather than strand.

Fix: `engine_keyboard_resync.cpp` wraps the one call through which the per-frame
event fetch pulls the keyboard batch, diffs the engine's view against
`GetDeviceState`, and appends a release for any key the engine still holds that
DirectInput reports up, after two consecutive frames of disagreement with no
transition for that key. Presses are never synthesised. Full model in
`docs/llm-docs/gui-and-input-internals.md` "keyboard_transition_stream".

What to look for in the next logs of either game: `KeyboardResync: installed`
at bring-up; a `synthesised release for DIK 0x..` line whenever a loss was
repaired (0x2e = C, the user's turn-right key); `KeyboardResync.Device` edges
dating any DirectInput-level gaps. A `CameraSpinDiag: UNCOMMANDED ROTATION`
that is NOT followed within a second by a synthesised release is a new cause.
KOTOR 2 confirmation (`patch-20260914-115230.log`, 12 minutes of deliberate
provocation, no spin, no fault): four releases repaired — Enter at a load end,
F4 at the quit popup twice, W during a popup — every one at an engine
`active=0 → 1` edge, none anywhere else, i.e. no misfire in ordinary play.
The `KeyboardResync.Device` edge only reports a device lost while the
engine still believes input is active; gaps the engine itself opens are
already dated by the `DirectInput state` poll. KOTOR 1 shares the code and
its call site is byte-verified offline, but has not had a session yet.

Which of the two invisible gaps produced the 09:10:58 episode is still
unknown; the new lines answer that on the next occurrence.

**Second KOTOR 2 spin, 2026-09-14 (`patch-20260914-122504.log`, 12:30:12 to the
quit at 12:30:25): NOT a lost release.** The re-sync ran every frame through it
and found the engine's key view and DirectInput in agreement; its four repairs
that session all sat at real `active=0 → 1` edges, the last one at the quit
popup. No pad, no snap-turn of ours, no UI owning the camera, the player
standing still at onset, cursor parked at (24,2) and never moving. The KOTOR 2
`UpdateCamera` twin was decompiled the same day (camera-and-swoop.md, "KOTOR 2
twin"): its band is 2 px, so x=24 is outside it; the vertical band the park does
sit in is a no-op in the follow camera; the native right-stick source needs a
controller enumerated at start (there were none); the mouse-delta path needs
cursor motion. Every input the camera itself reads is therefore excluded by the
log, which leaves the player creature rotating with the chase camera following
(indistinguishable from a camera turn in the spin report), or a source outside
`UpdateCamera`. KOTOR 2 snaps the player's facing to the selected target — the
instantaneous compass jumps in that log coincide one-to-one with target changes
(E cycling, show-object changes under the parked cursor), and the engine target
changed three times during the spin (the `Combat.Diag: DELTA target` lines
date those). Instrumentation built and applied to both games 2026-09-14:
every `UNCOMMANDED ROTATION` line now also carries `playerYaw` / `playerRate` /
`playerTurn` (the player creature's own facing, from the server object — a
rate matching the camera's means the creature turned and the chase camera
followed; near zero means the camera's own turn path), `turnAxis` (the
engine's PollInput(0x11c) answer, i.e. its own held-turn-key view),
`stickX` (K2 native right stick, `n/a` on K1), `mouseY`, `rightClick`
(K2 global now known) and, K2 only, `acclMirror` / `turnedFlag` (module +0x98
/ +0x100, written only when AcclTurnCamera applies a turn). The END line
carries `playerTurn` too. Read the next spin's first report line: it names
the driver.

### GOG KOTOR 2 relocation port — executed and locally verified 2026-09-04, awaiting the tester round

The "mod has never loaded on GOG K2" defect (silent vanilla game since v0.7.0;
fingerprint: K2 `logs/` directory never created, diagnosed from
`userlogs/maybe08k2modnotworking.7z`) is fixed by the full Steam→GOG address
relocation: `kdev sigscan --k2` + the two re-scripts generate and validate the
K2 rebase tables and `kotor2-gog.hooks.toml` (all 265 addresses resolved, every
offline check green — Lane's per-store databases 10/10, the patcher's own hook
byte-verify 26/26). Local GOG smoke test passed: the DLL initialises
(`build=relink-2021-09-09`), hooks fire, the main menu's focused button is read
and announced. Full record in `docs/kotor2-port.md` "GOG build (2021 relink)".

What remains is the GOG tester's confirmation round (speech through real play,
not just the main menu). Note for reading that tester's logs: during the local
smoke test the game crashed at the main menu whenever another fullscreen app
stole foreground — but VANILLA GOG K2 (proxy removed) crashed identically, so
that is engine/environment behaviour, not the port.

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
