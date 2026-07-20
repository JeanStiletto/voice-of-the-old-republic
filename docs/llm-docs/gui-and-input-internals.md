# GUI & input pipeline internals (RE reference)

CSWGui* struct offsets, text indirection, panel routing, the cursor/hit-test surfaces, listbox model, and the in-DLL input pipeline.

> Migrated from the agent memory store on 2026-06-14 (memory-system cleanup).
> Each section below is one former memory note, preserved verbatim. Verify
> addresses/offsets against current code before relying.

## kotor_gui_struct_offsets
_Concrete byte offsets and vtable indices for CSWGuiControl/Button/Label, used to extract speakable text in hooks_

KOTOR 1's GUI hierarchy (CSWGuiControl base + Button/Label/Navigable subclasses) — concrete offsets verified live by reading `text_params.text` CExoString from focused controls in the German Steam build and getting clean strings ("Neues Spiel", "Optionen", etc.) via the patch's `OnSetActiveControl` hook.

**Why:** every future hook that wants to announce a control needs these. Recomputing them from `docs/llm-docs/re/swkotor.exe.h` each time is error-prone — the header gives struct *fields* but not always cumulative byte offsets, and the substruct sizes have to be back-derived from "next field's marker offset" hints. These were derived once and verified end-to-end.

**How to apply:** anywhere we have a `CSWGuiControl*` and want speakable text, follow the same lookup chain in `Accessibility.cpp::ExtractAnnounceableText`: tooltip on base → vtable downcast to button → vtable downcast to label.

## Base class CSWGuiControl (size 0x5c)
- `+0x00` vtable (`GuiControlMethods*`)
- `+0x28` tooltip_string.c_string (`char*`, ANSI in active codepage)
- `+0x2c` tooltip_string.length (`uint32`)
- `+0x50` id (`int`)

## Cumulative subclass sizes (back-derived from next-field marker offsets in the .h)
- `CSWGuiControl`    = 0x5c
- `CSWGuiNavigable`  = 0x6c   (control + 4 ints up/left/down/right)
- `CSWGuiBorder`     = 0x74
- `CSWGuiText`       = 0x70   (vtable + extent + gui_string + text_params + 1 field)
- `CSWGuiLabel`      = 0x140  (control + border + text)
- `CSWGuiButton`     = 0x1c4  (navigable + border ×2 + text)

## Concrete text-string offsets within subclasses
`text_params.text` is a CExoString (= `{ char* c_string; uint32 length }`):
- `CSWGuiButton` text CExoString at `+0x16c` (c_string), `+0x170` (length)
- `CSWGuiLabel`  text CExoString at `+0x0e8` (c_string), `+0x0ec` (length)

## GuiControlMethods vtable indices (downcasts)
All are `__thiscall`, return `this` cast or NULL. Trivial implementations — safe to call from hooks. Index = element number, byte offset = index * 4.
- `[19]` AsNavigable
- `[20]` AsLabel           — checked, works
- `[21]` AsLabelHighlight  — wired up; same `+0xe8` text offset (LabelHilight embeds CSWGuiLabel at offset 0)
- `[22]` AsButton          — checked, works (covers CSWGuiCharButton, CSWGuiActivatedButton, and reportedly CSWGuiButtonToggle via inheritance)
- `[23]` AsButtonToggle    — wired up as defensive fallback; same `+0x16c` text offset (ButtonToggle embeds CSWGuiButton at offset 0)

## Known vtable addresses (from Lane's Ghidra DB at `docs/llm-docs/re/k1_win_gog_swkotor.exe.xml`)
- `0x0073E658` `CSWGuiButton_vtable`         — *standard* CSWGuiButton, shared by main-menu buttons, in-game-menu icons, chargen class icons, and "+/-" picker arrows. Image-only sites are just instances with empty text fields, not a different class. Don't treat the vtable address as a "subclass" or "override" indicator.
- `0x0073E8E8` `CSWGuiLabelHilight_vtable`
- `0x0073E840` `CSWGuiListBox_vtable`
- `0x0073E9D0` `CSWGuiSlider_vtable`
- `0x0073E5B8` `CSWGuiLabel_vtable`
- `0x00741878` `CAurGUIStringInternal_vtable` — used by `ReadGuiString`'s vtable check before deref'ing `gui_string` (chargen Class crash fix, 2026-05-03).

## Subclasses with NO `AsX` accessor in GuiControlMethods
These fall through every downcast and need other discriminators (vtable-pointer comparison or a class-specific hook):
- `CSWGuiSlider`   — `max_value` at `+0x70` (i32), `cur_value` at `+0x74` (i32), inside the navigable region
- `CSWGuiEditbox`  — `edit_text` (CSWGuiEditText) embedded at `+0xe0`; the displayed CExoString is at `+0x158` (= 0xe0 + 0x78)
- `CSWGuiListBox`  — `selection_index` at `+0x2c6` (i16), embedded sub-object at `+0x29c`. Its **own** `SetActiveControl(CSWGuiControl*, int)` at `0x0041c160` is what dispatches row focus — must be hooked separately if you want row-level announcements (panel-level SetActiveControl never fires for in-listbox navigation).

## Calling __thiscall from __cdecl handler (MSVC)
```cpp
typedef void* (__thiscall* PFN_Downcast)(void* this_);
void** vtable = *reinterpret_cast<void***>(control);
auto fn = reinterpret_cast<PFN_Downcast>(vtable[index]);
void* concrete = fn(control);  // null if downcast fails
```

---

## kotor_text_indirection
_KOTOR's actually-rendered text lives on CAurGUIStringInternal at +0x14 of CSWGuiText.gui_string — the inline CExoString and strref can be empty, gui_string is always populated_

CSWGuiText::Draw at 0x00416240 reads visible text exclusively through `this->gui_string` (a `CAurGUIStringInternal*` at +0x14 of CSWGuiText). text_params is unused at draw time. CSWGuiText::Initialize at 0x00417310 calls `NewCAurGUIString(text_params.text.c_string, ...)` which allocates the heap buffer; CAurGUIStringInternal's constructor stores the null-terminated c_string copy at offset +0x14 within itself (Ghidra-named field5_0x14).

