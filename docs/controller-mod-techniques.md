# KOTOR 1 — menu overlay / nav-chain / keyboard-activation techniques

Hand-off note for a developer building a **controller mod** on top of the same
engine surfaces the Voice of the Old Republic accessibility mod uses. You know
the game and the native-modding base, so this skips fundamentals and just maps
our techniques to files + engine entry points. All addresses are for the
Steam/GoG 1.0.3 build (bytes identical between the two).

Everything below lives in `patches/Accessibility/`. It's a single injected DLL
(loaded via a `dinput8.dll` proxy → KotorPatcher). Hooks are declared in
`hooks.toml` and patched in by Lane Dibello's Kotor-Patch-Manager (KPatchCore);
each hook is a detour with a cut-byte block + register-source spec.

---

## 1. The two input pipelines (the single most important thing to get right)

KOTOR has a layered input path:

  WndProc → CExoInput (DirectInput) → CClientExoAppInternal::ProcessInput →
  CSWGuiManager::HandleInputEvent → per-panel vtable dispatch

There are **two** places you can take input, and you need both:

**A. The manager hook — `CSWGuiManager::HandleInputEvent @ 0x40c8e0`.**
This is the central GUI input dispatcher. Hook it and you see every key the
engine considers "bound" plus the four logical nav codes. This is where ALL
menu/overlay navigation should be intercepted. Our hook is `OnHandleInputEvent`
in `menus.cpp`; the wiring/dispatch order is the bottom third of that file.

Key facts:
- Codes arriving here are engine InputIndices, NOT Win32 VKs and NOT
  WinMessages (a value of 514 is *not* WM_LBUTTONUP — different layer, numeric
  collision only). The nav codes are pre-translated logical actions:
  `kInputNavUp/Down/Left/Right = 0xb6..0xb9`, `kInputEnter1/2 = 0xb5/0xbb`,
  `kInputEsc1/2 = 0xb4/0xdf`. Full list in `engine_input.h`.
- `param_2`: 0 = key release, 1/-1 = analog axis, anything else = press. Edge-
  detect on press.
- To **consume** a key (stop the engine acting on it) you return the right rv
  and stop dispatch. See `docs/upstream-prs.md` PR-1 — KPatchManager supports a
  `consumed_exit_address` at the function epilogue `0x40cbcb` to swallow events.
- The engine's keymap layer **drops any scancode not bound in `swkotor.ini`
  `[Keymapping]` before this hook fires.** So unbound keys never arrive here.

**B. Win32 polling — `GetAsyncKeyState` from the per-frame tick.**
For keys the engine drops (anything not in `[Keymapping]`), poll the OS directly
each frame and edge-detect with a static `prev`. Reference impl:
`cycle_input.cpp::PollWin32` and `interact_hotkey.cpp`. Self-gate on
`GetForegroundWindow()` == our PID so you don't fire while the user is in
another window.

For a controller mod this maps cleanly: read the gamepad (XInput) from the same
per-frame tick, edge-detect button presses, and route them into whichever of the
two paths the target surface uses. **In menus/overlays, synthesize into path A
(the manager dispatch), not path B** — see the gotcha in §4.

The per-frame tick we hang everything off is `CSWGuiManager::Update @ 0x40ce70`
(single caller = the client main loop). Hook point `0x40ce76`, `this` in `ebp`.
Our dispatcher is `core_tick.cpp`. Doing deferred work here (rather than inside
the input hook) avoids re-entrancy when you call engine functions that
themselves dispatch.

---

## 2. The navigation chain (the menu-overlay abstraction)

This is the reusable abstraction for "arrow through whatever panel is on screen."
It is **generic** — it works on any foreground GUI panel without per-screen code,
because it reads the panel's real control list.

Files: `menus_chain.{h,cpp}` (the chain), `menus.cpp` (focus tracking + speech).

The model:
- A panel is a `CSWGuiPanel` with a `CExoArrayList<CSWGuiControl*>` of children.
  Each control carries an extent (screen rect) at `+0x04` (left/top/width/
  height, all int) and an id at `+0x50`.
- `RebindChain(panel)` walks the children, recurses one level into sub-dialog
  listboxes, **sorts by `extent.top`** (visual top-to-bottom order), squashes
  the empty-text cycle-arrow flankers around value displays, and anchors the
  cursor on the engine's current active control. Result is a flat
  `ChainEntry[]` (control ptr + center x/y) that arrow keys walk linearly.
