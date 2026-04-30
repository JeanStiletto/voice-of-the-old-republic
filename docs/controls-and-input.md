# Controls & Input — Survey

Working document. Catalogues every default keyboard control, every mouse-required action, and the controller-support situation for **KOTOR 1 PC (Steam, v1.0.3)**. The mouse-only list at the bottom is the authoritative backlog of accessibility gaps we work through one screen at a time.

## Sources

- **Official KOTOR PC Manual** (BioWare 2003, hosted by Steam): `http://cdn.akamai.steamstatic.com/steam/apps/32370/manuals/KOTORI_Manual.pdf` — pages 4–7 contain the canonical "Default Controls" table and "Keyboard Map". Pages 38, 41, 47–49 describe in-game UI flows in mouse-verb terms.
- **`swkotor.ini` `[Keymapping]` section** — present in our Steam install. The game ships an in-game **Options → Gameplay → Key Mapping** screen (driven by `swconfig.exe` / the engine itself) that writes back to this section as `ActionNNN=DIK_scancode` pairs. Numeric Action IDs are opaque without the engine's name table; the values are standard DirectInput scancodes (`DIK_A=0x1E=30`, `DIK_W=0x11=17`, `DIK_SPACE=0x39=57`, etc.).
- **Community references:** Neoseeker Star Wars Wiki (`KotOR Keyboard Controls`), Steam community threads, GameFAQs.

The official manual contains **zero** references to gamepads, joysticks, controllers, or DirectInput-for-game-controllers. Native controller support is not present in the PC release.

## Default keyboard controls

### Character & camera movement
- W — Move Forward
- S — Move Backward
- Z — Strafe Left
- C — Strafe Right
- A — Rotate Camera Left
- D — Rotate Camera Right
- Caps Lock — Toggle Free Look
- Hold Ctrl (or hold Mouse 2) — Look About (free pan)

### Interacting with the world
- Spacebar (or Pause/Break) — Pause game
- Q — Cycle targets to the left
- E — Cycle targets to the right
- R (or Mouse 1) — Default action on current target (attack, talk, open, bash)
- Mouse 1 — Select object (in 3D world)

### Target Action menu hotkeys (the menu floating above the selected target)
- 1 — Use leftmost action
- 2 — Use centre action
- 3 — Use rightmost action

### Action Menu hotkeys (player's own action bar)
- 4 — Use current Friendly Force Power
- 5 — Use current Medical / Repair item
- 6 — Use current Miscellaneous item (typically grenades)
- 7 — Use current Mine

### Leader / party commands
- F — Cancel Combat
- Tab — Change Leader (cycle controlled party member)
- V — Solo Mode
- G — Stealth Mode
- X — Flourish Weapon

### Gameplay miscellany
- T — Show Tooltips
- Esc — Game Menu
- F4 — Quick Save
- F5 — Quick Load
- F8 — Quick Save (alt; per Neoseeker)

### Quick menu screen access
- J — Messages and Feedback
- M — Map and Party Management
- L — Quests
- K — Skills / Feats / Force Powers
- O — Options
- P — Player Record Sheet
- I — Party Inventory
- U — Equip Character

### Mini-game commands (swoop racing / turret)
- W — Move Up / accelerate (swoop: thrust to next gear)
- S — Move Down
- A — Move Left
- D — Move Right (swoop: also shift gears)
- Spacebar / Enter / Mouse 1 — Fire turret / shift gears in swoop
- Pause/Break or P — Pause mini-game

### Dialogue
- 1–9 — Select that-numbered response (manual page 38: *"Dialogue responses can also be selected by pressing a number key that corresponds with the list of dialog choices"*)
- Mouse 1 — Advance NPC line / select highlighted response

## Notes on `swkotor.ini` `[Keymapping]`

The user can rebind every action via in-game **Options → Gameplay → Key Mapping**, which persists to `[Keymapping]` in `swkotor.ini`. Format is `ActionNNN=DIK_scancode`. Action IDs are arbitrary game-internal identifiers (e.g. `Action214=62` is F4 = Quick Save by default). Scancode values are standard DirectInput.

**Implication for the mod:** when we announce "press F4 to quick-save" we should *not* hardcode the default — read the user's `[Keymapping]` and announce their actual binding. Building the Action-ID → human-readable name table is a downstream task; can be cross-referenced from the engine binary or by capturing what `swconfig.exe` displays per row.

## Controller / gamepad support

### Native support — none

The PC release of KOTOR 1 has no controller support of any kind. The manual never mentions gamepads; the executable does not enumerate XInput or DirectInput game controllers; PCGamingWiki and the Steam community confirm. (KOTOR 2 PC eventually got controller support via an Aspyr patch years after release; KOTOR 1 PC never did.)

### Community workarounds

- **Steam Input** — Big Picture / per-game controller config can map a controller to keyboard/mouse. Community profiles exist.
- **Controller Companion** (paid Steam app, ~$3) — KOTOR profiles available.
- **reWASD / Xpadder** — generic remappers.

### Implication for our mod

We can't piggyback on an XInput layer the game already wires up. If we ever wanted gamepad navigation we'd have to ship it ourselves (SDL2 or XInput inside our injected DLL, mapped to synthesized keystrokes or directly into our own UI hook layer). This is **deprioritized** — most blind users prefer keyboard + screen reader, and the existing mouse-only widgets are equally inaccessible to a gamepad. Better to make every screen keyboard-navigable first; revisit gamepad only if a user specifically asks.

## Accessibility gaps — the mouse-only / mouse-required action backlog