For overridden subclasses where `text_params.text` (CExoString) and `text_params.str_ref` are both empty (CSWGuiInGameMenu's 8 icon labels at vtable=0x0073E8E8 are the canonical case — verified via 584 zero-text speculative-read events in patch-20260502-190936.log), gui_string still holds the visible string because it's how the engine renders.

**Layout offsets:**
- `gui_string` at +0x14 within CSWGuiText
- For CSWGuiLabel: CSWGuiText starts at +0xD0, so gui_string ptr at **+0xE4**
- For CSWGuiButton: CSWGuiText starts at +0x154, so gui_string ptr at **+0x168**
- Within CAurGUIStringInternal: c_string at **+0x14** (null-terminated)

**How to apply:** Extraction path order is `gui_string -> inline CExoString -> strref -> text_object`. gui_string is the most reliable; the others remain as fallbacks for transient init states. Implemented as `ReadGuiString` + first step in `ExtractTextOrStrRefIndirect` in Accessibility.cpp.

**Identification helpers:**
- vtable 0x0073E8E8 = CSWGuiLabelHilight subclass (vtable[20]=0x00641DB0=identity downcast)
- vtable 0x0073E658 = CSWGuiButton image-only icon subclass (vtable[22]=0x00641DB0=identity)
- function 0x00641DB0 = `mov eax, ecx; ret` -- identity downcast shared by overriding subclasses

**Decompiled function addresses (Lane's gzf, Steam 1.0.3 = GoG bytes):**
- CSWGuiText::Draw @ 0x00416240
- CSWGuiText::Initialize @ 0x00417310
- CAurGUIStringInternal::CAurGUIStringInternal @ 0x0045B990
- CAurGUIStringInternal::SetString @ 0x00458F70
- NewCAurGUIString @ 0x0045BDF0
- CSWGuiLabel::Draw @ 0x00417750
- CSWGuiLabelHilight::Draw @ 0x00417880
- CSWGuiButton::Draw @ 0x00417AB0

---

## foreground_panel_routing
_Routing must use modal_stack[top]/panels[top], not last-walked g_currentPanel; flows that pre-instantiate multiple panels in one frame break per-walk tracking_

`CSWGuiManager` (singleton at `*0x7A39F4`) holds two `CExoArrayList<CSWGuiPanel*>`:
- `panels`       at +0x88 (data ptr +0x88, size +0x8c)
- `modal_stack`  at +0x94 (data ptr +0x94, size +0x98). Layout same as panels — the leading 4 bytes Ghidra leaves unnamed are the data pointer.

Foreground panel = `modal_stack[size-1]` if `modal_stack.size > 0`, else `panels[size-1]`.

Engine functions: `CSWGuiManager::PushModalPanel @ 0x40bd90`, `PopModalPanel @ 0x40be00`, `GetPosInModalStack @ 0x40ab70`. Implemented as `GetForegroundPanel(mgr)` in Accessibility.cpp.

**Why:** Flows like character creation pre-instantiate multiple panels in one frame (Standard/Custom modal + Default wizard + Custom wizard). Each one fires `SetActiveControl` during construction, leaving `g_currentPanel` (last-walked) pointing at a backgrounded wizard. Routing chain rebind off `g_currentPanel` lands on the wrong panel; Enter on wizard buttons no-ops because the modal above is the actual focus owner. Verified `patch-20260502-164320.log` lines 309-318 (modal_stack snapshot) and `patch-20260502-165125.log` line 168 (`Routing: fg=0FBA00E8 current=0FD5EB78 (using fg)` — first chargen modal correctly resolved).

**How to apply:** For chain-routing decisions (rebind, Enter gate, Left/Right gate) use `GetForegroundPanel(mgr)` resolved at key-event time. Keep `g_currentPanel` for per-instance state where last-focused IS what's needed: sibling-label lookups, cycle-category capture, panel-title announcement. Sample at use-time, not at panel-walk-time — during construction the new panel isn't yet on the manager's arrays.

---

## unified_cursor_engine_surfaces
_Durable engine addresses, struct offsets, and call signatures used by the menu-nav unified-cursor design — surfaced by SARIF + DumpBytes investigation in session 5_

Engine-side surfaces our keyboard-as-cursor menu nav builds on. All addresses
verified against Lane's GoG-derived gzf; bytes match Steam (per
`project_ghidra_gog_steam_bytes_match.md`).

**Why:** the whole unified-cursor design (`docs/menu-nav-design.md`) collapses
into "compute target coords, call `MoveMouseToPosition`" because the engine
already exposes a primitive that does cursor-update + hit-test +
active-control update in one shot. Knowing these surfaces existed reshaped
the implementation plan from "synthesize cursor with Win32 + manual focus
management" to "drive engine's own pipeline." Future sessions need this map
to avoid redesigning around weaker primitives.

**How to apply:** anywhere we want to move the synthesized cursor, hit-test a
point, or read a control's screen rectangle, use these directly. Don't reach
for Win32 `SetCursorPos` / hand-rolled hit-tests when the engine has it.

## Globals (singleton pointers, all in the same block)

- `0x7A39E4` → `CExoInput*`
- `0x7A39E8` → `CExoResMan*`
- `0x7A39F4` → `CSWGuiManager*` ← the GuiManager singleton
- `0x7A39FC` → `CAppManager*`

Read pattern: `auto* gm = *reinterpret_cast<CSWGuiManager**>(0x7A39F4);`

## CSWGuiManager methods

All `__thiscall`. Pass GuiManager as first arg via the C++ pointer.

- `MoveMouseToPosition(int x, int y)` `@ 0x40c790` — cursor + hit-test + hover
  + active-control update in one call. Internally calls `SetMousePos` then
  `HandleMouseMove`. **Use this for cursor-sync; don't roll your own.**
- `HitCheckMouse(int x, int y, CSWGuiPanel** outPanel, CSWGuiControl** outCtrl, int)` `@ 0x40abe0`
  — direct hit-test if you only need "what's under (x,y)" without moving the cursor.
- `HandleMouseMove(int x, int y)` `@ 0x40c1e0` — what `MoveMouseToPosition` calls.
- `HandleInputEvent(int code, int state)` `@ 0x40c8e0` — central GUI input
  dispatcher. Epilogue at `0x40cbcb` (use as `consumed_exit_address` for
  arrow-key consumption — see `docs/upstream-prs.md` PR-1).
- `Update(float dt)` `@ 0x40ce70` — per-frame tick; single caller is
  `CClientExoAppInternal::MainLoop @ 0x602eb0`. Right place for
  deferred-from-input work (avoids reentrancy when calling
  `MoveMouseToPosition` from within `HandleInputEvent`). Verified hookable
  mid-function at `0x40ce76` with cut `[0x8b, 0x85, 0x8c, 0x00, 0x00, 0x00]`,
  source `ebp` = `this`.

## CExoInput

- `SetMousePos(int x, int y)` `@ 0x5df5d0` — `__thiscall` on `*(CExoInput**)0x7A39E4`.
  Engine-internal; usually you call `MoveMouseToPosition` instead.

## Struct offsets

`CSWGuiControl` (extends what `project_kotor_gui_struct_offsets.md` documents
for text — this entry adds the extent slice):

- `+0x00` vtable
- `+0x04` extent (inline `CSWGuiExtent`, 16 bytes)
- `+0x14` parent_control
- `+0x18` child_controls (CExoArrayList)

`CSWGuiExtent` (at `ctrl + 0x04`, all `int`):

- `+0x00` left
- `+0x04` top
- `+0x08` width
- `+0x0C` height

Center of any control:

```cpp
int cx = ctrl->extent.left + ctrl->extent.width  / 2;
int cy = ctrl->extent.top  + ctrl->extent.height / 2;
```

## Selectability filter (available, not yet used)

- `CSWGuiControl::GetIsSelectable() -> bool` `@ 0x4189d0` (vtable slot
  unverified). Overrides on `CSWGuiEditbox @ 0x418670`, `CSWGuiListBox @
  0x41a6c0`, `CSWGuiPazaakCard @ 0x67de60`. Available if/when we want to
  filter non-navigable controls from the chain. Per the design doc we don't
  filter yet — keep all controls, learn from real evidence first.

## Numpad scancodes (default `[Keymapping]`)

DIK_NUMPAD0–9 + DIK_DECIMAL (scancodes 71–83) all appear bound to existing
actions in vanilla `swkotor.ini`. Numpad is **not** free as a side channel;
arrow-keys-overwrite was the right design pick for that reason among others.

---

## movemouse_hittest_shift
_Engine quirk: MoveMouseToPosition's internal hit-test resolves to the button one row above the cursor on tabbed panels. Compensate by adding tab spacing to y._

`MoveMouseToPosition(x, y)` @ `0x40c790` runs an internal hit-test as part of the move and writes the result to `mouseOverControl` (`GuiManager+0x8`). On the Options panel and its children, this hit-test consistently resolves to the button whose center is at `y - tabSpacing` — never the button at the cursor's actual y. tabSpacing matches the chain's tab-pitch (45 px on Steam 1.0.3 Options panel: tabs at y=149, 194, 239, 284, 329).

**Why:** Confirmed via `mouseOver before=NULL after=<button-above>` logging — the shift is in `MoveMouseToPosition`'s hit-test path itself, not stale state. Best guess: cursor hotspot coord-system mismatch (programmatic moves don't get the same OS-cursor-hotspot translation as real WM_MOUSEMOVE events). Real mouse usage works because the OS supplies pre-corrected coords. Main menu and other non-tabbed panels do not exhibit the shift.