- Rebind only on panel-pointer change (cheap), not per frame.
- Navigation handlers, all in `menus_chain.cpp`, called in order from
  `OnHandleInputEvent`:
  - `HandleNavStep` — Up/Down/Home/End: advance the cursor, announce, warp.
  - `HandleEnterActivation` — Enter: classify the focused entry and activate it.
  - `HandleLeftRight` — Left/Right: slider in/decrement, or cycle-arrow flanker.
  - `HandleEsc` — routes Esc to the right close/cancel button.

**Which panel is "foreground"** is not the last panel you saw a focus event for.
Resolve it at event time: `CSWGuiManager` singleton at `*0x7A39F4` holds
`panels[]` (+0x88) and `modal_stack[]` (+0x94). Foreground = top of modal_stack
if non-empty, else top of panels. Chargen pre-instantiates several panels in one
frame, so last-walked tracking lands on the wrong one. See `GetForegroundPanel`.

**Sub-screen "drill"** quirk worth knowing: opening Inventory/Map/Journal keeps
the 8-icon `CSWGuiInGameMenu` strip as the foreground panel (the engine does
`SendPanelToBack` on the new screen). So when drilled in, retarget the chain to
the actual sub-screen panel (`FindActiveSubScreenPanel`, lowest-index InGame{X}
in panels[]). See `g_drilledIntoSubScreen` in `menus.cpp`.

---

## 3. Reading control text (for any on-screen feedback you want to surface)

If your controller mod ever needs the label of the focused control (radial menu
captions, tooltips, etc.), the extraction chain is in `menus_extract.cpp`
(`extract::FromControl`). Offsets are in `gui-and-input-internals.md`. Summary:

- `CSWGuiControl` base size 0x5c: vtable +0x00, tooltip CExoString +0x28, id +0x50.
- The **actually-rendered** text is not the inline CExoString — it lives on a
  `CAurGUIStringInternal` at `gui_string + 0x14`. For a Label that's `+0xE4`, for
  a Button `+0x168`. The inline CExoString/strref are often empty (notably the
  in-game menu icons), so read gui_string first, fall back to the others.
- Downcast via `GuiControlMethods` vtable: `[20]` AsLabel, `[22]` AsButton, etc.
  All `__thiscall`, trivial, safe to call from a hook. Call pattern in §"Calling
  __thiscall" of `gui-and-input-internals.md`.

The canonical focus signal is `CSWGuiPanel::SetActiveControl @ 0x40a630` — fires
exactly once per real focus change, covers every panel including the main menu.
Hook mid-function at `0x40a638` (EDI = panel, ESI = new control). Do NOT use
HandleFocusChange (fires twice, reports the wrong control). Our hook is
`OnSetActiveControl` in `menus.cpp`.

---

## 4. Activation — how we "press" a control programmatically

This is the part everyone gets wrong first. **Do not call
`CSWGuiPanel::SetActiveControl` to activate** — it skips invariants the engine
maintains in the click flow and crashes (the manager pointer ends up as an
indirect call target a frame later). We burned seven attempts proving this; see
the `activation_via_click_sim` section of `gui-and-input-internals.md`.

The reliable primitive is **synthesize a mouse click at the control's center
coordinate**:

- `CSWGuiManager::MoveMouseToPosition(x, y) @ 0x40c790` — does cursor move +
  hit-test + hover + active-control update in one call. Use this to "focus" a
  control by warping the engine's own cursor onto it.
- Then `HandleLMouseDown` + `HandleLMouseUp` (activate code 0x27) at that coord
  for the actual press. For engine-known buttons we drive the button's own
  handlers; for listbox interiors we click into the extent.
- Compute the coordinate from the control extent: `cx = left + width/2`,
  `cy = top + height/2`.

Defer the actual click to the next `Update` tick rather than firing it inside
the input hook (re-entrancy). Our queue is `g_pendingActivate*` drained in
`TickPendingOps` (`menus.cpp`).

**Hit-test shift gotcha:** on tabbed panels `MoveMouseToPosition`'s internal
hit-test resolves to the control one tab-row *above* the cursor (programmatic
moves miss the OS cursor-hotspot translation real WM_MOUSEMOVE gets). Add the
tab-row pitch to y when warping onto a tab. Details + the compensation
(`ComputeTabClickOffsetY`) in `menus_chain.cpp` and the `movemouse_hittest_shift`
note.

