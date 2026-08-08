# KOTOR 1 — controller support (backport of the KOTOR 2 work)

**Status: CONFIRMED WORKING IN GAME (2026-08-08, first round).** Written as one
piece for a single combined test round, on the dev's explicit call: one full
backport to work on rather than one part after another. The round passed with
no defects reported.

Evidence: `<K1 install>\logs\patch-20260808-064640.log`. Menus navigate and
activate through the injected arrow cluster (33 injections, each followed by a
real chain rebind or step and a `Menus.Input ... CONSUMED`), the sticks walk
and turn, the action menu opens and drives, the trigger chords and
LB / RB / Start / Back / RT all fire, and all 30 synthesised movement presses
have their matching release — nothing was left held.

**Superseded by that round's own finding:** the left trigger no longer switches
a D-Pad mode. That round exposed what the mode cost — with action-menu mode on,
A belonged to the menu permanently, so interacting with anything meant switching
back first, and the menu's own B close was undone by the next press re-opening
it. The trigger is now a plain open/close toggle on the action menu and the
D-Pad has one job. See "The binding set" below; the mode is gone from both
games.

**Risk 1 below is settled: the extended-scancode arrows DO reach the engine's
DirectInput read.** That was the port's biggest unknown and it needs no
fallback.

**The one surface the round did not cover is the quick menu (Y)** — zero
`Pad.quick` lines in the log. Everything about it is untested: whether it
opens, reads, clamps, fires its five game actions, hands Help to the mod's key
list, and stands the left stick down while up.

Sibling document: `docs/kotor2-controller-plan.md`, which is where the binding
set, the D-Pad's two modes and the trigger layer were designed. **Read that
first.** This file records only what is different on KOTOR 1 and why.

Engine reference for KOTOR 2: `docs/llm-docs/k2-controller-support.md`.

---

## The finding the whole port hangs on

KOTOR 2 ships real gamepad support. **KOTOR 1 does not, and cannot be made to
without writing a DirectInput device ourselves.** Verified by decompile, not
inferred:

- `CExoRawInputInternal`'s constructor creates exactly two DirectInput devices,
  the system keyboard and the system mouse. It then zeroes the joystick device
  array, the joystick state array and the device count, and sets the
  `currentJoyStick` global to 0.
- No other code in the executable creates a third device. There is no
  `EnumDevices` call for game controllers, and the only two device GUIDs
  referenced are the system keyboard and the system mouse. The binary contains
  a 256-object keyboard data format and a 7-object mouse data format — and no
  joystick data format at all.
- `currentJoyStick` has exactly ONE reference in the whole binary: the write of
  0 in that constructor. Nothing ever reads it.
- The Aurora joystick plumbing is otherwise all still there.
  `CExoRawInputInternal::GetJoystickBuffer` is a near-twin of KOTOR 2's reader
  — buttons emitted at code `0x30 + index`, POV hats decoded into four
  directional pseudo-buttons, axes with a 25% dead zone — and
  `CExoInputInternal::GetEvents` calls it for every device past index 1. The
  `InputIndices` enum still carries `JOYSTICK_XAXIS`, `JOYSTICK_YAXIS`,
  `JOYSTICK_HAT`, `JOYSTICK_SLIDER0..2` and `JOYSTICK_BUTTON0..14`.
- All of it is dead: the read early-outs on the null device array before it
  touches anything.

So the KOTOR 2 model — "the engine hands us pad events, we translate the codes"
— has no KOTOR 1 counterpart. **On KOTOR 1 the mod is the pad driver.**

## What that changes, and what it does not

Only the event SOURCE differs. The meaning of every control is shared code:
`RouteToOverlay`, `DispatchWorldDpad`, `DispatchMapDpad`, the trigger layer and
the cycle bindings are the same functions on both games, so a binding is never
written twice and the two games play identically.

- **KOTOR 2** — engine events arrive at `TranslateManagerEvent` (GUI manager)
  and `TranslateClientEvent` (in-world). Unchanged by this port.
- **KOTOR 1** — `pad::PollButtons()` edge-detects the XInput button word that
  `pad::Tick()` sampled, and dispatches it through those same helpers.