**How to apply:** When warping the cursor to a tab-cluster button — both for arrow navigation AND for Enter click-sim — add the tab spacing to y. Compute the spacing in `RebindChain` from adjacent tab entries' cy difference (`g_tabClickOffsetY`). Apply only when `IsTabButton(target)` is true; non-tab buttons (close, OK/Cancel, listbox settings) don't need the offset and `g_tabClickOffsetY` stays 0 for non-tabbed panels. With the offset: `MoveMouseToPosition(x, y+tabSpacing)` makes the engine's hit-test resolve to the intended target. Verified end-to-end in `patch-20260502-123837.log` — every Enter on a tab opens the correct sub-panel.

---

## activation_via_click_sim
_Programmatic UI activation must go through HandleLMouseDown/Up at coordinate; SetActiveControl is structurally insufficient and crashes. See docs/tab-crash-investigation.md and docs/menu-nav-design.md Phase 3._

For any programmatic UI activation in the Accessibility patch — Tab cycling between tabs, per-setting interaction inside the Options listbox, future toggle/slider manipulation — the primitive is **synthesized mouse click at coordinate** (`CSWGuiButton::HandleLMouseDown` + `HandleLMouseUp` for engine-known controls; coordinate-based click into a listbox for internal lines). Not `CSWGuiPanel::SetActiveControl`.

**Why:**

1. *Structural insufficiency.* The Options listbox has `controls.size == 1` (one multi-line label blob). Individual settings are not `CSWGuiControl*`s, so `SetActiveControl(CSWGuiControl*)` cannot target them. Per-setting activation is impossible via that primitive regardless of whether crashes are fixed.

2. *Crashes via skipped invariants.* Seven attempts (cursor-warp, sync setActive, deferred setActive, manager guard, deferred + guard + warp, PlayGuiSound diagnostic) all crashed at `mgr+5` — the engine doing an indirect call with the manager pointer in the call target. Root cause: `SetActiveControl` skips pre/post-click invariants the engine maintains via `HandleLMouseDown`/`HandleLMouseUp`. Some pointer the click flow normally writes ends up holding the manager value; next frame the engine `CALL`s it.