**Why path A, not B, for menus:** the generic chain builds a control list for
*any* foreground panel — including custom game panels like the Pazaak board. If
you Win32-poll Enter and let the engine deliver it too, BOTH fire (the chain
activates a button AND your custom handler runs). So a custom panel navigator
must intercept at the manager hook via a `TryHandleInput(panel, code, val, &rv)`
that returns "consumed" BEFORE the generic chain handlers run. See how
`pazaak.cpp`, `menus_galaxymap.cpp`, `menus_abilities.cpp` register ahead of the
chain in `OnHandleInputEvent`. Win32 polling is only safe for scancodes the
engine drops entirely (letters, punctuation).

---

## 5. The GUI "tab" construct — two screens, two strategies

KOTOR's tabbed screens are **not** a real tab control. The recurring shape is: a
`CSWGuiListBox` at `controls[0]` plus a contiguous cluster of plain
`CSWGuiButton`s — those buttons ARE the tabs, and clicking one changes what the
listbox/detail area shows. But the two tabbed screens we handle behave
differently under the hood, so we handle them two different ways. This is the
distinction worth understanding before you build pad navigation for them.

**Strategy A — Options/Settings: flatten tabs into the generic chain.**
(`menus_chain.cpp`.) Here each tab opens a **separate sub-dialog** that contains
the real interactive controls (toggles, sliders, cycle buttons). So we don't
treat the tabs specially at the nav level — `DetectTabsCluster(panel, &start,
&count)` just identifies the tab buttons (state in `g_tabbedPanel /
g_tabsStart / g_tabsCount`) and they sit in the linear chain alongside
everything else. The user arrows onto a tab button and presses Enter; that
click-sims the button (with the +y hit-test compensation from §4 — `IsTabButton`
gates it) and the engine swaps the sub-dialog in. The settings listbox inside a
tab is **decorative** (a single multi-line label blob, `controls.size == 1`, not
per-row controls) so we silence it; the real controls are in the sub-dialog,
which the same chain navigates with no per-screen code. There is no dedicated
"settings tab" subsystem — it's all the generic chain.

**Strategy B — Fähigkeiten/Abilities: a dedicated two-level navigator.**
(`menus_abilities.cpp`, `HandleInput`.) Same visual shape (3 tabs: Skills /
Powers / Talents + one `LB_ABILITY` listbox), but here the tabs **repaint the
same shared listbox** rather than opening separate panels, and the engine's own
selection-change paths are mouse-only / crash-prone. So flattening into the chain
doesn't fit. Instead this screen gets a two-level submenu that **intercepts at
the manager hook before the generic chain ever runs** (registered ahead of the
chain in `OnHandleInputEvent`, via the `TryHandleInput(panel, code, val, &rv)`
"return consumed" contract):
  - Tab level (where you land): Up/Down move between available tabs (clamped, no
    wrap), Enter drills into the tab's list, Esc closes the screen.
  - List level (after Enter): Up/Down browse rows (clamped), Esc returns to the
    tab level. We drive the listbox cursor ourselves (`DriveListBoxSelection`),
    then call the engine's `OnAbilitySelectionChanged` to repaint the detail
    area, then read it back.

**Why the divergence:** Options tabs are *navigation into separate control sets*
(a tab is just a button you activate → chain), while Abilities tabs are *a view
switch over one shared list* with no separate panel and broken engine input
paths (→ owns its keys with a custom navigator). The deciding question for any
new tabbed screen: does activating a tab hand you a fresh panel of real
controls, or just repaint one in place? The former reuses the chain; the latter
wants a dedicated handler.

(A third, related shape: the **character sheet** isn't tabbed at all — it's a
flat panel of inline value labels. We turn those labels into *virtual* text-only
chain entries so the user can arrow through stats. See
`menus_charsheet.cpp::ForEachStatRowAnchor` + `ExtractStatRow` — useful as the
pattern for "make non-control text navigable" if your pad UI needs it.)

For a controller mod: a shoulder-button "next/prev tab" maps cleanly to Strategy
A (move chain cursor onto the next tab button + click-sim, reusing `IsTabButton`)
for Options, but for Abilities-style screens you'd hook the dedicated navigator's
tab level instead. The native multi-row `CSWGuiListBox` keyboard model lives at
`0x41ce20` (arrow = selection ±1, Enter = activate row) if you ever want to drive
a real multi-row list directly rather than through our cursor helper.

