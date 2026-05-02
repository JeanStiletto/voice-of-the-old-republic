# Menu Navigation — Unified Cursor Design

Handoff doc. Captures the current design and the data needed to implement it.

## Model

**Arrow keys move a synthesized cursor; Enter clicks.** Same model in menus
and in the 3D world. In menus the cursor snaps to consecutive
`panel.controls` entries; in the world it moves smoothly. One input pipeline,
one announcement source, one activation pipeline.

The whole design hinges on a single engine call, `CSWGuiManager::MoveMouseToPosition`,
which the SARIF investigation in session 5 surfaced. It does cursor update +
hit-test + active-control update in one shot, so the menu-nav layer is mostly
"compute target coords, queue a deferred call to that function."

**Caveat on tabbed panels:** `MoveMouseToPosition`'s internal hit-test resolves
to the button at `cursor.y - tabSpacing` on Options-style panels — a consistent
45-px shift on Steam 1.0.3, exactly matching the tab pitch. Cause unverified
(best guess: cursor hotspot coord-system mismatch — programmatic moves don't
get the OS-cursor-hotspot translation that real WM_MOUSEMOVE events get). Real
mouse usage is unaffected. We compensate by adding `g_tabClickOffsetY` (computed
per-panel from chain tab spacing) to y when warping to a tab button, both for
arrow nav and Enter. Without the offset every nav step + click lands one tab
above the intended target. See `memory/project_movemouse_hittest_shift.md` and
`patch-20260502-122734.log` line 91 for the disambiguating diagnostic
(`before=NULL after=Gameplay` proves the shift is in MoveMouseToPosition's
hit-test, not stale state).

## Key engine surfaces (from SARIF)

### Globals

- `*(CSWGuiManager**)0x7A39F4` — singleton GuiManager pointer.
- Adjacent: `0x7A39E4 = CExoInput*`, `0x7A39E8 = CExoResMan*`, `0x7A39FC = CAppManager*`.

### CSWGuiManager methods

- `MoveMouseToPosition(int x, int y)` `@ 0x40c790` — internally calls
  `CExoInput::SetMousePos` then `HandleMouseMove`, which calls `HitCheckMouse`
  and `UpdateMouseOverControl`. Single primitive for cursor + hover refresh.
- `HandleInputEvent(int code, int state)` `@ 0x40c8e0` — already hooked at
  `0x40c907` mid-function.
- `HitCheckMouse(x, y, **outPanel, **outCtrl, _)` `@ 0x40abe0` — direct hit-test
  if needed; usually we let `MoveMouseToPosition` do it.
- `Update(float dt)` `@ 0x40ce70` — per-frame tick. Single caller is
  `CClientExoAppInternal::MainLoop @ 0x602eb0` (one call per frame, after
  input dispatch). Used as the deferred-call site (see "Reentrancy" below).
  Hook cut detailed in "Update hook" section.
- `HandleMouseMove(x, y)` `@ 0x40c1e0` — what `MoveMouseToPosition` ultimately drives.

### Struct offsets

`CSWGuiControl`:
- `+0x0`  vtable
- `+0x4`  extent (inline `CSWGuiExtent`, 16 bytes)
- `+0x14` parent_control
- `+0x18` child_controls (`CExoArrayList`)
- (memory) `+0xe8` label text, `+0x16c` button text — see
  `project_kotor_gui_struct_offsets.md`

`CSWGuiExtent` (at `ctrl + 0x4`):
- `+0x0`  left   `int`
- `+0x4`  top    `int`
- `+0x8`  width  `int`
- `+0xC`  height `int`

Center of any control:
```cpp
int cx = ctrl->extent.left + ctrl->extent.width / 2;
int cy = ctrl->extent.top  + ctrl->extent.height / 2;
```

### Other useful methods (for filtering, deferred work)

- `CSWGuiControl::GetIsSelectable` `@ 0x4189d0` — returns `bool`. Overridden on
  `CSWGuiEditbox`, `CSWGuiListBox`, `CSWGuiPazaakCard`. Available if we ever
  decide to skip non-selectable controls. Per Decision 3 we don't filter yet.

## Reentrancy: why we use a deferred Update hook