3. *Mouse navigation works.* The user reports mouse activation of tabs is fine. Replicating the engine's expected path is more reliable than inventing a shortcut.

**How to apply:**

- Treat `SetActiveControl` calls in our DLL as observation hooks only (the existing `OnSetActiveControl` mid-function hook for announcement) — never as a programmatic write.
- The click-sim primitive is **live** and is the activation path for both Tab/Enter on tabs and Enter on buttons inside sub-dialogs. Logged as `Update: FireActivate target=…` and `Update: click-sim Down=1 Up=1 at (x,y)`. Migration is complete; no programmatic `SetActiveControl` calls remain in the patch (verified 2026-05-02 — only `OnSetActiveControl` / `OnListBoxSetActiveControl` observation hooks reference the engine function).
- Esc-to-close in sub-dialogs also routes through click-sim via `FireActivate(Schliess)`, gated on `g_currentPanel != g_tabbedPanel` so the parent Options panel's own Esc passes through unchanged.

---

## listbox_keyboard_model
_How arrow keys + Enter behave on a focused listbox. Engine-side facts; the path-C "synthetic cursor over the blob" plan that motivated this RE has been dropped — settings listboxes are decorative._

KOTOR's `CSWGuiListBox` has a built-in keyboard model — when the listbox is the focused control, arrow keys move `selection_index` and Enter activates the current row. Decompile evidence at `0x0041ce20` (`HandleInputEvent`):

- `param_1 = 0x3d` (up) and `0x3e` (down) — calls `SetSelectedControl(this, selection_index ± 1, 1)`. Wraps if `bit_flags & 0x40` is set; otherwise clamps.
- `param_1 = 0x31` / `0x32` — page-up/down equivalents.
- `param_1 = 0x1f5`, `0x1fc`, `0x1fb`, `0x1fd`, `0x1fe` — scrollbar-driven page/scroll codes.

**Two modes determine behavior when `selection_index == -1`:**
- `bit_flags & 0x200` set → "scrolling-only" mode. Arrow keys move `top_visible_index` (viewport scroll), do NOT establish a selection. `selection_index` stays `-1` forever.
- Bit clear → arrow keys initialize selection to row 0 by calling `SetSelectedControl(this, 0, 0/1)`.

**Why the Options-Gameplay settings listbox reads as `selection_index=-1, controls.size=1`:**
- `controls.size == 1` is the truth — the listbox holds a single multi-line `CSWGuiLabel` whose CExoString has all 8 settings joined by `\n`. There is no native per-line cursor.
- The listbox is **decorative** — sighted players never click into it; pressing Enter on the tab opens a separate sub-dialog (`Spieleinstellungen` panel) that contains the real interactive controls. Confirmed 2026-05-02.

**Why:** This used to scaffold a "synthesize per-line keyboard navigation over the blob + click at line Y" plan. That plan has been dropped — the same content (Difficulty, Autom. Levelaufst., …) is exposed via the sub-dialog as 12 individual buttons + toggles + cycle controls, which the chain primitive (`Chain rebind` / `MoveMouseToPosition` / `FireActivate`) already navigates without any per-screen code.

**Click path (absorbs the former `project_listbox_click_flow` note):** a mouse
click resolves via `HitCheckMouseLocal` per child → `HandleLMouseDown` (event
0x1f8) → `HandleLMouseUp` (0x27 = activate) → `SetSelectedControl`. This does NOT
help the decorative settings listbox either (controls.size==1, so a click can't
disambiguate a row); the sub-dialog is still the real interaction surface.

**How to apply:** Don't write listbox-internal navigation. If a listbox sits inside a tabbed parent and `controls.size == 1` with a multi-line label child, silence it (we do this already via `g_tabbedPanel` detection + `ListBox blob silenced (tabbed mode)`). Real per-setting interaction is in the sub-dialog opened by Enter on the tab. The engine-level RE above remains useful only for non-decorative listboxes (saved-game list, future inventory list, etc.) where `controls.size > 1` and rows are real `CSWGuiControl*`s.

---

## dialog_reply_hover_select
_Why the mod parks the cursor during dialogue. Decompiled 2026-07-20 (droid repair-menu field report)._

The `CSWGuiDialog` reply listbox (`+0x19c4`) has **no native keyboard reply navigation** — vanilla only ever selects a reply by mouse. Its selection is slaved to the cursor:

- `CSWGuiListBox::HandleMouseMove @0x0041c400` — `HitCheckMouseLocal` finds the row under the cursor and, if it differs from `selection_index` (and `bit_flags & 1` is clear), calls `SetSelectedControl(this, hitRow, 1)`. So every mouse move the manager processes forces `selection_index` onto the hovered row.
- `CSWGuiManager::HandleMouseMove @0x0040c1e0` tail-calls the listbox's `HandleMouseMove` for the listbox under the cursor.
- `CSWGuiDialog::SetReplies @0x006a86a0` selects row 0, then **explicitly calls `CSWGuiManager::HandleMouseMove(mouse_x, mouse_y)`** (line ~210) when a node opens — so the initial selection immediately snaps to whatever row the resting cursor sits on.
- `CSWGuiDialogComputer::SetReplies @0x006a94e0` → `UpdateSkills @0x006a8240` paints Repair/Computer-Use ranks + spike/part counts into labels near the replies; the computer dialog is label-rich.

**Consequence for the mod:** dialogue reply nav is faked by writing `selection_index` directly (`DriveListBoxSelection`), but the engine's hover-select overwrites that write whenever the OS cursor overlaps a reply row. On 4:3 the resting cursor sits in empty space so it never bites; a widescreen/HD layout can leave it hovering a reply, and the engine then re-snaps selection to that row on every `HandleMouseMove`, making the other replies unreachable (field log: Tatooine Dune Sea droid `tat18_10droid_03`, repair submenu — manual-repair + Back rows unreachable/unspoken).

