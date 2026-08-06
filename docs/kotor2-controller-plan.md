# KOTOR 2 — controller support plan

**Status: ALL PHASES IMPLEMENTED, STILL EFFECTIVELY UNTESTED (2026-08-06).**
Phases 0–5 are written and the patch builds clean. A second live round ran but
tested nothing: the controller was connected AFTER launch, so `CExoInput` — which
enumerates game controllers exactly once at input init — had no joystick device
and the engine emitted no pad event at all. The log
(`patch-20260806-162206.log`) contains zero codes in the 47–54 range and no
`Pad:` lines beyond XInput binding, i.e. nothing of ours had anything to act
on. **The next round must have the pad connected before launch.**

That round was not wasted, though: it refuted the first round's reading of the
in-world movement signal. See Phase 2.

This file is written to be executed cold: a fresh session should be able to work
straight through it without the investigation conversation.

**Where the code lives.** Everything pad-specific is in three new module
pairs, plus small edits at the seams they hook into:

- `pad_input.{h,cpp}` — the single translation seam. Phases 0, 1 and 2.
- `pad_quickmenu.{h,cpp}` — the Y-button Quick Menu navigator. Phase 3.
- `pad_actionmenu.{h,cpp}` — `CSWGuiActionMenuIos` suppression. Phase 4.
- Seam edits: `menus_dispatch.cpp` (calls the seam, F1 gate corrected),
  `engine_input.cpp` (log annotation), `cycle_input.{h,cpp}`
  (`DispatchPadAction`), `help.{h,cpp}` (`HandleNavCode` / `ToggleMenu` /
  `SpeakContextHelp` + the Controller section), `interact_dispatch.{h,cpp}`
  (`InteractNarratedTarget`), `engine_keymap.{h,cpp}`
  (`ForwardBackwardCommanded` / `AnyMovementCommanded`), `core_tick.cpp`
  (three new phases), `strings*` (24 new localised strings, 7 languages).

**The pad binding set, as built.** In menus the pad simply IS the keyboard.
In the world:

- D-Pad left / right — previous / next object in the current cycle category
- D-Pad up / down — previous / next category
- A — interact with the narrated target (the keyboard's Enter, not the
  engine's default-action-on-last-clicked)
- LT + D-Pad left / right — nearest / farthest object
- LT + D-Pad up / down — announce focus / walk to focus
- RT + D-Pad up / down — beacon to focus / open the unified action menu
- RT + D-Pad left / right — context help / the F1 list
- Y — the engine's Quick Menu, now spoken
- LB / RB, Back, X, Start — left to the engine

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

**D5 — the Quick Menu (Y) is silent.** Expected; it is not built from
`CSWGuiControl`s so the chain cannot see it. Phase 3.

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

**Key finding that shapes this phase: the D-Pad is completely free in the
world.** All four codes arrive at the manager during world play, nothing
consumes them, nothing happens. And they arrive at a hook we already own, so no
new input reader is needed for them.

Bindings to add:

- D-Pad left / right — previous / next object in the current cycle category
- D-Pad up / down — previous / next category
- LT and RT become modifier layers for the remainder: nearest, farthest,
  announce focus, walk to focus, beacon

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

No chord consume turned out to be needed: every trigger binding pairs with the
D-Pad, which the engine ignores in the world anyway, so nothing has to be taken
away from the engine. `A` is the one button we do claim, and it is claimed
outright rather than as a chord.

Two bindings beyond the plan's list, both because a pad player otherwise has no
route to them at all: **RT + D-Pad down** opens the mod's unified action menu
(the Shift+Enter gesture), and **RT + D-Pad left / right** reach the two help
surfaces. See Phase 5 — the engine's Help panel is a picture.

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

## Phase 3 — Quick Menu navigator — IMPLEMENTED

The Y-button menu (`CSWGamepadMenuIos`). Custom textured quads, not
`CSWGuiControl`s — the chain cannot see it. Build a dedicated navigator on the
Abilities-screen pattern (Strategy B in `docs/controller-mod-techniques.md` §5):
read the selection index and speak the label.

- Eight entries, fixed order: Menus, Party Leader, Solo/Party, Stealth,
  Quick Save, Free Look, Switch Weapons, Help.
- Labels come from `override/gamepad.txt` by line index — **line order is the
  API**, 8 strings × 5 languages (EN, FR, IT, DE, ES in that order).
- Selection index at `+0x68`, sub-selection at `+0x6c`, sub-count at `+0x70`.
  Entry 1 (Party Leader) expands to a 3-item sub-list. Entry 3 (Stealth) is
  skipped when the gate at `+0x7c` is clear.
- Offsets, addresses and the input handler are in
  `docs/llm-docs/k2-controller-support.md`.

**As built, with two deliberate departures from the sketch above.**

*Labels come from our own string tables, not from `gamepad.txt`.* That file is
read by line index in a fixed five-language block, so it speaks whatever
language the GAME is in rather than the language the MOD is set to, and it has
to be present and unmodified. The eight entries are fixed engine functionality,
so naming them ourselves costs nothing and keeps every spoken string in one
place. Eleven new `PadQuick*` ids, seven languages.

*The `+0x7c` Stealth gate is not read.* We read the live index, so an entry the
engine skips is simply an index we never see — there was nothing to handle.

The navigator arms on the Y press (`pad_input` routes it) rather than on a
state poll, and stands down on A, B, or a second Y — all three are cancels or
activations in the menu's own input handler. Three safeguards keep a stale arm
from silently costing the player their D-Pad bindings: the arm expires after
1500 ms if the engine never moves the index off -1 (its open path can refuse),
it drops when the state becomes unreadable, and it drops when the world goes
away.

Party Leader's sub-list speaks the party member's name for the slot, falling
back to "Slot N" when the roster cannot be read.

## Phase 4 — the action menu — IMPLEMENTED (the riskiest piece; read this)

`CSWGuiActionMenuIos` is the pad's equivalent of our unified action menu: nine
categories (six personal action columns + three target-action rows), each
expanding to a variant list; D-Pad up/down steps variants, A activates, B closes.

**Decision already taken: suppress theirs, open ours.** Our unified menu covers
the same nine categories, drives the same engine calls, already speaks, and is
already tested. Reading theirs would mean reverse-engineering a second custom
widget tree across two levels for no functional gain.

Still open before implementing: **which input picks the category (level 1)**.
The variant level is nailed; level 1 runs off a per-category active test in
`FUN_00746950` writing the global `DAT_00a10340`. One log line from a focused
test settles it, and it decides whether suppressing their menu also frees that
input for us.

**As built — and the open question is now answered by the log rather than
blocking on it.** `pad_actionmenu.cpp` watches `DAT_00a10340` every tick. On
the open edge it logs the category, writes -1 (the engine's own "nothing open"
value, which its draw and input paths already have to handle), and opens the
mod's unified action menu on the narrated target instead.

Two things to watch in the first test round:

1. **The likeliest outcome is that `Pad.ActionMenu` lines never appear at
   all.** The most plausible level-1 opener is A on a target — and the pad's A
   binding from Phase 1 already claims A in the world, so theirs would never
   open. If the log is silent, that IS the answer to the open question.
2. **This is the one place that WRITES engine state.** If the pad action menu
   misbehaves — a stuck highlight, a frozen category, anything odd right after
   an action — the write is the first suspect and backing it out is a two-line
   change (drop the `CloseEngineMenu()` call and keep the log). The write is
   SEH-guarded and every occurrence is logged.

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

The "no way in at all" worry is closed from the other end too: **RT + D-Pad
right** opens this list and **RT + D-Pad left** speaks the current screen's
keys, so a pad user reaches both help surfaces without touching a keyboard, and
navigates the list with the D-Pad (`help::HandleNavCode`).

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

**World — cycling (Phase 1).**

8. D-Pad left/right steps objects, up/down steps categories, each with the same
   speech the keyboard `,` `.` produce.
9. LT + D-Pad left/right — nearest / farthest object.
10. LT + D-Pad up — repeat focus. LT + D-Pad down — walks there; press again to
    cancel.
11. RT + D-Pad up — beacon; press again to cancel.
12. RT + D-Pad down — the unified action menu opens and speaks; D-Pad navigates
    it, A fires, B closes.
13. RT + D-Pad left — this screen's keys. RT + D-Pad right — the full list.
14. A on a focused door / container / NPC — it opens / loots / talks, i.e. what
    keyboard Enter does.

If the triggers do nothing at all, grep the log for `Pad: XInput` — either the
bind line naming the DLL, or the "no XInput DLL resolved" line.

**World — drive loop (Phase 2).** With T3-M4 (or any wheeled droid) as leader,
push the LEFT STICK into a wall and hold. The drive loop should go silent after
roughly a second, and stay silent until you actually move off. Grep
`FootstepSup.rolling` for `pushing=1` — that flag being 1 with no key held is
the whole point of the phase. `Pad.stick` logs the edges of the same signal, so
if `pushing` stays 0, that line says whether the stick read reached us at all.

**Quick Menu (Phase 3).** Y in the world. Expect "Quick menu" then the focused
entry; D-Pad steps entries and each is spoken; Party Leader expands and speaks
party names; A activates, B or Y closes. Then confirm the D-Pad is back on the
cycle bindings afterwards — a stuck arm would leave them dead, and that is the
failure mode worth checking explicitly.

**Action menu (Phase 4).** Grep the log for `Pad.ActionMenu`. Silence means the
engine's menu never opened (most likely — Phase 1's A binding would have
claimed its opener), which is a pass. Lines mean it did open, and the category
number in them answers the last open question in this file.

**Help (Phase 5).** Open the F1 list any way you like and read to the end: a
"Controller" section with thirteen lines should be there with a pad connected,
and completely absent without one.

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