---

## 6. Pickers (two unrelated things share the word)

**6a. List pickers — the "select-then-confirm" listbox construct.**
A whole family of screens share one shape: arrow a listbox cursor (announced
inline per row), Enter commits a panel-specific action, Esc backs out. Members:
container loot, save/load slot list, the **equip picker** (pick a weapon/armor
for a slot), and the **workbench upgrade picker** (pick a mod for an upgrade
slot). These are collapsed into a single spec-table dispatcher —
`menus_listbox.cpp`, contract in `menus_listbox.h::TryHandleInput`. Each panel is
one `ListBoxPanelSpec`: how to match it, when it's armed, where its listbox is,
how to announce a row, what Enter/Esc do, fall-through behavior. Adding a new
one of these is one spec entry (~30 lines), not a new handler.

Two reusable helpers do the heavy lifting (see the
`select_then_confirm_listbox_helpers` note):
- `DriveListBoxSelection(lb, navDown, minSel, &out)` — pure cursor mutation:
  writes `selection_index`, scrolls `top_visible_index`, returns old/new index +
  row count + the row pointer. Pass `minSel=1` for listboxes whose row 0 is a
  `.gui`-time template (the equip picker's `LB_ITEMS`), 0 otherwise. This
  deliberately bypasses the engine's own onSelectionChanged, which is why callers
  that need the detail area repainted call the engine handler explicitly
  afterward.
- `QueueButtonByIdActivate(panel, buttonId, prefix)` — Enter/Esc commit: debounce
  + queue a click-sim of the confirm/cancel button by its `.gui`-time id.

Two arming nuances worth copying:
- The **equip picker** arms when chain-Enter activates an equip slot button
  (`BTN_INV_*`) and stays armed until commit/Esc/panel-gone (`ArmEquipPicker` /
  `IsEquipPickerArmed` / `DisarmEquipPicker`). Its Enter commit intentionally
  bypasses `QueueButtonByIdActivate` and direct-calls `OnItemSelected` — the
  click pipeline can't reach the row buttons through the listbox extent.
- The **workbench upgrade picker** arms the same way off `BTN_UPGRADE3X/4X` slots
  (`ArmWorkbenchUpgradePicker` etc.).
- A per-tick monitor (`TickListboxMonitors`) announces row changes and disarms a
  picker whose panel has vanished from `CSWGuiManager.panels[]`.

Note the *select-AND-act-on-the-row* surfaces (Options sub-dialog setting
buttons, dialog reply list, read-only description lists) deliberately do NOT use
these helpers — different semantics; they go through the chain.

**6b. The in-world action picker — context-sensitive verb dispatch.**
Different beast, same word. This is the engine's "what does clicking this object
do" pipeline (open / talk / Security / Bash / Disable Trap / …), driven for an
arbitrary target without moving the cursor. `engine_picker.{h,cpp}`,
`acc::picker::Drive`. Relevant if your pad mod wants a context-action button or a
radial: it replicates the cursor-setup the engine normally does on hover
(`SetMainInterfaceTarget` → `GetDefaultActions` → click gate →
`HandleMouseClickInWorld`), reads back the action descriptor at `+0x4c8`, and can
either dispatch the default or open the radial (Shift+Enter semantics). Key
addresses: `HandleMouseClickInWorld @ 0x620350`, `GetDefaultActions @ 0x620620`,
`SetMainInterfaceTarget @ 0x62b000`. Watch the client/server handle split
(client ids carry the `0x80000000` high bit) and the "leave player input enabled
so the engine walks-to-target" rule documented in the header.

---

## 7. A worked example of a custom keyboard-driven overlay

The closest analog to "build my own overlay menu with its own nav + activation"
is the **unified action menu** — our in-world radial replacement. Files:
`unified_action_menu.{h,cpp}`, driven from `interact_hotkey.cpp`.

It's instructive because it does NOT go through the manager hook or the chain at
all. It:
- Opens on Shift+Enter, pauses the world via an overlay hold.
- Takes its own nav/Enter/Esc through the Win32 poll (the in-world arrows/Enter
  are unbound, so the engine never sees them).
- Renders its own item list and speaks the focused row.
- Has an Esc-consume latch (`input_pipeline.h` `NoteOverlayEscClosed` /
  `ConsumeOverlayEscLatch`) to defeat a poll-vs-event race where closing the
  overlay AND the engine's pause menu both fired on one Esc.

If your controller overlay is a fully custom surface (your own draw, your own
model) rather than a navigator over existing engine panels, this is the pattern
to copy. If instead you want to drive the game's *existing* menus with a pad,
copy the chain (§2) + click-sim (§4) instead.

---

## 8. Input acquisition / focus edge cases you WILL hit

`engine_input.{h,cpp}` exists almost entirely to work around DirectInput
acquisition bugs that bite any input mod:

- **Cold-start dead keyboard:** the engine only Acquires its DI devices on a
  `CExoInput::SetActive(1)` *edge*, which it drives from game-state events, not
  WM_ACTIVATE. On a cold launch that edge can be missed and the menu is keyboard-
  dead until alt-tab. `EnsureInputAcquired` / `ForceReacquireInput` replicate the
  edge.
- **Windowed focus bleed:** KOTOR acquires the keyboard at BACKGROUND cooperative
  level, so it keeps reading keys while another window is foreground. We mirror
  foreground via `ReleaseInput` / `RequestInputReacquire` on WM_ACTIVATEAPP.

A gamepad mod via XInput sidesteps the keyboard acquisition issue but you'll
still care about the foreground-mirroring (don't drive the game while the user is
in another window). The deferred-reacquire drain pattern (`DrainPendingReacquire`
once per tick) is the safe way to do SetActive cycling off a window event.

---

## 9. Minimal scaffold — gamepad → engine cursor → activate

The smallest thing that proves the loop: poll a pad button each frame, warp the
engine's own cursor onto the focused chain control, and activate it on the next
tick. It ties together §1 (tick + edge-detect), §2 (foreground panel + control
extent), and §4 (MoveMouseToPosition, then deferred click-sim). Illustrative —
you supply the hook wiring (mirror `core_tick.cpp`) and the click-sim helper
(lift `FireActivate` / the pending-op queue from `menus_pending.cpp`); the engine
reads and signatures are real for 1.0.3.

```cpp
#include <windows.h>
#include <xinput.h>
#pragma comment(lib, "xinput.lib")

// --- engine surfaces (see §"Key engine addresses") ---
struct GuiMgr;  // opaque
static GuiMgr*  Mgr()       { return *reinterpret_cast<GuiMgr**>(0x7A39F4); }
typedef void (__thiscall* PFN_MoveMouse)(GuiMgr*, int x, int y);
static auto MoveMouseToPosition = reinterpret_cast<PFN_MoveMouse>(0x40C790);

// Foreground panel = top of modal_stack, else top of panels. Each is a
// CExoArrayList at mgr+0x88 (panels) / mgr+0x94 (modal_stack): {data*, size}.
static void* ForegroundPanel(GuiMgr* m) {
    auto arr = [&](size_t off, void*** data, int* size) {
        auto base = reinterpret_cast<char*>(m) + off;
        *data = *reinterpret_cast<void***>(base);
        *size = *reinterpret_cast<int*>(base + 4);
    };
    void** d; int n;
    arr(0x94, &d, &n); if (n > 0) return d[n - 1];   // modal_stack top
    arr(0x88, &d, &n); if (n > 0) return d[n - 1];   // panels top
    return nullptr;
}

// Center of a CSWGuiControl from its extent at +0x04 (left/top/width/height).
static void ControlCenter(void* ctrl, int* cx, int* cy) {
    int* e = reinterpret_cast<int*>(reinterpret_cast<char*>(ctrl) + 0x04);
    *cx = e[0] + e[2] / 2;
    *cy = e[1] + e[3] / 2;
}

// Your nav state — index into your own chain (build it like RebindChain, §2).
extern void* g_chain[]; extern int g_chainCount, g_chainIndex;

// Edge-detect one XInput button. Call once per frame per button you care about.
static bool Pressed(WORD button) {
    static WORD prev = 0;
    XINPUT_STATE s{};
    if (XInputGetState(0, &s) != ERROR_SUCCESS) { prev = 0; return false; }
    WORD now = s.Gamepad.wButtons;
    bool edge = (now & button) && !(prev & button);
    prev = now;
    return edge;
}

// Call once per frame from your Update @0x40CE70 hook (deferred work, not the
// input hook — avoids re-entrancy when engine fns dispatch).
void Tick() {
    if (GetForegroundWindow() != /* our hwnd */ nullptr) return;  // foreground gate
    GuiMgr* m = Mgr();
    void* panel = m ? ForegroundPanel(m) : nullptr;
    if (!panel) return;

    if (Pressed(XINPUT_GAMEPAD_DPAD_DOWN) && g_chainIndex + 1 < g_chainCount)
        ++g_chainIndex;        // (also re-announce / move cursor for feedback)
    if (Pressed(XINPUT_GAMEPAD_DPAD_UP)   && g_chainIndex > 0)
        --g_chainIndex;

    if (Pressed(XINPUT_GAMEPAD_A) && g_chainIndex < g_chainCount) {
        void* ctrl = g_chain[g_chainIndex];
        int x, y; ControlCenter(ctrl, &x, &y);
        // +tab-row pitch to y if this is a tab button (§4 hit-test shift).
        MoveMouseToPosition(m, x, y);   // focus via engine hit-test
        // Defer the actual press to NEXT tick, not here — see TickPendingOps.
        QueueActivateAt(x, y);          // your lift of FireActivate / click-sim
    }
}
```

What this deliberately skips, because the doc covers it: building the chain
(`RebindChain`, §2), the tab-button +y compensation (§4), consuming the input so
the engine doesn't double-act (§4 — only relevant if you also let the engine see
the key), and the click-sim internals (§4). The point is the wiring: pad edge →
`MoveMouseToPosition` → deferred activate.