**Mitigation (shipped v0.6.1):** `MonitorDialogReplies` parks the OS cursor to the top-left letterbox corner `(2,2)` once a reply panel arms (one-shot latch), so hover-select finds no row and the keyboard writes stick. Corner, not a computed off-list point, because `MoveMouseToPosition`'s hover→active promotion **crashes on a label** and the corner is empty in every dialog variant. Same engine behaviour and the same fix as the workbench/equip pickers (`ParkPickerCursorOffList`). Alternative not taken: setting the listbox `bit_flags | 1` disables hover-select at the source (see the `& 1` gate above) — cheaper but bit 0's other semantics are unverified.

---

## listbox_spec_extensions
_Two optional callbacks added to the listbox-driven panel dispatcher; lifts panel-specific title speech and per-row enrichment out of inline special cases_

`menus_listbox.cpp`'s `ListBoxPanelSpec` carries five panel-specific
callbacks now (was three): the original `announce`, plus two added
2026-05-09:

  * **titleOverride(panel) → const char*** — replacement title speech
    for panels whose .gui-baked title is broken-by-default. Looked up
    via the public `acc::menus::listbox::GetTitleOverride(panel)`,
    called from `menus.cpp::AnnouncePanelTitle` before the generic
    label-walk. Returns nullptr to fall through to the generic walk
    (the common case — most spec entries leave it null). First user:
    SkillInfoBox spec, gated on `FindFeatsCharGenPanel()` so future
    Force-Powers / Skills reuse of the same engine slot can layer on
    different titles.

  * **enrichRow(panel, r)** — per-row side-channel speech, fired AFTER
    `announce`. Used when the spec needs to fetch supplementary speech
    text from an auxiliary engine source (e.g. SkillInfoBox calls
    `CSWGuiFeatsCharGen::OnEnterFeat(featId)` on the underlying main
    panel to refresh `description_listbox`, then reads + speaks it).
    Keeps `announce` as the simple "row-name + N of M" speech every
    spec already has — enrichment is opt-in.

**Why:** Adding a 4th spec entry (SkillInfoBox) made it visible that
some specs have "speak the row" + "speak supplementary aux state"
shape, while others are pure "speak the row". Splitting them keeps
each callback's job small. Same reasoning makes title overrides
naturally a per-spec concern rather than scattered `if (kind == X)`
checks in menus.cpp.

**How to apply:** When adding a new listbox-shaped panel,
  * if its .gui title is wrong, set `titleOverride`;
  * if per-row navigation needs to fetch state from elsewhere
    (description from a different panel, computed cost/availability,
    etc.), put it in `enrichRow` rather than stuffing it into
    `announce`.

---

## select_then_confirm_listbox_helpers
_DriveListBoxSelection + QueueButtonByIdActivate dedup the Container/Equip-picker/SaveLoad pattern; reuse for any new panel of the same shape_

`patches/Accessibility/menus.cpp` has two file-static helpers that cover the
"arrow-keys mutate listbox.selection_index; Enter/Esc → confirm/cancel
button by .gui-time id" pattern:

- `DriveListBoxSelection(lb, navDown, minSel, &out)` — pure cursor mutation.
  Writes selection_index, scrolls top_visible_index, fills `ListBoxNavResult`
  with old/new indices, rowCount, and the row pointer at newSel. Pass
  `minSel=1` for listboxes whose row 0 is a .gui-time PROTOITEM template
  (equip-picker LB_ITEMS); 0 elsewhere.
- `QueueButtonByIdActivate(panel, buttonId, logPrefix)` — Enter/Esc handler:
  debounces against `g_pending*`, sets `g_pendingActivate +
  g_pendingActivateTarget + g_navSpeechSuppressBudget`, logs, returns bool.

**Why:** before extraction, the same ~30-line block (read selPtr/topPtr/ippPtr,
clamp, scroll-window) appeared three times (Container, Equip-picker,
SaveLoad). The SaveLoad fix would have been a fourth copy. Helpers cut
duplication by ~150 lines and made the SaveLoad / Container / Equip-picker
handlers read at the same level of abstraction.

**How to apply:** any new panel where the user picks a listbox row then
confirms via a separate button (not the row itself) — module picker,
journal quest list, galaxy-map planet picker, future PartySelection
flows — should reuse these helpers rather than open-coding the
cursor + button-queue logic.

The select-AND-act-on-row pattern (Options sub-dialog setting buttons,
dialog reply listbox with engine-bound arrow keys, read-only description
listboxes) deliberately does NOT go through these helpers — different
semantics. See the `IsSaveLoadPanel` block comment in menus.cpp for the
full risk catalogue.

Equip-picker Enter intentionally bypasses `QueueButtonByIdActivate` — it
uses `g_pendingEquipCommit` to direct-call `OnItemSelected` because the
click pipeline can't reach the row buttons through the listbox extent
(see docs/equip-flow-investigation.md).

---

## oncontrolentered_is_active_gate
_Inventory/Store/likely Abilities OnControlEntered no-ops if focused entry's is_active flag is 0 — keyboard nav must force it before calling_

CSWGuiInGameInventory::OnControlEntered @ 0x006b3d10 wraps its entire
body in `if ((param_1->button).navigable.control.is_active != 0)`.
When that flag is 0, the function returns without touching the
description listbox or item-stats labels.

**Why:** mouse-driven play sets `is_active=1` via
`HandleLMouseDown`'s CaptureMouse path on every click. Hover paths
and keyboard nav don't. So OnControlEntered effectively requires "the
user has clicked this item at least once this session" — fine for
the engine's intended UX (description shows on click + hover),
breaks our peek which calls OnControlEntered cold from chain nav.

**The equipped row is the canonical victim:** it never gets clicked
in normal play (no reason to), so `is_active` stays 0 forever.
Keyboard peek on the equipped Kleidung was a perpetual no-op until
we added the workaround. Other rows happen to get clicked across a
session and pass the gate.