Calling `MoveMouseToPosition` directly from inside our `HandleInputEvent` hook
is reentrant — the engine is mid-dispatch, and `MoveMouseToPosition` mutates
input-system + GUI state via `HandleMouseMove` → `UpdateMouseOverControl`.
Same class of problem as the listbox-entry-hook toxicity from session 4.

Solution: defer to the next `CSWGuiManager::Update` tick.

```cpp
static bool g_pendingCursorMove = false;
static int  g_pendingX, g_pendingY;

// in OnHandleInputEvent (input dispatch — cheap work only):
g_pendingX = cx; g_pendingY = cy;
g_pendingCursorMove = true;

// in OnUpdate (per-frame tick, post-dispatch):
if (g_pendingCursorMove) {
    g_pendingCursorMove = false;
    auto* gm = *(CSWGuiManager**)0x7A39F4;
    gm->MoveMouseToPosition(g_pendingX, g_pendingY);
}
```

One-frame lag (~16ms at 60fps), inaudible. Tolk speech still fires synchronously
from the input hook, so the audible response feels instantaneous.

### Update hook (verified)

SARIF + DumpBytes confirmed the cut. Disassembly of the prologue:

```
0x40ce70: 51                     push ecx           ; local var slot
0x40ce71: 53                     push ebx
0x40ce72: 55                     push ebp
0x40ce73: 56                     push esi
0x40ce74: 8b e9                  mov  ebp, ecx      ; this → EBP
0x40ce76: 8b 85 8c 00 00 00      mov  eax, [ebp+0x8c]   ← hook cut (6 bytes)
0x40ce7c: 57                     push edi
... loop iterating panels[0x88], calling each panel's vtable[0x38]
```

- **Cut:** `0x40ce76`, 6 bytes `[0x8b, 0x85, 0x8c, 0x00, 0x00, 0x00]`, single
  memory-relative MOV — safe to relocate. Resume at `0x40ce7c`.
- **Source:** `ebp` → pointer (`this`). EBP holds the manager after the engine's
  `mov ebp, ecx` at `0x40ce74`. Callee-saved across our handler call.
- **No inbound jumps** land in `0x40ce76`–`0x40ce7b` — all internal jumps in
  Update target later addresses.
- **Reentrancy verified safe:** `MoveMouseToPosition` mutates hover/tooltip
  state via `HandleMouseMove`; Update's panel-iteration loop reads neither.
  Cursor moves on frame N+1 take effect for that frame's panel updates —
  correct ordering.

`hooks.toml` entry:

```toml
[[hooks]]
address = 0x0040ce76
type = "detour"
function = "OnUpdate"
original_bytes = [0x8b, 0x85, 0x8c, 0x00, 0x00, 0x00]
skip_original_bytes = false
exclude_from_restore = []

[[hooks.parameters]]
source = "ebp"
type = "pointer"
```

### Calling MoveMouseToPosition from C++

First time we'll call into the engine from our DLL (existing hooks only react).
Standard MSVC `__thiscall` function pointer:

```cpp
typedef void (__thiscall *MoveMouseToPositionFn)(void* gm, int x, int y);
constexpr MoveMouseToPositionFn MoveMouseToPosition =
    reinterpret_cast<MoveMouseToPositionFn>(0x0040c790);

auto* gm = *reinterpret_cast<void**>(0x7A39F4);
MoveMouseToPosition(gm, cx, cy);
```

If the framework wants a different declaration shape, we'll find out at link time.

## Engine-event consumption

The current framework wrapper (`wrapper_x86_win32.cpp`) has no conditional flow
control — after the handler returns, it unconditionally executes original
bytes + JMPs to `hookAddress + originalBytes.size()`. We need a one-feature
extension: a `consumed_exit_address` field that, when set and the handler
returns non-zero, makes the wrapper JMP to that address instead.

### Function epilogue (consumed exit target)

`CSWGuiManager::HandleInputEvent` ends with:

```
0x40cbcb: 8b 4c 24 24             mov  ecx, [esp+0x24]   ; saved old SEH ptr
0x40cbcf: 5f 5e 5d 5b              pop  edi, esi, ebp, ebx
0x40cbd3: 64 89 0d 00 00 00 00    mov  fs:[0], ecx       ; restore SEH chain
0x40cbda: 83 c4 20                 add  esp, 0x20
0x40cbdd: c2 08 00                 ret  8
```