Each item below is a screen or interaction that the manual describes only in mouse-verb terms ("left click on…", "drag…", "hover…"). These are our actual TODO list; tackle in priority order, lift each from "mouse-only" to "keyboard-navigable + screen-reader-announced".

Status legend: `[ ]` not started, `[~]` partially solved, `[x]` solved.

### Highest priority — fully menu-driven, blocks core game progression

- `[ ]` **Galaxy Map planet selection** — manual: *"Left click on the directional arrows to cycle through"*. The arrow widgets are mouse-only. Blocks every planet-to-planet transition mid-game.
- `[ ]` **Conversation scroll arrows when responses overflow 1–9** — manual: *"left click on the UP or DOWN arrows to scroll through the available responses"*. Number keys 1–9 cover only what's currently visible; longer reply lists are mouse-only to scroll. Some long-dialogue NPCs (especially in side quests / persuade chains) hit this.
- `[ ]` **Level-up flow — Skills / Feats / Force Powers** — manual: *"clicking on its icon and then left-click the ADD FEAT button"*. Tree navigation + Add Feat / Add Power confirmation is mouse-only. Triggers on every level-up; unavoidable.
- `[~]` **Party Inventory item activation / filtering** — manual notes equip slot selection works *"using the mouse or keyboard"* (partial keyboard already supported), but item activation, filter button, scroll bars are mouse-only.
- `[ ]` **Equip Character screen** — slot navigation is partial-keyboard per manual; item-pick from the right-side list is mouse-only.
- `[ ]` **Save / Load file picker** — mouse-click to choose a slot. Probably keyboard-navigable in vanilla via arrow keys (untested); needs verification then announcement coverage.
- `[ ]` **Quest Journal entry selection** — *"Left click on the directional arrows to cycle through"* the active quests. Mouse-only navigation; entry text needs to be announced once focused.

### Mid priority — minigames

- `[ ]` **Pazaak** — side-deck card selection (mouse highlight + click), drag-from-hand-to-play, flip arrows. All mouse-driven. Optional content but a major time sink for many players.
- `[ ]` **Turret minigame aiming** — **mouse-only reticle** (no axis maps to keyboard). Keyboard only covers fire (Space/Enter) and pause. Recurring; hard to replace without synthesizing motion.

### Lower priority — spatial / world interaction

- `[~]` **Distant world-object clicks** — the Q/E target-cycle covers most creatures and prominent interactables, but **distant containers / consoles / placeables** that don't sit in the cycle order are realistically mouse-only. Investigate whether the cycle order is exhaustive or filtered by category/distance.
- `[ ]` **Camera "Look About" pan** — the keyboard alternative ("Hold Ctrl") still requires a mouse cursor delta to actually pan. A/D rotate is full keyboard, so this only matters for screens where you really need fine pitch/yaw.
- `[ ]` **Map screen scroll/zoom** (the 2D mini-map / area-map view) — mouse-only.
- `[ ]` **Tooltip hover** — info appears on cursor hover; T toggles tooltips on/off but keyboard navigation through tooltipped items is not in the manual.

### Reference: Lane's prior keyboard-driven-mouse experiment

Before designing our own keyboard-nav layer, **read** [`LaneDibello/KeyMouseAccessibilityTest`](https://github.com/LaneDibello/KeyMouseAccessibilityTest). Lane's earlier accessibility experiment was specifically a **keyboard-driven mouse** for KOTOR — i.e. a prototype of using keyboard input to drive a synthetic mouse cursor in the 3D world / over GUI widgets. No README, source-only, but it's the closest existing prior art to what we'd build for the spatial / world-interaction items in this section. Worth a skim *before* committing to our own pattern for distant-object selection, look-about pan, map scroll/zoom, and turret aiming. Cloning is cheap (`gh repo clone LaneDibello/KeyMouseAccessibilityTest third_party/KeyMouseAccessibilityTest`) and worth doing the moment we start the spatial-input work.

## Suggested order of attack

Roughly aligned with story-progression criticality:

1. **Galaxy Map** (blocks every planet transition)
2. **Dialogue scroll arrows** (blocks long conversations)
3. **Level-up flow** (blocks every level-up after the first few)
4. **Equip Character / Party Inventory** (blocks gear management)
5. **Save / Load picker** (blocks recovery from death)
6. **Quest Journal** (read-only but high frequency)
7. **Map screen** (orientation aid; nice-to-have)
8. **Pazaak** (optional content)
9. **Turret minigame aiming** (rare, needs Lane's prototype as reference)
10. **Spatial mouse for far placeables** (rare; revisit with KeyMouseAccessibilityTest in hand)

Items 1–6 are 2D listbox/button widgets — likely solvable in the same way we did the main menu (hook focus changes on the panel + appropriate vtable downcasts + synthesize keyboard nav where the widget doesn't already accept arrow keys). Items 9–10 are spatial mouse work and will reuse / extend Lane's prototype.

## Open questions

- Does the engine accept arrow-key navigation on Galaxy Map / Quest Journal arrows out of the box, and we just need to announce the newly-selected item? Or are those widgets genuinely mouse-only with no keyboard handler? Answer with a hooked `HandleInputEvent` on the relevant panel.
- What is the canonical Action-ID → human-readable name mapping in the engine? Likely a string array next to the Key Mapping UI in `swconfig.exe` or in the main exe. Needed before we can read `[Keymapping]` and announce user-rebound keys correctly.
- For the dialogue overflow case (>9 responses): does any community mod (e.g. `DialogueRepliesPast9` in the Patch Manager examples) already extend the keyboard hotkeys past 9? Check the patch source — may give us the hook point for free.
- Do save/load and quest journal already accept arrow-key navigation in vanilla? Verify in-game before assuming we need to synthesize it.
