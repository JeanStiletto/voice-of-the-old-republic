# In-game menu / engine input pipeline investigation

End-to-end model of the KOTOR 1 keyboard input pipeline, from Win32 / DirectInput
through to per-control onClick dispatch. Written 2026-05-10 against `swkotor.exe`
SHA-256 `34e6d971…f34c88` (Steam v1.0.3) using Lane Dibello's GoG-derived Ghidra
database (bytes match Steam — see `project_ghidra_gog_steam_bytes_match`). All
addresses are absolute in the loaded image.

The previous session's commit `8d0b35a` left Esc-from-world broken because we
were hooking the manager layer without understanding the upstream layers that
sit above it. This document covers the layers above and below
`CSWGuiManager::HandleInputEvent` so the next session can choose hook sites
deliberately.

## Pipeline overview

Vertically (top = OS, bottom = per-control onClick):

```
DirectInput keyboard device (DInput8 IDirectInputDevice::GetDeviceData)
      ↓  raw {dwOfs, dwData, dwTimeStamp, dwSequence, dwAppData} 20-byte records
      ↓
CExoRawInputInternal::GetKeyboardBuffer  @ 0x005e3ae0
      ↓  fills CExoDeviceBuffer with up to 0x40 records per frame
      ↓
CExoInputInternal::GetEvents  @ 0x005e24e0
      ↓  walks per-class keymap, emits CExoInputEvent{state, ts, seq, *desc}
      ↓  for digital keyboard descriptors emits raw dwData (0x80 press / 0 release)
      ↓
CClientExoAppInternal::ProcessInput  @ 0x006227e0
      ↓  dispatches each event into ONE of two routes based on event-id
      ↓
   ┌──────────────────────────┴──────────────────────────┐
   ↓                                                     ↓
CClientExoAppInternal::HandleInputEvent             CSWGuiManager::HandleInputEvent
@ 0x00621210                                        @ 0x0040c8e0
("CLIENT-APP HANDLER")                              ("MANAGER")
   ↓                                                     ↓
   ↓  in-world hotkeys, screenshot, pause,               ↓ panel_translate(param_1)
   ↓  party-cycle, target-actions,                       ↓   0xb4, 0xdf       → 0x28
   ↓  ShowSWInGameGui(7) on Esc,                         ↓   0xb5, 0xbb       → 0x27
   ↓  may synthetically forward Esc to manager           ↓   0xb6/0xb7/0xb8/9 → 0x3d/3e/3f/40
   ↓  via CSWGuiManager::HandleInputEvent(0xb4, 1)       ↓
   ↓                                                     ↓ if modal_stack.size != 0:
   ↓                                                     ↓   modal_stack[top]->HandleInputEvent
   └─────────────────────────────────────────────────────┘ else:
                                                           ↓   for each p in panels[]: p->HandleInputEvent
                                                           ↓
                                                       CSWGuiPanel::HandleInputEvent  @ 0x00409e60
                                                           ↓ this->active_control->vtable->HandleInputEvent
                                                           ↓
                                                       CSWGuiButton / CSWGuiListBox / CSWGuiNavigable / …
                                                           ↓ class-specific gating (e.g. button: param_2 != 0 && param_1 == 0x27)
                                                           ↓ → CSWGuiNavigable::HandleInputEvent  @ 0x0041a9d0  (focus walk)
                                                           ↓ → CSWGuiControl::HandleInputEvent   @ 0x00418750
                                                                ↓ scan event_list[], call subscriber(gui_object, this)
                                                                ↓ stores is_active = param_2 BEFORE the call
                                                                ↓ → onClick / onSelect / etc.
```

## Layer 1 — DirectInput / `CExoRawInputInternal`

`GetKeyboardBuffer` is a thin wrapper around `IDirectInputDevice::GetDeviceData`
with `cbObjectData = 0x14` (sizeof DIDEVICEOBJECTDATA = 20 bytes). Each record
is 5 dwords:

- `dwOfs` (DIK_* scan code) — index into the InputIndices enum
- `dwData` — for keyboard always `0x80` (key down) or `0x00` (key up)
- `dwTimeStamp` — DI sequence timer
- `dwSequence` — DI sequence number
- `dwAppData` — unused

Re-acquires the device on `DIERR_INPUTLOST` / `DIERR_NOTACQUIRED`. No keyboard
state is read via `GetAsyncKeyState` here. Win32 `WM_KEYDOWN/WM_KEYUP` is NOT
the path — DirectInput is used instead. `CExoInputInternal::BufferEvent`
@0x005e12a0 is mouse-only (its switch covers WM_MOUSEMOVE / WM_*BUTTON* /
WM_MOUSEWHEEL only); keyboard never touches that surface.

Implication: any hook attempting to capture keyboard input upstream of the
manager has to either intercept DirectInput or hook into one of the
`CExoInputInternal` / `ProcessInput` layers.

## Layer 2 — `CExoInputInternal::GetEvents` @ 0x005e24e0

Translates the device buffer into a `CExoArrayList<CExoInputEvent>`. Each
`CExoInputEvent` is 16 bytes: `{int state, WinMessages msg (= dwTimeStamp for
keyboard), WPARAM w_param (= dwSequence), short dx, short dy}`. The
`change_mouse_x` / `change_mouse_y` fields are reused — for keyboard events
the upper 8 bytes are the DI metadata; for mouse events they're cursor
deltas.

The crucial dispatcher inside this function is the `switch(pCVar4->field5_0x14)`
on the bound `CExoInputEventDesc`'s **type tag**:

- `case 0, 2, 5` — store raw `dwData` into descriptor's `field1_0x4`, emit
  no event (continuous-value ports — accumulators)
- `case 1` — binary digital event:
  - if `IsDigital(deviceIndex, dwOfs) == false` (analog-classified): quantize
    to `{-1, 0, 1}` via deadzone, emit event with `field0_0x0 = local_b8`
  - if `IsDigital(...) == true` (digital-classified): emit event with
    `field0_0x0 = (raw dwData)` — **0x80 on press, 0 on release**
