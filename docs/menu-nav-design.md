# Menu Navigation — Unified Cursor Design

Handoff document. Captures architectural decisions, findings, and open
investigations from session 4 (2026-04-30) so a fresh session can pick up
implementation without re-deriving the design.

## TL;DR

KOTOR's menu focus-cycle is data-driven via per-button event handlers baked
into `.gui` data files. On most screens those handlers reach only a subset of
the panel's controls — the user can keyboard-navigate to 2 of 5 Options tabs,
3 of 14 inner-settings buttons, etc. There are no hidden hotkeys, no community
mods, and no native flag to flip. We have to build the navigation layer
ourselves.

We chose a **unified arrow-keys-drive-cursor** model: the user's arrow keys
move a synthesized cursor; in menus the cursor *snaps* between consecutive
`panel.controls` entries (no spatial scanning); on 3D-world / spatial widgets
the cursor moves smoothly (Lane's `KeyMouseAccessibilityTest` model). One
mental model, one input pipeline, one announcement source. Engine-driven
focus events become informational (logged but not spoken during a synth
window) so the user always hears our deterministic chain.

Phase 1 (next session's first deliverable) is **speak-only** chain navigation
through `panel.controls` — no cursor movement, no click synthesis, no
`SetActiveControl` calls. The minimum that proves the idea: arrow keys
deterministically walk the panel's children with stable order, panel-open
triggers an enumeration via Tolk, engine's parallel announcements get
suppressed during a brief window. Once that's stable, layer on cursor
movement (Phase 2), focus alignment (Phase 3), click activation (Phase 4),
and spatial mode (Phase 5).

## Findings (from session 4 in-game testing + decompile)

### Settings menu architecture

- **Outer Options panel `07526E88`** has 9 children: 5 tab buttons (Gameplay,
  Auto-Pause, Grafik, Sound, Feedback), one "Optionen" header label, one
  "Schliess." close button, plus a multi-line-blob listbox. Engine's tab cycle
  visits only Gameplay + Feedback — Auto-Pause / Grafik / Sound are unreachable
  by keyboard, mouse-only by data-file design.
- **Inner settings panel `0752B1C0`** is the *real* per-setting interaction
  surface, opened by pressing Enter on a tab. 14 children (refilled to 17 in
  some sub-tab states). Cycle reaches ~3 of them. The two `vtable=0073E658`
  unreachable buttons in the 14-child view are the +/- adjustment icons (no
  text, no `str_ref` — TLK lookup can't help).
- **Multi-line listbox** (`controls.size = 1`, child = `CSWGuiLabel` with
  newline-separated setting names) is purely visual. `HitCheckMouseLocal`
  returns row 0 regardless of which visual line was clicked, so coord-based
  click synthesis can't disambiguate among the visual lines. Real per-setting
  buttons live on the inner panel `0752B1C0`, not inside the listbox.

See `memory/project_listbox_keyboard_model.md`,
`memory/project_listbox_click_flow.md`,
`memory/project_kotor_gui_struct_offsets.md`.

### How input reaches a control

Decompile of `CSWGuiManager::HandleInputEvent` (0x0040c8e0):

1. Receives `(this, param_1, param_2)` where `param_1` is a raw scancode-ish
   action ID and `param_2` is press(`128`)/release(`0`) state.
2. Two inline switches translate KOTOR-internal "logical action" codes into
   `InputIndices` values:
   - `0xb4` / `0xdf` → `0x28` (KEYBOARD_F2, cancel/back)
   - `0xb5` / `0xbb` → `0x27` (KEYBOARD_F1, confirm/activate/Enter)
   - `0xb6` → `0x3d` (logical "up"/prev)
   - `0xb7` → `0x3e` (logical "down"/next)
   - `0xb8` → `0x3f`, `0xb9` → `0x40` (left/right; assignment unconfirmed)
3. Dispatches translated key to active panel's `HandleInputEvent`. Panel
   delegates to `active_control->HandleInputEvent`. The control looks up
   `event_list` for an entry matching the key code and calls the registered
   handler — that's where the per-button "next focus" cycle lives.

Our existing hook is at `0x0040c907` (mid-function, **before** the translation
switches), so we see raw codes (`0xb4-0xb9`, `0xdf`, etc.). `ManagerTranslateCode`
in `Accessibility.cpp` mirrors the engine's mapping for log readability.

### Click flow

Decompile of `HandleLMouseDown / HandleLMouseUp / HitCheckMouseLocal`:

- `HitCheckMouseLocal` reads global mouse coords via
  `CSWGuiPanel::GetLocalMouseCoords(parent, &x, &y)`, subtracts viewport_x/y,
  iterates `controls` and calls each child's `vtable->HitCheckMouse(localX,
  localY)`. Returns first hit + row index via out-param.
- `HandleLMouseDown` calls `SetSelectedControl(this, rowIdx, 1)` if needed,
  then fires `HandleInputEvent(this, 0x1f8, 1)` ("click begin").
- `HandleLMouseUp` re-hit-tests; if same row as down, fires
  `HandleInputEvent(this, 0x27, 1)` — `0x27` = `KEYBOARD_F1` = engine's
  "activate / confirm" code.

So a click is `SetSelectedControl + HandleInputEvent(0x27)`. The second call
is identical to what the engine fires on Enter for the focused control —
which means **if engine focus = our cursor target, Enter and click both
activate the same thing**. This is the lever Phase 3+4 use.

### Hook framework constraint

**Entry-point hooks on `CSWGuiListBox` are toxic** even when the hook never
fires. Discovered empirically (session 4): installing `HandleLMouseDown @
0x0041c4a0` with a clean 5-byte register-source cut caused title-screen arrow
nav to oscillate (focus moves to next button, engine reverts, user hears
double announcement). The pre-existing `SetActiveControl @ 0x0041c16b`
mid-function hook does NOT cause this. Either Lane's framework wrapper has a
latent bug for entry-point hooks on certain class layouts, or some indirect
call path scans bytes near these entry points. Workaround: hook mid-function
only; never at function entry on CSWGuiListBox.

See `memory/feedback_hook_design_register_sources.md`.

### What works now (committed at HEAD)

- Manager-level input hook at `0x0040c907` — captures every key on every screen
  with both press and release edges. `ManagerTranslateCode` produces readable
  log output.
- Panel-children walk on first focus into a previously-unseen panel — logs
  every direct child with id, vtable, extracted text or `src=none` diagnostic.
- Listbox-children walk + cursor read (`selection_index`, `top_visible_index`,
  `items_per_page`, `bit_flags`, `controls.size`) on every listbox event.
- Multi-line listbox blob enumeration via `SpeakBlobIfChanged` — newline-
  separated row text is queued as numbered Tolk utterances.
- TLK lookup re-enabled with SEH guard — many previously-unresolved
  `src=none` Buttons/Labels now resolve to localized strings (e.g.
  "Abbrechen", "Difficulty", "Wähle deine Klasse.").
- All log rate limits removed (full fidelity per
  `memory/feedback_log_no_rate_limits.md`).

### What does NOT work and why

- 3 of 5 Options tabs unreachable, 11 of 14 inner-settings buttons
  unreachable — broken `.gui` focus-cycle data, no hotkey workaround.
- +/- adjustment buttons are icon-only (no text, no `str_ref`) — TLK lookup
  can't resolve them; they read as `src=none control N`.
- Engine's focus cycle on the title screen works fine (3 cycles cleanly), but
  most other panels are partially or wholly broken.

## Decisions

### 1. Unified arrow-keys-drive-cursor (over hybrid mode)

User-facing model: **one set of keys does one thing in all contexts**. Arrow
keys move a synthesized pointer; Enter clicks. No mode toggles, no special
keys. In menus the cursor "teleports" between consecutive `panel.controls`
entries (snap-to-element); in 3D world space it moves smoothly. Same code
path, same announcement pipeline, same activation pipeline.

Rejected: hybrid mode (focus jumps in menus, smooth cursor in world). Real
costs (mode detection, two announcement paths, two activation paths) outweigh
its theoretical efficiency benefit, especially after snap-to-element makes
unified menu nav teleport-fast.

Rejected: numpad-only "extra" nav. User explicitly chose arrow-keys-overwrite
because two-mode systems impose cognitive load and the main-menu's working
arrow-cycle (which our overwrite would replace) is "one menu lost in exchange
for all menus working" — acceptable.

### 2. Stable chain = `panel.controls` iteration order

Down on element `i` → element `i+1`. Up → `i-1`. Clamp at `[0, size-1]`. No
wrapping (user wants borders, not wrap-around). Same chain every session for
the same panel — `panel.controls` order is set once when the engine builds
the panel from its `.gui` and doesn't change at runtime.

### 3. Don't filter "suspicious" elements

Multi-line-blob listboxes, `src=none` icon buttons, NULL slots, vtables we
haven't classified — keep them all in the chain for now. Hearing
`"control 11"` or `vtable=0073E658` placeholders gives more diagnostic info
than silently skipping. We'll filter from real evidence later, not from
guesses now.

### 4. Panel-open enumeration via Tolk (no separate "list elements" key)

When `OnSetActiveControl` sees a *new* panel pointer, queue Tolk speech for
every `panel.controls` entry in order. No discoverability hotkey needed —
the user hears the inventory automatically on entering a screen.
Implementation: existing panel walk (currently log-only) gets a sibling Tolk
queue per child. Re-entering the same panel does nothing extra (dedup
against last-announced panel pointer).

### 5. Engine-event suppression window

Engine's broken cycle still fires `SetActiveControl` events on its own when
its data is partially complete. To prevent doubled announcements, set
`g_lastChainAnnounce` timestamp on every chain-driven speak; in
`OnSetActiveControl`, if `(now - g_lastChainAnnounce) < 100ms`, skip the
Tolk speech path (still log). Our chain becomes the single source of truth
for what the user hears. Engine focus state can drift; user experience
stays anchored to our chain.

### 6. Phased implementation, speak-only first

Phase 1 = chain announcement only (no cursor movement, no
`SetActiveControl` calls). Lowest risk, validates the chain UX before
introducing the harder pieces. Phases 2-5 layer on cursor sync, focus
alignment, click activation, spatial mode.

### 7. Main-menu working cycle is acceptable collateral

Title screen's K/L cycle works fine today. Once we install our chain
override, that cycle is gone (replaced by our identical-or-better chain).
User explicitly accepts: "one menu lost in exchange for all menus working."

## Open investigations (do these before / during Phase 2-3)

These are not blockers for Phase 1 (speak-only) but become required as we
layer cursor movement and click activation on top.

### I-1. Engine's "what's under cursor" hit-test entry point

For Phase 3 (cursor sync for hover side effects), we'll move the system
cursor to a target element and want to verify the engine sees the same
element under it. SARIF query for top-level hit-test on `CSWGuiManager`,
likely something like `HandleMouseInput` or `ProcessMouseMovement`.

```bash
SARIF="docs/llm-docs/re/k1_win_gog_swkotor.exe.sarif"
jq -r '.runs[0].results[]
  | select(.ruleId == "FUNCTIONS")
  | select(.properties.additionalProperties.namespace == "CSWGuiManager")
  | .properties.additionalProperties
  | "\(.location)  \(.name)\t\(.value)"' "$SARIF"
```

Done when: we have an address + signature for a function that takes screen
coords and returns the topmost hovered control (or NULL).

### I-2. CSWGuiExtent struct layout

For Phase 2 (cursor snap to element center), we need each control's screen
rectangle (left, top, width, height). `CSWGuiControl` likely has an
`extent: CSWGuiExtent` member; SARIF DATATYPE entry will give offsets.

```bash
jq -r '.runs[0].results[]
  | select(.ruleId == "DATATYPE")
  | select(.properties.additionalProperties.name == "CSWGuiExtent")
  | .properties.additionalProperties.fields // {}
  | to_entries[]
  | "\(.value.offset)\t\(.key)\t\(.value.type.name // .value.type.kind)"' "$SARIF" | sort -n -k1
```

Also need the offset of `extent` within `CSWGuiControl`. Cross-reference
with `memory/project_kotor_gui_struct_offsets.md` (already has Button text
+0x16c, Label text +0xe8 — extent should be near the start of the control).

Done when: we can read `(left, top, width, height)` for any
`CSWGuiControl*`.

### I-3. CSWGuiManager global pointer

To find the currently active panel from anywhere (not just from our hook
event params), we need the `CSWGuiManager` global pointer. The
`HandleLMouseDown` decompile uses `GuiManager->field2_0x8` as the active
panel — that's the field, but where does `GuiManager` live?

```bash
jq -r '.runs[0].results[]
  | select(.ruleId == "SYMBOLS")
  | select(.properties.additionalProperties.name == "GuiManager")
  | .properties.additionalProperties' "$SARIF"
```

Workaround if not found: track the most recent panel from
`OnSetActiveControl` events into a static `g_currentPanel`. Phase 1 can
use that exclusively.

### I-4. Cursor coords vs. screen coords vs. windowed/fullscreen

Lane's prototype uses Win32 `SetCursorPos(int x, int y)` — desktop pixel
coords. The engine reads cursor via `GetCursorPos` (presumably). For
fullscreen-exclusive, both should agree. For windowed, `SetCursorPos` is
desktop-relative; engine probably uses client-area coords; we'd need a
`ScreenToClient` translation. Test in-game which mode the user runs.

Done when: we know the conversion (or know there isn't one needed) between
the screen-rect coords stored in `CSWGuiExtent` and the coords we feed to
`SetCursorPos`.

### I-5. Are numpad keys free in default `[Keymapping]`?

Was a fallback option for "extra navigation key" before we settled on
arrow-keys-overwrite. Probably not needed, but if Phase 5 (spatial mode)
ever needs a side channel for cursor speed / mode toggle, knowing what's
free helps.

```bash
grep -i "numpad\|NUMPAD" "/c/Program Files (x86)/Steam/steamapps/common/swkotor/swkotor.ini"
```

Done when: documented which numpad scancodes (DIK_NUMPAD0 through 9) appear
in `[Keymapping]` in default config.

### I-6. CSWGuiControl `IsNavigable` / focus-skip flag

Some controls in `panel.controls` are decorative-only (header labels,
background images) and probably shouldn't be in our chain even though
they're in the iteration. Engine likely has a flag distinguishing these
(per `CSWGuiControl::GetIsSelectable` from listbox decompile, vtable index
unknown). Find it; we may eventually want to skip non-selectable controls
from the chain.

Decision per user: don't filter yet. Investigate but use only if real
evidence shows the chain has problems.

```bash
jq -r '.runs[0].results[]
  | select(.ruleId == "FUNCTIONS")
  | select(.properties.additionalProperties.name | test("IsSelectable|IsNavigable"))
  | .properties.additionalProperties
  | "\(.namespace)::\(.name)  @\(.location)  \(.value)"' "$SARIF"
```

## Implementation roadmap

Each phase has a definite "done" condition and is testable in isolation.
Build → apply → launch → log-check loop is well-validated.

### Phase 1 — Speak-only chain navigation (NEXT SESSION FIRST)

**State:**
```cpp
static void* g_currentPanel = nullptr;       // tracked from OnSetActiveControl
static void* g_chainPanel   = nullptr;       // panel our chain is bound to
static int   g_chainIndex   = 0;             // current position in panel.controls
static int   g_chainSize    = 0;             // cached panel.controls.size
static uint64_t g_lastChainAnnounce = 0;     // for engine-suppression window
```

**`OnHandleInputEvent` additions:**
- Detect `param_1 == 0xb6` (up) or `0xb7` (down). Skip if `param_2 == 0`
  (release).
- If `g_currentPanel != g_chainPanel`: rebind chain (`g_chainPanel = g_currentPanel;
  g_chainIndex = 0; g_chainSize = panel.controls.size`).
- Compute new index: clamp `g_chainIndex ± 1` to `[0, g_chainSize - 1]`. If
  unchanged (at edge), still announce current element so user gets edge feedback.
- Read target = `panel.controls.data[g_chainIndex]`.
- `ExtractAnnounceableText(target, buf)` → `tolk::Speak(buf, false)`.
- `g_lastChainAnnounce = GetTickCount64()`.

**`OnSetActiveControl` additions:**
- `g_currentPanel = panel`.
- If `panel != g_lastEnumeratedPanel`: walk `panel.controls`, queue Tolk speech
  for every child's text (existing log-only walk → also speak). Set
  `g_lastEnumeratedPanel = panel`. Possibly prefix with `"Panel:"` or panel
  identifier (panel ID? title from a known child? TBD).
- If `(GetTickCount64() - g_lastChainAnnounce) < 100`: skip Tolk speak path
  (still log normally). Engine-suppression window.

**Done when:** in-game, on Options screen, pressing arrow keys announces
elements in stable `panel.controls` order. Pressing Down on the last element
re-announces the same element (edge clamp). Re-entering the panel doesn't
re-enumerate (dedup).

**Risk:** low. No new hooks; no `SetActiveControl` calls; no system cursor
manipulation. Pure additive announcements. Engine still does whatever it
does in parallel (we just suppress its announcements during our window).

### Phase 2 — Cursor synchronization

Make the system cursor follow the chain so engine hover side effects fire
naturally on the current chain element.

**Prereqs:** I-2 (CSWGuiExtent layout), I-4 (cursor coord conventions).

**Changes:**
- After the chain announcement in `OnHandleInputEvent`: read `target.extent`,
  compute center `(left + width/2, top + height/2)`, call
  `SetCursorPos(centerX, centerY)`.
- Verify in-game that hover state updates visibly (sighted observer or via
  log output of any new engine-side hover events).

**Done when:** moving the chain cursor visibly highlights elements (sighted
verification) and any hover-only labels become readable.

**Risk:** medium. Cursor coord math could be wrong (windowed vs fullscreen);
engine might not refresh hover state without a cursor-move event we don't
synthesize.

### Phase 3 — Engine focus alignment

Ensure engine's `active_control` matches our chain target so Enter activates
the correct thing.

**Prereqs:** Phase 1 stable.

**Approach:** call `panel->vtable->SetActiveControl(panel, target)` directly
from our `OnHandleInputEvent` after computing the chain target. This fires
our existing `OnSetActiveControl` hook recursively — guard with
`g_inProgrammaticFocus` flag so we don't re-announce or re-enter.

**Risk:** medium. Our forced `SetActiveControl` aligns engine, but engine's
*own* per-button event handler might still run after our manager hook returns
and re-move focus. Mitigation: detect this in `OnSetActiveControl` and revert
once. Test for oscillation.

### Phase 4 — Click activation via Enter

Let user press Enter to activate the chain target.

**Prereqs:** Phase 3 (engine focus aligned with chain).

**Approach 1 (preferred):** do nothing. Engine's existing Enter handling
dispatches to `active_control` which is now our chain target (Phase 3). Free
activation.

**Approach 2 (fallback):** intercept Enter (0xb5) in our hook, synthesize a
`mouse_event(LEFTDOWN/UP)` at cursor position. Engine's click pipeline
focuses + activates the cursor's hit target. Independent of focus state.

**Done when:** user can press Enter on a chain element and the engine
performs the corresponding action (button click, +/- adjust, etc.).

### Phase 5 — Spatial mode (3D world / non-panel screens)

When `g_currentPanel == nullptr` (no panel active), switch to Lane's
smooth-cursor model:
- Arrow press → set/clear direction-flag bit (Lane's `dirBitFlags`).
- Background tick (timer or main-loop hook) every ~16ms calls
  `SetCursorPos(curX + speed × xDir, curY + speed × yDir)`.
- After move: hit-test (I-1 prereq), `ExtractAnnounceableText`, announce if
  changed.

**Prereqs:** I-1, plus all of Phase 1-4.

**Done when:** cursor moves smoothly in 3D world, hover announces
interactables, click activates them.

## Open issues (carry forward)

- `+/-` icon buttons (vtable=0073E658, no text, no `str_ref`) — TLK lookup
  doesn't help. They'd read as `"control N"` placeholder. Acceptable for now;
  user can position chain on them and press Enter to test +/-. Long-term: a
  per-vtable name table ("control with vtable 0073E658 = +/- button").
- TLK `c_string` leak — engine-CRT allocations we don't free. Bounded across
  a session but unbounded across many sessions. Future cleanup: find
  `CExoString::~CExoString` and call it.
- Multi-line listbox in chain — currently entries blob-announce on focus.
  In Phase 1 the chain might land *on* the listbox and the user hears the
  whole "List, N items, 1. ..." enumeration. Acceptable. Later: skip listbox
  blobs from chain (filter-from-evidence).

## Reference logs

- **Last working baseline (Phase 0):** `<install>/logs/patch-20260430-110057.log`
  (972 lines, 0 SEH, all current features active).
- **Listbox entry-hook regression:** `patch-20260430-073819.log` (55 lines,
  one HandleInputEvent fired, focus oscillation symptom).
- **Manager hook + decompile session:** `patch-20260430-070010.log` (332
  lines, panel walks visible, multi-line blob first observed).

## Memory references (.claude/projects/.../memory/)

Project-state:
- `project_listbox_keyboard_model.md` — selection_index = -1 semantics,
  scroll-mode vs selection-mode
- `project_listbox_click_flow.md` — HitCheckMouseLocal + LMouseDown/Up +
  SetSelectedControl + activation via 0x27
- `project_kotor_gui_struct_offsets.md` — Button text +0x16c, Label text
  +0xe8, vtable indices for downcasts
- `project_main_menu_input_path.md` — historical, superseded by manager-level
  hook
- `project_kpatchmanager_lea_bug.md` — framework wrapper bug, PR opportunity

Behavior rules:
- `feedback_hook_design_register_sources.md` — mid-function + register
  sources only; NO entry-point hooks on CSWGuiListBox
- `feedback_log_no_rate_limits.md` — never throttle diagnostic logs
- `feedback_never_silence_fallback_announcement.md` — fallback placeholders
  must speak, dedup is for resolved text only
- `feedback_explain_decisions_step_by_step.md` — walk through decisions
  individually; bulk lists fail
- `feedback_discovery_doc_format.md` — known/suspected/open structure for
  research docs

## Build / run quick reference

From project root (`C:\Users\fabia\Dev\kotor`):

```bash
# Build
tools/kdev/bin/Debug/net10.0/win-x64/kdev.exe build

# Apply
tools/kdev/bin/Debug/net10.0/win-x64/kdev.exe apply

# Launch
tools/kdev/bin/Debug/net10.0/win-x64/kdev.exe launch --monitor

# Logs
ls -1t "/c/Program Files (x86)/Steam/steamapps/common/swkotor/logs/" | head -3
```

Bash/Git Bash on Windows; absolute paths; no `cd /d`.

## Re-entry checklist for next session

1. Read this file end-to-end.
2. `git log --oneline -5` to see what's already on `main`.
3. Run investigations I-1, I-2, I-3 (~15 min total via SARIF queries).
4. Implement Phase 1 (speak-only chain). One commit. Test in-game; adjust.
5. Iterate Phases 2-5 once Phase 1 is stable in user's hands.