---

## File map (start here, in order)

Source repo: **https://github.com/JeanStiletto/voice-of-the-old-republic** —
all paths below are under `patches/Accessibility/`; the RE reference is under
`docs/llm-docs/`.

- `menus.cpp` — `OnHandleInputEvent` (manager input hook + dispatch order),
  `OnSetActiveControl` (focus signal), pending-op queue.
- `menus_chain.{h,cpp}` — the navigation chain: RebindChain + the four Handle*
  dispatchers + tab-cluster detection (Strategy A) + hit-test compensation.
- `menus_abilities.{h,cpp}` — dedicated two-level tab navigator (Strategy B).
- `menus_listbox.{h,cpp}` — select-then-confirm picker dispatcher (§6a) +
  DriveListBoxSelection / QueueButtonByIdActivate helpers.
- `menus_charsheet.{h,cpp}` — virtual chain entries over inline value labels.
- `engine_picker.{h,cpp}` — in-world context-action / radial picker (§6b).
- `menus_extract.cpp` — control-text extraction.
- `engine_input.{h,cpp}` — input-code constants + DirectInput acquisition fixes.
- `cycle_input.cpp`, `interact_hotkey.cpp` — Win32 GetAsyncKeyState polling
  reference for engine-dropped keys.
- `unified_action_menu.{h,cpp}` — fully custom keyboard-driven overlay example.
- `input_pipeline.{h,cpp}` — upstream client-app input hooks + overlay-Esc latch.
- `core_tick.cpp` — per-frame tick dispatcher (where polling + deferred work run).
- `hooks.toml` — every hook's address, cut bytes, and register-source spec.

Engine RE reference (offsets, addresses, decompiled behavior):
- `docs/llm-docs/gui-and-input-internals.md` — the master doc for all of the
  above: CSWGui* offsets, gui_string text indirection, panel/foreground routing,
  cursor + hit-test surfaces, listbox model, the in-DLL input pipeline. Read this
  first.
- `docs/llm-docs/action-menu-and-combat.md` — radial/personal action surfaces if
  you reuse the engine's own action menu.
- `docs/llm-docs/re/` — Lane's Ghidra XML + SARIF + exported C header for the
  exe.

Key engine addresses, quick reference (1.0.3 Steam/GoG):
- CSWGuiManager singleton: `*0x7A39F4`; CExoInput: `*0x7A39E4`.
- HandleInputEvent `0x40c8e0` (epilogue `0x40cbcb`); Update `0x40ce70`.
- MoveMouseToPosition `0x40c790`; HitCheckMouse `0x40abe0`.
- SetActiveControl `0x40a630`; PushModalPanel `0x40bd90`; SendPanelToBack `0x40bd20`.
