# KOTOR 2 (Steam/Aspyr) — controller support and the gamepad GUI

Discovery doc, first pass (2026-08-06). Format is KNOWN / SUSPECTED / OPEN.
All addresses are KOTOR 2 Steam `swkotor2.exe` (6,588,416 bytes, image base
0x400000). The exe is **not** SteamStub-encrypted, so every byte here was read
straight off disk; Ghidra project `kotor2`, program `swkotor2.exe`.

Read this before touching controller work. The short version: KOTOR 2's Steam
build ships **real, live gamepad support** with a hard-coded binding table and
three iOS-derived overlay surfaces. There is no "alternate GUI mode" — the pad
drives the same `_p` panels, plus overlays that only a pad can open.

---

## KNOWN — the build really has gamepad support

Evidence chain, none of it inferred:

- `CExoInput` DirectInput init (`0x00730750`) calls
  `IDirectInput8::EnumDevices(DI8DEVCLASS_GAMECTRL=4, cb=0x007306a0,
  pvRef=this, DIEDFL_ATTACHEDONLY=1)`. The callback just counts
  (`this->device_count += 1`).
- Per device it allocates two parallel arrays of 0x134-byte slots —
  `this+0x30` (current) and `this+0x34` (previous) — each holding a
  272-byte (0x110) `DIJOYSTATE2` plus a timestamp at +0x110 and axis range
  values after it. Device COM pointers live in the array at `this+0x2c`,
  device count at `this+0x18`.
- `SetCooperativeLevel(hwnd, 0xA)` = `DISCL_BACKGROUND | DISCL_NONEXCLUSIVE`.
  **The pad is NOT grabbed exclusively**, so a second reader (ours) can open
  the same device, and XInput is unaffected either way.