Stack state at `0x40cbcb` matches stack state right after our cut bytes run
(EDI/ESI/EBP/EBX saved, locals + SEH frame intact, `[ESP+0x24]` = saved SEH
ptr). JMPing here from the wrapper after the cut is a clean exit.

### Framework change required

Eight files, ~50 lines, fully additive (default behavior unchanged for every
existing hook). Tracked in `docs/upstream-prs.md`. Wrapper tail layout, with
cut bytes emitted *before* the conditional jump so both paths leave the stack
in the post-cut state expected at the consumed-exit target:

```asm
; (register restore complete; eax excluded from POPAD if consuming)
<cut bytes>                            ; original instructions, e.g. PUSH EDI
TEST EAX, EAX                          ; 85 c0
JZ +5                                  ; 74 05  (skip the consumed JMP)
JMP rel32 to consumed_exit_address     ; e9 ?? ?? ?? ??  (consumed)
JMP rel32 to hookAddress + cut.size()  ; fall-through (non-consumed)
```

If the cut bytes ran *after* the conditional jump, the consumed path would
JMP without the cut's stack mutations applied and the target's POP/ADD ESP
would corrupt — the design's `0x40cbcb` target assumes EDI/ESI/EBP/EBX are
on the stack from the cut's `PUSH EDI`. Emitting the cut first keeps both
paths' stack state identical.

### Hook config after the change

```toml
[[hooks]]
address = 0x0040c907
type = "detour"
function = "OnHandleInputEvent"
original_bytes = [0x8b, 0xf1, 0x57, 0x89, 0x5e, 0x68]
skip_original_bytes = false
exclude_from_restore = ["eax"]
consumed_exit_address = 0x0040cbcb

# parameters unchanged
```

```cpp
extern "C" __declspec(dllexport)
int __cdecl OnHandleInputEvent(void* gm, int code, int state) {
    LogInputEvent(code, state);
    if (state != 0 && IsArrowKey(code)) {
        DoChainNavigation(gm, code);
        return 1;     // CONSUMED — engine skips translation + dispatch
    }
    return 0;         // PASS-THROUGH — engine handles normally
}
```

Sequence: ship the framework change first, validate existing hooks unchanged,
then enable consumption on our input hook, then build Phase 1+2 on top.

## Decisions (carried forward from session 4)