**How to apply:** any peek/refresh path that calls a
`CSWGui*::OnControlEntered` for description-stage purposes must
force `entry.is_active != 0` before the call. The minimal fix in
`peek_description.cpp::RefreshInventory` is save → set 1 → call →
restore (is_active is read by other engine paths — border
rendering, focus chain, click activation gates — so don't leave it
mutated). Likely the same pattern applies to
CSWGuiStore::OnControlEntered and CSWGuiInGameJournal::OnControlEntered;
verify by decompiling each before adding their refresh adapters to
peek's panel registry.

---

## main_menu_input_path
_SetActiveControl is the canonical panel-level focus-change entry point that fires for arrow-key nav + mouse + main menu. HandleFocusChange is per-control noise; the older "use HandleInputEvent" theory was wrong_

**The right hook for "control just got focus" announcements is `CSWGuiPanel::SetActiveControl` at `0x0040a630`** (`void __thiscall(CSWGuiControl* param_1)`). It fires once per actual focus change, covers every panel including the main menu (and every screen the menu navigates to), and gives the new control as `param_1` (or NULL on deactivation).

**Why this displaced the earlier theory:** A previous session believed the main menu bypassed the focus path entirely and that `CSWGuiMainMenu::HandleInputEvent` (0x0067b380) was the only entry point. That was wrong. `CSWGuiMainMenu::HandleInputEvent` only handles letter/F-key shortcuts — arrow-key navigation does NOT reach it. `CSWGuiControl::HandleFocusChange` (0x0041896b) was also misleading: it fires twice per nav (old loses + new gains), is bypassed by some screens, and reports the affected control rather than "the new active one." `SetActiveControl` is the panel-level abstraction that all navigation funnels through, so it's the correct single signal.

**Verified 2026-04-30** in the German Steam build: every arrow-key press on the main menu fires SetActiveControl exactly once with the new button as param_1; navigating into Options switches the panel pointer transparently and the same hook keeps firing for sub-screens. Tooltip extraction → vtable downcast (AsButton/AsLabel) yields the actual label ("Neues Spiel", "Optionen", "Gameplay" …).

**Mid-function hook point:** `0x0040a638`, after the prologue copies args into registers — EDI = panel (this), ESI = newControl (param_1). Original 5 bytes for relocation: `8b 4f 1c 3b ce` (MOV ECX, [EDI+0x1c]; CMP ECX, ESI). Both register/memory-relative. Captures both args without touching the framework's broken stack-param wrapper.

**How to apply:** any new "what just got focus" feature should hook here, not at HandleFocusChange. HandleFocusChange remains useful for diagnostics (per-control before/after) but should not drive announcements.

**Related per-button handlers** (still useful when the *commit* matters more than the focus): `CSWGuiMainMenu::OnNewGamePicked` at `0x67afb0`, `OnQuitButtonPressed` at `0x67b4a0`, etc.

---

## engine_event_value_layers
_A param value's meaning at the WndProc layer doesn't carry into the GUI dispatch layer; numerical collisions with WinMessages mid-engine are misleading_

The `param_2` int passed to `CSWGuiManager::HandleInputEvent` at 0x40c8e0 is NOT a Win32 WinMessage. The function's body (decompiled) only checks param_2 for `== 0` (release), `== 1` / `== -1` (analog axis), and treats everything else as "press". The dispatch switch at 0x40c91b is on event-class codes 0x1F..0x26, which are NOT WinMessages.

So a value like `param_2 = 514` at this layer does NOT mean WM_LBUTTONUP — it's an internal "press-state / event-class" code that happens to numerically collide. Verified by hooking `CExoInputInternal::BufferEvent` (0x5e12a0) and observing that no WM_LBUTTONUP event was ever queued from the WndProc during a scenario where `param_2=514` reached the manager.

**Why:** Lost half a debugging session assuming `514 == WM_LBUTTONUP` and chasing a "synthetic mouse click after setActive" that didn't exist. The real source was an engine-internal cascade event on a different manager pointer.

**How to apply:** When a value at a hook site numerically matches a well-known constant (WinMessage, virtual-key code, etc.), don't assume the meaning carries unless the function body confirms it. Read the actual disassembly/decompilation. The engine has multiple dispatch layers (WndProc → CExoInput queue → CClientExoAppInternal::ProcessInput → CSWGuiManager → panel vtable) and each translates values into its own internal codes.

Also: `CExoInputInternal::BufferEvent` is mouse-only despite the generic name — its switch only covers WinMessages 0x200..0x20A. Keyboard events take a different ingest path (likely DirectInput polling), so this hook is useless for keyboard-side diagnostics.

---

## inworld_input_pipeline
_Engine keymap drops scancodes not in kotor.ini before CSWGuiManager::HandleInputEvent fires; use GetAsyncKeyState polling for unbound keys_

`CSWGuiManager::HandleInputEvent @0x40c8e0` is the right hook for **menu-side** input and for keys that ARE bound in `kotor.ini` `[Keymapping]`, but it does NOT see in-world raw scancodes for unbound keys. The engine's keymap layer drops anything not bound to a logical action before reaching the manager.

**How to apply:** For new in-world keybinds (Pillar 4 cycle, Pillar 2 view-mode toggle, Pillar 3 beacon-cancel, etc.) where the keys aren't in stock kotor.ini, poll the OS keyboard state directly from `OnUpdate` via `GetAsyncKeyState(VK_*)`. Edge-detect with per-key static `prev` flags. Self-gate on:

- `GetForegroundWindow()` matching our PID — otherwise we'd fire when the user types `,/./-` in another app
- `GetPlayerPosition()` succeeding — keeps cycle keys silent in menus / chargen / mid-load

Reference impl: `acc::cycle_input::PollWin32` in `patches/Accessibility/cycle_input.cpp`. Add `#pragma comment(lib, "user32.lib")` for the Win32 imports.

The OnHandleInputEvent path can stay in place as a backup for users who do bind the keys via kotor.ini — share per-action handlers between paths so behaviour is identical regardless of ingestion source.

