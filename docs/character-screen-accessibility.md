# Character Creation Screen — Accessibility Plan

Working investigation + implementation plan for making KotOR's character
creation flow usable with a screen reader. Scope: every panel reachable from
"Neues Spiel → Eigener Charakter" until the chargen flow finalises. Three
problem classes drive the work, each tracked separately below.

Scaffolded in the known / suspected / open format. Update as facts solidify
or assumptions get invalidated.

## Sources used

- `logs/last-patch.log` (older session) and the newest in-game session at
  `Steam/.../logs/patch-20260507-223523.log` — full panel walks for every
  chargen sub-screen the user visited.
- `docs/llm-docs/re/swkotor.exe.h` — Lane's typed structures
  (`CSWGuiAbilitiesCharGen`, `CSWGuiClassSelection`, `CSWGuiPortraitCharGen`,
  `CSWGuiNameChargen`, `CSWGuiSkillsCharGen`, `CSWGuiFeatsCharGen`,
  `CSWGuiEditbox`, `CSWGuiEditText`).
- `docs/llm-docs/re/k1_win_gog_swkotor.exe.xml` — verified vtable→class
  symbols (`CSWGuiButton_vtable @0x73e658`,
  `CSWGuiLabelHilight_vtable @0x73e8e8`,
  `CSWGuiSlider_vtable @0x73e9d0`,
  `CSWGuiEditbox_vtable @0x73eac8`).
- `patches/Accessibility/menus.cpp` — existing slider deferred-input model
  (`g_pendingSliderInput` / `IsSlider` / sibling-label fallback).
- `C:\Users\fabia\Dev\arena\src\Core\Services\BaseNavigator\BaseNavigator.InputFields.cs`
  + `UIFocusTracker.cs` — the arena project's input-field UX we're mirroring.

## Per-panel inventory (from the newest log)

Every chargen sub-screen, in flow order, with the parent struct from
swkotor.exe.h matched to the panel-walk fingerprint.

### Class selection — `CSWGuiClassSelection`

- Panel `07506E88`, 17 children
- 6× `CSWGuiButton` (the class icons, vtable=0x73e658, **empty text** —
  image-only). Indices 11..16 in the panel.
- 1× `CSWGuiLabel class_label` showing the *currently focused* class name
  ("Gauner" in the captured run). Engine updates this label as the user
  hovers/focuses an icon.
- `description_label`, `instruction_label`, "Abbrechen" button, etc.
- The same panel struct backs the second-level "Standard- vs.
  Eigener Charakter" prompt (`12E6A8C0` in the log).

### Portrait + tab strip — `CSWGuiCustomPanel`

- Panel `075B4270`, 21 children. This is the **chargen "spine"** panel:
  6 highlight labels (vtable=0x73e8e8 = CSWGuiLabelHilight) for the step
  number strip, 6 numbered labels ("1".."6"), 6 step buttons
  (`stepname_buttons[]`: Portr&auml;t / Attribute / F&auml;higkeiten / Talente /
  Name / Spielen), plus Zur&uuml;ck and Abbrechen. The active step is the one
  whose hilight label is rendered.
- The 4-step variant (`074919D0`) is the same struct used for the Standard
  Character flow (Portr&auml;t / Name / Spielen).

### Portrait selection — `CSWGuiPortraitCharGen`

- Not visited in the captured run; struct known.
- `portrait_label` is a real CSWGuiLabel (already accessible).
- `right_arrow_button` / `left_arrow_button` — two buttons that cycle
  `portrait_id`. The label updates as you cycle.
- `head_3d_scene_control` is the live 3D head — irrelevant for screen
  readers.

### Attributes — `CSWGuiAbilitiesCharGen`

- Panel `074FD278`, 37 children
- Per attribute (6 of them: Stärke / Geschicklichkeit / Verfassung /
  Intelligenz / Weisheit / Charisma):
  - `ability_label[i]` (CSWGuiLabel) — the localised attribute name
  - `ability_button[i]` (CSWGuiButton) — text="8" (the *current* value)
  - `ability_plus_button[i]` (CSWGuiButton) — image-only, empty text
  - `ability_minus_button[i]` (CSWGuiButton) — image-only, empty text
- Cost / Modifier labels next to the focused attribute.
- Description listbox at id=29 (the `Beschreibung` pane).
- Empfohlen / OK / Abbrechen buttons.
- Truth-source ints already in the struct: `new_str / new_dex / new_con /
  new_wis / new_int / new_cha` (`ulong`s) and `ability_points_remaining`.