1. **Unified cursor model** — same keys do the same thing everywhere. No mode toggles.
2. **Stable chain = `panel.controls` iteration order.** Down → +1, Up → −1, clamp at ends, no wrap.
3. **Don't filter "suspicious" elements yet.** Multi-line listboxes, icon-only
   buttons, NULL slots, unknown vtables — all stay in the chain. Filter from
   evidence, not guesses. (`GetIsSelectable` is available when we're ready.)
4. **Panel-open enumeration via Tolk.** First focus into a new panel queues
   speech for every child in order. Re-entering the same panel does nothing.
5. **Consume arrow keys at the input hook; don't let the engine see them.**
   Handler returns non-zero for arrow keys → wrapper jumps directly to the
   function epilogue at `0x40cbcb`, skipping translation + dispatch. Engine
   never runs the broken `.gui` cycle, never fires `SetActiveControl(Y)`,
   nothing to suppress. Other keys (Tab, Enter, mouse) pass through normally.
   Requires a one-feature framework extension (see "Engine-event consumption"
   below + `docs/upstream-prs.md`). Also keep a tiny self-dedup flag so our
   own `MoveMouseToPosition` triggering `SetActiveControl(X)` next frame
   doesn't double-announce — different problem from engine suppression.
6. **Phased speak-only first.** Cursor sync added once chain announcement is
   stable. Activation comes free from the engine.
7. **Main-menu working K/L cycle is acceptable collateral.** "One menu lost in
   exchange for all menus working."

## Roadmap

### Phase 1+2 — Speak + cursor sync (DONE)

Shipped. Chain navigation walks `panel.controls` (recursing one level into
listboxes with `controls.size > 1`), sorted by `extent.top` so arrow-down is
visually top-to-bottom. State:

```cpp
struct ChainEntry { void* control; int cx; int cy; };
static ChainEntry g_chain[64];
static void* g_currentPanel = nullptr;
static void* g_chainPanel   = nullptr;
static int   g_chainIndex   = 0;
static int   g_chainCount   = 0;
static int   g_tabClickOffsetY = 0;   // hit-test shift compensation (see Phase 3)

static bool g_pendingCursorMove = false;
static int  g_pendingX, g_pendingY;
static void* g_pendingTarget = nullptr;
```

`OnHandleInputEvent` (returns int — 1 = consumed, 0 = pass-through):
- On `0xb6` (up) / `0xb7` (down) with `state != 0`:
  - If `g_currentPanel != g_chainPanel`: `RebindChain(g_currentPanel)`.
  - Compute `g_chainIndex ± 1`, clamp `[0, g_chainCount-1]`.
  - `e = g_chain[g_chainIndex]`.
  - `AnnounceControl(e.control)` via Tolk.
  - Compute `cursorY = e.cy + (IsTabButton(e.control) ? g_tabClickOffsetY : 0)`.
  - Write `g_pendingX = e.cx`, `g_pendingY = cursorY`, `g_pendingTarget = e.control`,
    set `g_pendingCursorMove`.
  - Return 1 (consumed).
- On Enter (`0xb5`/`0xbb`) with `state != 0` on a chain target:
  - If target is a tab button: queue deferred click-sim (cursor warp + manager
    HandleLMouseDown/Up) with the same `+g_tabClickOffsetY` adjustment.
  - Else: queue deferred direct activate via `vtable[15].HandleInputEvent(0x27, 1)`.
  - Return 1.
- On Esc (`0xb4`/`0xdf`) inside a tabbed parent's sub-dialog: queue deferred
  FireActivate on the sub-dialog's Schliess button. Return 1.
- For all other keys: return 0 (pass-through).

`OnSetActiveControl` additions:
- `g_currentPanel = panel`.
- First focus into a panel: `WalkChildren` log dump; `AnnouncePanelTitle` once.
- If `param == g_pendingTarget`: skip Tolk announcement (self-dedup, we already
  spoke from the input hook). Clear `g_pendingTarget`.

`OnUpdate` (mid-`CSWGuiManager::Update` @ `0x40ce76`):
- If `g_pendingCursorMove`: clear, call `MoveMouseToPosition(g_pendingX, g_pendingY)`.
  Log `mouseOver before/after` for diagnostic.
- If `g_pendingClick`: clear, call manager `HandleLMouseDown(1)` then `HandleLMouseUp()`.
  (See Phase 3 below for the path.)
- If `g_pendingActivate`: clear, call `FireActivate(target)` via vtable[15].

**Done.** Verified end-to-end on main menu, chargen, save/load, Options panel
+ all sub-dialogs (Spieleinstellungen, Feedback-Optionen, Auto-Pause-Optionen,
Grafikeinstellungen, Soundeinstellungen), quit-confirmation popup.

### Phase 3 — Activation primitive (synthesized mouse click)

**Tab activation: DONE.** Per-setting activation inside the Options listbox:
not yet started.

**What works for tabs:** Enter on a tab queues a deferred click-sim on
`OnUpdate`: cursor warp via `MoveMouseToPosition` to the tab's `(cx, cy + g_tabClickOffsetY)`,
then manager `HandleLMouseDown(1)` followed by `HandleLMouseUp()`. The engine
runs its full natural click pipeline against `mouseOverControl` (which the
warp's hit-test populates correctly because we compensated for the tab-pitch
shift). Verified opening Spieleinstellungen, Feedback-Optionen,
Auto-Pause-Optionen, Grafikeinstellungen, Soundeinstellungen.

**Two activation paths**, picked at Enter time:

1. **Tab buttons** (in the tabbed parent panel) → click-sim. Required because
   each tab's onClick handler (`CSWGuiInGameOptions::OnGraphics`, etc.) gates
   on `param_1->is_active != 0`, a flag set by `HandleLMouseDown` but not by
   direct vtable[15] dispatch. So `FireActivate` silently no-ops on a tab.
2. **Everything else** (sub-dialog Schliess/OK/Cancel, settings buttons in
   sub-dialog listboxes, main menu) → direct `vtable[15].HandleInputEvent(0x27, 1)`
   "FireActivate". Bypasses hit-test, so buttons covered by overlapping listbox
   extents (chain-navigated settings, Schliess inside an Options sub-dialog)
   still fire correctly.

**Direct vtable[6]/[7] dispatch on the chain target was tried as a workaround
for the hit-test shift and CRASHES** on the second tab+ click. The button's
`HandleLMouseDown`/`HandleLMouseUp` depend on manager-side state (probably the
+0x1c mouse_held bit and other setup) that the manager's wrapper performs.
Skipping the wrapper leaves the button in an inconsistent state.

**The +g_tabClickOffsetY fix** (added 2026-05-02) supersedes the prior plan to
RE the crash and replicate hidden state. The shift was the actual root cause
of activation landing on the wrong tab; once compensated, the engine's normal
click pipeline works as advertised. See top of file for the shift mechanism
and `memory/project_movemouse_hittest_shift.md`.

**Still open in Phase 3:**

- Per-setting activation inside the Options listbox. The listbox has
  `controls.size == 1` (one multi-line label blob); individual settings are
  not `CSWGuiControl*`s. The engine reaches them only via mouse clicks
  resolved by y-coordinate inside the listbox. Need a coordinate-based click
  primitive that lands inside the listbox at the right line — exact engine
  entry point TBD.
- Slider drag. Click-sim sequence: down at handle → cursor moves via
  `MoveMouseToPosition` → up at target.

### Phase 4 — Spatial mode (3D world / non-panel screens)

When `g_currentPanel == nullptr`, switch to Lane's smooth-cursor model:
- Arrow press/release sets/clears direction-flag bits (`dirBitFlags`).
- `OnUpdate` adds `(speed × xDir, speed × yDir)` to last cursor coords each
  tick and calls `MoveMouseToPosition`. Engine's hover refresh tells us what's
  under the cursor (`*outCtrl` from `HitCheckMouse` if we want it explicitly,
  or just observe `OnSetActiveControl` events).
- Announce changes in the focused control via Tolk.

Same primitive (`MoveMouseToPosition`), different driver. No new engine
surfaces required.

## Open issues (carry forward)

- `+/-` icon buttons (vtable `0x73E658`, no text, no `str_ref`) — read as
  `"control N"`. Acceptable; long-term: per-vtable name table.
- TLK `c_string` leak — bounded per session, unbounded across many. Future:
  call `CExoString::~CExoString` after use.
- Multi-line listbox in chain — entries blob-announce on focus; chain may
  land *on* the listbox and read the whole list. Acceptable; later, filter
  blob listboxes from chain.
- **Listbox auto-readout on tab focus** is now silenced via tabbed-mode
  detection: when `panel.controls[0]` is a `CSWGuiListBox` with a tab cluster
  detected by `DetectTabsCluster`, the per-tab listbox blob is parsed into
  virtual lines and `SpeakBlobIfChanged` is suppressed. Logged as `ListBox
  blob silenced (tabbed mode); N virtual lines parsed`. Per-line readout will
  be a sub-mode the user enters explicitly — not yet implemented; tracked
  under Phase 3 "per-setting interaction" in the roadmap.
- **Engine lagged `SetActiveControl` re-promotion to Gameplay** after each
  chain step in tabbed panels. The panel's tab-switch logic resets
  `active_control` to its default (first tab = Gameplay) after the engine's
  hover-to-active fires for the chain target. Currently dedup hides it from
  audio but it's noisy in the log. Optional cleanup item; see Re-entry
  checklist.

## What's at HEAD (Phase 1+2 + Phase 3 tabs)

- Manager-level input hook at `0x40c907` — every key, both edges, readable log
  via `ManagerTranslateCode`.
- Panel-children walk on first focus into a new panel.
- Listbox-children walk + cursor read on every listbox event.
- Multi-line listbox blob enumeration via `SpeakBlobIfChanged`.
- TLK lookup with SEH guard.
- Flat chain navigation (`g_chain[]`) over `panel.controls` + one-level listbox
  recursion, sorted by `extent.top`. Anchored on `panel.activeControl` at
  rebind time.
- Arrow keys consume + announce + warp cursor. Tab buttons get the
  `+g_tabClickOffsetY` cursor-y compensation (computed from chain spacing in
  `RebindChain`).
- Enter on tabs: deferred click-sim (cursor warp + manager
  HandleLMouseDown/Up) with the same offset compensation.
- Enter on non-tabs (sub-dialog Schliess/OK, settings buttons covered by
  listbox extents, main menu): deferred direct activate via
  `vtable[15].HandleInputEvent(0x27, 1)`.
- Esc inside a tabbed parent's sub-dialog: FireActivate the sub-dialog's
  Schliess button.
- Tabbed-panel detection (`DetectTabsCluster`) arms `g_tabbedPanel`/`Start`/`Count`
  for the listbox virtual-line cursor and the tab-button gating in
  `IsTabButton`.
- All log rate-limits removed.
- Last verified working: `<install>/logs/patch-20260502-123837.log` (every Enter
  on a tab opens the matching sub-panel; chain step `mouseOver after=` matches
  the chain target).

## Constraints

- **No entry-point hooks on `CSWGuiListBox`** — toxic even when never fired
  (session 4 finding). Mid-function only on that class. Other classes
  (`CSWGuiManager`, `CSWGuiPanel`) are fine entry-point or mid-function;
  we still prefer mid-function with register sources per
  `feedback_hook_design_register_sources.md`.
- **GoG-derived bytes from Lane's gzf match Steam** — paste into `hooks.toml`
  unchanged.
- **No log rate-limits** — full fidelity per `feedback_log_no_rate_limits.md`.
- **Fallback announcements never silently drop** — per
  `feedback_never_silence_fallback_announcement.md`.

## Build / run

From `C:\Users\fabia\Dev\kotor`:

```bash
tools/kdev/bin/Debug/net10.0/win-x64/kdev.exe build
tools/kdev/bin/Debug/net10.0/win-x64/kdev.exe apply
tools/kdev/bin/Debug/net10.0/win-x64/kdev.exe launch --monitor
ls -1t "/c/Program Files (x86)/Steam/steamapps/common/swkotor/logs/" | head -3
```

## Memory references

Project state:
- `project_listbox_keyboard_model.md` — selection_index semantics
- `project_listbox_click_flow.md` — click → activation chain
- `project_kotor_gui_struct_offsets.md` — Button/Label text offsets, vtable indices
- `project_kpatchmanager_lea_bug.md` — framework wrapper bug, PR opportunity

Behavior:
- `feedback_hook_design_register_sources.md` — mid-function + register sources
- `feedback_log_no_rate_limits.md` — never throttle diagnostic logs
- `feedback_never_silence_fallback_announcement.md` — placeholders speak, dedup is for resolved text only
- `feedback_explain_decisions_step_by_step.md` — walk through decisions individually

## Re-entry checklist

1. Read this file + `docs/tab-crash-investigation.md` + `docs/upstream-prs.md`.
2. `git log --oneline -5` for what's at HEAD.
3. ✅ Phase 1+2 chain navigation + cursor sync — done.
4. ✅ Phase 3 tab activation via click-sim with hit-test-shift compensation —
   done. Verified in `patch-20260502-123837.log`.
5. **Next:** Phase 3 per-setting interaction inside the Options listbox.
   Listbox has `controls.size == 1` so `SetActiveControl` can't target
   individual lines; need a coordinate-based click that lands inside the
   listbox at a specific y. Investigate `CSWGuiListBox::HandleLMouseDown` /
   `HandleLMouseUp` (mid-function only — entry-point hooks on listbox are toxic
   per session 4).
6. **Then:** sliders. Click-sim sequence: down at handle → cursor moves →
   up at target.
7. **Then:** Phase 4 (spatial mode) once panels are fully done.

**Optional cleanup** (won't move the goalpost but improves polish):
- Suppress the engine's lagged `SetActiveControl` re-promotion to Gameplay
  (the panel default) after each chain step — currently dedup hides it from
  audio but it shows up as noise in the log. Filter in `OnSetActiveControl`
  on "this control is in chain but isn't `g_chain[g_chainIndex]` and we're
  in chain-active mode."
- Add panel-extent offset for popup buttons (popup chain entries are in
  panel-local coords; today MoveMouseToPosition gets `(145, 85)` instead of
  `(400, 333)`). The popup currently relies on `FireActivate` (no hit-test)
  so this is cosmetic — the announced control opens correctly even though
  the cursor visually misses.
