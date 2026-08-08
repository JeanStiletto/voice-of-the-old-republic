# KOTOR 2 — controller support plan

**Status: BASELINE CONFIRMED WORKING IN GAME (2026-08-06, third round).**
Confirmed live: A activates, the stick and D-Pad navigate menus, the right
stick turns the camera and the direction announcement fires, the left stick
moves and the drive-loop suppression arms off it. Committed on that basis.

Test-round history, because two of the three taught the file something:

1. First round — the defect list below (D1–D5), and a reading of the in-world
   movement signal that later proved wrong.
2. Second round — tested nothing: the pad was connected AFTER launch, so
   `CExoInput` (which enumerates controllers exactly once at input init) had no
   joystick device and the engine emitted no pad event at all. Not wasted: it
   refuted the movement signal. See Phase 2. **A round with the pad connected
   late tests nothing — plug it in first.**
3. Third round — baseline confirmed, plus the Quick Menu refutation (Phase 3)
   and three defects now fixed and awaiting their own round: the "control N"
   noise in the Y menu, pad navigation not cancelling the previous entry's
   speech, and the left stick navigating menu entries on the map screen instead
   of driving the map cursor.
4. Fourth round — **the premise under Phase 1 turned out to be wrong**, and
   correcting it reshaped the whole in-world binding set. The in-world D-Pad is
   not free: it is the engine's action menu, categories on left/right and
   entries on up/down, A confirming. Every earlier round had been consuming it
   and reporting "nothing happens", which for a blind tester is what a silent
   visual highlight looks like. See "The D-Pad is the action menu" below.

This file is written to be executed cold: a fresh session should be able to work
straight through it without the investigation conversation.

**Where the code lives.** Everything pad-specific is in two module pairs, plus
small edits at the seams they hook into:

- `pad_input.{h,cpp}` — the single translation seam. Phases 0, 1 and 2.
- `pad_actionmenu.{h,cpp}` — `CSWGuiActionMenuIos` suppression. Phase 4.
- Seam edits: `menus_dispatch.cpp` (calls the seam, F1 gate corrected),
  `engine_input.cpp` (log annotation), `cycle_input.{h,cpp}`
  (`DispatchPadAction`), `help.{h,cpp}` (`HandleNavCode` / `ToggleMenu` /
  `SpeakContextHelp` + the Controller section), `interact_dispatch.{h,cpp}`
  (`InteractNarratedTarget`), `engine_keymap.{h,cpp}`
  (`ForwardBackwardCommanded` / `AnyMovementCommanded`), `engine_panels.{h,cpp}`
  + `menus_chain.cpp` (`PanelKind::GamepadQuickMenu` + its decorative filter),
  `map_ui_cursor.cpp` (stick pan), `core_tick.cpp` (two new phases), `strings*`
  (13 new localised strings, 7 languages).

## The D-Pad is the engine's action menu — and the mod takes it

The single most important fact in this file, and the one every earlier round
got wrong. During world play the D-Pad drives `CSWGuiActionMenuIos`: **left and
right step the nine categories, up and down step the entries in the open
category, A confirms.** For a sighted player it is not a menu you open — it is
simply there, over a running world, always one thumb away.

The engine's menu stays suppressed either way (`pad_actionmenu.cpp`); what
changed is what the mod puts in its place.

**First attempt — adopt the idiom.** The D-Pad drove the mod's own unified
action menu in a LIVE mode: never paused, never closed after firing, always
under the thumb. Because the D-Pad then had two jobs — FINDING a thing and
ACTING on it — and each wants all four directions, the left trigger switched
between them and the mode announced itself.

**Rejected after the first live round (2026-08-08).** The always-open surface
cost the pad its A button: with the mode on, A always belonged to the menu, so
interacting with anything meant switching the mode back first. And the menu's
own close was undone by the next press, which re-opened it from the mode. In
practice the "saved" binding was not saved at all — you switched to the mode you
needed anyway.

**As built now.** The D-Pad has ONE world job, the object cycle. The left
trigger opens the unified action menu and closes it again — a plain toggle, the
pad's Shift+Enter and Esc, with B as the second way out. The menu is the same
menu the keyboard opens, including the "Action Menu" auto-pause option: with it
on, opening pauses the world and firing keeps the menu open so several actions
queue into one round; with it off, the world runs and firing closes the menu out
of combat. LIVE mode is deleted — one menu, one behaviour, whichever device
opened it.

**The pad binding set, as built.** In menus the pad simply IS the keyboard,
with two additions: on the container and the store panels LB and RB carry the
keyboard's Q / E mode toggle (take vs give, buy vs sell), because the engine
leaves the shoulders free there. On the map screen the left stick pans the
virtual cursor and the D-Pad runs the mod's Map cycle — left / right for the
previous / next map hint, up / down for the category — which is the same
mapping it has in the world, and the same thing the keyboard's `,` / `.` do
there. In the world:

- LT (alone) — open the unified action menu, or close it if it is up
- D-Pad left / right — previous / next object
- D-Pad up / down — previous / next cycle category
- Action menu open, D-Pad left / right — previous / next action category
- Action menu open, D-Pad up / down — previous / next entry
- A — default action on the narrated target (the keyboard's Enter, not the
  engine's default-action-on-last-clicked); with the action menu open, fire the
  selected entry, because the menu is an overlay and claims the press first
- B — close the action menu when it is up; otherwise the engine's own
- RT (alone) — announce the facing in degrees (the keyboard's AltGr)
- LT + LB — audio beacon to the focused object
- RT + RB — walk to the focused object
- LT + RT — the current screen's keys (the keyboard's Ctrl+F1)
- LT + X — own status (the keyboard's H); RT + X — the action queue (Shift+H).
  Dispatched from the XInput sample (the only reader that sees a trigger), and
  the engine's X event is consumed while a trigger is held so the party leader
  does not also switch under the chord
- Right stick press — camera orient (the keyboard's N)
- LT + A, action queue open — clear the whole queue (the keyboard's Shift+Enter,
  which the queue reads as a physical Shift the pad cannot hold)
- Y — in the world, the engine's Quick Menu, now spoken; its **Help** entry opens
  the mod's key list instead of the engine's controller picture. In a MENU it is
  the mod's peek modifier: held, the D-Pad's up / down read the focused entry's
  description block by block (the keyboard's Shift+arrow). The modifier is the
  XInput button state, so the engine's menu-class Y event is simply consumed
- X (Switch Party Leader), B, LB / RB alone, Back (Options Menu),
  Start (Pause Combat), L3 (Flourish) — left to the engine

**Why H is on a chord and not a button.** Every XInput-visible button already
has a job the dev uses — Start pauses, Back opens Options, L3 flourishes, R3
orients the camera, X switches the leader, Y is the Quick Menu. The one spare
button on the hardware is the Series pad's **Share**, and no game-facing API
reports it (see the Share note in `docs/llm-docs/k2-controller-support.md`);
reaching it would take a raw-HID reader, which was weighed and declined. So H
and Shift+H sit on the triggers, where the mod already has a modifier.

Each trigger's solo job fires on RELEASE, and a chord taken during the hold
cancels it. This is not a user-facing rule — it is only what makes LT+LB a
beacon rather than a beacon plus an unwanted action menu, and firing on press
cannot do it because on press we do not yet know a chord is coming.

The solo jobs are only CLAIMED where the triggers are sampled (`pad::Tick`, in
OnUpdate); they are RUN by `pad::PollTriggers` from the input-poll slot, both
games. LT opens the unified action menu, and the menu's hard rule forbids
populating it from OnUpdate — that is the phantom-confirm bug. `PollTriggers`
also runs after `PollButtons` so a menu opened this tick is not fired into by
an A press sampled in the same tick.

**Not bound on the pad:** nearest / farthest object (keyboard Ctrl+`,` /
Ctrl+`.`) and repeat-focus (keyboard `-`). Both lost their LT + D-Pad home when
the trigger layer was rebuilt, and neither was worth a new one.

Engine reference for everything below: **`docs/llm-docs/k2-controller-support.md`**.
Read it before Phase 3 or later; Phases 0–2 need only the code table repeated
here.

## Goal

KOTOR 2's Steam/Aspyr build ships working gamepad support. Make our mod fully
usable with a controller: everything the mod speaks for keyboard users must
speak for pad users, every mod feature must be reachable from the pad, and the
two pad-only overlay surfaces must be readable.

**There is no "controller GUI mode" to support.** The pad drives the same `_p`
panels; Aspyr added overlays on top. Do not go looking for a GUI switch — there
isn't one (verified: no `.gui` swap, no ini key, no 2DA, no mode flag).

## The one thing to internalise first

The pad reuses the **keyboard InputIndex numbering**. `InputIndexName` therefore
prints misleading keyboard names for pad events. When reading a log, translate:

At `CSWGuiManager::HandleInputEvent` — our `OnHandleInputEvent` hook, both in
menus and in the world:

- 47 (logs `KEYBOARD_F9`) = D-Pad **left**
- 48 (`KEYBOARD_F10`) = D-Pad **right**
- 49 (`KEYBOARD_F11`) = D-Pad **up**
- 50 (`KEYBOARD_F12`) = D-Pad **down**
- 51 (`KEYBOARD_A`) = left stick **vertical**, `val` −1 = up, +1 = down, 0 = centred
- 52 (`KEYBOARD_B`) = left stick **horizontal**, −1 = left, +1 = right, 0 = centred
- 0x27 (`F1(0x27 activate-code)`) = **A** button
- 40 (`KEYBOARD_F2`) = **B**, 41 (`KEYBOARD_F3`) = **X**, 42 (`KEYBOARD_F4`) = **Y**
- 53 (`KEYBOARD_C`) = **LB**, 54 (`KEYBOARD_D`) = **RB**, 46 (`KEYBOARD_F8`) = **Back**

At `CClientExoAppInternal::HandleInputEvent` — our `OnClientHandleInputEvent`
hook, in-world only:

- 204 = LB, 205 = RB, 224 = Start, 11 = Back
- ~~65 and 66 = left-stick movement~~ — **REFUTED. They are the mouse X and Y
  axes.** See Phase 2.

**LT and RT produce nothing at all.** The joystick reader never emits the lZ
axis, which is where both triggers live. Confirmed live: zero log lines.

### Telling pad from keyboard

Codes 47–54 are also real `bindablekeys.2da` rows (F9–F12, A, B, C, D). They are
shared numbering, not pad-exclusive.

- **51 / 52** — the pad sends `val` = ±1 and the engine's own preamble keys on
  exactly that. Use `val == 1 || val == -1` as the discriminator. This is
  engine-native, so it cannot drift.
- **47–50** — F9–F12 are unbound in stock `[Keymapping]`, and the engine drops
  unbound scancodes before our hook, so in practice these can only be the pad.
  Breaks only if a user binds F9–F12 in the game's own key-mapping screen.
  Acceptable; note it in the code comment.
- **0x27** — physical F1 or pad A. Discriminate with
  `GetAsyncKeyState(VK_F1)`: F1 physically down means F1, otherwise pad A.

## Confirmed defects (from the 2026-08-06 test round)

Evidence: `<K2 install>\logs\patch-20260806-132454.log`.

**D1 — pad A activates nothing, anywhere.** `menus_dispatch.cpp:304` (`if
(param_1 == kInputActivate)`) unconditionally swallows 0x27 so a physical F1
cannot double as Enter in menus. The pad's A button *is* 0x27, so every A press
is eaten (`HELP-F1-SUPPRESSED` ×25 in the log). **The comment at
`menus_dispatch.cpp:297` — "0x27 here is unambiguously F1" — is now false and
must be corrected.**

**D2 — menus announce several entries per press.** We do not consume the pad nav
codes, so the engine runs its own navigation, which emits multiple
`SetActiveControl` events for one press (observed: Auto-Pause → Feedback →
Auto-Pause) and the focus monitor speaks each. Keyboard nav logs as `CONSUMED`
and yields exactly one. **Same root cause** as the unreliable Options tabs, the
"just clicking" on tabs, and the wrong announcement order — with a pad the user
is hearing the *engine's* navigation model, not our chain's.

**D3 — pad B backs out silently.** Our Esc path matches `kInputEsc1`/`kInputEsc2`
(0xb4 / 0xdf, pre-translation logical Esc). Pad B arrives as 0x28, so the engine
closes the panel and our close-and-announce path never runs.

**D4 — drive-loop silence never arms on stick movement.**
`audio_footstep_suppress.cpp` arms it from `engine_keymap::ForwardBackwardKeyHeld()`
(`engine_keymap.cpp:486`), which polls keyboard VKs only. A stick-driven walk
holds no key, so the loop never arms and a wheeled droid grinding a wall keeps
sounding. This is the confirmed cause of the reported symptom.

Related but **not** shown to be broken: the same file's stuck heuristic
documents its thresholds as assuming **"no analog input"**
(`audio_footstep_suppress.cpp:32`), a premise pad support invalidates. The test
round did not actually catch it failing — at the wall the log recorded
`stuck=0` with `wspeed=1.216 m/s`, i.e. real displacement, so the detector was
behaving correctly on the input it saw. Treat the stale premise as a
**verify-then-decide** item, not a known defect: re-check it under a genuine
partial-tilt stick push into a wall before touching any threshold.

**D5 — the Quick Menu (Y) is silent.** Expected — but the stated reason ("it is
not built from `CSWGuiControl`s so the chain cannot see it") is wrong. It is,
and the chain can. See Phase 3.

## Defects from the third round (2026-08-06) — all fixed, none re-tested

Evidence: `<K2 install>\logs\patch-20260806-163136.log`.

**D6 — the Quick Menu reads "control N" every other press.** Not a silent
menu: the chain sees the panel and reads the eight captions correctly, but also
the 14 captionless icon quads sitting between them. Fixed with a decorative
filter — Phase 3.

**D7 — pad navigation never cancels the previous entry's speech.** Every menu
announcement in the mod speaks with `interrupt=false`, which works on the
keyboard only because the screen reader itself cuts queued speech on a
keypress. A pad press produces no keystroke, so entries queue and the lag grows
with every press. Fixed by issuing the cancel ourselves at the pad seam
(`NoteNavPress` → `prism::Silence()`) — precisely what the reader would have
done. Deliberately NOT fixed by flipping the announce paths to `interrupt=true`:
that would change keyboard behaviour and break the announcements that
intentionally queue (title, then focus).

**D8 — on the map screen the left stick navigated menu entries.** The map's
virtual cursor is the analog surface there, and the stick is the natural driver
for it — it is the screen's twin of walking. Fixed: the seam swallows the axis
events while `map_ui_cursor::IsActive()`, and the cursor's pan vector falls
back to `pad::StickVector()` when no walk key is held. Keys still win when both
are given.

**D9 — in combat the pad could only ever attack.** A fires the default action,
which on a hostile is Angreifen, and that was the pad's ONLY way to act on a
target: the other eight categories (force powers, medpacs, grenades, a
different attack) sat behind RT + D-Pad down. That binding worked, but the
ergonomics were wrong — mid-combat a chord is a lost round, and the log shows
the user never found it (`Pad: trigger state` fires 12 times, never with a
D-Pad press inside the hold).

Fixed first by moving the action menu to a **bare X press** — then, one round
later, **superseded entirely**. X is gone and back to the engine's Switch Party
Leader; the action menu lives on the D-Pad. See "The D-Pad is the action menu"
near the top of this file.

The X fix was right about the problem and wrong about the answer. It reasoned
that the gesture reaching the other eight categories must not be a chord, and
put it on the nearest free button. The real answer was that it should not be a
*gesture* at all: the engine had already put that surface under the D-Pad, and
the mod was consuming the codes that drove it.

**Answered: how a SIGHTED pad player reaches the engine's action menu.** With
the D-Pad. And the reason `Pad.ActionMenu` logged zero lines across every round
is not the A-button inference this file recorded — it is that Phase 1 consumed
all four D-Pad codes in the world from the day it landed, so the engine's menu
was never reachable while the mod was loaded.

The general lesson is the same one the codes-65/66 refutation taught, from the
other direction: **"nothing happens" from a blind test round means "nothing was
announced".** A purely visual effect and no effect at all are indistinguishable
from the tester's seat, and the difference has to come from somewhere else — a
decompile, a log line, or a sighted pass.

**Not a defect — the empty cycle in a fresh area.** D-Pad category stepping
found nothing anywhere, but the log shows why and it is correct behaviour: the
discovery set for that module loaded with **0 keys**
(`Discovery: loaded set var=ACC_DISC_106per keys=0`), and `,`/`.` drive the
discovery tier by default. `BuildListing` scanned 194 objects, saw 14 doors and
43 placeables, and filtered all of them out because none had been narrated
there yet. Nothing was discovered because no passive narration fired in that
area — the player arrived and went straight into dialogue. Turning on
"Extended cycling" in mod settings widens the same keys to everything in the
area, which is the answer for a player who wants to sweep an unfamiliar room.

## Phase 0 — unblock menus — IMPLEMENTED

Fixes D1, D2, D3 together. Small, and the payoff is that pad menu navigation
becomes byte-for-byte the keyboard experience.

**Design decision already made: normalise, don't branch.** Add one translation
seam at the top of `OnHandleInputEvent` (`menus_dispatch.cpp`) that rewrites pad
codes into the existing logical codes *before* any existing handler runs. No
downstream handler changes. Do **not** add pad cases to `menus_chain_input.cpp`'s
four handlers — that would scatter the same knowledge across four sites.

Mapping to apply in the new seam:

- 49 → `kInputNavUp`, 50 → `kInputNavDown`, 47 → `kInputNavLeft`, 48 → `kInputNavRight`
- 51 with val −1 → `kInputNavUp`; 51 with val +1 → `kInputNavDown`
- 52 with val −1 → `kInputNavLeft`; 52 with val +1 → `kInputNavRight`
- 0x27, only when `GetAsyncKeyState(VK_F1)` says F1 is **not** down → `kInputEnter1`
- 40 (pad B) → `kInputEsc1`

Two mechanics to get right:

- **Axis release pairing.** Codes 51/52 send `val = 0` on centring, and that
  release carries no direction. Keep a tiny per-axis "last direction" latch so
  the release is rewritten to the same logical code the press became — otherwise
  the `PAIR-CONSUMED` release tracking in `menus_dispatch.cpp` mismatches and a
  later real press gets swallowed.
- **D-Pad has no release event.** The log shows `val=1` only, never a release.
  So no pairing work is needed there — but do not *assume* a release will come.

Also in this phase:

- Correct the false comment at `menus_dispatch.cpp:297`. **Done** — and the
  gate itself now asks `acc::pad::IsPhysicalF1()`, so it is a second lock on
  the same door rather than a comment.
- Make `InputIndexName` annotate pad-plausible codes so future logs are
  readable — e.g. print `KEYBOARD_F12(50) [pad D-Pad down?]`. Do **not** rename
  outright: the numbering is genuinely shared with real keyboard rows. **Done**
  — `acc::pad::CodeHint`, appended by `InputIndexName`.

**Built stronger than planned in one place.** The plan accepted "a user who
binds F9–F12 in the game's own key-mapping screen breaks the D-Pad" as a known
caveat. The seam instead asks `GetAsyncKeyState` for the twin key of every
shared code (F9–F12, A, B, F1, F2) and stands down when the physical key is
actually held, so the caveat is closed for one OS call per event.

**Test for Phase 0** (pad connected *before* launch — device enumeration happens
once at input init):

1. Main menu: D-Pad up/down, then left stick up/down. Expect exactly one spoken
   entry per press, same as the keyboard.
2. Options: D-Pad down four times. Expect one entry per press — this is the
   direct regression test for D2.
3. Options tabs: D-Pad through them. Expect our order, not the engine's, and no
   stray clicking.
4. Press A on a settings entry. Expect it to activate — the D1 regression test.
5. Press B. Expect the back sound **and** the panel-name announcement.
6. Press physical F1. Expect the keybind list to open, not an activation — this
   is the D1 discriminator test and it must not regress.

## Phase 1 — in-world bindings — IMPLEMENTED

**The key finding that shaped this phase was wrong.** It read: "the D-Pad is
completely free in the world — all four codes arrive at the manager during
world play, nothing consumes them, nothing happens." The codes do arrive at a
hook we already own, which is the part that held; but the engine consumes them
to drive `CSWGuiActionMenuIos`, and "nothing happens" was a blind reading of a
silent visual highlight. The binding set below was rebuilt on the corrected
premise — see "The D-Pad is the action menu". What survives unchanged: the
manager hook is the right place, no new input reader is needed, and the cycle
bindings themselves are exactly as described (they are now one of the D-Pad's
two modes rather than its only job).

Reading the triggers needs **XInput**, not DirectInput: in DirectInput both
triggers share the single lZ axis and cancel each other out, so LT cannot be
told from RT. `bLeftTrigger` / `bRightTrigger` are separate under XInput, and
XInput coexists with the engine's grab (the engine holds the pad
`DISCL_BACKGROUND | DISCL_NONEXCLUSIVE`). Poll it from `core_tick.cpp`, edge-
detect with a static `prev`, and gate on foreground — the same shape as the
existing `cycle_input.cpp::PollWin32`.

Chords (LT/RT + an engine-used button) require consuming the second button at
the manager or the client hook. The mechanism already exists — the
modifier-shadow consume in `menus_dispatch.cpp` / `input_pipeline.cpp` — and pad
buttons arrive there as ordinary codes, so it transfers directly.

**As built.** XInput is resolved at run time (`xinput1_4` → `1_3` → `9_1_0` via
`LoadLibrary`, never linked or delay-loaded — a delay-load that fails to
resolve takes the process down with 0xC06D007F). Slot search is throttled to
one sweep per 2 s while no pad answers. Trigger threshold is 60 of 255, firmer
than XInput's own 30: these are a MODIFIER, and one that engages on a resting
finger would silently reroute the D-Pad.

**The chord consume the first cut skipped is now required.** That cut reasoned
"every trigger binding pairs with the D-Pad, which the engine ignores in the
world anyway" — the same wrong premise, and it collapsed with it. The chords
now pair with the shoulders, which the engine really does use (Cycle Target
left / right), so they are claimed at `OnClientHandleInputEvent` via
`pad::TranslateClientEvent`. That is the route the engine performs the action
on; consuming at the manager would leave it running alongside ours.

**The one genuinely dangerous code.** The right stick press arrives in-world as
InputIndex `0x01`, which is also `MOUSE_BUTTON1` — the right mouse button.
Consuming it blindly takes mouse-look away from every player with a pad plugged
in. `TranslateClientEvent` checks `GetAsyncKeyState(VK_RBUTTON)` first, the
same twin-key discriminator the shared manager codes use.

The trigger threshold is 60 of 255, firmer than XInput's own 30: a trigger that
engaged on a resting finger would silently switch the D-Pad's mode.

## Phase 2 — analog movement — IMPLEMENTED (on a corrected premise)

**The first round's movement signal was a misreading, and the correction is
the interesting part of this phase.** The round reported that holding the left
stick produced, at `OnClientHandleInputEvent`, "code 65 = the held direction,
rock-stable, five distinct values (848, 852, 891, 1743, 2015)" and "code 66 =
continuously varying, a sawtooth in steps of 87".

**Codes 65 and 66 are the mouse X and Y axes.** The second round
(`patch-20260806-162206.log`) settles it arithmetically:

- Every value 65 takes is exactly a `MoveMouseToPosition` **X** the mod itself
  issued while walking the navigation chain — 1743 is the main-menu button
  column, 852 and 891 are other panels' control columns. Five distinct values
  because the mod warps the cursor to a small set of control positions, not
  because a stick rested in three directions.
- The "sawtooth in steps of 87" is the main menu's **row spacing**: its buttons
  sit at y = 831, 918, 1005, 1092, and 66 tracks cursor Y offset by a constant
  border inset (918 → 881, 1005 → 968). The analog sweep was our own chain
  stepping down the button column.
- They arrive on the **title screen**, where no world and no player exist, and
  they arrived in a session where the engine had enumerated no joystick at all.

Worth keeping as a general lesson: an analog-looking value stream observed at an
engine hook may be the mod's own cursor warps coming back round. Correlate
candidate analog values against `MoveMouseToPosition` before believing them.

**As built.** The stick is read directly through XInput (`sThumbLX` /
`sThumbLY`), sampled in the same `pad::Tick()` that already polls the triggers —
no engine signal involved. `acc::pad::StickMoving()` is true while the squared
magnitude clears a 12000-of-32767 dead zone, above XInput's own 7849 because
the question is "is the player commanding translation", not "has the stick left
centre".

This is not a fallback; it is the better mechanism. The shipped binding chart
puts Move on the left stick and Rotate Camera on the right, so the left stick's
magnitude IS the translation signal — sharper than the keyboard heuristic it
joins, which cannot distinguish a walk key from a strafe key without the axis
buckets.

The union lives in `engine_keymap`, not at the call sites: the two consumers
(`ForwardBackwardCommanded` for the drive loop, `AnyMovementCommanded` for the
autowalk cancel) both want the same answer, so "…or the pad" written twice would
be the same knowledge in two places. The keyboard-only queries are untouched and
still exported.

Thresholds in the stuck heuristic were NOT touched, per the verify-then-decide
note on D4.

**Direction announcements are NOT a defect — item closed 2026-08-06.** The
earlier report of "no direction announcement while moving with the stick" was
explained by the user: the stick was held in a single direction for the whole
stretch, so the facing never changed and no sector was ever crossed. Silence is
the correct behaviour — `camera_announce.cpp` announces sector *crossings*, and
there were none. Nothing to fix.

If this ever needs a real test, it has to be one that actually crosses a sector
boundary: rotate with the **right** stick past a cardinal boundary and listen for
the direction word. The open question that test would answer — and the only one
worth spending a round on — is whether continuous stick rotation ever settles
long enough to satisfy the announcement's stability quiet window, which a
keyboard tap-rotate trivially does.

## Phase 3 — Quick Menu navigator — IMPLEMENTED, then DELETED as unnecessary

The plan called for a dedicated navigator reading `+0x68` / `+0x6c`, on the
premise that the Y menu's widgets were custom textured quads the navigation
chain could not see. **That premise is wrong** (third round,
`patch-20260806-163136.log`): the Quick Menu is an ordinary `CSWGuiPanel`
pushed onto `modal_stack` with 22 `CSWGuiButton` children, and the chain walks
it and reads the real captions — "Menüs", "Gruppenanführer", "Einzelmodus",
"Stealth-Modus", "Schnellspeichern", "Freie Sicht", "Waffe wechseln", "Hilfe".

The navigator was not merely redundant, it was harmful: it stood the D-Pad down
while armed, so on any machine where the offsets resolved it would have shut
the working chain out entirely. (In the test round its own read failed and it
disarmed immediately, which is the only reason the chain got the keys and the
truth became visible.) `pad_quickmenu.{h,cpp}` and its eleven strings are
deleted.

**What the panel actually needs** is a decorative filter. Fourteen of the 22
children are icon and background quads — same `CSWGuiButton` class, every `id`
is `-1`, so neither class nor `.gui` id tells them apart. What does: they carry
no caption by any route, and their `is_active` / `bit_flags` read as garbage.
Unfiltered the chain is 22 entries long and every other press speaks the
"control N" placeholder, which is exactly the symptom the round reported.

As built: `PanelKind::GamepadQuickMenu`, identified by vtable `0x0099dd3c`
(K2-only, so the constant poisons to 0 on KOTOR 1 and the detector declines
there without a read), and one clause in `IsDecorativeControl` dropping any
child the extractor cannot get text out of. Everything else — navigation,
activation, close — is the chain the mod already had.

The Party Leader sub-list is untested: entry 1 expands into three items, and
whether those arrive as fresh captioned children (which the chain would pick up
on its next rebind) or as a state change inside the same widgets is the one
open question left on this panel.

## Phase 4 — the action menu — IMPLEMENTED (the riskiest piece; read this)

`CSWGuiActionMenuIos` is the pad's equivalent of our unified action menu: nine
categories (six personal action columns + three target-action rows), each
expanding to a variant list; D-Pad up/down steps variants, A activates, B closes.

**Decision already taken: suppress theirs, open ours.** Our unified menu covers
the same nine categories, drives the same engine calls, already speaks, and is
already tested. Reading theirs would mean reverse-engineering a second custom
widget tree across two levels for no functional gain.

**Level 1 is D-Pad left / right** — settled in the fourth round. So suppressing
their menu does not merely free an input, it frees the whole D-Pad. The mod
first kept their *idiom* while replacing their *menu*; that was reverted, and
the freed D-Pad now carries the object cycle while the left trigger opens the
mod's menu. See "The D-Pad is the engine's action menu — and the mod takes it".

**As built.** `pad_actionmenu.cpp` watches `DAT_00a10340` every tick and, on an
open edge, logs the category and writes -1 (the engine's own "nothing open"
value, which its draw and input paths already handle). It does not open ours in
its place: arming a menu from a tick would break the unified menu's hard rule
about input context, and the player's own left trigger is the way in.

Two things to watch:

1. **`Pad.ActionMenu` lines should never appear.** The four D-Pad codes are
   consumed in the world before the engine sees them, so its menu has no way to
   open. A line in a log means a press got past the seam — that is the thing to
   investigate, not the close.
2. **This is the one place that WRITES engine state.** If anything looks odd
   right after an action — a stuck highlight, a frozen category — the write is
   the first suspect, and backing it out is a two-line change (drop the
   `CloseEngineMenu()` call, keep the log). The write is SEH-guarded and every
   occurrence is logged.

## Phase 5 — help for pad users — IMPLEMENTED

The engine's own Help panel is a bare image of the controller
(`cntrl_xb360_<lang>` / `cntrl_ps3_<lang>` textures), so it tells a blind player
nothing. Add a spoken key list for pad bindings, mirroring what F1 does for the
keyboard. Arguably this should move earlier: without it a pad user has no way in
at all.

**As built.** A ninth group, `Grp::Controller`, in the existing `help.cpp`
catalog — same catalog, same F1 list, same Ctrl+F1 context summary, so the pad
lines are not a separate surface to maintain. Thirteen entries, tagged for the
screens they apply to.

Every controller row carries a `padOnly` flag and is filtered by one predicate
(`acc::pad::Connected()`), so the F1 list and the Ctrl+F1 summary cannot
disagree about whether the section exists. With no pad the group emits nothing —
not even its header — so a keyboard player's list is byte-for-byte what it was.

`Connected()` is sticky and satisfied by either route: XInput reporting a pad,
or an unmistakably pad-shaped event arriving at the GUI manager (which covers
pads XInput does not enumerate). Only two event shapes count — a stick axis
carrying `val = ±1`, and a D-Pad code — because the face and shoulder codes are
shared with C, D and the function keys, which a keyboard-only KOTOR 2 session
really does produce.

The "no way in at all" worry is closed from the other end too, and the fourth
round improved on how. **Both triggers together** speak the current screen's
keys. The list itself is reached through the engine's own affordance: the Y
Quick Menu's eighth entry is **Help**, and what it opens is exactly that
useless controller picture — so the mod claims that entry and opens its own key
list instead. The entry now does what it always promised, no binding was spent
on it, and a pad player looking for help finds it where the game already says
it is. Navigation is the D-Pad (`help::HandleNavCode`) as before.

The entry is identified by position — after the decorative filter the chain is
exactly the eight captioned entries in `gamepad.txt`'s own order, Help last —
and the count is checked first, so if the filter ever lets a quad through the
mod declines rather than firing the wrong entry.

## Combined test round (all six phases in one pass)

Everything below landed together, so one round covers it. Order matters only in
that the menu steps prove the seam before the world steps rely on it.

**Menus (Phase 0).**

1. Main menu: D-Pad up/down, then left stick up/down. Exactly one spoken entry
   per press, same as the keyboard.
2. Options: D-Pad down four times. One entry per press — the direct D2
   regression test.
3. Options tabs: D-Pad through them. Our order, not the engine's, no stray
   clicking.
4. A on a settings entry — it activates. The D1 regression test.
5. B — the back sound AND the panel-name announcement.
6. Physical F1 — the keybind list opens, nothing activates. The D1
   discriminator, and it must not regress.
7. With the list open, navigate it with the D-Pad and close it with B.

**World — the D-Pad and the action menu (Phase 1).**

8. D-Pad left/right steps objects, up/down steps cycle categories — the same
   speech the keyboard `,` `.` produce. This is the D-Pad's only world job.
9. A on a focused door / container / NPC — it opens / loots / talks, i.e. what
   keyboard Enter does. There is no state that can make A mean anything else.
10. Pull LT and let go. The action menu opens and speaks its category, exactly
    as keyboard Shift+Enter does — including the "Action Menu" auto-pause
    option: on → the world's pause cue; off → the world keeps running.
11. Left/right steps categories, up/down steps entries, A fires.
12. Fire something. Paused (option on, or in combat): the menu stays open on
    the same entry and the action answers "…, Platz N", so a second A queues
    another. World running out of combat: the menu closes, matching the mouse
    radial and the keyboard.
13. Pull LT and let go again — the menu closes. Repeat with B: same close. After
    either, the D-Pad steps objects again and A interacts again.
14. **The collision test.** Pull and hold LT, press LB, release LT. Expect the
    beacon only — and NO action menu. Then the same with RT + RB (autowalk).
    Press each a second time to cancel.
15. Pull RT alone and let go — the facing in degrees, same phrasing as keyboard
    AltGr. Then on the map screen, same button: the map variant of that line.
16. Hold LT, pull RT, release both — this screen's keys. Neither the action menu
    nor degrees may also fire.
17. Press the right stick — the camera turns to the beacon's next waypoint, or
    to the next compass direction with no beacon armed. Then, **with a mouse
    connected, right-click**: mouse-look must still work. That is the
    `MOUSE_BUTTON1` guard, and it is the one regression here that would hurt a
    player who never touches a pad.
18. Press X — the engine's Switch Party Leader, back where it was. It must no
    longer open the action menu.

If the triggers do nothing at all, grep the log for `Pad: XInput` — either the
bind line naming the DLL, or the "no XInput DLL resolved" line. If a chord
fires its action *and* the engine's (a beacon plus a target cycle), the consume
is not landing: grep `Diag.ClientHIE` for `CONSUMED — pad binding`.

**World — drive loop (Phase 2).** With T3-M4 (or any wheeled droid) as leader,
push the LEFT STICK into a wall and hold. The drive loop should go silent after
roughly a second, and stay silent until you actually move off. Grep
`FootstepSup.rolling` for `pushing=1` — that flag being 1 with no key held is
the whole point of the phase. `Pad.stick` logs the edges of the same signal, so
if `pushing` stays 0, that line says whether the stick read reached us at all.

**Quick Menu (Phase 3).** Y in the world. Expect "Quick menu" then the focused
entry; D-Pad steps entries and each is spoken; Party Leader expands and speaks
party names; A activates, B or Y closes. Then confirm the D-Pad is back on
whichever mode it was in afterwards — a stuck arm would leave it dead, and that
is the failure mode worth checking explicitly.

Then step to the LAST entry, **Help**, and press A: the mod's key list must
open, not the controller picture. If the engine's picture appears instead, grep
for the `Quick Menu chain has N entries` line — the decorative filter let
something through and the mod declined on purpose.

**Action menu (Phase 4).** Grep the log for `Pad.ActionMenu`. Silence is the
pass: the D-Pad codes are consumed before the engine sees them, so its menu
cannot open. Any line means a press got past the seam.

**Help (Phase 5).** Open the key list from the Quick Menu's Help entry and read
to the end: a "Controller" section with fourteen lines should be there with a
pad connected, and completely absent without one.

## How to run and test

- **Plug the controller in before launching.** `CExoInput` enumerates game
  controllers exactly once at input init; a pad connected later is invisible.
- `kdev apply --game k2` with the game closed, then launch. Watch for the stale-
  DLL trap: apply silently skips the copy if the game holds the DLL open —
  verify `patches/accessibility.dll` mtime after applying.
- Logs: `<K2 install>\logs\patch-*.log`. The manager hook logs every arriving
  code unconditionally, so no extra instrumentation is needed for input work.
- **Nudge sticks, don't hold them.** The joystick reader emits an axis event
  every frame while a stick is off-centre and there is no repeat gate on the
  axis path (only the D-Pad has one, 150 ms at `manager+0x72`). Holding a stick
  writes hundreds of lines.
- If the log shows continuous axis chatter while nothing is touched, the pad has
  stick drift and we need our own dead zone on top of the engine's 25%.

## Not doing

- Rebinding the engine's own pad bindings. They are hard-coded in the exe (not
  in `keymap.2da`, `bindablekeys.2da`, or any ini) and would need a detour of the
  mapping table in `CExoInputInternal::GetEvents`. No reason to, while LT/RT and
  the in-world D-Pad are free.
- Anything with the `_x` (Xbox) `.gui` files. They are dead legacy data; the
  Steam build does not use them.