**Applies to in-world Enter + arrows when our radial is armed too** (verified 2026-05-05 `patch-20260505-101621.log`): Enter and Up/Down/Left/Right are unbound in-world, so when the radial is mounted via `CSWGuiMainInterface::PopulateMenus` they never reach the manager hook in `menus.cpp`. `interact_hotkey::PollHotkey` Win32-polls all five with rising-edge detection and routes them straight into `radial_menu::HandleInputEvent(kInputEnter1 / kInputNav*)`. Esc IS bound (pause/options) and reaches the manager normally — keep its existing route to avoid double-fire.

**OEM VK codes are layout-dependent.** The physical key right of `.` is `VK_OEM_2` on US QWERTY (`/`) but `VK_OEM_MINUS` on German QWERTZ (`-`). Listen for both if you want a layout-portable "row of cycle keys" (`,` `.` `-`/`/`).

**Why:** Found during Pillar 4 lay-off 4 gate test 2026-05-04 (`patch-20260503-215023.log`: 86 events captured at the manager hook, zero raw scancodes for `,/./-`). Cost half a session before we proved the manager wasn't seeing them. Memorialised so future "key not firing in-world" bugs go straight to the polling path instead of re-investigating the manager hook.

---

## hotkeys_claim_vs_consume
_Suppressing a hotkey from sites that run before BeginTick (e.g. manager input hook) needs ClaimRisingEdge, not Consume — Consume's last=now is a no-op when both reflect the prior tick_

`acc::hotkeys::Consume(Action)` works only AFTER `BeginTick` has sampled
the current tick's `now`. It sets `last = now`, which collapses the
rising-edge math for the rest of the tick.

`OnHandleInputEvent` (CSWGuiManager input hook) fires BETWEEN
`EndTick` of one OnUpdate and `BeginTick` of the next, in the engine's
input-dispatch window. At that moment both `now` and `last` still hold
the previous tick's values. `Consume(last=now)` does nothing because
the next `BeginTick` will overwrite `now` to the new (true) state while
`last` stays at the old (false) state — Pressed() returns true on the
rising edge anyway.

**Fix shape:** add a one-shot guard bit on `EdgeState` (`claimed`).
- `ClaimRisingEdge(Action)` sets the bit.
- `Pressed(Action)` returns false if the bit is set.
- `EndTick()` clears the bit so subsequent presses fire normally.
- `Set(Action, Binding)` also clears it (rebind shouldn't leak state).

**How to apply:** if you want to suppress a hotkey rising edge from
any code path that runs in the engine's input-dispatch window (manager
hook, client input hook, …), use `ClaimRisingEdge`. Use `Consume` only
from sites that run inside `Dispatch()` between `BeginTick` and
`EndTick` (most of our per-tick tickers).

Implementation: hotkeys.cpp `EdgeState::claimed` + `ClaimRisingEdge`,
declared in hotkeys.h, called from menus.cpp::OnHandleInputEvent for
the InteractTarget/InteractForceRadial pair on Enter press. Landed
in commit 8da823e.

---

## inner_populate_menus_crashes
_Direct 0x00689410 calls crashed because we passed CGameObject* where CSWCObject* expected — fix is to use wrapper @0x00689d80 only_

Calling `CSWGuiTargetActionMenu::PopulateMenus(creature, mode, target, &result) @ 0x00689410` directly with `target = CClientExoApp::GetGameObject(handle)` was unsafe because **inner expects `CSWCObject*`, not `CGameObject*`**. The wrapper at 0x00689d80 does the missing downcast `target = game_object->vtable->AsSWCObject(game_object)` before calling inner. Without it: `target_type = GetTargetInterfaceTargetType(player, garbage_swc)` returns junk, then `field1_0x24[(char)target_type * 3 + row]` indexes past the 12-int field1 array → reads stack-adjacent memory → /GS canary fires on the next NWScript tick. Crash signature `STATUS_STACK_BUFFER_OVERRUN (0xC0000409)` with `ExecuteCommandDestroyObject` on the stack is consistent with delayed canary trip post-corruption.

**Why:** Object-handle resolution in KOTOR returns CGameObject (the polymorphic base). Engine consumers that need CSWCObject (the client-side concrete type) all run a vtable downcast first — same pattern in HandleMouseClickInWorld dispatch path and DoTargetAction. Skipping that conversion is the bug, not anything specific to the inner populate.

**How to apply:** The fix landed 2026-05-05: `engine_picker.cpp` empty-descriptor branch now calls only `CSWGuiMainInterface::PopulateMenus(main_interface)` (wrapper, no args). The wrapper does the downcast and calls inner correctly. `PopulateFromArgs` / `ResolveClientPlayerCreature` / `ResolveClientGameObject` were removed from `engine_radial`. If a future use case needs to populate against a non-hover target, write `main_interface->field1_0x64` directly and call the wrapper — don't reimplement the AsSWCObject path.

**Verified live 2026-05-05** (`patch-20260505-101621.log`): Shift+Enter on Endar Spire Door B populated `action_lists[1]` cleanly with no crash; subsequent `DoTargetAction` dispatched the Sicherheit action and the door opened. No /GS canary on follow-up NWScript ticks.

**Forensic refs:** patch-20260505-073407.log (multi-mode iteration crash), patch-20260505-080045.log + dump swkotor.exe.55936.dmp (single-call crash on second Enter). Decomp evidence: `docs/radial-menu-investigation.md`.

---

## menu_chain_builds_for_all_panels
_The generic menu chain processes ANY foreground GUI panel (even custom game panels like the Pazaak board); a custom navigator must consume keys at the manager hook or input double-fires_


The generic menu chain (RebindChain + HandleNavStep/HandleEnterActivation/HandleLeftRight in menus_chain.cpp, driven from `OnHandleInputEvent`) builds and navigates a control list for **any** foreground GUI panel — including custom game panels like the Pazaak board (`CSWGuiPazaakGame`), even though it's identified structurally rather than as a `PanelKind`. Confirmed in patch-20260601-082845.log: the board got a 28-entry chain, so pressing Enter fired BOTH our card-play AND the chain activating button "Runde beenden" (the "strange things" the user hit).