### Skills — `CSWGuiSkillsCharGen`

- Panel `0FD9A748`, 45 children
- Same shape as attributes. 8 skills (Computer / Sprengstoff / Tarnung /
  Bewusstsein / &Uuml;berreden / Reparieren / Sicherheit / Heilen) — each has
  a value-button + name-label + plus/minus arrows.

### Talents / Feats — `CSWGuiFeatsCharGen`

- Panel `07475F60`, 13 children. Different shape from attributes/skills: a
  feats listbox + description listbox + "Empfohlen" / "Hinzuf. Talent" /
  OK / Abbrechen, plus `name_label` and `selections_remaining_label`.
- The listbox itself already announces correctly.

### Name entry — `CSWGuiNameChargen`

- Panel `07500458`, 140 child slots (most NULL — sparse layout)
- Real children: title label, "Name" label, **edit field at id=132**
  (`07500688`, vtable=`0x0073EAC8` = `CSWGuiEditbox_vtable`), OK button,
  Zufallsname (Random) button, Abbrechen button.
- The edit field embeds a `CSWGuiEditText` whose typed text lives in a
  `CExoString`.

## Three problem classes

### 1. Diverse labels not read correctly (classes, portraits, image-only buttons)

**Known**

- `vtable=0x73e658` is the standard `CSWGuiButton` — the "speculative read
  miss" entries in our log are not vtable-unrecognised; they fire because
  the button's `gui_string` / inline `CExoString` is empty. Class icons,
  portrait arrows, and chargen ± arrows all share this property: the icon
  is the visual, no text is set.
- The `siblinglabel-fallback` already kicks in on these and announces the
  *nearest* label — useful in the attribute panel (says "Stärke") but
  misleading for the ± buttons (says "Stärke" for both arrows).
- For class selection, `CSWGuiClassSelection.class_label` is the engine's
  own "currently focused class name" — we should read it as the source of
  truth, not the icon button itself.
- For portrait selection, `CSWGuiPortraitCharGen.portrait_label` is the
  engine-maintained currently-shown portrait name; `portrait_id` is the
  raw integer.

**Suspected**

- The class icons are a `CSWGuiClassSelChar` array (6 entries); index 0..5
  maps to a fixed class order. Mapping (Soldat / Gauner / Wachläufer /
  optional KotOR-2 prestige in 4..5) is likely encoded in `classes.2da`
  with the same row order. Verifying the order is a quick test:
  navigate icon-by-icon, log `class_label` text on each focus.
- Each icon button has its own `gui_object` / `tooltip_string` (inherited
  from `CSWGuiControl`) which the engine *might* fill with a shorter
  class name. Worth peeking before we hard-code from 2DA.

**Open**