- `case 3` — accumulating axis: store `field1_0x4 += dwData`, no immediate
  event; later poll picks it up
- `case 4` — 2-button virtual axis: store negative button into descriptor[1]'s
  `field1_0x4`, positive button into descriptor[1]'s `field5_0x14`. Events come
  from the timer-driven repeat loop at LAB_005e2d32 which composes from those
  two slots.

`CExoRawInputInternal::IsDigital` @ 0x005e39f0 returns `true` for any keyboard
device (param_1 = 0). So **all keyboard digital descriptors** emit events
carrying raw `dwData` (0x80 / 0).

The repeat loop at LAB_005e2d32 walks `field5_0x138`'s linked list (the per-key
held-state tracker), and on key-held thresholds emits events with
`field0_0x0 = 0` (initial held event) or `field0_0x0 = puVar13[4]` (subsequent
repeats — that slot holds the descriptor's stored last-value).

**Why some events show val=128 and others val=1** — see "Param_2 semantics"
below. Short version: directly-bound digital keys come through as 128/0; keys
that the upstream client-app handler synthetically re-issues to the manager
arrive with `param_2 = 1` because the synthetic call site hard-codes that
value.

## Layer 3 — `CClientExoAppInternal::ProcessInput` @ 0x006227e0

Called per-frame from `CClientExoAppInternal::MainLoop` @0x00602eb0 with
`deltaT`. Pulls events via `CExoInput::GetEvents`, deduplicates rapid
re-presses on a small set of axis codes (cases 1, 2, 0xa, 0xb, 0x27..0x2e,
0x35, 0x36 — the rapid-cancel logic in the inner switch), then dispatches:

```c
eventID    = event.desc->field5_0x14;       // logical action code
inputActive = event.field0_0x0;              // value (128 / 1 / -1 / 0 …)
if ((eventID < 0x27 || eventID > 0x40)            /* not a mouse-button-range code */
 && ( sw_gui_status == 1                          /* in-world */
   || ( !(0xb4 <= eventID && eventID <= 0xbb)     /* not Esc / direction / confirm */
     && eventID != 0xce                           /* not party-cycle */
     && eventID != 0xdf                           /* not Esc2 */
     && eventID != 0xf3 && eventID != 0xf4        /* not menu-left / menu-right */
     && !(0xfe <= eventID && eventID <= 0x106)    /* not extended cluster */ ))) {
    /* CLIENT-APP route */
    HandleInputEvent(this, eventID, inputActive);
}
else if (inputActive == 0
      || client_options->camera_mode != 5
      || gui_in_game->field55_0xdc != 0) {
    /* MANAGER route */
    if (sw_gui_status == 1
     || input_class == 2 || input_class == 3) {
        CSWGuiManager::HandleInputEvent(this->gui_manager, eventID, inputActive);
    }
}
else {
    /* Drop input (camera_mode == 5 hard-locks input) */
}
```

So the routing depends on **two** state bits:

- `sw_gui_status` — 1 in-world, 2 main menu, 3 sub-screen showing
- `input_class` — 0 normal, 1 minigame, 2 GUI-mode, 3 GUI+combat, 4 free-look

In-world (`sw_gui_status == 1`): every non-mouse event goes to the **client-app
handler**. None of them reach the manager directly. The client-app handler is
the one that translates Esc into "open pause menu", `M` into "open map", `I`
into "open inventory", etc. **THIS is the upstream layer the previous session
couldn't find.**

In-menu (`sw_gui_status != 1`): the special set
`{0xb4-0xbb, 0xce, 0xdf, 0xf3, 0xf4, 0xfe-0x106}` is routed to the manager
directly. Other non-mouse events still go to the client-app handler.

## Layer 4a — `CClientExoAppInternal::HandleInputEvent` @ 0x00621210

The "client-app" handler. Massive switch on `eventID`. Highlights for our work:

- `case 0x19, 0xd9` — minigame fire (player->Fire())
- `case 0x41/0x42` — mouse axis (SetMouseX/SetMouseY)
- `case 0x43/0x44` — mouse buttons (PerformLButton[Down|Up], PerformRButton...)
- `case 0x46` — mouse wheel
- `case 0x49-0x4c, 0x4 (3,4)` — generic camera/movement (caseD_3 fall-through)
- `case 0x50` — screenshot (writes KotOR%04d.tga)
- `case 0xb5` — confirm-action when `input_class == 2` only; falls to LAB_00622111 → manager
- `case 0xc8 / 0xc9` — re-emits 0x3d / 0x3e to main_interface (Q-cycle / E-cycle)
- `case 0xcc, 0xcd` — `SelectNearestObject` (Tab cluster)
- `case 0xce` — change character to next living party member
- `case 0xcf` — `ShowSoloModeQuery`
- `case 0xd0` — free-look camera toggle
- `case 0xd1-0xd8` — open/toggle SWInGame sub-screens (Map, Inventory, etc.):
  - calls `pCVar1->sw_gui_status == 3 ? SwitchToSWInGameGui : ShowSWInGameGui`
  - param_1 - 0xd1 is the GUI id (0..7)
  - sets `input_class = 2`, calls `ShowMouse(this, 1)`
- **`case 0xdf` (Esc1) — the path our previous session couldn't find:**
  - if release (param_2 == 0), return immediately
  - if dead, show death message-box
  - else if `gui_in_game->field45_0xb4 == 0` (UI not blocked) and player party valid:
    - if `sw_gui_status == 3`: HideSWInGameGui (close the visible sub-screen)
    - else if `input_class == 0`: `ShowSWInGameGui(7)` (7 = pause / InGameOptions)
    - if either succeeded → return (no manager call)
  - else: synthetically reissue with `param_2 = 1; param_1 = 0xb4; CSWGuiManager::HandleInputEvent(...)`
- `case 0xe0, 0xfd` — Pause / SetPausedByCombat toggle, plays gui sound 6
- `case 0xe1` — tooltip-instant mode
- `case 0xe2/0xe4/0xe6` — DoTargetAction + SelectNextAction (Shift modifier)
- `case 0xe8/0xea/0xec/0xee` — DoPersonalAction
- `case 0xef` — radial / target action (the one we hooked in `engine_action_picker`)
- `case 0xf0` — clear combat mode
- `case 0xf1` — Pause (variant)
- `case 0xf2` — flourish weapons
- `case 0xf5/0xfa/0xfb` — clear actions
- `case 0x107` — quickload
- `case 0x108` — stealth toggle

Returns 0 (or unaff_EBP — callee-saved register left from caller's frame) for
events it doesn't handle. The return value is read into `local_104` in
`ProcessInput` and used to gate tooltip-timer reset (non-zero = something
happened, reset tooltip).

## Layer 4b — `CSWGuiManager::HandleInputEvent` @ 0x0040c8e0  (where our hook lives)

Receives `(this, param_1, param_2)`. Stores `param_1` into `this->field23_0x68`
unconditionally (every call, regardless of edge). Then forks on `param_2`:

### Press path (param_2 != 0): 0x0040c907 onwards (after the JZ at 0x0040c90d)

Two switches:

1. **Axis-translation switch** on param_1 ∈ {0x33, 0x34, 0x37, 0x38} — an axis
   with `param_2 == 1` becomes one direction code, `param_2 == -1` becomes
   the opposite, anything else passes through. After this, **param_2 is
   unconditionally set to 1** (regardless of the original press value).

2. **Cooldown switch** on translated param_1 ∈ {0x2f, 0x30, 0x31, 0x32}.
   Holds a timer in `field22_0x64` and a tag in `field28_0x72` that suppresses
   re-fires within 0x96ms (150ms) for the same direction. Other codes fall
   through to LAB_0040c9f2 (the dispatch code, shared with release path).

### Release path (param_2 == 0): LAB_0040c9f2 onwards

Skips both press-path switches. Code retranslation at LAB_0040c9f2:

- `case 0xb4, 0xdf` — if `param_2 != 0`, retranslate to 0x28. **On release
  (param_2 == 0), 0xb4/0xdf are NOT retranslated** — they pass through with
  their original code.
- `case 0xb5, 0xbb` — unconditionally retranslate to 0x27 (no param_2 check).
  So both press and release of confirm get 0x27.
- `case 0xb6, 0xb7, 0xb8, 0xb9` — unconditionally retranslate to
  0x3d, 0x3e, 0x3f, 0x40 respectively.

### Final dispatch (both paths)

```c
if (modal_stack.size != 0) {
    /* one foreground modal: top of modal_stack handles it */
    (modal_stack[top]->vtable->HandleInputEvent)(param_1, param_2);
}
else {
    /* no modal: copy panels[] then dispatch to each non-removed panel */
    for (i = 0; i < panels.size; ++i)
        if (panel still in panels[]) panel->HandleInputEvent(param_1, param_2);
}
```

After dispatch, walks `panels[]` and removes any with bit_flags 0x600 set
(deferred-remove and cleanup destroyed panels in-line — this is why
`PopModalPanel` from inside an input handler doesn't free until next dispatch).

## Layer 5 — Per-panel `HandleInputEvent`

Each panel-class override gets the (translated) code and (mostly-original)
param_2. The default base `CSWGuiPanel::HandleInputEvent` @ 0x00409e60 is a
single line: `if (active_control != null) active_control->vtable->HandleInputEvent()`.

Panel-class overrides typically:

1. Filter on a few special codes (e.g. MessageBox handles 0x28 = Esc-translated,
   0x39/0x3a = listbox shifted, 0x1f6/0x1f7).
2. Handle their own panel-level state (e.g. close-on-Esc).
3. Delegate the remainder to `CSWGuiPanel::HandleInputEvent(panel, param_1, param_2)`
   which forwards to `active_control->vtable->HandleInputEvent`.

**Critical detail for press-vs-release contract: `CSWGuiButton::HandleInputEvent`
@ 0x0041ad40 only plays its click sound when `(param_2 != 0) && (param_1 == 0x27)`.
But it always falls through to `CSWGuiNavigable::HandleInputEvent` regardless.**

`CSWGuiNavigable::HandleInputEvent` @ 0x0041a9d0:

- If `param_2 != 0`: walk the focus chain (link offsets 0x5c-0x68 = prev/next/up/down)
  for codes `{0x2f, 0x30, 0x31, 0x32, 0x3d, 0x3e, 0x3f, 0x40}`, call
  `gui_object->vtable[8](newControl, 1)` (= SetActiveControl on the panel)
  if a valid focus target was found.
- Then **always** falls through to `CSWGuiControl::HandleInputEvent`.

`CSWGuiControl::HandleInputEvent` @ 0x00418750:

```c
if (param_2 != 0) {
    if (param_1 == 0)      vtable->SetActive(1);    // mouse-enter
    else if (param_1 == 1) vtable->SetActive(0);    // mouse-leave
}
/* then unconditionally: */
for (i = 0; i < event_list.size; ++i) {
    if (event_list[i].event_flag != param_1 || event_list[i].func == NULL) continue;
    this->field10_0x48 = param_1;
    this->is_active    = param_2;       /* ← this is what subscribers read */
    event_list[i].func(event_list[i].gui_object, this);
    return;
}
```

This is **THE** event-dispatch surface for button onClick / listbox onSelect /
checkbox onToggle / etc. Every control's high-level event subscriptions land
here.

**This loop runs for BOTH press AND release.** Subscribers must check
`this->is_active != 0` to gate to "press only" behaviour — see the project
memory `project_oncontrolentered_is_active_gate.md`.

## Why our log shows val=128 vs val=1 vs val=0

- `val = 0` — keyboard release. Always. Every digital descriptor's release
  emits raw 0 from `GetEvents` (digital path) and that propagates through
  `ProcessInput` → manager untouched.
- `val = 128` — keyboard press, **direct** route from `GetEvents` to manager.
  This happens when (a) `sw_gui_status != 1` AND (b) the event-id is in the
  manager-routed set `{0xb4..0xbb, 0xce, 0xdf, 0xf3, 0xf4, 0xfe..0x106}`. The
  raw DirectInput `dwData = 0x80` survives unchanged through GetEvents
  (digital descriptor + IsDigital(keyboard, _) = true → emit raw) and
  unchanged through ProcessInput → manager.
- `val = 1` — keyboard press **synthetically reissued** by the client-app
  handler. The only synthesis path observed is `case 0xdf` (Esc) at
  LAB_00622111 in `CClientExoAppInternal::HandleInputEvent`, which hard-codes
  `param_2 = 1; param_1 = 0xb4`. So `val = 1` for Esc means "Esc came from
  the upstream's pause-menu logic, not a fresh DirectInput press".
- `val = -1` — never observed for keyboard (would be a digital release of an
  analog-classified key — `IsDigital(0, _) == true` always for keyboard, so
  the analog branch is never hit).

So in the log:
- `Menus.Input #30 logical(180) val=1`: the user pressed Esc on a screen where
  `sw_gui_status != 1` (the pause menu was up; status 3). Wait — but val=1
  implies synthesis. Need to re-verify: synthesis only happens in-world
  (sw_gui_status == 1). If we see val=1 in-menu, that contradicts the model.
  Most likely explanation: the user pressed Esc twice — first time was
  in-world (synthesised, never reached us as press because the upstream
  returned without forwarding when ShowSWInGameGui succeeded), the second
  time was in-pause-menu (raw 128). What we see as `val=1` must be the
  upstream's synthesis path firing AFTER ShowSWInGameGui returned 0
  (failure), e.g. when `gui_in_game->field45_0xb4 != 0` (UI was blocked) or
  the party state was bad. **GAP**: needs live in-game verification with a
  diagnostic build that logs both routes.

## The Esc-from-world break and why the previous session got stuck

Pre-fix: the wrapper EFLAGS clobber meant our handler's TEST EAX,EAX
overwrote ZF from the cut bytes' implicit set. Looking at our hook bytes
`8b f1 57 89 5e 68` (MOV ESI,ECX; PUSH EDI; MOV [ESI+0x68],EBX) — none of
these set ZF in a way the engine reads. So actually the EFLAGS clobber bug
wasn't manifesting on THIS hook directly. But the wrapper's TEST EAX,EAX
dispatch on `consumed_exit_address` was ALSO broken.

Post-fix: PUSHFD/POPFD properly preserves flags, AND the `consumed` path now
correctly skips the engine's dispatch. Our handler returning 1 jumps to
0x0040cbcb (function epilogue) — meaning **we do NOT run the engine's
panel-translation switch and we do NOT dispatch to any panel**. That's
exactly what we want for keys we want to swallow (arrow nav we're handling
ourselves, etc.).

The Esc-from-world UX break is unrelated to the wrapper fix per se. The
sequence was always:

1. Esc PRESS arrives via `ProcessInput` → enters `CClientExoAppInternal::HandleInputEvent`
   with `eventID = 0xdf`, `inputActive = 128` (raw)
2. Inside `case 0xdf`: `ShowSWInGameGui(7)` succeeds, sets input_class to 2,
   ShowMouse(1), returns 0 — **WITHOUT calling the manager**. So our hook
   never sees the press.
3. Esc RELEASE arrives. `case 0xdf` returns immediately on `param_2 == 0`.
   Manager not called. Hook not fired.

But we observe `val=0` reaching our hook for Esc release. That implies a
SECOND path. Most likely: now that the pause menu is open, `sw_gui_status`
is 3 (sub-screen showing), so the next input frame routes through the
`else` branch in ProcessInput — i.e. the manager directly. Esc release
arrives with `eventID = 0xdf, val = 0`. Manager translates 0xdf → 0x28
ONLY on press; on release it dispatches 0xdf raw to the active panel. The
InGameMenu / sub-screen panel sees `(0xdf, 0)` and... probably ignores it
(the InGameOptions panel only checks `param_2 != 0`).

So the OPEN-then-CLOSE behaviour is the engine receiving a SECOND Esc
**press** while the menu is up:

- Frame N: Esc held down — ProcessInput sees one event with val=128 (press).
- Frame N+1: Esc still held — no new event from DirectInput (key already in
  down state).
- Frame N+M: Esc released — ProcessInput sees event with val=0 (release).
- Frame N+M+1: user presses Esc again — val=128 press.

If pause menu opens at N and the user releases at N+M, the release goes to
manager directly (not upstream — sw_gui_status now 3, but Esc is in the
manager-routed set, so it goes to manager regardless). The InGameOptions
panel sees `(0xdf, 0)` → no-op. So no close on release.

The "open then immediately close" must be a SECOND press during the same
held-Esc period, which is plausible if the engine's repeat logic at
LAB_005e2d32 emits a synthetic repeat-press while Esc is held. The repeat
loop emits `field0_0x0 = puVar13[4]` from the descriptor's stored last
value — for digital, that's `local_b8` from the previous emit. For keyboard
digital, `local_b8` is the raw `dwData = 0x80` (because IsDigital→true).
So the repeat fires with val=128, manager translates to 0x28, panel handles
it as a CLOSE.

**Net hypothesis (UNVERIFIED — needs in-game test):**
- Frame N: Esc PRESS in-world → upstream opens pause via ShowSWInGameGui(7).
  Manager not called. UI now in pause menu.
- Frame N+1: Esc still held. Repeat-loop in GetEvents emits a synthetic
  press event. ProcessInput now sees `sw_gui_status == 3` (sub-screen showing)
  → routes 0xdf to manager directly with val=128. Manager translates to
  0x28. Active panel (InGameOptions) sees Esc-press → triggers PrevSWInGameGui
  / OnQuit handler → closes the pause.
- Frame N+M (user releases): val=0 release → manager → InGameOptions → no-op.

If this is correct, the fix is to suppress the repeat event at the manager
boundary while we know we just opened a pause from the upstream. **But that
information lives in the upstream layer, not the manager.** The cleaner fix
is to either (a) hook the upstream's ShowSWInGameGui call to mark a "just
opened — eat the next repeat-press of Esc" flag, or (b) hook
`CExoInput::ClearEvents` at the right moment (the upstream's case 0xdf
already calls `CExoInput::ClearEvents(ExoInput)` at LAB_006219ec for the
death-message-box path, suggesting this is the engine's own cure for the
held-key replay problem when state changes).

Note: `CExoInput::ClearEvents` is mentioned in the upstream source for the
party-cycle path (case 0xce), the free-look path (case 0xd0), and the
quickload path (case 0x107) — **but NOT for the pause-menu open path**.
That's a vanilla bug or oversight; the engine arguably should clear events
when transitioning into pause too. We could fix it by patching the case
0xdf path to call ClearEvents, OR by suppressing repeat-events in our hook
based on a flag we set when the upstream opened the pause.

## Press-vs-release contract per key class

For our hook at 0x0040c907 (BEFORE the JZ, i.e. seeing both edges raw):

### Esc1 (`param_1 == 0xb4`) and Esc2 (`param_1 == 0xdf`)

- **In-world** (`sw_gui_status == 1`): NEVER reaches the manager. The upstream
  client-app handler intercepts both press AND release in `case 0xdf` /
  `case 0xb4`, and only forwards synthetically (`param_2 = 1; param_1 = 0xb4`)
  if both `ShowSWInGameGui(7)` and the death-message-box paths fail. So in-world
  Esc presses are typically invisible to our hook.
- **In a sub-screen** (`sw_gui_status == 3`): goes to manager directly. We
  see press (val=128) and release (val=0). Manager translates 0xb4/0xdf →
  0x28 on press only.
- **In a modal**: goes to manager (sw_gui_status irrelevant — modal_stack
  takes over). Same dispatch.

### Confirm/Activate (`param_1 == 0xb5` or `0xbb`)

- Manager retranslates BOTH press and release to 0x27 unconditionally (no
  `if (param_2 != 0)` gate around the retranslation). The downstream panel
  sees 0x27 with both edges. `CSWGuiButton::HandleInputEvent` only plays the
  click sound on press, but the underlying `CSWGuiControl::HandleInputEvent`
  fires the event_list scan (i.e. button onClick) on **both** edges. That's
  why our pair-consume fix is essential: any time we consume a press, the
  matching release will fire onClick on the now-focused control.

### Direction codes (`param_1 ∈ {0xb6, 0xb7, 0xb8, 0xb9}`)

- Manager retranslates to {0x3d, 0x3e, 0x3f, 0x40} unconditionally on both
  edges. `CSWGuiNavigable::HandleInputEvent` only walks the focus chain on
  press (`param_2 != 0`). Release reaches `CSWGuiControl::HandleInputEvent`'s
  event_list scan — most controls don't subscribe to the direction codes,
  so it's a no-op in practice.
- For listbox children (rows): the listbox's own `SetActiveControl`
  reassignment in `CSWGuiNavigable` (vtable[8]) implicitly fires our
  `OnListBoxSetActiveControl` hook. Release does NOT reassign focus.

### Hotkeys (M, I, J, …) — `eventID ∈ {0xd1..0xd8}`

- **In-world**: routed to client-app handler (since not in the manager-only
  set). Client-app handler calls `ShowSWInGameGui` / `SwitchToSWInGameGui`,
  sets input_class to 2, calls ShowMouse(1). Press only — release returns 0
  at the top of the case branch.
- **In a sub-screen**: still routed to client-app handler (not in
  manager-only set), same toggle-or-switch logic. Press path; release ignored.
- These never reach our manager hook, in-world OR in-menu.

### Tab (`param_1 == 0xce`)

- Routed to manager directly regardless of `sw_gui_status`. Press: hits
  `default` (manager retranslation switch doesn't have a 0xce case) → goes
  to dispatch unchanged → panel.HandleInputEvent(0xce, 1) → typically the
  dialog/menu's "next/prev" handler.

### Direction-axis events (analog, `param_1 ∈ {0x33, 0x34, 0x37, 0x38}`)

- Press path: first switch translates them to direction codes based on
  param_2's sign (1 / -1). Then the cooldown switch may suppress within 150ms
  of a previous emit. Then dispatch.
- Release: skips both switches, dispatches with original param_1.

## Where vanilla "open pause" fires from, and what closes it

**Open**: in-world, Esc PRESS goes to `CClientExoAppInternal::HandleInputEvent`'s
`case 0xdf` → calls `gui_in_game->ShowSWInGameGui(7)` (7 = the InGameOptions /
pause sub-screen id). Sets `input_class = 2`, calls `gui_in_game->SetSWGuiStatus(3, 1)`,
calls `ShowMouse(this, 1)`. Returns from `HandleInputEvent` without forwarding
to the manager.

**Close**: depends on the sub-screen and how the user invokes close:
- Esc on InGameOptions: routes to manager (sw_gui_status==3 means "in sub-screen"
  but the routing condition is `eventID == 0xdf` is in manager set). Manager
  translates 0xdf (press) → 0x28. InGameOptions panel handles 0x28 →
  `OnQuit` handler → reorders panels[] (does NOT pop — see the existing
  hook in `engine_subscreen.cpp` discussion). For pause specifically the
  close path eventually calls `CGuiInGame::PrevSWInGameGui` @ 0x0062cdf0 which
  actually pops.
- Click "Hauptmenü" / "Beenden": fires the appropriate button's onClick,
  which calls a cleanup chain ending at the same place.

## Recommended hook sites (for the next session)

For full keyboard input visibility / control:

1. **`CClientExoAppInternal::ProcessInput` @ 0x006227e0** — between
   `eventID = …` and the dispatch branch. Sees every event before routing
   decision. Cleanest single-point capture for ALL keyboard input. Cut
   would have to land in the dispatch loop body — there are several
   register-only instructions to choose from; need a DumpBytes pass.
2. **Above ProcessInput, hook `CExoInputInternal::GetEvents` @ 0x005e24e0**
   exit point. Exposes the full event array for one frame. Enables
   filtering / synthesizing events at the source.
3. **`CClientExoAppInternal::HandleInputEvent` @ 0x00621210** — entry-time
   hook to capture the in-world hotkey / Esc / pause path. Pair this with
   the existing manager hook to see both routes. Useful for verifying the
   "synthetic param_2 = 1" hypothesis above.

For specific Esc handling:

4. **Hook `case 0xdf` inside `CClientExoAppInternal::HandleInputEvent`** —
   right after `ShowSWInGameGui(7)` succeeds. Set a "just-opened pause"
   flag, then call `CExoInput::ClearEvents(ExoInput)` to discard the
   in-flight repeat-press. This eliminates the open-then-close double-fire
   without changing engine semantics for any other path.
5. **OR hook `CGuiInGame::ShowSWInGameGui` @ (find via xref)** — flag
   "pause just opened, eat next press at manager boundary". Less invasive
   than the case-specific hook.

For correct release-on-focused-control suppression (already done):

6. Keep the existing pair-consume in `OnHandleInputEvent`. The vanilla
   contract (verified via `CSWGuiControl::HandleInputEvent`) is that the
   event_list scan fires on BOTH press AND release with `is_active = param_2`.
   Subscribers that don't gate on `is_active != 0` will fire their callback
   on release. Our pair-consume protects callers that consume a press from
   accidentally firing a callback they didn't intend at the new focus
   target.

## Open questions / gaps to verify

- **Live verification of the val=1 vs val=128 distinction**: needs a
  diagnostic build that logs entry to `CClientExoAppInternal::HandleInputEvent`
  AND to `CSWGuiManager::HandleInputEvent` with a sequence counter, so we
  can see whether the val=1 events seen in the manager are preceded by an
  upstream call. Until this is verified, the synthesis-only hypothesis is
  reasonable but not proven.
- **The exact close-on-second-press behaviour**: needs a test where the
  user holds Esc for a controlled duration (e.g. 200ms) and the test logs
  every event the manager and upstream see in the open/close window.
  This will distinguish "DirectInput repeat-press fires while Esc held" vs
  "synthetic from upstream" vs "the user actually pressed Esc twice".
- **Hotkey behaviour in-pause-menu**: `M`, `I`, `J` etc. when the pause
  menu is up. Per the routing logic (`sw_gui_status != 1`, hotkey codes
  0xd1..0xd8 are NOT in the manager-only set, so they go to the client-app
  handler), they should still fire `ShowSWInGameGui` / `SwitchToSWInGameGui`
  even when pause is up. Current code's behaviour with this case is
  unverified.
- **What `field45_0xb4` on `CGuiInGame` means**: it gates Esc handling in
  the upstream's `case 0xdf`. Could be "dialog active" or "dialog box
  open" — needs a memory probe during a known dialog state to pin down.
- **Mouse-button events with WinMessages-equiv values**: project memory
  `project_engine_event_value_layers` notes that `param_2 = 514` does NOT
  mean WM_LBUTTONUP at the manager layer. Verified — confirmed here by
  `CExoInputInternal::BufferEvent` decompilation showing it only handles
  WM_MOUSE* and translates them to internal `IVar4`/`uVar3` codes, not
  preserving the WinMessage value into the manager.

---

## 2026-05-10 SESSION 2 RESULTS

### Diagnostic build added

Three new hooks shipped in this session for cross-stream input correlation:

1. **`Diag.ProcInput`** — `CClientExoAppInternal::ProcessInput` @ 0x006227fb
   (cut at 0x006227fb after SEH+stack setup, register-only 5-byte cut). Emits
   one frame marker per call with monotonic seq counter.
2. **`Diag.ClientHIE`** — `CClientExoAppInternal::HandleInputEvent` @ 0x00621210
   (function-entry, 5-byte cut covering 5 PUSHes). Logs every event the
   upstream client-app handler sees, including caller EIP (so synthesised
   re-issues from LAB_00622111 stand out).
3. **`Menus.Input` seq= prefix** — modified existing manager-side
   `OnHandleInputEvent` to stamp `seq=N` from the same shared counter, so
   upstream / manager / per-frame events interleave with comparable seq
   values.

Counter source: `acc::diag::input::NextSeq()` in `diag_input_pipeline.cpp`
(InterlockedIncrement on a process-wide LONG).

### Hypothesis Q1 (val=1 synthesis path) — **CONFIRMED**

`patch-20260510-093604.log` seq=490/491 captures the exact pattern:
```
seq=490 Diag.ClientHIE   logical(223) val=128       ← upstream Esc PRESS
seq=491 Menus.Esc        cancel panel=…73F66E8 kind=MessageBox target=…73F6BA0
seq=491 Menus.Input #29  logical(180) val=1 CONSUMED  ← synthesised manager call
seq=491 Update           FireActivate target=…73F6BA0
```

Synthesis fires **only** when `field45_0xb4 != 0` (i.e. a MessageBox is
pushed onto modal_stack). In-world Esc presses *without* a MessageBox up
go upstream-only (open pause via `ShowSWInGameGui(7)`) and never produce
a manager-side press at all. The doc's prediction was right.

Every other Esc press observed in the log routed upstream-only with
`val=128` and produced no manager press.

### Hypothesis Q2 (held-Esc DirectInput repeat) — **DISPROVED**

`patch-20260510-130303.log` Esc presses range from 3-frame (~100ms) to
21-frame (~700ms) hold durations. **No second press event ever fires
within a single hold** — the engine's per-key repeat loop at
`LAB_005e2d32` apparently doesn't auto-emit while the held state is
maintained between buffer pulls (or the engine's own
`ClearEvents(ExoInput)` call inside `case 0xdf`'s pause-open success
path drains it). So the held-key repeat hypothesis from Q2 is wrong.
The cooldown fix (`CExoInput::CoolDownEvent(0xb4/0xdf, 350)`) works
mechanically but doesn't address the actual user-visible bug.

### Bug 2a (arrow keys can't navigate Alt+F4 popup) — **FIXED**

When a `MessageBox` is pushed, `sw_gui_status` stays `==1` (modals don't
flip status). Per ProcessInput's routing, with `sw_gui_status==1` arrow
codes 0xb6-0xb9 route to the upstream client-app handler, which has no
case for them → silently dropped.

Fix: extended `OnClientHandleInputEvent` to forward direction codes to
`CSWGuiManager::HandleInputEvent` directly when `modal_stack.size > 0`.
Verified live in `patch-20260510-130303.log` — the user can now navigate
the quit-confirm popup with arrows (multiple `Diag.ClientHIE: arrow
forwarded to manager` lines around seq=535-849).

### Bug 2b (synthesised Esc passthrough) — **PARTIAL**

Added: `OnHandleInputEvent` in menus.cpp now passes through synthesised
events (`param_2 == 1 && Esc`) without consuming, letting the engine's
natural panel dispatch handle modal cleanup. Logged as
`Menus.Input … val=1 SYNTHESISED-PASSTHROUGH`.

Live verification incomplete — a 3-second user-input gap in the test
session means we don't yet have data on whether walk+Enter still break
after dismiss. Need focused repro.

### Bug 1 (Esc opens pause then "closes itself") — **ROOT CAUSE LOCATED, NOT FIXED**

The investigation uncovered a path the engine's published model doesn't
mention and we didn't anticipate.

**Symptom (user-visible):** press Esc in-world. Pause sub-screen opens
visually. Screen reader announces "Optionen", "Spiel speichern",
"Ausrüstung", "Nachrichten", "Optionen". Within ~1 second, the user
finds themselves unable to navigate the menu (arrow keys do nothing
useful) — but can walk in the open world.

**Diagnostic chain:**

1. Cooldown didn't help. Auto-drill arms correctly but doesn't help
   either.
2. `CGuiInGame::SetSWGuiStatus` diagnostic hook @ 0x0062aa00 added.
   Captures every status transition with caller EIP.
3. `patch-20260510-140218.log` revealed an undocumented status value:
   `sw_gui_status` flips `1 → 3 → 4` immediately after Esc-open.
   Status 4 isn't in the doc's "1=in-world, 2=main-menu, 3=sub-screen"
   model — it's a fourth state.
4. The `3 → 4` transition's caller_eip = `0x0062cbe2` lies inside
   `CGuiInGame::HideSWInGameGui` (function entry @ 0x0062cba0,
   +0x42 in). So `HideSWInGameGui` is being called immediately after
   pause opens — that's what auto-closes the visible pause UI.
5. `CGuiInGame::HideSWInGameGui` diagnostic hook @ 0x0062cba0 added.
   Captures every Hide invocation with caller EIP.
6. `patch-20260510-140950.log` captured: caller_eip = `0x006aaf00`,
   which is inside `CSWGuiInGameOptions::HandleInputEvent` (function
   entry @ 0x006aaec0 per Ghidra decompile — note SARIF labeled the
   nearby `0x006aae50` "ReallyQuit" but the actual function spanning
   our caller is HandleInputEvent).
7. The decompile of `CSWGuiInGameOptions::HandleInputEvent`:
   ```c
   void CSWGuiInGameOptions::HandleInputEvent(this, param_1, param_2) {
     if (param_2 != 0) {                                    // ← gate: PRESS only
       switch(param_1) {
       case 0x28:  // KEYBOARD_F2 (manager-translated Esc)
       case 0x2d:
       case 0x2e:
       case 0xdf:  // raw Esc2
         this_00 = GetInGameGui(...);
         iVar1 = HideSWInGameGui(this_00, 0);              // ← THE CLOSE CALL
         if (iVar1 != 0) SetInputClass(client, 0, 1);
       case 0x35:  // fall-through
       case 0x36:
         SaveOptions();
       }
     }
     CSWGuiPanel::HandleInputEvent(&this->panel, param_1, param_2);
     return;
   }
   ```

**The contradiction:** the code gate says Hide fires only on PRESS
(`param_2 != 0`). But our log shows Hide firing immediately AFTER a
RELEASE (`Menus.Input #34 seq=220 val=0`), with no intervening manager
PRESS event in our log. So either:

- (a) The press DOES reach the manager but our hook misses it. Our
  manager hook is at 0x0040c907, mid-function. If the press path takes
  a different code path that bypasses our hook, we'd miss it. Worth
  re-verifying with a function-entry hook on
  `CSWGuiManager::HandleInputEvent` @ 0x0040c8e0.
- (b) Something OTHER than `CSWGuiInGameOptions::HandleInputEvent` is
  the caller and our address-to-function mapping is wrong. Could
  re-verify by adding an entry-time hook on
  `CSWGuiInGameOptions::HandleInputEvent` and logging the actual
  param_1/param_2 values when Hide fires from it.
- (c) There's an engine path (vtable dispatch chain, event_list scan,
  etc.) that routes the release synchronously to a code site that
  itself calls HandleInputEvent with param_2=1 internally as part of a
  "deactivate panel" gesture.

**Most likely explanation by elimination:** the manager's release
dispatch broadcasts `(0xdf, 0)` to all panels via the
`for each panel: panel->HandleInputEvent(p1, p2)` loop. One of those
panels has a release-handler subscriber that synchronously *re-fires*
the press path on the focused button. Or the engine's action-mapping
table (the `case 0xdf` in InGameOptions also matches `case 0x28`)
indicates 0x28 is a separately-bound key — maybe pressing release of
0xdf gets re-issued internally as 0x28 PRESS via some legacy keymap
translation we haven't decoded.

### Verified-but-no-longer-load-bearing fixes shipped this session

These are in `patches/Accessibility/` and built clean, but didn't
resolve the reported bug:

- **Esc cooldown** in `diag_input_pipeline.cpp::OnClientHandleInputEvent`
  — `CExoInput::CoolDownEvent(0xb4, 350)` + `(0xdf, 350)` on every Esc
  PRESS. Mechanically correct (suppresses repeat-emits per engine
  cooldown semantics), but the bug isn't a held-Esc-repeat issue.
- **Synthesised-Esc passthrough** in `menus.cpp::OnHandleInputEvent`
  — recognises `param_2==1 && Esc` and returns 0 without consuming,
  so the engine's natural modal-dispatch handles popup-cancel and
  state cleanup. Live verification incomplete.
- **Auto-drill on direct sub-screen open** in
  `menus_monitors.cpp::AnnounceNewSubScreens` — calls
  `acc::menus::SetDrilledIntoSubScreen(true)` whenever a new
  sub-screen panel is detected. Verified firing in
  `patch-20260510-132024.log` line 6076 / `patch-20260510-140950.log`.
- **`SubScreen.Status` diagnostic** — every
  `CGuiInGame::SetSWGuiStatus` call logs `(this, new_status, p2,
  caller_eip)`. Stays in the build for future investigation.
- **`SubScreen.Hide` diagnostic** — every
  `CGuiInGame::HideSWInGameGui` call logs `(this, param_1,
  caller_eip)`. Stays in the build.
- **Arrow-forward-to-manager when modal is up** — covers Bug 2a, the
  one fix in this session that genuinely resolved a user-visible bug.

### UX design discussion (not yet implemented)

Reviewed three architectural options for the in-game menu:

1. **Suppress vanilla case 0xdf, build artificial "chooser" menu** —
   user opens a virtual list (Equipment / Inventory / Character /
   Map / Abilities / Journal / Options / Messages), arrows + Enter
   dispatch `ShowSWInGameGui(N)`. Pros: clean architecture, single
   "open" model. Cons: fights the engine, breaks vanilla pause
   timing.

2. **Keep vanilla Esc behaviour, add Tab cycling** — Esc opens
   InGameOptions normally, auto-drill arms, `Tab` /
   `Shift+Tab` calls `SwitchToSWInGameGui(prev/next)` to cycle
   through the strip without leaving the sub-screen. Discoverable
   via Tab; expert hotkeys (M/I/C/etc.) keep working. **User
   preferred this option.** Not implemented yet because Bug 1
   (the auto-close) blocks the foundation.

3. **Pure hotkey access** — drop the strip-as-keyboard-navigable
   concept, rely on M/I/C/J for direct-open. Strip is a visual aid
   only. Risk: an unbound or rebound-away sub-screen becomes
   unreachable.

### Next session — verifying the Bug 1 root cause

To unblock Bug 1, the next diagnostic step is to add an **entry-time
hook on `CSWGuiInGameOptions::HandleInputEvent` @ 0x006aaec0**. Log
every call with `(param_1, param_2, caller_eip)`. The log will tell
us:

- Whether Hide fires from a `param_2==0` invocation (contradicts the
  decompile — would suggest engine state I haven't accounted for) or
  a `param_2!=0` invocation we didn't observe in the manager log
  (suggests a separate dispatch path).
- The caller_eip for `CSWGuiInGameOptions::HandleInputEvent` itself —
  i.e. who's calling THAT function. If it's
  `CSWGuiPanel::HandleInputEvent` @ 0x00409e60 (forwarding from
  active control), that points us at the active control's
  HandleInputEvent — probably a button.

Once the trigger is identified, the fix is one of:

- (a) Suppress the `case 0xdf` branch in
  `CSWGuiInGameOptions::HandleInputEvent` from firing when we just
  opened pause via Esc (debounce window via Hide-suppress flag).
- (b) Patch the engine code byte-level to skip the Hide call (risky
  — could break the legitimate close-on-Esc-in-pause path).
- (c) Hook `CGuiInGame::HideSWInGameGui` itself to consume when
  invoked within N ms of the corresponding pause-open via Esc.

Option (c) is mechanically simplest — extend the existing diagnostic
hook on HideSWInGameGui to *consume* with `consumed_exit_address`
when the suppression window is active. Already have the hook in place,
just need to add a state-machine flag and convert from void return to
int return.

### File map of changes shipped this session

- `patches/Accessibility/diag_input_pipeline.{h,cpp}` (NEW) —
  cross-stream seq counter + ProcessInput / ClientHandleInputEvent
  diagnostic hooks + Esc cooldown + arrow-forward.
- `patches/Accessibility/engine_input.{h,cpp}` — added
  `CoolDownInputEvent()` wrapping `CExoInput::CoolDownEvent`
  @ 0x005df4b0 via the global slot at 0x007A39FC.
- `patches/Accessibility/engine_subscreen.{h,cpp}` — added
  `OnSetSWGuiStatus` and `OnHideSWInGameGui` diagnostic handlers.
- `patches/Accessibility/menus.{h,cpp}` — added
  `IsDrilledIntoSubScreen()` / `SetDrilledIntoSubScreen()`
  accessors; synthesised-Esc passthrough; seq= stamping on every
  Menus.Input log line.
- `patches/Accessibility/menus_monitors.cpp` — auto-drill-arm in
  `AnnounceNewSubScreens` when a new sub-screen is detected without
  drill already armed.
- `patches/Accessibility/hooks.toml` — five new `[[hooks]]` entries:
  `OnProcessInput`, `OnClientHandleInputEvent`, `OnSetSWGuiStatus`,
  `OnHideSWInGameGui`. (`OnSwitchToSWInGameGui` was already present.)
- `patches/Accessibility/exports.def` — exports for the four new
  handlers.