**Implication:** a custom navigator for a GUI panel must intercept input at the manager hook (`menus.cpp OnHandleInputEvent`) via a `TryHandleInput(activePanel, param_1, param_2, rv)` that returns consumed (`return trackPress(rv)`) BEFORE the generic chain handlers run — exactly how the listbox/editbox dispatchers and now `acc::pazaak::TryHandleInput` / `acc::menus::pazaakdeck::TryHandleInput` work. Win32-polling alone (GetAsyncKeyState) is NOT enough for keys the engine delivers to GUI panels (arrows, Enter): the engine + chain still act on them. Win32 poll is only safe for scancodes the engine drops before the manager hook (letters, `,`/`.`/`-` — see [[project_inworld_input_pipeline]]).

Related: [[project_hotkeys_claim_vs_consume]], [[feedback_pazaak_arrow_zone_nav]].

---

## ingame_subscreen_drill
_SwitchToSWInGameGui keeps the InGameMenu strip in fg via SendPanelToBack; drill mode retargets chain to sub-screen on Enter_

The icon strip (`CSWGuiInGameMenu`) is the foreground panel for the entire time any in-game sub-screen (Inventory / Map / Journal / …) is open. Discovered via SARIF xref trace from the icon onClick stubs.

**Dispatch chain** (each icon's `OnXxxButtonPressed @0x624cf0..0x624dd0` is a 32-byte stub):

- `CClientExoApp::GetInGameGui @0x5ed690`
- `CGuiInGame::SwitchToSWInGameGui(int GUI_id) @0x62cf10`
  - removes the previously active sub-screen via `RemovePanel`
  - lazy-creates the new one via `UpdateCreatedInGameGUI @0x62b1e0`
  - `CSWGuiManager::AddPanel @0x40bc70`
  - **`CSWGuiManager::SendPanelToBack @0x40bd20`** ← strip stays foreground
  - `CSWGuiInGameMenu::SetActiveControlID @0x624bd0` (highlight matching icon)
  - `CExoString` + `CVirtualMachine::RunScript @0x5d0fc0` (spawns the per-screen tutorial popup)

**Why:** Without `SendPanelToBack`, our chain keeps targeting the strip's 8 icons forever. Verified in `patch-20260502-214100.log` — opening Inventory / Map / Character all produced identical chain dumps targeting the strip.

**How to apply:** Drill model in `Accessibility.cpp` — `g_drilledIntoSubScreen` flag set on Enter activate when chain panel is `InGameMenu`, routes `OnHandleInputEvent` to `FindActiveSubScreenPanel()` (lowest-index InGame{X} panel in `panels[]`) when fg is the strip. Esc clears the flag (does NOT close the sub-screen). Override is gated on fg-is-the-strip so tutorial modals + Options sub-tabs route through fg directly. Full design in `docs/menu-nav-design.md` Phase 3.5.

---

## menu_freeze_investigation
_Intermittent multi-second menu freeze — diagnosis, shipped mitigations, and the tick watchdog that localises recurrences_


Symptom (reported by dev + other users): during menu navigation a ~once-per-session
freeze of a few seconds — sound stops, no input, then resumes. Random moment (main
menu, save slot, options, first message box).

Key diagnostic fact: KOTOR refills its **streaming-audio buffers on the main-loop
tick**, so "sound cuts out" ⇒ the **main thread blocked**. A blocked tick can't log,
so a real freeze appears as a *gap* in the patch log (1s timestamp resolution).

Shipped mitigations (commit 4c31837, 2026-06-01):
- **log.cpp: `OutputDebugStringA` now gated on `IsDebuggerPresent()`** — it was the
  prime suspect. It takes the system-wide DBWinMutex and stalls every call when any
  background process (AV/EDR, DebugView, telemetry) drains the debug buffer; it fired
  on every line on the main tick. File log stays 100% full (testers read the file).
- **engine_player.cpp PartyLeader probe `Write`→`Trace`** — was 58% of a 42k-line
  session (24,395 lines / 1 distinct value). Measured after: 24,395 → 2.
- **`BlockLog` primitive (log.{h,cpp})** — folds identical multi-line snapshots whose
  repeats are interleaved across the block stride (line-level [[project_listbox_keyboard_model]]
  Trace can't). Has a streaming `Key()` channel to fingerprint on semantic content
  (ids+text+vtable) excluding volatile heap pointers — needed because the engine
  recreates the same panel at fresh addresses. Wired into `WalkChildren`
  (Menus.PanelWalk/ListBox/EmptyChain) and `LogManagerStack`.

Tick watchdog (commit 39bfc7b) in `core_tick.cpp`: times each Dispatch phase + the
inter-tick wall gap, logs ONLY past threshold (200ms dispatch / 750ms gap) so normal
frames are silent. Two line shapes:
- `Watchdog: SLOW TICK total=Xms — slowest phase '<name>' Yms` → stall in OUR dispatch.
- `Watchdog: SLOW FRAME gap=Xms` → stall OUTSIDE dispatch (engine / between-frame hook).

IMPORTANT interpretation: **save loads and area loads legitimately block the tick**
(a save load = queued FireActivate → engine load-game handler fn=0041AD40 ≈ 1.6s) and
WILL trip the watchdog. Those are benign. The genuine freeze is a Watchdog line during
**pure menu navigation with no load/transition nearby**. As of 2026-06-01 the real
freeze has not yet recurred post-fix; waiting on more sessions to confirm the
OutputDebugString gate fixed it, or to capture the culprit if not.

Deferred: `Menus.PerKind` block-fold (emitted scattered across extraction functions,
needs a threaded block context). Logging-cost rule: dev ships with FULL logging for
beta testers — no debug verbosity flag; reduce spam by dedup, never by dropping info.

---