Where the ENGINE has to act — walk, turn the camera, navigate one of its own
panels, fire a game action — KOTOR 1 synthesises the player's own bound key as
a DirectInput scancode (`key_inject`). That is the mechanism camera_orient's
snap-turn has always used, and it is the only one that works: the engine
consumes its movement and turn axes inside its own per-frame update, so writing
to them out of band moves nothing. It also means the pad in a KOTOR 1 menu is
*literally* the keyboard — every downstream ordering property (press/release
pairing, the modal-stack routing, the engine's own control focus) holds without
a second dispatch context.

`key_inject` is deliberately policy-free — no foreground gate inside it, because
a gate there would swallow the RELEASE of a key held when focus was lost, which
is the one failure this must never cause. Callers gate; `pad_movement` releases
everything through one `ReleaseAll()` on every exit path.

## The binding set

Identical to KOTOR 2's, including the left trigger's action-menu toggle — the
dev chose parity explicitly, so a player who owns both games learns one
controller.

In the world:

- Left stick — move (forward/back on Action280, strafe on Action281), eight-way
- Right stick — rotate the camera (Action284)
- LT alone — open the unified action menu, or close it if it is already up
  (the pad's Shift+Enter, and its Esc)
- D-Pad left / right — previous / next object
- D-Pad up / down — previous / next cycle category
- Action menu open, D-Pad left / right — previous / next action category
- Action menu open, D-Pad up / down — previous / next entry
- A — default action on the narrated target; with the action menu open, fire
  the selected entry (the menu is an overlay and claims the press first)
- B — with the action menu open, close it; otherwise the engine's own cancel
- RT alone — announce the facing in degrees
- LT + LB — audio beacon to the focused object
- RT + RB — walk to the focused object
- LT + RT — the current screen's keys
- LT + X — own status (the keyboard's H); RT + X — the action queue (Shift+H).
  Bare X still switches the party leader. Both chords are read from the XInput
  sample rather than from an X event, because only XInput reports the trigger
  state and because the status readout has to answer in menus too
- LB / RB alone — cycle target left / right (SelectPrev / SelectNext)
- X — switch party leader (ChangeChar)
- Y — the quick menu
- Back — options screen (Options)
- Start — pause (Pause)
- LT + A, action queue open — clear the whole queue (the keyboard's Shift+Enter)
- L3 — flourish weapon (Flourish)
- R3 — camera orient (the keyboard's N)

In menus the pad simply IS the keyboard: D-Pad navigates (with a 400 ms / 150 ms
auto-repeat the mod supplies, since KOTOR 1 has no engine repeat gate to
inherit), A is Enter, B is Escape, LB / RB step the in-game menu's sub-screens
(PrevMenu / NextMenu) — which is how a pad reaches the map, the journal and the
equipment screens at all. On the container and the store the shoulders carry the
keyboard's Q / E mode toggle instead, exactly as on KOTOR 2. On the map screen
the left stick pans the virtual cursor and the D-Pad runs the Map cycle; both
come for free from the shared code.

Every game action is fired through the player's `swkotor.ini [Keymapping]` bind
(`engine_keymap::GameActionScancode`), falling back to keymap.2da's own default
for the row, so a rebind is honoured.

**Free Look is deliberately absent** from the quick menu. Its default bind is
Caps Lock — a stateful OS toggle and the screen reader's own modifier key — so
injecting it would fire NVDA and leave the keyboard latched.
`InputIndexToScancode` refuses to resolve Caps Lock at all, so no caller can
inject it by accident.

## The quick menu (Y)

KOTOR 2's is an engine panel the navigation chain can walk. KOTOR 1 has no such
panel, so the mod supplies one: an in-DLL overlay modelled on the F1 help list,
one entry shorter than KOTOR 2's because KOTOR 1 has no Switch Weapons.

Entries, in order: Menus (the engine's GUI action), Party Leader, Solo Mode,
Stealth, Quick Save, Help. Help opens the mod's key list — the same claim the
mod makes on KOTOR 2, where the engine's Help entry opens a picture of a
controller.

It does not pause the world (it is an overlay over a running game, as on KOTOR
2), but it DOES stand the left stick down, so the player cannot walk off
mid-choice. It declines to open while the F1 list is up, because that list gets
first refusal on every nav code and a menu underneath it would be unreachable.

## Where the code lives

- `key_inject.{h,cpp}` — new. The shared scancode injection. camera_orient's
  private `SendKey` now forwards to it.
- `pad_input.{h,cpp}` — `PollButtons()`, the K1 dispatch, `InWorld()`,
  `RightStickVector()`. The two Translate* seams stay KOTOR 2 only.
- `pad_movement.{h,cpp}` — new. Left stick walks, right stick turns.
- `pad_quickmenu.{h,cpp}` — new. The Y menu. (Not to be confused with the
  KOTOR 2 module of the same name that was written and deleted in Phase 3 —
  that one was a navigator for the ENGINE's panel and was unnecessary. This one
  builds a menu KOTOR 1 does not have.)
- `engine_keymap.{h,cpp}` — `MoveAxisScancode`, `GameActionScancode`, and
  `InputIndexToScancode` extended to Tab / Escape / F1–F12.
- `help.cpp` — the Controller section was already gated on pad presence rather
  than on the title, so it needed only two new lines (X and Start).
- `core_tick.cpp` — three new phases.
- `strings*` — ten new ids across seven languages.

## Combined test round

Plug the controller in before launching. (That rule is KOTOR 2's, where the
engine enumerates once at input init. It does not apply on KOTOR 1 — XInput is
polled every tick and rescans every 2 s — but keeping one habit for both games
is cheaper than remembering which is which.)

**Menus.**

1. Main menu: D-Pad up/down. One spoken entry per press. Hold a direction —
   it should repeat at a readable pace, not run away.
2. Options: D-Pad through the tabs and the entries. Our order, no stray
   clicking.
3. A on a settings entry — it activates. B — the back sound and the panel name.
4. LB / RB on the in-game menu strip — it steps sub-screens. This is the only
   route a pad has to the map.
5. A container and a store: LB or RB flips take/give and buy/sell.

If the D-Pad does nothing at all in a menu, the suspect is the extended
scancode: the arrow cluster is injected as base scancode + `KEYEVENTF_EXTENDEDKEY`
and the engine may not read it that way. Grep `menu key injected` — if those
lines are present and nothing moved, that is the answer, and the fallback is a
direct call into `acc::menus::chain::HandleNavStep`.

**World — movement.** Push the left stick in each of the eight directions. The
character walks and strafes as the keyboard does. Let go — it stops. Alt-tab
away mid-push and come back: nothing must be left held (grep `Pad.move` for a
`press` with no matching `release`). Then the right stick left and right: the
camera turns, and the direction announcement fires when a sector boundary is
crossed.

**World — the D-Pad and the action menu.**

6. D-Pad left/right steps objects, up/down steps categories — the same speech
   `,` and `.` produce. This is the D-Pad's only world job; it has no modes.
7. A on a focused door / container / NPC — it opens / loots / talks. A must do
   this every time, with no state that can make it mean something else.
8. Pull LT and let go: the action menu opens and speaks its category, exactly
   as Shift+Enter does. Left/right steps categories, up/down entries, A fires.
   With the "Action Menu" auto-pause option ON the world freezes here and the
   menu stays open after firing, so several actions can be queued (each answers
   "…, Platz N"); with it OFF the world keeps running and firing closes the menu
   out of combat.
9. Pull LT and let go again: the menu closes. Repeat with B instead of LT — same
   close. After either, the D-Pad steps objects again and A interacts again.
10. **The collision test.** Hold LT, press LB, release LT — the beacon only, and
    NO action menu. Then RT + RB (autowalk). Then RT alone (degrees), then
    LT + RT (this screen's keys).

**World — the remaining buttons.** X switches party leader. Back opens options.
Start pauses. L3 flourishes. R3 turns the camera to the beacon's next waypoint.

**Action queue.** In combat, hold RT and press X: the queue opens and speaks its
depth. D-Pad up / down then walk the entries, A removes the one you are on (only the last of that character's
queue can go — anything else answers "Cannot remove this action"), LT + A clears
the queue, B closes. Let LT go afterwards and confirm the action menu does NOT
open: the chord marks the hold. Then LT + X anywhere — in the world and on the
inventory screen — for the status readout, and confirm bare X still switches the
party leader.

**Tooltips in menus.** On the inventory, hold Y and tap D-Pad down: the item's
description, one block per tap. Let Y go and press it again — the readout starts
at the first block. Then press Y alone in a menu (nothing happens) and in the
world (the quick menu opens).

**Quick menu.** Y in the world: "Quick menu" then the focused entry; D-Pad steps
entries, clamping at both ends; A fires; B or Y closes. Check that the left
stick does NOT walk while it is open. Then step to Help and press A — the mod's
key list opens.

**Help.** Open the key list and read to the end: a Controller section with
twenty-one lines, present with a pad connected and completely absent without one.

**If nothing happens at all,** grep the log for `Pad: XInput` — either the bind
line naming the DLL, or "no XInput DLL resolved".

## Known risks, in the order I would bet on them

1. **Extended-scancode arrows** may not reach the engine's DirectInput read.
   Menu navigation is the only thing that depends on it; the fallback is one
   function call away and is named in the code comment.
2. **The screen reader seeing injected keys.** camera_orient has injected the
   turn key for a long time without trouble, but movement injects letters, held
   for seconds, continuously. If NVDA's "speak typed characters" picks these up
   it will be immediately obvious.
3. **A stranded held key.** Every guard releases through one path and the guards
   run every tick, but this is the failure with consequences outside the game,
   so the alt-tab step above is worth doing deliberately.
4. **The action menu opened from the poll.** `PollButtons` and `PollTriggers`
   both run from the same slot as the keyboard poll for exactly this reason, but
   the phantom-confirm bug the KOTOR 2 action-menu watcher records is the one to
   watch for: a menu that fires its first entry on its own. `PollTriggers` runs
   AFTER `PollButtons` so a menu the left trigger opened this tick cannot also
   be fired into by an A press sampled in the same tick.
