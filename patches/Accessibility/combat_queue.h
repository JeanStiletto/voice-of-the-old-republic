// Combat action-queue submenu — Phase 3A.
//
// Layer: input/menu submenu, modeled directly on actionbar_menu.h.
//
// User contract while active:
//   Open       — opens submenu over the controlled creature's combat
//                queue. Speaks "Aktionsschlange, N Aktionen" or
//                "Aktionsschlange ist leer" if empty.
//   Up / Down  — cycles through queued actions; each focused entry
//                announces "Angriff Malak, 1 von 3" etc.
//   Enter      — removes the focused entry from the queue. Speaks
//                "Entfernt: Angriff" and re-announces the new focus
//                (or closes if the queue is now empty).
//   Shift+Enter — clears the entire queue and auto-closes.
//   Esc        — closes without changes.
//
// Self-disarm: closes when the controlled creature changes / combat
// round drops out / chain pointer faults.
//
// **Skeleton scope** — the per-index remove primitive isn't directly
// exposed by the engine (`RemoveLastAction @0x4d37b0` only removes the
// most recent, `RemoveSpellAction` is type-specific). The skeleton's
// Enter handler currently calls `ClearAllQueuedCombatActions` for the
// last entry and falls through to `QueueRemoveFailed` for non-tail
// entries — see the docs/combat-system.md "open" item for the path
// forward (find positional remove primitive in SARIF, splice the
// linked list manually, or repeat-RemoveLast + re-queue).

#pragma once

namespace acc::combat::queue {

// Open the queue submenu over the controlled creature. Returns true if
// the gate armed (queue had >= 1 entry — empty queue path speaks the
// "leer" cue and returns false without arming).
bool Open();

// True when the gate is armed.
bool IsActive();

// Manager-level input gate. Called from interact_hotkey's poll AFTER
// the actionbar / radial gates. Press-edge only.
bool HandleInputEvent(int code, int value);

// Forced disarm. Called when the player creature changes mid-session
// or a chain resolution fails. No engine-side cleanup — engine owns
// the queue.
void ForceDisarm(const char* reason);

// Per-tick auto-disarm probe. Closes the submenu if the creature /
// combat round becomes unresolvable, or the queue is now empty.
// Called from core_tick.cpp.
void Tick();

// Win32 polling for the open hotkey (default Shift+K). Same Win32-edge
// pattern as interact_hotkey's other hotkey polls. Self-gates on
// foreground window + in-world. Called from interact_hotkey.cpp's
// PollHotkey alongside the other in-world hotkeys.
void PollWin32Hotkey();

}  // namespace acc::combat::queue