- Shipped assets in `<K2>\override\`: `cus_gpad_*.tga` (the quick-menu icons),
  `cntrl_xb360_{eng,fre,ger,ita,spa}.tga` and `cntrl_ps3_*` (the controller
  layout diagrams), and `gamepad.txt` (the quick menu's 8 labels in 5
  languages).
- RTTI classes that exist only in K2: `CSWGuiMainInterface::CSWGuiActionMenuIos`,
  `CSWGuiMainInterface::CSWGamepadMenuIos`, `CSWGuiMainInterface::CSWGuiHelpPanel`,
  `CSWGuiControllerLossBox`, `AspyrFloatingButtonIcon`.
- Imports: `DINPUT8.dll!DirectInput8Create` only. **No XInput, no Steam Input,
  no `ISteamController`.** Everything runs through DirectInput 8.

There is **no ini key and no 2DA** for pad bindings: `keymap.2da` (81 rows) and
`bindablekeys.2da` (103 rows) are keyboard-only, and no `swkotor2.ini` section
mentions the pad. The mapping is hard-coded in the exe — so it is
**not user-rebindable**, and a mod that wants different bindings has to detour
the table.

## KNOWN — the shipped binding chart

Read directly off `override\cntrl_xb360_eng.tga` (the in-game Help image). PS3
diagram is the same mapping with PlayStation glyphs.

- Left stick — Move
- Left stick button (L3) — Flourish Weapon
- Right stick — Rotate Camera
- Right stick button (R3) — First-Person View
- D-Pad — Navigate UI and Menus
- A / Cross — Default Action
- B / Circle — Cancel / Close
- X / Square — Switch Party Leader
- Y / Triangle — Quick Menu
- LB / L1 — Cycle Target (Left)
- RB / R1 — Cycle Target (Right)
- **LT / L2 — Unused**
- **RT / R2 — Unused**
- Back / Select — Options Menu
- Start — Pause Combat

The two "Unused" labels are literal and confirmed in code (see below), which is
what makes them available to us.

## KNOWN — how a pad press becomes an engine input code

Three stages.

**1. `CExoInputInternal::ReadJoystickDevice` @ `0x00731390`** — per device,
`GetDeviceState(0x110, &DIJOYSTATE2)`, diffed against the previous frame, and
emitted as 0x14-byte records `{code, value, 0, 0, 0}`:

- Buttons: `code = 0x30 + buttonIndex` (0..31), value = pressed.
- POV hats raw: `code = 0x20 + hatIndex`, value = angle/100 (or -1 = centred).
- POV hats decoded into four directional pseudo-buttons per hat:
  up `900+i`, down `904+i`, right `908+i`, left `912+i`.
- Axes, with a 25% dead zone: lX→`0x00`, lY→`0x04`, lRx→`0x0c`, lRy→`0x10`,
  slider0→`0x18`, lRz→`0x1c`.
- **lZ (offset 0x08) is never emitted.** On an Xbox pad in DirectInput, LT and
  RT are the two halves of the shared lZ axis — so the triggers are read by
  nobody. That is the mechanism behind "Unused" on the diagram.
- `DAT_00a7ea70[deviceIndex]` is set to 1 on a successful read, 0 on failure.
  This is the live "pad present and responding" flag.
- A compat flag at `CExoInput+0x1c` swaps lRx/lRy for lZ/lRz and permutes the
  button numbering (a PS3-style pad layout). Cleared at init.

**2. `CExoInputInternal::GetEvents` @ `0x0072c7e0`** maps raw device events to
engine InputIndices, per **InputClass** (the function's third parameter, 0..5).
The local table is `codes[context][2]` (two alternates per context, exactly like
`keymap.2da`'s A/B bindings). InputClass → context row: 0→1, 1→2, 2→0, 3→0,
4→4, 5→4.

Context row 0 (the GUI/menu class) and row 1 (the in-world class):

- Button 0 (A) — row0 `0x27`; row1 slot1 `0x27`; row2 `0xd9`; row4 `0x10`
- Button 1 (B) — row0 `0x28`; row1 `0x28`; row2 `0x16`; row4 `0x11`
- Button 2 (X) — row0 `0x29`; row1 `0x29`; row4 `0x4d`
- Button 3 (Y) — row0 `0x2a`; row1 `0x2a`; row2 `0xf0`; row4 `0x4e`
- Button 4 (LB) — row0 `0x35`; row1 `0xcc`
- Button 5 (RB) — row0 `0x36`; row1 `0xcd`
- Button 6 (Back) — row0 `0x2e`; row1 `0x0b`
- Button 7 (Start) — row0 `0x2d`; row1 `0xe0`; row2 `0xfd`
- Button 8 (L3) — row1 `0xf2`
- Button 9 (R3) — row1 `0x01`; row4 `0x01`
- D-Pad up — rows 0 and 1 `0x31`
- D-Pad down — `0x32`
- D-Pad left — `0x2f`
- D-Pad right — `0x30`
- Buttons 10..31 — **no mapping at all** (the switch stops at button 9).

`0x27` and `0x28` are the same InputIndices the manager produces for keyboard
Enter and Esc *after* translation — so at the panel level pad A and keyboard
Enter are indistinguishable. **At our hook they are not** (see below).

**3. `CSWGuiManager::HandleInputEvent` @ `0x00410AA0`** — K2 adds a
gamepad preamble that KOTOR 1 does not have, ahead of the familiar keyboard
translation:

- Analog axis events (`param_2` = ±1) are turned into nav codes:
  `0x33`→`0x3e`/`0x3d` (down/up), `0x34`→`0x40`/`0x3f` (right/left),
  `0x37`→`0x3a`/`0x39`, `0x38`→`0x3c`/`0x3b`. So **the sticks navigate menus
  too**, arriving as the same codes keyboard arrows end up as.
- D-Pad codes `0x2f`..`0x32` get an axis-lock + 150 ms (`0x96`) repeat gate
  stored at `manager+0x72` / `manager+0x64`, then **pass through untranslated**.
- Then the K1-identical keyboard translation: `0xb4`/`0xdf`→`0x28`,
  `0xb5`/`0xbb`→`0x27`, `0xb6`→`0x3d`, `0xb7`→`0x3e`, `0xb8`→`0x3f`,
  `0xb9`→`0x40`. Our `ManagerTranslateCode` is therefore correct on K2 as well.
- Dispatch is unchanged from K1: vtable slot `0x3c` on the modal-stack top, else
  on every panel. `CSWGuiPanel::HandleInputEvent` @ `0x0040eaa0` just forwards
  to the panel's active control at `+0x20`, so real key handling is per-control.

**Consequence for our hooks.** `OnHandleInputEvent` is cut at `0x00410AC8`,
which is *before* all of the above. So we see the pre-translation code and can
tell pad from keyboard:

- keyboard Enter = `0xb5`/`0xbb`, pad A = `0x27`
- keyboard Esc = `0xb4`/`0xdf`, pad B = `0x28`
- keyboard arrows = `0xb6`..`0xb9`, pad D-Pad = `0x2f`..`0x32`,
  pad stick = `0x33`/`0x34`/`0x37`/`0x38` with `param_2` = ±1

## KNOWN — confirmed live (test round 2026-08-06, `patch-20260806-132454.log`)

Every code below was observed arriving at our own hooks with a pad connected.
**The pad reuses the keyboard InputIndex numbering**, so `InputIndexName` prints
misleading keyboard names — the name in brackets is what our log currently
shows, not what was pressed.

At `CSWGuiManager::HandleInputEvent` (menus AND in-world):

- D-Pad left = 47 (logs as `KEYBOARD_F9`), right = 48 (`KEYBOARD_F10`),
  up = 49 (`KEYBOARD_F11`), down = 50 (`KEYBOARD_F12`), all `val=1`
- Left stick = 51 (`KEYBOARD_A`) and 52 (`KEYBOARD_B`), `val` = -1 / 0 / +1
- A = 39 / `0x27` (logs as `F1(0x27 activate-code)`)
- B = 40 (`KEYBOARD_F2`), X = 41 (`KEYBOARD_F3`), Y = 42 (`KEYBOARD_F4`)
- LB = 53 (`KEYBOARD_C`), RB = 54 (`KEYBOARD_D`), Back = 46 (`KEYBOARD_F8`)

At `CClientExoAppInternal::HandleInputEvent` (in-world only):

- LB = 204 (`0xcc`), RB = 205 (`0xcd`), Start = 224 (`0xe0`), Back = 11 (`0x0b`)
- ~~Left-stick movement = 65 and 66 with analog magnitudes~~ — **REFUTED
  2026-08-06, second round. Codes 65 and 66 are the MOUSE X and Y axes.** See
  "The in-world movement signal" below.

**Disambiguating pad from keyboard.** Codes 47–54 are also real bindablekeys
rows (F9–F12, A, B, C, D), so they are shared numbering, not pad-exclusive:

- 51 / 52: the pad sends `val` = ±1; the engine's own preamble keys on exactly
  that, so `val == 1 || val == -1` is the engine-native discriminator.
- 47–50: F9–F12 are unbound in stock `[Keymapping]`, and the engine drops
  unbound scancodes before this hook — so in practice these can only be the pad.
  Breaks only if the user binds F9–F12 in the game's own key mapping.

**Settled OPEN items:**

- Stock `_p` panels DO respond to the D-Pad, and to the stick, identically —
  the manager converts stick axes to the same nav codes keyboard arrows become.
- Engine-side pad navigation DOES move the engine's active control, so our
  `SetActiveControl` focus hook fires. That is why the main menu already speaks.
- ~~The in-world D-Pad is **inert and free**: all four codes arrive at the
  manager during world play, nothing consumes them, nothing happens.~~
  **REFUTED 2026-08-06 (fourth round, by play rather than by log).** The codes
  do arrive, and the engine *does* consume them: they drive
  `CSWGuiActionMenuIos`. The round that called them inert was a blind test
  watching a silent, purely visual highlight move — "nothing happens" meant
  "nothing was announced". See the level-1 answer below.
- LT/RT confirmed dead — zero log lines from either.

**Three defects the round exposed, all ours, not the engine's:**

1. **Pad A does nothing, anywhere.** The help system suppresses raw `0x27` at the
   manager so F1 cannot double as Enter in menus (`HELP-F1-SUPPRESSED`, 25 times
   in the log). Pad A *is* `0x27`.
2. **Menus announce several entries per press.** We do not consume the pad nav
   codes, so the engine runs its own navigation, which emits multiple
   `SetActiveControl` events for one press (observed: Auto-Pause → Feedback →
   Auto-Pause) and our focus monitor speaks each. Keyboard nav is `CONSUMED` and
   produces exactly one. Same root cause as the unreliable tabs and wrong order.
3. **B backs out silently.** Our Esc path listens for the pre-translation logical
   Esc (`0xb4` / `0xdf`); pad B arrives as `0x28`, so the close/announce path
   never runs.

Plus one analog-assumption break: `audio_footstep_suppress.cpp` arms the
drive-loop silence from `ForwardBackwardKeyHeld()`, which is keyboard-only, so a
stick-driven walk never arms it. (Its stuck heuristic also documents its
thresholds as assuming "no analog input", but the round did not catch that
failing — verify before changing it.) Fixed by reading the left stick through
XInput; see the movement-signal section below for why NOT through the engine.

**The in-world movement signal — codes 65 and 66 are the MOUSE, not the stick.**

The first round read them as the left stick: "65 = the held direction, stable
while held; 66 = continuously varying, a regular sawtooth in steps of 87". That
was wrong, and the second round (`patch-20260806-162206.log`) settles it
arithmetically:

- Every value 65 ever takes is exactly a `MoveMouseToPosition` **X** the mod
  itself issued during chain navigation — 1743 (the main-menu button column),
  852, 891, 848. Five distinct values across a session because the mod warps
  the cursor to a small set of control positions, not because a stick rested in
  three directions.
- The "sawtooth in steps of 87" in code 66 is the main menu's **row spacing**:
  its buttons sit at y = 831, 918, 1005, 1092, and 66 tracks cursor Y (offset
  by a constant border inset — 918→881, 1005→968). The "analog sweep" was our
  own chain walking down the button column.
- They arrive **on the title screen**, before any world or player exists, and
  they arrived in a session where the engine had enumerated no joystick at all.

So there is currently **no established engine-side signal for pad movement**,
and the question is moot for our purposes: read the left stick through XInput
(`sThumbLX` / `sThumbLY`) alongside the triggers. That is what the mod does —
it is correct by construction, independent of engine RE, and immune to this
whole class of misreading.

The general lesson, worth keeping: an "analog-looking" value stream observed at
an engine hook may be the mod's own cursor warps coming back round. Correlate
candidate analog values against `MoveMouseToPosition` before believing them.

## KNOWN — the "alternate GUI" is three overlays, not a mode switch

There is no code path that swaps `.gui` files, no `_x` (Xbox) layout use, and no
global "controller mode" flag: `DAT_00a7ea70` is read by the two input functions
and nothing else. What Aspyr added is a set of always-constructed overlays that
only a pad opens.

`CSWGuiMainInterface` holds:

- `+0x98` — `CSWGuiActionMenuIos`, heap-allocated `0x15418` bytes, ctor
  `0x00747210`. The in-world action menu.
- `+0x98` → `+0xd588` — `CSWGamepadMenuIos` (embedded), ctor `0x0074a500`.
  The Y-button Quick Menu.
- `+0x98` → `+0x148b4` — `CSWGuiHelpPanel`. Draws the `cntrl_xb360_<lang>` /
  `cntrl_ps3_<lang>` texture; the format strings are at `0x0099d904` /
  `0x0099d8f4` and the language suffix is picked at `0x00745e60`.
- `+0x70` — `CSWGuiInGameMenu` (ctor `0x00754ed0`), the ordinary 8-icon strip.

### CSWGamepadMenuIos — layout and state

- Ctor `0x0074a500` builds four arrays of `0x1d0`-byte widgets: 8 at `+0x250`,
  8 at `+0x10d0`, 3 at `+0x1f50`, 3 at `+0x24c0`. Sets `+0x68 = -1`, `+0x74 = 0`,
  `+0x78 = <owner>`.
- Layout/draw `0x00753d60(bool rebuildTextures)` assigns the icon textures
  (`cus_gpad_gen/gen2`, `map/map2`, `solo/solo2`, `ste/ste2`, `save/save2`,
  `fper/fper2`, `hand/hand2`, `help/help2`, background `cus_gpad_bg`) and writes
  the 8 label strings.
- Labels come from `GetGamepadString(index)` @ `0x00753cd0` — lazily loads
  `override/gamepad.txt` into the string table at `0xa7eafc` as table 1, then
  reads `(index, stride 8)`. The file is 8 strings x 5 languages, in file order:
  **Menus, Party Leader, Solo/Party, Stealth, Quick Save, Free Look,
  Switch Weapons, Help** (English block; then FR, IT, DE, ES).
- State: `+0x68` = selected entry 0..7 (wraps mod 8), `+0x6c` = sub-entry,
  `+0x70` = sub-entry count, `+0x7c` = a gate that makes entry 3 (Stealth) be
  skipped when clear.
- Input `0x00754950` (vtable slot 15 of vtable `0x0099dd3c`) accepts BOTH
  vocabularies: up `0x31`/`0x3d`, down `0x32`/`0x3e`, left `0x2f`/`0x3f`,
  right `0x30`/`0x40`, activate `0x27`, cancel `0x28`/`0x2a`.
  Entry 1 (Party Leader) expands into the 3-item sub-list.
- Open `0x00753680` — shows the menu if the world state allows and
  `DAT_00a7ead0 == 0`.

### CSWGuiActionMenuIos — the pad's target + personal action surface

This, not the Quick Menu, is the pad's equivalent of the radial plus the action
bar — i.e. of our unified action menu. Two levels, both verified:

- **Level 1 — nine categories.** `FUN_00746950` iterates exactly 9
  (`for i < 9`, per-category active test `FUN_007469a0`), with the open category
  in the global `DAT_00a10340` (-1 = none).
- **Level 2 — the variants of the open category.** Up to 32 widgets at
  `+0x10074 + i*0x1d0` carry a selected bit; `FUN_00746fc0` returns the selected
  index, `FUN_00747090(delta)` clears all highlights and steps modulo the live
  count in `DAT_00a7eaec`.
- **Input** (`0x00747130`, vtable slot 15): `0x31` D-Pad up = step -1,
  `0x32` D-Pad down = step +1, `0x27` A = activate the selected variant,
  `0x28` B = close.
- **Level 1 is D-Pad left / right** — `0x2f` / `0x30`. Settled 2026-08-06 by
  play, not by decompile: a sighted pass over the shipped build steps
  categories with left/right and entries with up/down, A confirming. This
  closes the file's longest-standing OPEN item, and it means the whole D-Pad
  is the engine's action surface during world play — the menu is not something
  a pad player *opens*, it is simply there.
- **Activate** (`FUN_0074d930(category, variantIndex)`) splits at 6 and stamps
  the engine's own selection fields — the same mechanism KOTOR 1 uses:
  - category 0..5 → `*(this + 0x1c7c + cat*4) = action_id`, the per-column
    selected-action-id (K1's `+0x1bac + slot*4`), then `FUN_00751750`.
  - category 6..8 → `*(this + [this+0x1c76]*0xc + 0xd8 + cat*4) = action_id`,
    i.e. indexed by target type × 3 rows (K1's `field1[target_type*3 + row]`),
    then `FUN_00751710(cat-6)`.
  So **6 personal action columns + 3 target-action rows = the 9 categories**,
  and the pad drives the engine's real action machinery, not a parallel one.
- The action lists themselves come from `FUN_0077e650(slot, &list)` — K2's
  `GetPersonalActions` twin — refilled in `FUN_0074cfe0` for **7** slots, of
  which 6 are displayed as columns. Entries are 0x3c bytes with `action_id` at
  `+8`. Matches K1's `PopulateMenus` shape exactly.

**~~These widgets are NOT `CSWGuiControl`s.~~ REFUTED 2026-08-06 (third round,
`patch-20260806-163136.log`).** They are ordinary `CSWGuiButton`s and the
Quick Menu is an ordinary `CSWGuiPanel`: opening it pushes panel `vtable
0x0099dd3c` onto `modal_stack` with **22 children**, and the generic chain
walks them and reads the real captions straight out — "Menüs",
"Gruppenanführer", "Einzelmodus", "Stealth-Modus", "Schnellspeichern", "Freie
Sicht", "Waffe wechseln", "Hilfe".

So no dedicated navigator is needed, and one written against `+0x68` / `+0x6c`
is actively worse: it would swallow the D-Pad and shut the working chain out.

What the panel does need is a **decorative filter**. Fourteen of the 22
children are the icon and background quads — the same `CSWGuiButton` class,
every `id` is `-1`, so neither class nor `.gui` id distinguishes them. What
does: they carry no caption by any route (the extractor's whole cascade comes
back empty, and reading their `gui_string` faults outright), and their
`is_active` / `bit_flags` read as garbage. Unfiltered, the chain is 22 entries
long and every other step speaks the "control N" placeholder. The mod
identifies the panel by vtable (`PanelKind::GamepadQuickMenu`) and drops the
captionless children in `IsDecorativeControl`.

The lesson generalises past this panel: "custom quads, the chain cannot see
them" was inferred from the ctor building fixed `0x1d0`-byte widget arrays.
Fixed arrays of widgets and registration in the panel's
`CExoArrayList<CSWGuiControl*>` are not mutually exclusive — the ctor evidence
never actually spoke to the second question. One `Menus.PanelWalk` line settled
it.

## KNOWN — what is free for our own bindings

- **LT and RT.** The engine never reads the trigger axis, so they are fully
  free. Caveat: in DirectInput both triggers share the single lZ axis and cancel
  each other out, so we cannot tell LT from RT that way — **read them via
  XInput** (`bLeftTrigger` / `bRightTrigger` are separate there). XInput coexists
  with the engine's non-exclusive DirectInput grab.
- **Pad buttons 10 and above** — no engine mapping. A standard Xbox pad exposes
  only 0..9 in DirectInput, so this matters only for richer pads.
- **Chords** — any face/shoulder button held together with LT or RT, since we
  would be doing the combining ourselves.

**Where a chord has to be claimed.** A physical button is mapped to a
*different* InputIndex per input class, and the two arrive at two different
hooks. The in-world class is the one on which the engine actually performs the
action, so a chord that must take an action away from the engine has to be
consumed at `CClientExoAppInternal::HandleInputEvent`, not at the GUI manager.
In-world codes worth knowing:

- LB = `0xcc`, RB = `0xcd` — Cycle Target left / right.
- L3 = `0xf2` — Flourish Weapon. Cosmetic; the freest button on the pad.
- R3 = `0x01` — First-Person View. **`0x01` is also `MOUSE_BUTTON1`, the right
  mouse button (mouse-look).** Anything binding R3 must check that the physical
  button is up before consuming, or every mouse user with a pad plugged in
  loses right-click. `pad::TranslateClientEvent` does this with
  `GetAsyncKeyState(VK_RBUTTON)`, the same twin-key test the shared manager
  codes use.

L3 and R3 have **no** GUI-class mapping at all, so they are in-world only.

## SUSPECTED

- `CSWGuiActionMenuIos` is the in-world action surface a pad opens on a target
  (the pad's counterpart of the radial). Constructed unconditionally on PC; not
  yet observed live.
- InputClass row 0 is the GUI/menu class and row 1 the in-world class. The
  A-button code landing in row0 slot0 and row1 slot1 fits, but the class→context
  mapping has not been traced back to the caller that picks the class.
- `CSWGuiControllerLossBox` is the "controller disconnected" modal (ctors
  `0x007bde80`, `0x007be4c0`). Name only; behaviour unread.

## OPEN

- Do the **stock** `_p` panels' controls handle `0x2f`..`0x32`? The base panel
  forwards to the focused control, and only `CSWGamepadMenuIos` has been read.
  If stock controls only understand `0x3d`..`0x40`, then D-Pad menu navigation
  works through some path not yet found, and stick navigation (which *is*
  translated to `0x3d`..`0x40`) would be the real menu input.
- What sets `CExoInput+0x1c` (the PS3-layout compat flag)?
- Whether `AspyrFloatingButtonIcon` (on-screen button prompts) appears on PC and
  where it takes its glyphs from.
- ~~Which input picks the CATEGORY (level 1) in `CSWGuiActionMenuIos`.~~
  **SETTLED 2026-08-06: D-Pad left / right.** See the class section above.
- Whether the pad ever changes what our `SetActiveControl` focus hook sees —
  i.e. whether engine-side pad navigation moves the engine's active control (in
  which case our announcements fire for free) or only moves a private cursor.

The four OPEN items are all cheaper to settle with **one live test round** than
with more decompiling: our manager hook already logs every arriving code with
`InputIndexName` (`menus_dispatch.cpp`, unconditional), so plugging a pad in and
pressing each button in a menu and in the world writes the whole answer into
`<K2 install>\logs\patch-*.log`.

## Tooling notes

- Ghidra: `KDEV_GHIDRA_PROJ=kotor2 KDEV_GHIDRA_PROGRAM=swkotor2.exe
  tools/ghidra-scripts/decomp.sh 0xADDR ...`
- The controller diagrams are ordinary TGAs; Pillow converts them to PNG for
  reading (`Image.open(tga).convert('RGB').save(png)`).
- `override/gamepad.txt` and `override/custom.txt` are plain UTF-8 line lists —
  the engine reads them by line index, so **line order is the API**. Editing
  them is the cheapest way to relabel the Quick Menu per language.