- Class **descriptions** for the audio "tooltip" the user asked for (e.g.
  "Soldat: gut im Nahkampf, hoher Trefferpunkt-Pool, schwere Rüstung
  einsetzbar"). These almost certainly live in `dialog.tlk` keyed by the
  same strrefs `classes.2da` references — needs a one-time RE pass to
  identify the strref columns.
- Portrait names per `portrait_id` — likely served via `portraits.2da` or
  read directly from `portrait_label.gui_string` (engine already updates
  it). Probably not necessary to walk the 2DA; just read the label.

**Plan**

1. Add a panel-kind detector for `CSWGuiClassSelection` (match by
   `panels[0]` parent + child count + presence of `class_label`). When
   focus moves to a class-icon button, look up the index in
   `class_selections[]` and announce **the parent's `class_label.gui_string`**
   as the focused-control name. This already works in the engine — we just
   need to read the *parent's* label, not the *focused button's* label,
   and override the sibling-label fallback for these specific controls.
2. Same trick for `CSWGuiPortraitCharGen`: announce
   `portrait_label.gui_string` instead of trying to read the arrow buttons'
   text. The arrow buttons can be announced as "vorheriges Portr&auml;t" /
   "n&auml;chstes Portr&auml;t" overrides via `strings.h`.
3. Chargen ± arrows in attributes / skills are handled by the cycle
   mechanism (see Section 2) — the `FindSiblingLabel` sharpening
   removes their misleading sibling-label fallback so the existing
   squash + `FindAdjacentArrow` path engages.
4. **(Optional, second pass)** Class description tooltip on focus,
   spoken once per class (deduplicated). 2DA strref lookup, then
   `GetSimpleString` (already used by `engine_offsets.h:kAddrGetSimpleString`).

### 2. Spend controls — reuse the existing cycle-widget mechanism

**Known**

- The options menu's Difficulty cycle (`[◀] Normal [▶]`) renders as
  three same-row siblings: empty-text left arrow, text-bearing value
  button, empty-text right arrow. The cycle plumbing in `menus.cpp`
  already handles this shape end-to-end:
  - `RebindChain` (menus.cpp:1830-1869) **squashes empty-text
    navigables that share a y-row with a text-bearing entry**, so the
    user only navigates Up/Down to the value button — the bare arrows
    stay clickable but aren't separate chain stops.
  - Left/Right on the value button calls `FindAdjacentArrow`
    (menus.cpp:1530), which finds the closest **empty-text** navigable
    neighbour at the same y-row → fires it via `g_pendingActivate`
    (menus.cpp:3168-3185).
  - The engine processes the click and rewrites the value button's
    text in place (`"Normal"` → `"Schwer"`, `"8"` → `"9"`).
  - `MonitorFocusedControl` (menus.cpp:3334) re-extracts the focused
    control's announceable text every frame and speaks the diff. Cycle
    value, slider value, and toggle on/off all flow through here, no
    per-widget code.
- The chargen "spend" widgets are structurally **identical** to the
  Difficulty cycle. From the captured panel walk at y=205:
  - `074FECC8 button text="8"` — value button (text-bearing)
  - `07500744` and `074FFCAC` — empty-text image-only ± arrows on the
    same y-row, ~67 px to either side of the value button.
- The real `CSWGuiSlider` path (vtable=0x73e9d0, used by Music / Voice
  / SFX / Movie / Gamma) is a *different* widget shape and is not
  involved here. Its deferred-input pump
  (`g_pendingSliderInput` → HandleInputEvent code 500/501) is the
  right model for sliders, the wrong model for chargen spinners.

**Why it doesn't already work**

- Both the chain squash and `FindAdjacentArrow` use
  `ExtractAnnounceableText() == nullptr` as their "empty-text"
  predicate.
- The chargen ± arrows have no `gui_string` and no inline `CExoString`,
  but our **siblinglabel-fallback** (`menus.cpp:FindSiblingLabel`)
  finds the nearest `ability_label` (e.g. `"Geschicklichkeit"`) and
  paints the arrows with it.
- That single fallback hit defeats both halves of the cycle mechanism:
  the squash skips the arrows (so they stay in the chain), and
  `FindAdjacentArrow` skips them as candidates (so Left/Right on the
  value button finds no neighbour).

**Suspected**

- Suppressing siblinglabel-fallback for chargen ± arrows is enough to
  make the cycle mechanism handle them — no per-panel detection, no
  truth-source reads, no bespoke dispatch, no new pump needed. The
  Difficulty-cycle codepath is already correct; it's just being
  starved of the empty-text-neighbour signal.
- The cleanest predicate is **positional**, not panel-kind-specific:
  - Control is a plain `CSWGuiButton` (vtable=0x73e658).
  - Its own text-extraction returns null (genuinely image-only).
  - A same-row text-bearing button neighbour exists within the
    same tolerance `FindAdjacentArrow` uses (~80 px, `dy <= 5`).
- That predicate also covers the **portrait-screen left/right arrows**
  for free (M2): `CSWGuiPortraitCharGen.left_arrow_button` /
  `right_arrow_button` are exactly the same shape, with
  `portrait_label` as the same-row text-bearing sibling.

**Open**

- Whether `FireActivate` on the chargen ± buttons actually decrements
  the attribute (the equip-screen taught us `FireActivate` can no-op
  when an internal `is_active`-style flag isn't raised). Single-run
  verification: focus value button, press Right, watch the log for
  the value-button text changing.
- Whether refusal-to-decrement (already at class minimum / max=18)
  fails silently. If silent, the per-frame monitor produces no
  re-announcement and the user gets no feedback on the keypress. Fix
  if observed: emit a localised "blockiert" cue when Left/Right was
  consumed but the value button text didn't change within a couple
  of frames.

**Plan**

1. **Sharpen `FindSiblingLabel`** (or add a wrapper): if the candidate
   *control* is a `CSWGuiButton` whose own text is empty AND there's
   a same-row text-bearing button neighbour within the
   `FindAdjacentArrow` tolerance, return null instead of the nearby
   label. This is the only structural change.
2. **Verify** by playing the chargen attribute + skill tabs:
   - Up/Down should land only on the value buttons (Stärke 8 →
     Geschicklichkeit 8 → Verfassung 8 → ...).
   - Left/Right on a value button should fire the ± neighbour and the
     monitor should speak the new value (`"8"` → `"9"`).
3. **(Conditional, only if step 2 fails)** Fall back to the bespoke
   path: detect `CSWGuiAbilitiesCharGen` / `CSWGuiSkillsCharGen` by
   panel vtable, read truth-source from `new_<attr>` /
   `ability_points_remaining`, dispatch via the dedicated handler
   the engine uses for the ± click (likely an analogue of
   `OnSelectSlot` on the equip panel).
4. **(Optional enhancement)** When focus is on a value button in
   `CSWGuiAbilitiesCharGen`, additionally announce the
   `ability_points_remaining` int and the per-attribute `cost_label`
   text. These aren't part of the cycle mechanism but enrich the
   spending UX. Gated behind a follow-up; the core M3 ships without it.

### 3. Input field — name entry, mirrored from arena's UIFocusTracker model

**Known**

- The chargen Name screen has exactly one `CSWGuiEditbox`
  (vtable=0x73eac8). It's the only screen in the entire game where this
  vtable appears (verified across the captured panel walks).
- `CSWGuiEditbox` layout (from swkotor.exe.h):
  - +0x00..+0x6c — embedded `CSWGuiNavigable`
  - +0x6c..+0xe0 — embedded `CSWGuiBorder` (single border, not the dual-
    border CSWGuiButton has)
  - +0xe0+ — `CSWGuiEditText`:
    - +0xe0..+0x150 — embedded `CSWGuiText` (gui_string @ +0xf4 absolute)
    - +0x150 short
    - +0x152 short
    - +0x154 undefined4
    - +0x158..+0x160 — `CExoString string` (the *typed* text, c_string @
      +0x158, length @ +0x15c)
- The engine handles character-by-character keyboard input itself; we don't
  need to replicate text editing. We need to **observe** the typed text
  and announce changes.
- Arena's flow (BaseNavigator.InputFields.cs):
  - Detect input field focus → "edit mode"
  - In edit mode: pass keys through, but Up/Down → re-announce content,
    Left/Right → announce char at cursor, Backspace → announce deleted
    char, Tab/Escape → exit edit mode, Enter → submit
  - Track previous text + caret position so each delta is computable.

**Suspected**

- The CSWGuiEditbox's caret position lives in one of the two `short`
  fields at +0x150 / +0x152. `field1_0x70 / field2_0x72` in the relative
  offsets — likely caret index and selection length, in some order.
- The engine's keystroke handler for the edit field is reachable via
  `CSWGuiEditbox::HandleInputEvent` (vtable[15] like other navigable
  controls). We should not need to replace it — only observe and react.

**Open**

- Whether the edit field has a focus state we can detect cheaply (likely
  yes: it's the `panel.activeControl` while focused, same as any other
  navigable). If so, `IsActiveEditbox(panel)` is a one-liner.
- Whether `CSWGuiNameChargen.random_button` ("Zufallsname") replaces the
  field text via the same `CExoString string` field or a different path.
  Probably the same — observable via the polling loop.

**Plan**

1. **Detect edit-mode** on each Update: walk the foreground panel's
   `activeControl`, check vtable == `0x73eac8`. If yes → we're in
   edit mode for this frame.
2. **Poll text + caret each frame** while in edit mode:
   - Read `c_string` at editbox+0x158 (snap to a local buffer, max ~32
     chars — Aurora editboxes are length-bounded).
   - Read caret short at +0x150 (or whichever proves correct).
   - Diff against last-frame snapshot.
3. **Announce deltas** like arena does:
   - Length grew by 1 → announce the new last character (or the char at
     `caret-1`).
   - Length shrunk → announce the char that *was* at the old caret
     position before deletion.
   - Caret moved without length change (Left/Right) → announce char at
     new caret.
   - Tab/Escape (already chain-handled) → exit cleanly; no re-entry into
     edit mode until the user re-focuses.
4. **Special-case Up/Down**: re-announce the entire field content (matches
   arena's UX). Down/Up don't move the caret in a single-line edit field,
   so this is a free key for "say full name".
5. **Special-case Enter**: announce "Name übernommen: <text>" before
   letting the engine fire the OK button. (Or just don't intercept and
   let the chain layer handle Enter as-FireActivate-on-OK.)
6. **Special-case `random_button` activation**: after the engine
   replaces the text, the polling-loop diff fires automatically; we get
   "Zufallsname: <generated name>" for free.

## Sequencing — recommended milestones

Each milestone is independently testable end-to-end (start a new chargen,
run through the relevant tab, verify announcements). Built in order so
each unblocks the next.

1. **M1 — class label override (small, fastest win)**
   - Add `CSWGuiClassSelection` panel-kind detection.
   - Override the focused-control name with `class_label.gui_string` for
     the 6 class icons.
   - Don't touch the rest of the panel (Abbrechen / description label
     keep working).
   - Ship: chargen step 1 reads correctly.

2. **M2 — portrait label override + arrow naming**
   - Add `CSWGuiPortraitCharGen` panel-kind detection.
   - Announce focused arrow as "vorheriges/nächstes Portr&auml;t" plus the
     new `portrait_label` after activation.
   - Ship: portrait step reads correctly.

3. **M3 — attribute spend control via cycle-mechanism reuse**
   - Sharpen `FindSiblingLabel` (or wrap it) to return null for
     image-only `CSWGuiButton`s that have a same-row text-bearing
     button sibling.
   - No new panel detection, no truth-source reads, no new pump — the
     existing cycle squash + `FindAdjacentArrow` + `MonitorFocusedControl`
     loop carries the load.
   - Verify by playing the attribute tab: Up/Down lands only on value
     buttons; Left/Right adjusts and the monitor speaks the new value.
   - Ship: attributes spendable from keyboard; values spoken on each
     change.
   - **Fallback (only if FireActivate no-ops on the ± buttons)**:
     bespoke `CSWGuiAbilitiesCharGen` detection + truth-source path.

4. **M4 — skill spend control (free with M3)**
   - The same `FindSiblingLabel` sharpening covers `CSWGuiSkillsCharGen`
     because the panel's structural shape (per-skill value-button
     flanked by image-only ± arrows on the same y-row) is identical.
   - Verify the same way as M3 against the skills tab.
   - **Fallback**: same as M3 — bespoke detection + truth-source read
     against `CSWGuiSkillsCharGen` if `FireActivate` doesn't propagate.

5. **M5 — input field for name**
   - Add CSWGuiEditbox detection.
   - Per-frame polling + diff + announcement (mirror arena).
   - Up/Down re-announce, Enter submits.
   - Ship: chargen completable end-to-end with screen reader.

6. **M6 — class descriptions (optional)**
   - Add `classes.2da` strref lookup → dialog.tlk via `GetSimpleString`.
   - Speak the description as a deferred "tooltip" after a short dwell
     on the class icon.

## Open RE work to unblock the milestones

These are concrete things that need a Ghidra session before the relevant
milestone can land. None of them are blockers for M1; M1 only needs the
already-known struct layout.

- M2: confirm `CSWGuiPortraitCharGen` field offsets — the struct is
  documented but not yet exercised. The portrait screen's left/right
  arrow buttons should also fall out of the M3 `FindSiblingLabel`
  sharpening (they have the same shape: image-only buttons with a
  same-row text-bearing sibling — `portrait_label`), so M2 may collapse
  into "verify the portrait screen happens to work after M3".
- M3 contingency: if `FireActivate(ability_plus_buttons[i])` no-ops
  (equip-screen-style `is_active` gating), find the engine method
  that performs the decrement (analogue of `OnSelectSlot` on the
  equip panel) and call it directly. Only triggered if the cheap
  path proves insufficient.
- M4 contingency: same as M3 against `CSWGuiSkillsCharGen`. Also
  worth confirming `CSWGuiSkillsCharGen` value-array layout in
  swkotor.exe.h:16720+ in case the fallback path is needed.
- M5: confirm caret-position offset (+0x150 vs +0x152 within
  CSWGuiEditText) by reading 4 bytes per frame for a couple of seconds
  during typing — whichever short tracks 1:1 with character count is the
  caret. The other one is selection length.

## Risks / invariants to watch

- **Don't disturb the engine's own input path.** Especially in M5, we
  *observe*, we don't replace. The arena project relies on Unity's
  built-in InputField; KotOR's editbox is engine-driven and we must not
  swallow the keystrokes the engine needs.
- **Don't silently drop announcements.** If our spend-control dispatch
  fails (e.g. value doesn't change), surface a "blockiert" cue rather
  than no announcement (per memory:
  `feedback_never_silence_fallback_announcement`).
- **Localise via strings.h, not inline literals** (per memory:
  `feedback_centralise_user_strings`). Every new string we introduce
  ("vorheriges Portr&auml;t", "verbleibende Punkte", "blockiert",
  "Name &uuml;bernommen") goes through `Get(Id)`.
- **Diff-based announcement, not polling-spam.** The edit-field poll
  must dedupe stable frames; only announce on actual delta.
